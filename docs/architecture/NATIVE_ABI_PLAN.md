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
| M3 | FFI 客户端示例（Python ctypes / Node NAPI） | 示例脚本能驱动模型并读取统计 |
| M4 | 渲染/app 层命令（窗口、相机、事件） | Electron 面板控制桌面窗口 |

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
