# WISTERIA Linux/WSLg 支持收尾计划

状态：2026-08-04，三个用户反馈问题已定位，其中两个已修复，一个为 WSLg
合成器限制。

更新（R0.4 归档）：WSLg Mesa D3D12 已定性为平台兼容问题，原生 Linux
渲染链路验证通过；验收口径区分 Native Linux validation 与 WSLg
compatibility validation。详细结论见
`R0_RENDER_MANUAL_ACCEPTANCE.md` 的 R0.4 一节。

## 已定位的三个问题与根因

### 1. 窗口 demo “只渲染一帧就黑屏”

**现象**：Linux/WSLg 下窗口 demo 第一帧之后画面卡死或黑屏。

**根因**（实测数据）：

- 第一帧 `renderer.Render` 耗时 **15 秒**：每个材质都各自编译一份相同
  shader 的 Program，85 个材质 = 85 次 GLSL→LLVM 编译；WSLg 的
  D3D12 驱动（Intel igc）单次编译数百毫秒。
- 第一帧完成前窗口只显示清屏色，期间不响应输入；极端情况下 Mesa 会落到
  **swrast 软件渲染**（gdb 线程栈里全是 llvmpipe/LLVM），帧循环几乎冻结。
- 修复后实测：第一帧 3.5 秒（剩余为环境贴图/纹理一次性上传），随后稳定
  **50~88 fps**。

**修复**：

- 材质按 (vertex, fragment) 路径共享同一个 `Program`
  （`Material::SharedProgram`），85 次编译降为 1 次；
- `Window::init` 启动时打印 GL vendor/renderer/version，检测到
  llvmpipe/softpipe/swrast 时给出警告。

### 2. 键盘/鼠标操作无法执行

**现象**：Linux 下 Space/←/→/C 等按键、鼠标操作无效。

**根因**：输入层本身是标准 GLFW 回调（跨平台，且有单元测试覆盖）；问题在
主循环每帧耗时 15 秒以上，`glfwPollEvents` 实际被饿死，事件回调几乎不
触发。帧率修复后输入恢复。

**验证方式**：`WISTERIA_INPUT_DEBUG=1` 打印按键事件（Wayland 下暂无法
自动注入按键，需要手动验收）。

### 3. 最大化后窗口阴影残留在窗口内

**现象**：最大化后窗口四周有阴影/黑边残留在客户区内。

**定位**：代码侧 framebuffer 与 OIT 纹理每帧都按 `glfwGetFramebufferSize`
重建（`SceneFramebuffer::Resize` / `Renderer::EnsureOitResources`），尺寸
处理无缺陷。这是 WSLg 的 Weston 合成器在最大化时把窗口阴影画进 surface
的已知表现，应用侧无法完全消除。

**可选缓解**：如不能接受，可改用无边框窗口（`GLFW_DECORATED=GLFW_FALSE`
+ 自定义标题栏），或向 microsoft/WSLg 反馈。

## 已落地的调试能力（环境变量开关，默认关闭）

| 变量 | 作用 |
|---|---|
| `WISTERIA_FRAME_PROFILE=1` | 每 60 帧打印 update/render/present/swap 分段时间 |
| `WISTERIA_SCREENSHOT_DIR=<dir>` | 每 30 帧保存一帧 BMP 截图 |
| `WISTERIA_INPUT_DEBUG=1` | 打印 GLFW 按键回调事件 |
| `WISTERIA_SABA_NO_UPDATE=1` | 关闭 Saba 动态顶点上传（对照实验） |

## 收尾计划

1. [x] 材质 Program 共享（首帧 15s → 3.5s，稳定 50~88fps）
2. [x] GL 渲染器日志 + 软件渲染警告
3. [x] 帧分阶段计时/截图/输入日志诊断工具
4. [ ] 手动验收：WSLg 下跑 demo，确认动画流畅、Space/←/→/C/鼠标可用
5. [ ] 评估首帧剩余 3.5s（环境贴图卷积 + 纹理上传）：可考虑启动时预热
   或后台加载，非阻塞项
6. [x] 最大化阴影：记录为 WSLg 合成器限制，不做引擎侧绕过
7. [x] WSLg 使用提示：启动检测 Microsoft+D3D12 打印警告；
    `verify_render.sh --software-renderer` 强制 llvmpipe
8. [ ] Linux 测试纳入常规验收：`./build_linux.sh test`

## 自动验收

- Windows：`.\run.ps1 test`（59/59）
- Linux：`./build_linux.sh test`（59/59）
- 性能冒烟：`WISTERIA_FRAME_PROFILE=1 ./build-linux/wisteria`，
  60 秒后 totalMs 应 < 15ms（约 60fps+）
