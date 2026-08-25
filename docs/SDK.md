# WISTERIA SDK

v1.1.0 起，WISTERIA 提供可安装的 SDK。当前阶段只正式发布 **Stable C ABI**；C++ 头文件作为源码级 API 供同源码树/后续版本使用，不承诺二进制兼容。

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
    └── native/
        ├── wisteria_stable_runtime.h   正式 ABI：runtime v1
        ├── wisteria_stable_render.h    正式 ABI：render v1
        └── wisteria_native.h           legacy v0.7（experimental）
```

CMake imported targets：

```cmake
Wisteria::native        共享库
Wisteria::sdk_headers   公共 include 目录
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

## 4. 线程与生命周期

- `WisteriaStableContext` 是 creator-thread-affine：同一 Context 的所有调用必须发生在创建它的线程上。这是调用方前置条件，v1 不做运行时线程校验。
- 状态码是权威结果；`wisteria_stable_last_error` 是 best-effort 的 sticky 诊断文本，成功的调用不会清空它。
- 可扩展结构体都带 `struct_size` / `struct_version`。调用方必须显式填充，库只识别已知版本。

## 5. 版本规则

| 版本 | 含义 |
| --- | --- |
| 产品版本 `WISTERIA_VERSION_*` | 每次发布递增，当前 1.1.0 |
| Runtime C ABI `WISTERIA_STABLE_RUNTIME_ABI_VERSION` | 当前 1 |
| Render C ABI `WISTERIA_STABLE_RENDER_ABI_VERSION` | 当前 1 |

规则：

- C ABI major 不兼容时拒绝使用；minor 只允许 additive 修改。
- 产品版本可以上升，而 C ABI 版本保持不变。
- legacy `wisteria_native.h` v0.7 不参与稳定承诺。

## 6. 平台

| 平台 | 状态 |
| --- | --- |
| Windows / MSVC | Supported |
| Linux / GCC / Clang | Supported（使用 `build_linux.sh` 构建） |
| WSLg / llvmpipe | Supported fallback |

## 7. 与 C++ 头文件的关系

- `include/wisteria/` 下的 C++ 头文件是源码级 API，随版本可能发生源码不兼容调整；不保证跨版本二进制兼容。
- C++ SDK 的正式打包（`wisteria_core` / `wisteria_platform` 的 install/export）计划在后续版本发布。

## 8. 验证

一键构建、安装并运行消费测试：

```powershell
.\run.ps1 sdk
```

等价步骤：

```powershell
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --config RelWithDebInfo --parallel
cmake --install build --config RelWithDebInfo --prefix build/sdk-install --component wisteria-sdk
cmake -S tests/sdk_consumer -B build/sdk-consumer -DCMAKE_PREFIX_PATH=build/sdk-install
cmake --build build/sdk-consumer --config RelWithDebInfo
build/sdk-consumer/RelWithDebInfo/wisteria_sdk_consumer.exe
```
