#pragma once

// Authority fixture manifest for WISTERIA tests. tests/ must not hard-code
// asset paths; every path resolves through this manifest by fixture ID.
// The manifest is the single source of truth for what belongs to the fixed
// CORE set (repository-owned, must exist) versus the optional FULL_ASSETS
// set (project MMD/VMD assets, required only when the tier is enabled).

#include "test_support.hpp"

#include <nlohmann/json.hpp>

#include <fstream>
#include <string_view>

namespace
{
std::filesystem::path FixtureRepositoryRoot()
{
    // WISTERIA_TEST_FIXTURES points at <root>/tests/fixtures/fixtures.json.
    return std::filesystem::path(WISTERIA_TEST_FIXTURES)
        .parent_path()
        .parent_path()
        .parent_path();
}

const nlohmann::json& FixtureManifest()
{
    static const nlohmann::json manifest = []() -> nlohmann::json
    {
        std::ifstream stream(WISTERIA_TEST_FIXTURES);
        if (!stream.is_open())
        {
            throw std::runtime_error(
                "fixture manifest is unreadable: " WISTERIA_TEST_FIXTURES
            );
        }
        return nlohmann::json::parse(stream);
    }();
    return manifest;
}

// Resolves a fixture ID from the manifest to an absolute repository path.
// An unknown ID is a hard error: tests must not invent paths.
std::filesystem::path FixturePath(std::string_view id)
{
    const nlohmann::json& manifest = FixtureManifest();
    for (const char* group : {"core", "fullAssets"})
    {
        for (const auto& entry : manifest[group])
        {
            if (entry.at("id").get<std::string>() == id)
            {
                // Manifest paths are UTF-8. On Windows a narrow std::string
                // would be decoded with the ANSI code page, corrupting any
                // non-ASCII (e.g. CJK) fixture names. Construct through
                // std::u8string so the path is interpreted as UTF-8 on all
                // platforms.
                const std::string utf8 = entry.at("path").get<std::string>();
                return FixtureRepositoryRoot() /
                    std::filesystem::path(std::u8string(
                        reinterpret_cast<const char8_t*>(utf8.data()),
                        utf8.size()
                    ));
            }
        }
    }
    throw std::runtime_error(
        "fixture id not in manifest: " + std::string(id)
    );
}

// Full-asset tests only execute under WISTERIA_TEST_FULL_ASSETS. In the
// default CORE tier they are reported NOT_CONFIGURED as a group, so the CORE
// result is identical on every machine regardless of local assets.
void RequireFullAssetsTier()
{
#if !defined(WISTERIA_TEST_FULL_ASSETS)
    NotConfigured("FULL_ASSETS tier is not enabled");
#endif
}

// FULL_ASSETS fixture existence check. With the tier enabled a missing
// fixture is a hard failure; the tier gate above already skipped the caller
// otherwise, so this path only runs under FULL_ASSETS.
void RequireFullAsset(std::string_view id)
{
    const std::filesystem::path path = FixturePath(id);
    if (std::filesystem::is_regular_file(path))
        return;
    throw std::runtime_error(
        "FULL_ASSETS fixture missing: " + std::string(id) +
        " at " + path.string()
    );
}

// FULL_ASSETS directory existence check (same semantics as above).
void RequireFullAssetDirectory(std::string_view id)
{
    const std::filesystem::path path = FixturePath(id);
    if (std::filesystem::is_directory(path))
        return;
    throw std::runtime_error(
        "FULL_ASSETS directory missing: " + std::string(id) +
        " at " + path.string()
    );
}

// CORE fixtures must exist in every tier. A missing core asset is a hard
// failure and may never be skipped.
void RequireCoreAsset(std::string_view id)
{
    const std::filesystem::path path = FixturePath(id);
    Require(
        std::filesystem::is_regular_file(path),
        "core fixture missing: " + std::string(id) +
            " at " + path.string()
    );
}
}  // namespace
