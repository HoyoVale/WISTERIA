// C5C: VRM 1.0 extension parsing smoke test.
//
// Reads a minimal GLB fixture, extracts the glTF JSON chunk with the internal
// ParseGlbJson helper, and parses VRMC_vrm through the vendored VRM.h header.
#include <nlohmann/json.hpp>

#define USE_VRMC_VRM_1_0
#include <VRMC/VRM.h>

#include "assets/glb_json.hpp"

#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace
{
bool ReadFile(const char* path, std::vector<unsigned char>& bytes)
{
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream)
        return false;
    const std::streampos end = stream.tellg();
    stream.seekg(0, std::ios::beg);
    bytes.resize(static_cast<std::size_t>(end));
    stream.read(
        reinterpret_cast<char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size())
    );
    return stream.good();
}
}

int main(int argc, char* argv[])
{
    if (argc != 2)
    {
        std::cerr << "usage: wisteria_vrm_parse_smoke <model.glb>\n";
        return 2;
    }

    std::vector<unsigned char> bytes;
    if (!ReadFile(argv[1], bytes))
    {
        std::cerr << "cannot read fixture: " << argv[1] << '\n';
        return 3;
    }

    std::string error;
    const auto json = wisteria::assets::ParseGlbJson(bytes, error);
    if (!json.has_value())
    {
        std::cerr << "ParseGlbJson failed: " << error << '\n';
        return 4;
    }
    if (!json->contains("extensions") ||
        !(*json)["extensions"].contains("VRMC_vrm"))
    {
        std::cerr << "fixture has no VRMC_vrm extension\n";
        return 5;
    }

    try
    {
        VRMC_VRM_1_0::Vrm vrm;
        VRMC_VRM_1_0::from_json(
            (*json)["extensions"]["VRMC_vrm"],
            vrm
        );
        if (vrm.specVersion != "1.0")
        {
            std::cerr << "unexpected VRM specVersion: " << vrm.specVersion << '\n';
            return 6;
        }
        if (vrm.meta.name != "WISTERIA Fixture")
        {
            std::cerr << "unexpected VRM meta name\n";
            return 7;
        }
        if (vrm.humanoid.humanBones.head.node != 2U ||
            vrm.humanoid.humanBones.hips.node != 0U ||
            vrm.humanoid.humanBones.leftHand.node != 11U ||
            vrm.humanoid.humanBones.rightFoot.node != 8U)
        {
            std::cerr << "VRM humanoid bone mapping mismatch\n";
            return 8;
        }
    }
    catch (const std::exception& error)
    {
        std::cerr << "VRM parse failed: " << error.what() << '\n';
        return 9;
    }

    std::cout << "VRM parse smoke OK (spec=" << "1.0" << ")\n";
    return 0;
}
