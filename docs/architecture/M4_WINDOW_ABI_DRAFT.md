# M4 窗口 C ABI 草案（v0.3，实验接口）

目标：让 Python / Node / Electron 等前端通过 `wisteria_native` 打开**真实
桌面窗口**并驱动 MMD demo（模型、动作、物理、镜头、输入），而不是只能跑
headless。

## 设计决策

1. **窗口所有权**：C ABI（`wisteria_native`）拥有 GLFW 原生窗口 + GL 上下文
   + 渲染器；前端只发命令。与现有 `wisteria.exe` 同一条渲染链路。
2. **驱动模式**：拉模式（pull）。前端对每个 Context 每帧只调用一次
   `wisteria_poll_and_render(ctx, dt)`，C ABI 内部执行
   `输入帧开始 → glfwPollEvents → 场景更新 → 渲染 → swap`。不做内部自循环，
   方便 Electron rAF / Python 循环接管节奏。
3. **场景复用**：窗口加载直接复用 `SetupSabaMmdDemoScene`（Saba 全链路：
   VMD 动画 / IK / morph / 物理 / CPU 蒙皮 / VMD 相机），与 `wisteria.exe`
   行为一致；不重新发明场景 API。
4. **与 M2 并存**：headless 模型句柄（M2）与窗口句柄（M4）在同一
   `WisteriaContext` 下互不干扰。
5. **线程契约不变**：一个 context 单线程驱动；跨线程由调用方串行化。
6. **实验状态**：v0.x 不承诺 ABI 稳定；窗口接口不得作为当前核心架构的反向依赖。

## 新增句柄与枚举

```c
typedef uint64_t WisteriaWindow;

enum WisteriaKey {        /* 与内部 InputKey 一一对应 */
    WISTERIA_KEY_W, A, S, D, Q, E, LEFT_SHIFT, ESCAPE, R, P, B, L, V,
    M, C, G, H, F3, SPACE, LEFT, RIGHT
};

enum WisteriaMouseButton { LEFT, RIGHT, MIDDLE };
```

## 新增 API（全部返回 WisteriaStatus）

### 窗口生命周期

```c
wisteria_window_create(
    WisteriaContext context, int width, int height,
    const char* title, WisteriaWindow* out_window);
wisteria_window_destroy(WisteriaContext context, WisteriaWindow window);
```

### demo 加载（与 wisteria.exe 等价）

```c
wisteria_window_load_demo(
    WisteriaContext context, WisteriaWindow window,
    const char* model_path,   /* 空 = 默认蕾米埃尔-白 */
    const char* motion_path,  /* 空 = 默认梦的翅膀 */
    const char* scene_path,   /* 空 = 无场景；传路径则进入场景模式 */
    float physics_fps,        /* 默认 120 */
    int32_t max_sub_steps);   /* 默认 10 */
```

内部调用 `SetupSabaMmdDemoScene`，模型/动作/相机轨道全部就绪，此后每帧
`wisteria_poll_and_render` 即可。

### 帧驱动

```c
wisteria_poll_and_render(
    WisteriaContext context, float delta_time);

/* v0.2 compatibility wrapper; still advances every window in the context. */
wisteria_window_poll_and_render(
    WisteriaContext context, WisteriaWindow window, float delta_time);
wisteria_window_should_close(
    WisteriaContext context, WisteriaWindow window, int32_t* out_closed);
```

### 输入查询（在 poll_and_render 之后读取）

```c
wisteria_window_is_key_down(ctx, win, WisteriaKey, int32_t* out);
wisteria_window_was_key_pressed(ctx, win, WisteriaKey, int32_t* out);
wisteria_window_was_key_released(ctx, win, WisteriaKey, int32_t* out);
wisteria_window_is_mouse_button_down(ctx, win, WisteriaMouseButton, int32_t*);
wisteria_window_cursor_delta(ctx, win, float* out_x, float* out_y);
wisteria_window_scroll_delta(ctx, win, float* out_y);
wisteria_window_set_cursor_captured(ctx, win, int32_t captured);
```

### 相机控制

```c
wisteria_window_set_camera(ctx, win, const float position[3],
                           const float target[3], const float up[3]);
wisteria_window_camera_pose(ctx, win, float out_position[3],
                            float out_target[3], float out_up[3]);
wisteria_window_set_camera_speed(ctx, win, float move_speed);
```

### 可选（v0.2 收尾再加）

- `wisteria_window_capture_bmp(ctx, win, const char* path)`：复用
  `WISTERIA_SCREENSHOT_DIR` 同款 BMP 截图逻辑，方便前端远程取图。

## 核心实现步骤

1. `Application` / `WindowManager` 增加公开的 `PollEventsAndRender(dt)`
   （把私有循环体拆成公开方法，`Application::Run` 继续用它）；
2. `wisteria_native` 的 `Context` 增加懒初始化的 `Application` 与窗口注册表，
   窗口句柄映射到 `WindowManager` 中的 `Window*`；
3. 头文件升到 v0.2，实现上述 API；所有包装沿用 `catch(...)` 兜底与
   UTF-8 路径转换；
4. 新增 C++ 测试 `TestNativeAbiWindowWhenAvailable`：创建窗口 → load_demo →
   渲染 30 帧 → 读输入/相机 → 关闭（无显示环境时自动跳过）；
5. Python：`examples/python/native_window_demo.py`（开窗 + 循环渲染 +
   Space/C/←/→ 提示，窗口关闭即退出）；
6. Node：扩展 N-API 插件，新增 `runWindowDemo(options)`；
7. 双平台验收：Windows + WSLg 下 Python/Node 各开窗跑 demo，帧率与
   输入查询正常。

## 验收清单

- [ ] Windows/Linux 编译通过，`nm`/dumpbin 看到新导出符号
- [ ] 自动测试：headless 全量 59+ 仍通过，窗口测试在 WSLg 下通过
- [ ] Python 脚本弹出真实窗口并动画（肉眼/截图确认）
- [ ] Node 脚本弹出真实窗口并动画
- [ ] 输入查询（按键/鼠标增量/滚轮）在示例中有输出
- [ ] 窗口关闭 → `should_close` 为真 → 脚本正常退出，无泄漏/崩溃
