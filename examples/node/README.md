# WISTERIA native C ABI —— Node N-API 示例

`native_mmd_demo.js` 通过自编译的 N-API 插件调用 `wisteria_native`，
流程与 Python 示例一致：加载模型/动作 → 配置物理 → 步进 N 帧 →
顶点诊断 → pause/resume。

## Stable ABI smoke（R1.9 Phase 0E）

`stable_smoke.js` 通过 `binding_stable.cc` 只调用冻结的 stable 面
（`wisteria_stable_runtime.h` + `wisteria_stable_render.h`）：
Generic entity → capabilities → exact step/replay → checkpoint →
RenderSession 单帧 RGBA8 → status 语义（NOT_FOUND/UNSUPPORTED）→
last_error。

```bash
cd examples/node
npm run build-stable     # 或 node-gyp rebuild（同时构建两个插件）
cd ../..
node examples/node/stable_smoke.js
```

这是 0E 的非阻塞 compatibility smoke（normative acceptance 是 Python
ctypes，见 `script/stable_abi_ctypes_test.py`）；环境缺少 node-gyp /
VS Build Tools 时不阻塞 R1.9 closure。

## 构建

需要 Node.js（>= 12）与 node-gyp（Windows 需要 VS Build Tools）。

```bash
cd examples/node
npm run build          # 等价于 node-gyp rebuild
```

## 运行

在项目根目录执行（插件会按相对路径找动态库）：

```bash
node examples/node/native_mmd_demo.js --frames 720
```

找不到动态库时：

```text
WISTERIA_NATIVE_LIB=C:\path\to\wisteria_native.dll   (Windows)
WISTERIA_NATIVE_LIB=/path/to/libwisteria_native.so   (Linux)
```

## 开窗 demo（M4）

加 `--window` 参数即打开真实桌面窗口（960x720），流程与 Python 开窗示例
一致：

```bash
node examples/node/native_mmd_demo.js --window --frames 360
```

窗口内快捷键：Space 暂停/恢复、C 切换 VMD 相机、←/→ 调速度、Esc 关闭。
