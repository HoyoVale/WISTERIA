# R0 Linux / Windows 渲染人工验收

本验收用于确认 WISTERIA(17) 的结构稳定化补丁是否解决以下问题：

1. Linux/WSLg 窗口卡在第一帧；
2. Mesa 下黑屏、花屏或随机材质；
3. 两个独立 C ABI Context 误用同一组 OpenGL Program；
4. 销毁一个 C ABI Context 后，另一个窗口被 `glfwTerminate` 连带终止；
5. Windows 与 Linux 使用不同当前工作目录时找不到 shader/texture。

它不是最终性能基准，也不证明所有 MMD 物理细节已经一致。

## Windows 一键验收

在项目根目录运行：

```powershell
powershell -ExecutionPolicy Bypass -File .\script\verify_render.ps1
```

使用指定模型/动作：

```powershell
powershell -ExecutionPolicy Bypass -File .\script\verify_render.ps1 `
  -Model "C:\path\character.pmx" `
  -Motion "C:\path\motion.vmd"
```

只跑 C++ 窗口，不跑 Python C ABI demo：

```powershell
powershell -ExecutionPolicy Bypass -File .\script\verify_render.ps1 `
  -SkipNativeDemos
```

## Linux / WSLg 一键验收

X11/XWayland（WSLg 推荐先用此项）：

```bash
chmod +x ./script/verify_render.sh
./script/verify_render.sh --backend X11
```

原生 Wayland：

```bash
./script/verify_render.sh --backend WAYLAND
```

只验证无桌面依赖的编译和 CTest：

```bash
./script/verify_render.sh --backend NULL
```

自定义模型/动作：

```bash
./script/verify_render.sh --backend X11 \
  --model "/path/character.pmx" \
  --motion "/path/motion.vmd"
```

## 验收流程

脚本会依次执行：

```text
1. WISTERIA_BUILD_NATIVE=ON 配置
2. 编译 wisteria / wisteria_tests / wisteria_native
3. CTest
4. C++ 桌面 demo 固定推进 180 帧
5. Python ctypes 单 Context 窗口 demo
6. Python ctypes 双 Context demo
7. 双 Context demo 中途销毁 Context A
8. Context B 继续运行到结束
9. 对每组 BMP 计算 SHA-256 和 RGB 像素范围
10. 检测全黑帧、首帧后固定黑屏，以及截图变化不足
```

截图默认位于：

```text
artifacts/render-smoke/windows/
artifacts/render-smoke/linux/x11/
artifacts/render-smoke/linux/wayland/
```

Linux 的 X11 与 Wayland 结果分目录保存，不再互相覆盖。

每组含第 1、31、61、91、121、151 帧。验证器会拒绝以下结果：

- 所有截图完全相同；
- 只有首帧可见，后续截图全部相同或全黑；
- 可见截图不足两张；
- 六张截图少于三个不同哈希。

双 Context demo 使用窗口创建时的稳定名称保存截图，A/B 不再因运行时标题相同而互相覆盖。

## 关键日志

正常情况应看到：

```text
[GL] vendor=... renderer=... version=...
[GL CHECK] frame=1 stage=render status=OK
[FRAME CAPTURE] frame=1 ... rgbRange=... fnv1a=...
[RENDER SMOKE] frame=60
[SABA SKIN] finite=true ...
[MULTI] destroying context A; context B must keep rendering
[MULTI] PASS: context B survived context A destruction
```

异常分类：

```text
GLFW window creation failed
显示后端或 DISPLAY/WAYLAND_DISPLAY 问题

renderer=llvmpipe / softpipe / swrast
正在使用软件 OpenGL；可能极慢，但不应卡第一帧

[GL ERROR] ... code=0x502
通常是 GL_INVALID_OPERATION；重点检查 Program/VAO/FBO 所属 Context

[FRAME SKIP] framebuffer=0x0
窗口最小化、显示服务未完成映射，或 framebuffer 获取失败

[CAPTURE ERROR] all captures are byte-identical
动画/物理未推进、present 未更新，或读取到固定错误缓冲

[CAPTURE ERROR] ... black/near-black
读取到全黑缓冲；Linux 上尤其要检查默认 framebuffer 的 read/draw buffer

[CAPTURE ERROR] every capture after the first is identical
典型的“首帧正常，后续黑屏/冻结”模式

