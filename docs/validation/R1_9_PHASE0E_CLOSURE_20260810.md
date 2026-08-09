# R1.9 Phase 0E — ABI Compatibility Matrix + Final Closure（2026-08-10）

> 状态：**CLOSED**；Final Closure approved — 2026-08-10
> （ChatGPT 对 `5d62ebd — R1.9 0E` + 0E Closure 审计包复审通过）。
> 前置：Final Micro Patch II（`daba47c`）四矩阵全绿。

## 1. 0E 验收面（契约 §5 / Decision 5）

```text
C smoke                     wisteria.abi-c-smoke（C99 布局/常量守卫）
跨进程 checkpoint           stable-checkpoint-cross-process
                            （MMD / Generic / FULL 三变体）
Python ctypes（normative）  wisteria.stable-abi-ctypes（新增）
Node N-API（非阻塞 smoke）  examples/node/stable_smoke.js（新增）
ABI compatibility matrix    C_ABI_SAFETY_MATRIX.md（94 legacy + 30 stable）
四矩阵                      Windows CORE/FULL + Linux CORE/FULL
```

## 2. 新增交付物

```text
script/stable_abi_ctypes_test.py
  Python 标准库 ctypes 驱动冻结 stable 面，无 pip 依赖：
  context/info → Generic entity/capabilities → fingerprint →
  prepare/step/replay → checkpoint create/serialize/deserialize/
  restore → RenderSession 单帧 RGBA8（非零）→ sequence range
  （PNG + manifest）→ Static entity/capabilities/render →
  status 语义（garbage=NOT_FOUND、Static step=UNSUPPORTED、
  Generic load_motion=UNSUPPORTED）→ last_error → GPU-safe teardown

examples/node/binding_stable.cc + stable_smoke.js
  N-API 插件只调用 stable 头（不碰 legacy v0.7）：
  Generic entity + exact step/replay + checkpoint + 单帧 render +
  status 语义 + last_error

CMakeLists.txt
  +wisteria.stable-abi-ctypes（CORE，Python3 已在 abi-safety-matrix
  前置依赖中 REQUIRED）

examples/node/binding.gyp / package.json / README.md
  +wisteria_stable_demo target 与 build-stable 脚本
```

## 3. 验证结果（2026-08-10）

```text
Windows CORE：10/10 PASS（新增 wisteria.stable-abi-ctypes）
Windows FULL：11/11 PASS
Linux CORE（WSL，llvmpipe）：12/12 PASS
Linux FULL（WSL，llvmpipe）：13/13 PASS
ABI safety matrix：94 legacy + 30 stable（导出面不变）

Node N-API stable smoke：PASS（Windows，node v24.12.0 + node-gyp 12.4.0）
  entity + exact step + checkpoint + render
Python ctypes：PASS（Windows + WSL 均通过 ctest）
```

## 4. 矩阵证据对照

```text
C smoke            layout/opaque handle/版本化 struct/常量守卫       PASS
跨进程（MMD）      N/N+1 wire bytes 跨进程逐字节一致                PASS
跨进程（Generic）  payload kind 2 跨进程 restore 后继续步进          PASS
跨进程（FULL）     生产 PMX/VMD N/N+1 一致                           PASS
ctypes（normative）完整 stable 流程 + 错误模型 + teardown            PASS
Node（smoke）      stable 面跨语言可调用、状态码可见                  PASS
```

## 5. R1.9 Final Closure 状态

```text
R1.9 0A  CLOSED ✅（契约分类表冻结）
R1.9 0B  CLOSED ✅（Final Fix + Micro Fix + Micro Patch II）
R1.9 0C  CLOSED ✅（Generic payload kind 2 跨进程）
R1.9 0D  CLOSED ✅（Final Fix + Micro Fix + Micro Patch II）
R1.9 0E  CLOSED ✅（ABI compatibility matrix + Final Closure）

R1.9 FINAL CLOSURE ✅（tag `r1.9-final-closure`）
```

## 6. 遗留债务（不阻塞）

```text
R1.7 native-Linux hardware EGL gate：真机条件具备时补跑
  script/verify_r17_native_linux.sh；WSL llvmpipe 是 compatibility
  record，不代表发布基线。
Node N-API：非阻塞 smoke；CI 无 node-gyp 时跳过。
```

## 7. 复审注意事项

1. ctypes 脚本是 normative：stable 结构体布局在 Python 侧手工镜像，
   与 C 头逐字段对应；任何 C ABI 布局漂移都会在这里暴露。
2. Node 绑定只通过 GetProcAddress/dlsym 解析 stable 符号，不链接
   wisteria_native 导入库——验证运行时符号面而非编译期声明。
3. 0E 没有新增任何 stable 导出（ABI matrix 保持 94+30）；
   只新增了验证面。
