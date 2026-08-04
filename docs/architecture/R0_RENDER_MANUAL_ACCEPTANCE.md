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
9. 对每组 BMP 计算 SHA-256
```

截图默认位于：

```text
artifacts/render-smoke/windows/
artifacts/render-smoke/linux/
```

每组含第 1、31、61、91、121、151 帧。若所有 BMP 完全相同，脚本直接失败并提示疑似卡帧。

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

所有截图完全相同
动画/物理未推进、present 未更新，或读取到固定错误缓冲

rgbRange=0..0
读取到全黑缓冲

Context B 在 A 销毁后失败
仍存在 GLFW 生命周期或进程级 GL 状态污染
```

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
