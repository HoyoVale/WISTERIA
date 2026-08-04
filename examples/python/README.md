# WISTERIA native C ABI —— Python ctypes 示例

`native_mmd_demo.py` 用纯标准库 `ctypes` 调用 `wisteria_native`，
headless 复现窗口 demo 的流程：

1. 创建 context；
2. 加载 PMX 模型（默认蕾米埃尔-白）；
3. 加载 VMD 动作（默认梦的翅膀）；
4. 配置 Saba 物理（120Hz、最多 10 子步、重力 -98）；
5. 循环播放并步进 720 帧（60fps）；
6. 每 60 帧输出动作帧号与顶点包围盒/位移诊断；
7. 演示 pause → update 不推进 → resume；
8. 卸载并销毁 context。

## 运行

先构建（Windows 或 Linux 均可）：

```powershell
.\run.ps1 compile          # Windows，产物在 build/RelWithDebInfo/
```

```bash
./build_linux.sh build     # Linux，产物在 build-linux/
```

然后在项目根目录运行：

```powershell
python examples/python/native_mmd_demo.py
```

```bash
python3 examples/python/native_mmd_demo.py
```

可选参数：

```text
--model <pmx>            模型路径（默认蕾米埃尔-白）
--motion <vmd>           动作路径（默认梦的翅膀）
--frames <n>             步进帧数（默认 720）
--fps <n>                逻辑帧率（默认 60）
--physics-fps <n>        物理固定步长（默认 120）
--max-substeps <n>       最大子步数（默认 10）
```

找不到动态库时设置环境变量：

```text
WISTERIA_NATIVE_LIB=C:\path\to\wisteria_native.dll
WISTERIA_NATIVE_LIB=/path/to/libwisteria_native.so
```

## 开窗 demo（M4）

`native_window_demo.py` 会打开一个真实的桌面窗口（960x720），驱动与
`wisteria.exe` 相同的 Saba MMD demo：

```powershell
python examples/python/native_window_demo.py --frames 360
```

窗口内快捷键：Space 暂停/恢复、C 切换 VMD 相机、←/→ 调速度、Esc 关闭。
脚本每帧调用 `wisteria_poll_and_render`（Context 级拉模式，每帧一次），并定期输出相机
位姿与按键/鼠标增量。

## 双 Context 回归 demo（R0）

`native_multi_context_demo.py` 同时创建两个独立的 C ABI Context，每个
Context 都拥有自己的 `Application`、OpenGL Context/share group 和 shader
Program 缓存。运行到一半时销毁 Context A，Context B 必须继续渲染：

```powershell
python examples/python/native_multi_context_demo.py --frames 240
```

```bash
python3 examples/python/native_multi_context_demo.py --frames 240
```

成功标志：

```text
[MULTI] destroying context A; context B must keep rendering
[MULTI] PASS: context B survived context A destruction
```

这个 demo 专门防止两类回归：跨 OpenGL Context 复用 Program ID，以及任意
Context 析构时过早执行 `glfwTerminate()`。
