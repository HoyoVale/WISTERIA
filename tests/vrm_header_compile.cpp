// C5A: compile-only regression for the vendored VRM.h parser.
//
// VRM.h is header-only and intentionally does not include nlohmann/json
// itself; consumers must include it first.
#include <nlohmann/json.hpp>

#define USE_VRMC_VRM_0_0
#define USE_VRMC_VRM_1_0
#include <VRMC/VRM.h>

#include <cstddef>

int main()
{
    // Default-construct both version data models. Actual VRM parsing tests
    // will be added with the C5B/C5C fixture work.
    VRMC_VRM_0_0::Vrm vrm0{};
    VRMC_VRM_1_0::Vrm vrm1{};
    (void)vrm0;
    (void)vrm1;
    return 0;
}
