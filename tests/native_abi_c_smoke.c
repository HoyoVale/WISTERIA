/*
 * R1.4 Phase 0A: pure-C layout/constant smoke for the Stable Runtime C ABI
 * v1 subset. Compiled with a C compiler only (no C++); asserts fixed layout
 * and numeric constants so a C caller never silently sees a different ABI.
 */

#include "wisteria/native/wisteria_stable_runtime.h"

#include <stddef.h>
#include <stdio.h>

#define STATIC_ASSERT(cond, name) \
    typedef char static_assert_##name[(cond) ? 1 : -1]

/* opaque handles are exactly uint64 */
STATIC_ASSERT(sizeof(WisteriaStableContext) == sizeof(uint64_t),
              context_handle_width);
STATIC_ASSERT(sizeof(WisteriaEntity) == sizeof(uint64_t),
              entity_handle_width);
STATIC_ASSERT(sizeof(WisteriaCheckpoint) == sizeof(uint64_t),
              checkpoint_handle_width);

/* versioned struct headers start with struct_size + struct_version */
STATIC_ASSERT(offsetof(WisteriaStableContextInfoV1, struct_size) == 0,
              context_size_offset);
STATIC_ASSERT(offsetof(WisteriaStableContextInfoV1, struct_version) == 4,
              context_version_offset);
STATIC_ASSERT(offsetof(WisteriaRuntimeCapabilitiesV1, struct_size) == 0,
              caps_size_offset);
STATIC_ASSERT(offsetof(WisteriaRuntimeCreationOptionsV1, struct_size) == 0,
              options_size_offset);
STATIC_ASSERT(offsetof(WisteriaCheckpointInfoV1, struct_size) == 0,
              checkpoint_info_size_offset);

/* fixed numeric constants */
STATIC_ASSERT(WISTERIA_STATUS_OK == 0u, status_ok);
STATIC_ASSERT(WISTERIA_STATUS_POISONED == 15u, status_poisoned);
STATIC_ASSERT(WISTERIA_BACKEND_ID_SABA_MMD == 1u, backend_saba);
STATIC_ASSERT(WISTERIA_PROFILE_ID_RAW == 1u, profile_raw);
STATIC_ASSERT(WISTERIA_PROFILE_ID_ADAPTIVE == 3u, profile_adaptive);
STATIC_ASSERT(WISTERIA_CAP_SUPPORTS_CHECKPOINT_SERIALIZATION == (1u << 6),
              cap_serialization);
STATIC_ASSERT(WISTERIA_CHECKPOINT_WIRE_VERSION == 1u, wire_version);
STATIC_ASSERT(WISTERIA_STABLE_RUNTIME_ABI_VERSION == 1u, abi_version);

int main(void)
{
    printf("wisteria stable ABI C smoke OK\n");
    return 0;
}
