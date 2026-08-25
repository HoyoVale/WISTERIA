// Installed WISTERIA SDK smoke test.
//
// Verifies that a plain C consumer can:
//   - include the stable C ABI headers from the install tree
//   - link against Wisteria::native via find_package(Wisteria)
//   - create/inspect/destroy a stable context
#include "wisteria/native/wisteria_stable_runtime.h"

#include <stdio.h>
#include <string.h>

int main(void)
{
    WisteriaStableContext context = 0U;
    WisteriaStableContextInfoV1 info;

    if (wisteria_stable_context_create(&context) != WISTERIA_STATUS_OK)
    {
        fputs("context_create failed\n", stderr);
        return 1;
    }
    if (context == 0U)
    {
        fputs("context_create returned a null handle\n", stderr);
        return 2;
    }

    memset(&info, 0, sizeof(info));
    info.struct_size = (uint32_t)sizeof(info);
    info.struct_version = 1U;
    if (wisteria_stable_context_info(context, &info) != WISTERIA_STATUS_OK)
    {
        (void)wisteria_stable_context_destroy(context);
        fputs("context_info failed\n", stderr);
        return 3;
    }
    if (info.abi_version != WISTERIA_STABLE_RUNTIME_ABI_VERSION)
    {
        (void)wisteria_stable_context_destroy(context);
        fprintf(stderr, "unexpected ABI version: %u\n", info.abi_version);
        return 4;
    }

    if (wisteria_stable_context_destroy(context) != WISTERIA_STATUS_OK)
    {
        fputs("context_destroy failed\n", stderr);
        return 5;
    }

    printf("WISTERIA SDK consumer OK (runtime ABI v%u)\n",
        (unsigned)info.abi_version);
    return 0;
}
