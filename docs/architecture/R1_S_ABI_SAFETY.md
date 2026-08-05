# R1.S — C ABI Safety

> 状态：R1.S1–R1.S3 已完成（2026-08-06）。目标：94 个导出函数异常不穿越
> C 边界、句柄失效/重复销毁/跨 Context 全部拦截、父子生命周期明确。

## R1.S1 — 异常边界全覆盖

目标：94 个导出函数全部有明确异常边界，0 未保护。

### 初版实现（已废弃）

- `GuardAbi(context, [&]{...})` 统一映射：
  `invalid_argument → INVALID_ARGUMENT`、`out_of_range → NOT_FOUND`、
  `std::exception → INTERNAL`、`... → INTERNAL`；
- 需要细粒度错误码的路径（IO/PARSE/INITIALIZATION）保留 raw try/catch
  并显式 SetError，不强行套 GuardAbi 以免吞掉错误码；
- 纯 leaf（`wisteria_status_name` / `version_major` / `version_minor`）
  标记 `PROVEN_NO_THROW_LEAF`。

初版问题（复审发现，已修复）：局部 `try` 不覆盖 FindContext/路径转换/
filesystem 调用；超长路径会让 `filesystem_error` 穿出 `extern "C"`；
`GuardAbi` 的 catch 内 `SetError`（std::string 分配）可能 bad_alloc 触发
`std::terminate`。

### Final Fixup 实现（当前）

- 新增 `InvokeAbi(contextHandle, lambda)` 统一最外层包装：FindContext、
  句柄校验、路径转换、filesystem 操作、核心逻辑全部在同一个异常边界内；
  lambda 直接返回 `WisteriaStatus`，预期错误码保留细粒度语义；
- `lastError` 改为固定容量 `char[512]`，`TrySetError` 强制 noexcept，
  异常处理路径不再可能二次抛异常；
- `GuardAbi` 保留为兼容模板（内部改用 TrySetError），新代码走 InvokeAbi；
- 90/94 个入口走 InvokeAbi；`wisteria_create_context`（全局创建，无
  Context 可查）保留自身 try/catch；3 个 leaf 不变。

当前矩阵（`script/gen_abi_safety_matrix.py --check` 自动校验，已接入
CTest `wisteria.abi-safety-matrix`）：

| 状态 | 数量 |
| ---- | ---- |
| 总计 | 94 |
| INVOKE_ABI | 90 |
| GUARDED | 0 |
| RAW_TRY | 1 |
| PROVEN_NO_THROW_LEAF | 3 |
| UNGUARDED | 0 |

新增回归测试：`Native ABI exception boundary`——20000 字符超长路径调用
`wisteria_load_model` 必须返回状态码而非抛异常，且 out handle 保持 0。

## R1.S2 — 句柄边界（generation 语义）

### 初版实现（有缺陷，已修复）

初版依赖「每个 Context 各自从 1 递增、从不复用」的局部计数。复审发现：

- 每个 Context/Scene 独立从 1 开始，跨 Context 同值句柄会命中另一个
  Context 的同值对象（`wisteria_update(contextB, modelA=1)` 会操作 B 的
  model 1）；
- 原测试是死分支：先断言 `model == 0`，后面的 `if (model != 0U)` 永不
  执行；
- Context 销毁后重建，旧子句柄可能命中新 Context 的同值对象。

### Final Fixup 实现（当前）

- 新增进程级 `AllocateOpaqueHandle()`（`std::atomic<uint64_t>` 全局单调
  分配器），所有 Context/Model/Motion/Window/Scene/SceneModel/Entity/Light
  句柄共用，绝不复用任何值；
- 删除各 Context/Scene 的 `next*Handle` 局部计数；
- C ABI 签名不变（句柄仍是不透明 `uint64_t`），调用方不应依赖句柄从 1
  开始或连续。

- stale handle：销毁后不在 map，必然 NOT_FOUND；
- double destroy：第二次必 NOT_FOUND；
- cross-context：全局唯一值，跨 Context 不可能同值；
- 父对象销毁后的子句柄：级联 erase（见 R1.S3）。

新增测试：

- `Native ABI handle boundaries`：真实 core fixture 双上下文创建模型，
  验证句柄全局唯一、cross-context 拒绝、stale 句柄不命中重建 Context；
- `Native ABI window/scene cascade`：window 先销毁 → scene 级联失效、
  句柄不复用。

## R1.S3 — 生命周期规则

当前父子关系：

```text
Context
└── Window ──(bound)── Scene
    └── Entity / Model / Light / Environment
```

规则（已实现并验证）：

- Window destroy 级联删除绑定它的全部 Scene（防悬空 `Window*`）；
- Scene destroy 仅在仍绑定窗口时替换空 Scene，不顶掉新绑定的场景；
- Entity destroy 双调用安全（第二次 NOT_FOUND）；
- Scene destroy 双调用安全；
- Context destroy 使所有子句柄失效。

## 验收

Windows + Linux（RelWithDebInfo）CORE 与 FULL_ASSETS 均 5/5
（新增 `wisteria.abi-safety-matrix` 检查）。
矩阵 `--check` 通过，UNGUARDED = 0。