Context B 在 A 销毁后失败
仍存在 GLFW 生命周期或进程级 GL 状态污染
```

## 2026-08-04 人工验收结论

本轮用户实测表明：

- Windows 桌面、单 Context C ABI、双 Context C ABI 截图均持续变化；
- 双 Context 中销毁 A 后 B 能继续运行，GLFW 生命周期修复有效；
- Linux/WSLg 日志中动画、相机和蒙皮数值持续更新，且没有 GL error；
- Linux/WSLg 第 1 帧可见，第 31 帧以后截图为 `rgbRange=0..0`；
- 旧验证器因为仍有两个不同哈希而误判通过；
- Python C ABI demo 还把 `120 Hz` 错传成 `1/120`，日志表现为
  `physicsFps=0.00833333`。

R0.1 因此增加：默认 framebuffer 显式 `GL_BACK/GL_FRONT` 选择、
截图 read-buffer 状态保存恢复、像素级截图验证，以及 C ABI demo 的 Hz 参数修正。


## R0.2：截图源与 WSLg 默认 framebuffer

R0.2 默认从 WISTERIA 自己的 `SceneFramebuffer` 读取截图：

```bash
./script/verify_render.sh --backend X11 --capture-source scene
```

这是引擎完成 OIT、蒙皮和场景合成后的权威图像，同时不触碰平台
swapchain。在 WSLg 的 Mesa D3D12 路径上，实测故障恰好发生在第一次
`glReadPixels` 读取默认 back buffer 之后：首帧正常，后续默认缓冲读回全黑，
但动画、蒙皮边界和 GL 调用仍继续变化。

旧路径保留为定向诊断：

```bash
./script/verify_render.sh --backend X11 --capture-source default
```

它用于确认默认 framebuffer / compositor / swapchain 的兼容性，不再作为
渲染正确性的默认判据。纯肉眼检查窗口时可完全关闭截图：

```bash
./script/verify_render.sh --backend X11 \
  --capture-source none \
  --skip-native-demos \
  --frames 600
```

Windows PowerShell 对应参数为：

```powershell
.\script\verify_render.ps1 -CaptureSource scene
.\script\verify_render.ps1 -CaptureSource default
.\script\verify_render.ps1 -CaptureSource none -SkipNativeDemos -Frames 600
```

日志中的截图项现在会明确标记来源：

```text
[FRAME CAPTURE] frame=31 source=scene readFbo=... readBuffer=0x8CE0 ...
[FRAME CAPTURE] frame=31 source=default readFbo=0 readBuffer=0x405 ...
```

## R0.2 验收结论（2026-08-04）

- Windows：`wisteria` / `wisteria_tests` / `wisteria_native` 编译通过，
  CTest `wisteria.core` 通过；桌面、单 Context、双 Context 截图持续变化，
  Context A 销毁后 B 正常继续。
- 独立 Debian（X11 硬件 GL）：渲染成功，截图与动画持续推进，无黑屏；
  确认引擎在正常 Linux GL 环境下不存在“首帧后黑屏”问题。
- WSLg（Mesa D3D12）仍复现首帧后默认缓冲读回全黑，但动画、蒙皮与
  GL 调用持续更新。结合 Debian 对照结果，判定为 WSLg/Mesa D3D12 的
  平台问题，而非引擎渲染链路问题；R0.2 的 scene-FBO 截图路径保留为
  在该平台上绕开默认 back buffer 读回故障的权威验收方式。

## 手工观察

三组窗口都应满足：

- 第 1 帧后人物动作持续推进；
- 窗口内容不是随机彩色块或残留显存；
- 调整窗口大小后画面恢复正常；
- 双 Context demo 同时出现两个窗口；
- Context A 关闭后 Context B 不闪退、不冻结、不变黑；
- 终端中没有持续出现的 `[GL ERROR]`。

## 回传材料

若 Linux 仍异常，保留并发送：

```text
artifacts/render-smoke/linux/desktop/run.log
artifacts/render-smoke/linux/native-single/run.log
artifacts/render-smoke/linux/native-multi/run.log
三组截图目录
终端中的 glxinfo -B 或 eglinfo -B 输出
```

这些材料足以把下一轮排查收敛到窗口后端、GL Context、framebuffer/present 或 MMD runtime，而不必重新审计整个链路。
