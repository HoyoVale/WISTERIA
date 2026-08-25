# WISTERIA SDK

v1.1.0 起，WISTERIA 提供可安装的 SDK，包含：

- **Stable C ABI**：正式的跨语言二进制接口；
- **C++ RAII 封装**：`wisteria/sdk/` 下 header-only 的类型安全包装，底层仍是 Stable C ABI。

完整 C++ 引擎头文件（`Scene` / `Renderer` 等）仍作为源码级 API 供同源码树使用，不承诺二进制兼容。

## 1. SDK 组成

安装树：

```text
<prefix>/
├── bin/
│   └── wisteria_native.dll            Windows 共享库（Linux 为 lib/ 下 .so）
├── lib/
│   ├── wisteria_native.lib            Windows import library
│   └── cmake/Wisteria/
│       ├── WisteriaConfig.cmake
│       ├── WisteriaConfigVersion.cmake
│       └── WisteriaTargets.cmake
└── include/wisteria/
    ├── core/version.hpp
    ├── native/
    │   ├── wisteria_stable_runtime.h   正式 ABI：runtime v1
    │   ├── wisteria_stable_render.h    正式 ABI：render v1
    │   └── wisteria_native.h           legacy v0.7（experimental）
    └── sdk/
        ├── wisteria_sdk.hpp            C++ RAII 封装总入口
        ├── context.hpp
        ├── entity.hpp
        ├── checkpoint.hpp
        ├── render_session.hpp
        └── status.hpp
```

CMake imported targets：

```cmake
Wisteria::native        共享库
Wisteria::sdk_headers   公共 include 目录
Wisteria::cpp           header-only C++ RAII 封装
```

## 2. 消费方式

### 2.1 CMake

```cmake
cmake_minimum_required(VERSION 3.20)
project(my_app LANGUAGES C)

find_package(Wisteria 1.1 CONFIG REQUIRED)

add_executable(my_app main.c)
target_link_libraries(my_app PRIVATE Wisteria::native)
```

纯 C 程序可以直接链接；不需要声明 C++ 语言。

### 2.2 手工链接（不推荐）

Windows：

```text
include:  <prefix>/include
lib:      <prefix>/lib/wisteria_native.lib
dll:      <prefix>/bin/wisteria_native.dll（运行时需在 PATH）
```

## 3. 使用 Stable C ABI

最小示例：

```c
#include "wisteria/native/wisteria_stable_runtime.h"

#include <stdio.h>
#include <string.h>

int main(void)
{
    WisteriaStableContext context = 0U;
    WisteriaStableContextInfoV1 info;

    if (wisteria_stable_context_create(&context) != WISTERIA_STATUS_OK)
        return 1;

    memset(&info, 0, sizeof(info));
    info.struct_size = (uint32_t)sizeof(info);
    info.struct_version = 1U;
    if (wisteria_stable_context_info(context, &info) != WISTERIA_STATUS_OK)
        return 2;

    printf("runtime ABI v%u\n", (unsigned)info.abi_version);

    wisteria_stable_context_destroy(context);
    return 0;
}
```

完整可编译版本：`tests/sdk_consumer/main.c`。

## 4. 使用 C++ RAII 封装

C++ 消费者链接 `Wisteria::cpp`：

```cmake
find_package(Wisteria 1.1 CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE Wisteria::cpp)
```

最小示例：

```cpp
#include "wisteria/sdk/wisteria_sdk.hpp"

int main()
{
    wisteria::sdk::Context context;
    const auto abi = context.RuntimeAbiVersion();
    return abi == WISTERIA_STABLE_RUNTIME_ABI_VERSION ? 0 : 1;
}
```

提供的封装：

| 类 | 说明 |
| --- | --- |
| `Context` | Context RAII、ABI 查询、last_error |
| `Entity` | 模型实体、morph override、motion、精确步进 |
| `Checkpoint` | 快照创建/恢复/二进制序列化 |
| `RenderSession` | 单帧离线渲染与帧序列 |
| `StatusError` | 携带 stable status code 的异常 |

封装层是 header-only 的，不新增二进制 ABI；兼容性仍由 Stable C ABI 契约管理。
## 5. 线程与生命周期

- `WisteriaStableContext` 是 creator-thread-affine：同一 Context 的所有调用必须发生在创建它的线程上。这是调用方前置条件，v1 不做运行时线程校验。
- 状态码是权威结果；`wisteria_stable_last_error` 是 best-effort 的 sticky 诊断文本，成功的调用不会清空它。
- 可扩展结构体都带 `struct_size` / `struct_version`。调用方必须显式填充，库只识别已知版本。

## 6. 版本规则

| 版本 | 含义 |
| --- | --- |
| 产品版本 `WISTERIA_VERSION_*` | 每次发布递增，当前 1.1.0 |
| Runtime C ABI `WISTERIA_STABLE_RUNTIME_ABI_VERSION` | 当前 1 |
| Render C ABI `WISTERIA_STABLE_RENDER_ABI_VERSION` | 当前 1 |

规则：

- C ABI major 不兼容时拒绝使用；minor 只允许 additive 修改。
- 产品版本可以上升，而 C ABI 版本保持不变。
- legacy `wisteria_native.h` v0.7 不参与稳定承诺。

## 7. 平台

| 平台 | 状态 |
| --- | --- |
| Windows / MSVC | Supported |
| Linux / GCC / Clang | Supported（使用 `build_linux.sh` 构建） |
| WSLg / llvmpipe | Supported fallback |

## 8. 与 C++ 头文件的关系

- `include/wisteria/sdk/` 是正式发布的 header-only C++ RAII 封装，ABI 兼容性由 Stable C ABI 保证。
- `include/wisteria/` 下的 C++ 头文件是源码级 API，随版本可能发生源码不兼容调整；不保证跨版本二进制兼容。
- `wisteria_core` / `wisteria_platform` 的完整 C++ 引擎库 install/export 计划在后续版本发布。

## 9. 验证

一键构建、安装并运行消费测试：

```powershell
.\run.ps1 sdk
```

生成 SDK zip 包：

```powershell
.\run.ps1 package
```

产物位于 `artifacts/sdk/wisteria-sdk-<version>-<platform>.zip`。

等价步骤：

```powershell
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --config RelWithDebInfo --parallel
cmake --install build --config RelWithDebInfo --prefix build/sdk-install --component wisteria-sdk
cmake -S tests/sdk_consumer -B build/sdk-consumer -DCMAKE_PREFIX_PATH=build/sdk-install
cmake --build build/sdk-consumer --config RelWithDebInfo
build/sdk-consumer/RelWithDebInfo/wisteria_sdk_consumer.exe
```
