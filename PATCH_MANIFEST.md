# WISTERIA(17) R0 结构稳定化与渲染验收补丁

## 应用

1. 先备份或提交当前项目；
2. 将本压缩包内容覆盖到 WISTERIA 项目根目录；
3. 删除旧文件 `src/native/wisteria_native.cpp`，或运行：

```powershell
.\APPLY_R0_PATCH.ps1
```

```bash
./APPLY_R0_PATCH.sh
```

补丁不会包含或覆盖 `assets/`、`third-party/`、`.git/` 和任何构建目录。

## Windows 验收

```powershell
powershell -ExecutionPolicy Bypass -File .\script\verify_render.ps1
```

## Linux / WSLg 验收

```bash
./script/verify_render.sh --backend X11
```

无桌面环境：

```bash
./script/verify_render.sh --backend NULL
```

完整说明：`docs/architecture/R0_RENDER_MANUAL_ACCEPTANCE.md`。

## 核心变化

- shader Program 缓存改为每个 `Application/ResourceManager` 独立持有；
- GLFW 初始化/终止改为进程级引用计数；
- 新增 Context 级 `wisteria_poll_and_render(context, dt)`；
- C ABI 拆分为 common/model/window/internal；
- `WISTERIA_BUILD_NATIVE` 默认关闭，按需开启；
- Linux GLFW 后端可选 X11、Wayland、Both、Null；
- 资源路径统一使用 `std::filesystem` 和 `WISTERIA_ASSET_ROOT`；
- demo 支持 `--frames`、`--fixed-dt`、`--motion`、`--render-smoke`；
- 新增自动截图、像素哈希、GL 分阶段错误诊断；
- 新增 Python 双 Context 生命周期回归 demo；
- 新增 Windows/Linux 一键人工验收脚本。

## 本环境验证

```text
Linux NULL backend
wisteria: build passed
wisteria_native: build passed
wisteria_tests: build passed
CTest: 100% passed
Python demos: py_compile passed
Bash scripts: bash -n passed
```

真实 X11/Wayland/WSLg 画面必须由本机人工验收。
