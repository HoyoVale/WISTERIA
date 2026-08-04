# Native C ABI 门面计划（方案 C）

## 目标

把 WISTERIA 核心打包成动态库（Windows `.dll` / Linux `.so` / macOS `.dylib`），
只暴露 `extern "C"` 函数，供 Electron（NAPI）、Tauri（Rust FFI）、Python
（ctypes）等前端直接调用。

## 分层策略

```text
前端 (JS/Rust/Python)
      │  调用 C ABI
      ▼
wisteria_native  (动态库，extern "C" 门面)
      │
      ▼
headless runtime  (SabaMmdRuntimeModel：导入/动画/物理/蒙皮，无 GL)
      │
      ▼
renderer / app 层（GLFW 窗口、相机、场景，后续里程碑）
```

M1 只做 **headless runtime**：不依赖 OpenGL，天然可编译到 Linux/WASM。
渲染层命令（开窗、相机、截图）放到 M4。

## 头文件与句柄模型

- 头文件：`include/wisteria/native/wisteria_native.h`（C99，可被 C/C++/FFI 直接包含）；
- 句柄：`WisteriaContext` / `WisteriaModel` / `WisteriaMotion` 均为
  `uint64_t` 不透明编号，内部注册表映射到 `unique_ptr`；
- 错误：统一 `enum WisteriaStatus` + `wisteria_last_error_message()` 取详情；
- 平台导出宏：Windows `__declspec(dllexport/dllimport)`，Linux/macOS
  `__attribute__((visibility("default")))`，一套头文件跨平台。

## 线程契约

- 一个 `WisteriaContext` 单线程使用；
- 跨线程控制由调用方串行化（命令队列）；
- GL 只在渲染层（M4）出现，headless 层无此约束。

## 里程碑

| 里程碑 | 内容 | 验收 |
|---|---|---|
| M1 | 头文件 + 句柄注册表 + Linux 编译 | Windows/Linux 都能编出 `wisteria_native` |
| M2 | 包装 Saba runtime：load model/motion、play/pause/reset、update、顶点诊断 | C ABI 测试：叶瞬光 load→play→720 帧 finite |
| M3 | FFI 客户端示例（Python ctypes / Node NAPI） | Python 与 Node 示例双平台跑通（见下） |
| M4 | 渲染/app 层命令（窗口、相机、事件） | Python/Node 开窗示例双平台跑通（见下） |

## M3 进度（2026-08-04）

已完成 Python ctypes 与 Node N-API 示例：

- `examples/python/native_mmd_demo.py`：加载蕾米埃尔-白 + 梦的翅膀 VMD，
  配置 120Hz / 10 子步 / -98 重力，步进 300/720 帧，每 60 帧打印动作帧号
  与顶点诊断，演示 pause→update 不推进→resume，最后卸载销毁；
- `examples/node/`（`binding.cc` N-API 插件 + `native_mmd_demo.js`）：同样
  流程，运行时动态加载 `wisteria_native`，node-gyp 构建；
- 两个示例在 Windows（Node v24）与 Linux（Node v12）双平台运行通过，顶点
  包围盒数据跨语言、跨平台一致；
- 修了一个 ABI 契约 bug：C ABI 接收 **UTF-8 路径**，但 Windows 上
  `std::filesystem::path(const char*)` 按 ANSI 代码页（zh-CN 为 GBK）解释，
  中文路径会损坏甚至抛异常。`PathFromUtf8()` 现在在 Windows 显式
  UTF-8→UTF-16 转换；所有包装函数补了 `catch (...)` 防止 C++ 异常跨 ABI
  逃逸；原生测试也改成传 UTF-8 路径验证。

> 注意：M3 示例是 **headless** 的（无 GL、无窗口），这是分层设计——C ABI
> 目前只包装 Saba headless runtime。要让脚本真正开窗渲染，属于 M4
> （窗口/相机/事件/渲染命令）。

## M4 进度（2026-08-04）

M4 窗口层已完成，接口草案见 `docs/architecture/M4_WINDOW_ABI_DRAFT.md`：

- C ABI v0.2：新增 `WisteriaWindow`、键/鼠标枚举与 15 个窗口函数
  （创建/销毁、demo 加载、拉模式渲染、输入查询、相机控制）；
- `Application::PollEventsAndRender(dt)`：把 `Run()` 的私有循环体拆成公开
  拉模式入口，前端每帧驱动；
- `SetupSabaMmdDemoScene` 支持显式 motion 路径与物理参数；
- 自动测试 `Native ABI window`：开窗 → 载 demo → 渲染 30 帧 → 相机/输入
  查询 → 关闭（无显示环境自动跳过）；
- Python `native_window_demo.py` 与 Node `--window` 示例双平台跑通，
  相机位姿跨平台一致；
- 修了一个 Windows 编译冲突：把 `windows.h` 隔离到独立 TU
  （`windows_path.cpp`），避免与渲染层全局枚举 `FLOAT/INT/UINT/UCHAR`
  同名冲突。

剩余可选项：`wisteria_window_capture_bmp` 截图 API、Electron 面板示例。

## Linux（WSL Ubuntu-22.04）测试计划

### 环境现状（2026-08-04 实测）

- Ubuntu-22.04（WSL2）正在运行；
- CMake 3.22.1、g++ 11.4、Ninja 1.10.1、Make 4.3；
- GLFW 3.3.6（pkg-config 可见）+ X11 开发头 + Mesa GL 头；
- `iconv` 可用（VMD Shift-JIS 的 Linux 分支依赖它）；
- WSLg 可跑 GUI，demo 窗口理论上可以直接显示。

### 步骤

1. 推荐：**直接在挂载的 Windows 项目里并行构建**（2026-08-04 实测可用，
   无需复制仓库）。在 WSL 里进入项目目录执行：

   ```bash
   cd /mnt/c/Users/hoyo/Desktop/temp/learn/FGGP
   ./build_linux.sh
   ```

   脚本会配置 `build-linux/`（Ninja + RelWithDebInfo），并行构建窗口 demo
   `wisteria`、动态库 `wisteria_native`、测试 `wisteria_tests`，然后自动跑
   测试。产物与 Windows 的 `build/` 完全隔离。

2. 常用动作（与 `run.ps1` 对应）：

   ```bash
   ./build_linux.sh build                 # 配置 + 编译全部目标
   ./build_linux.sh test                  # 配置 + 编译 + 跑测试（默认动作）
   ./build_linux.sh run --model assets/models/mmd/爱弥斯_pmx/爱弥斯.pmx
                                          # 配置 + 编译 + 开窗口 demo
   ./build_linux.sh configure             # 只配置
   ./build_linux.sh clean                 # 删除 build-linux（仅限项目内）
   ```

   支持 `-c Debug|Release|RelWithDebInfo`、`-G <generator>`、`-B <build-dir>`。

3. 可选：如果嫌弃 `/mnt/c` 9P 文件系统慢，仍可 rsync 到 WSL 原生盘再构建：

   ```bash
   rsync -a --exclude build --exclude build-linux --exclude .git \
       /mnt/c/Users/hoyo/Desktop/temp/learn/FGGP/ ~/fggp-linux/
   cd ~/fggp-linux && ./build_linux.sh
   ```

### 已知移植检查点

- `src/mmd/vmd_importer.cpp` 的 Shift-JIS：Windows 用 Win32 API，Linux 已写
  `iconv` 分支，需实跑验证；
- **纹理路径大小写（已修复，2026-08-04 实测）**：爱弥斯等 PMX 引用
  `Tex\sink22.png`，但磁盘目录实际是 `tex`。Windows 文件系统大小写不敏感，
  Linux 直接失败。`src/assets/texture_path_utils.hpp` 新增
  `ResolvePathCaseInsensitive()`：逐级用目录列举做大小写不敏感回退，找不到
  才保留原路径交给原有错误处理。
- **stb_image v2.30 zlib 解码器（已修复，2026-08-04 实测）**：部分合法 PNG
  （复现：爱弥斯 `T_R2T1AimisiMd10011Up01_D.png`，2048×2048 RGB，单段 4MB
  IDAT，最大压缩；`pngcheck` 报 “No errors detected”）在 Linux 普通构建
  （g++ -O2）下偶发 SIGSEGV：`stbi__parse_huffman_block` 的 `zout` 越过
  `zout_end`，`num_bits` 被“棘轮”到 76597，ASan/Valgrind/-O0/MSVC 均不触发，
  属于上游现存未修复 bug。方案：vendor miniz 3.1.2，`stb_image.h` 增加
  `WISTERIA_STBI_USE_TINFL` 开关，`src/common/stb_image.cpp` 把六个 zlib
  入口全部改用 `tinfl_decompress_mem_to_heap/mem_to_mem`。验证：Windows
  59/59；WSL 整轮测试 5 次 + gdb 下 3 次 + 跨模型导入测试 10 次全部通过。
- `script/*.ps1` 是 Windows 工具链，Linux 直接走 CMake 命令；
- MSVC 特有警告/`/W4` 语义在 g++ 下重新看一遍；
- 依赖（Assimp/Bullet/GLFW/Saba）均有 UNIX 分支，理论上无源码改动。

Linux 构建入口：`./build_linux.sh [action]`，动作包括
`configure / build / compile / test / run / clean / help`（`compile` 是
`build` 的别名，`run` 后面可直接跟 demo 参数，如 `--model`）。

### Linux 窗口渲染（2026-08-04 已打通）

WSLg 下窗口 demo 已能编译、启动并渲染（`MESA_DEBUG=1` 下 0 个 GL 错误）。
过程中修掉两个 Linux 专属问题：

1. **GLSL 内置函数名被遮蔽**：`mmd.frag` 的 `uniform sampler2D texture;`
   把内置 `texture()` 遮蔽，Windows 的 NVIDIA 驱动容忍，Mesa/glslang 直接
   拒绝编译。已改名为 `baseColorTexture`，C++ 侧 `ShaderInterface` 与默认
   材质绑定键同步改名。
2. **GPU 蒙皮采样器缺少有效缓冲纹理**：`mmd.vert` 静态引用
   `samplerBuffer boneMatrixPalette`，但 Saba CPU 蒙皮路径（动态顶点）下
   `UploadSkinning()` 提前返回，采样器默认指向单元 0 的 2D 纹理；Mesa 在
   draw 时校验 samplerBuffer 纹理完整性，报 `GL_INVALID_OPERATION`（每帧每
   网格，NVIDIA 不报）。修复：只要网格有骨骼就 `EnsureSkinningResources()`
   并把皮肤 TBO 绑到专用单元（创建时先分配一个单位矩阵 texel，保证纹理
   非空），再按需启用 GPU 蒙皮。

## 下一步

1. M1：实现 `wisteria_native.cpp`（句柄注册表 + 状态码，函数先返回
   `WISTERIA_ERROR_INTERNAL` 或最小实现）；
2. M2：把 `SabaMmdRuntimeModel` 包装进 `wisteria_load_model` /
   `wisteria_update` 等；
3. WSL 构建脚本 + 首次 Linux 编译修复。
