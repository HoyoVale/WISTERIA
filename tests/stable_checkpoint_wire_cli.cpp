/*
 * R1.4 Phase 0B: Stable C ABI cross-process checkpoint E2E CLI.
 *
 * Process A (dump) exercises the public stable surface end-to-end:
 *   context -> entity -> (optional VMD) -> replay N -> checkpoint N bytes
 *   -> step N+1 -> checkpoint N+1 bytes
 *
 * Process B (load) deserializes the N bytes, restores them on a fresh
 * entity, re-creates checkpoint N and steps to N+1. The harness compares
 * the wire bytes byte-for-byte (same build identity, deterministic codec).
 *
 * Fixture ids are resolved through tests/fixtures/fixtures.json so UTF-8
 * paths never cross the Windows argv ANSI code page.
 */

#include "wisteria/native/wisteria_stable_runtime.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
std::filesystem::path FixtureRepositoryRoot()
{
    return std::filesystem::path(WISTERIA_TEST_FIXTURES)
        .parent_path()
        .parent_path()
        .parent_path();
}

std::filesystem::path ResolveFixture(const std::string& id)
{
    std::ifstream stream(WISTERIA_TEST_FIXTURES);
    if (!stream.is_open())
    {
        throw std::runtime_error("fixture manifest is unreadable");
    }
    const nlohmann::json manifest = nlohmann::json::parse(stream);
    for (const char* group : {"core", "fullAssets"})
    {
        for (const auto& entry : manifest[group])
        {
            if (entry.at("id").get<std::string>() == id)
            {
                const std::string utf8 =
                    entry.at("path").get<std::string>();
                return FixtureRepositoryRoot() /
                    std::filesystem::path(std::u8string(
                        reinterpret_cast<const char8_t*>(utf8.data()),
                        utf8.size()
                    ));
            }
        }
    }
    throw std::runtime_error("fixture id not found: " + id);
}

std::string PathUtf8(const std::filesystem::path& path)
{
    const std::u8string utf8 = path.u8string();
    return std::string(
        reinterpret_cast<const char*>(utf8.data()),
        utf8.size()
    );
}

std::vector<std::uint8_t> ReadFile(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        throw std::runtime_error("cannot open input file");
    }
    return std::vector<std::uint8_t>(
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()
    );
}

void WriteFile(
    const std::filesystem::path& path,
    const std::vector<std::uint8_t>& bytes
)
{
    std::ofstream output(path, std::ios::binary);
    if (!output)
    {
        throw std::runtime_error("cannot open output file");
    }
    output.write(
        reinterpret_cast<const char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size())
    );
}

void FillDefaultOptions(WisteriaRuntimeCreationOptionsV1& options)
{
    std::memset(&options, 0, sizeof(options));
    options.struct_size = sizeof(options);
    options.struct_version = 1U;
    options.compatibility = WISTERIA_PROFILE_ID_RAW;
    options.fixed_time_step = 1.0f / 120.0f;
    options.max_sub_steps = 10;
    options.gravity[1] = -98.0f;
    options.physics_enabled = 1;
}

std::vector<std::uint8_t> SerializeCheckpointBytes(
    WisteriaStableContext context,
    WisteriaCheckpoint checkpoint
)
{
    std::uint64_t size = 0U;
    if (wisteria_stable_checkpoint_serialize(
            context,
            checkpoint,
            nullptr,
            &size
        ) != WISTERIA_STATUS_OK)
    {
        throw std::runtime_error("serialize size query failed");
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    if (wisteria_stable_checkpoint_serialize(
            context,
            checkpoint,
            bytes.data(),
            &size
        ) != WISTERIA_STATUS_OK)
    {
        throw std::runtime_error("serialize failed");
    }
    return bytes;
}

int DumpMode(
    const std::filesystem::path& modelPath,
    const std::filesystem::path& vmdPath,
    std::uint64_t frame,
    const std::filesystem::path& outN,
    const std::filesystem::path& outN1
)
{
    WisteriaStableContext context = 0U;
    if (wisteria_stable_context_create(&context) != WISTERIA_STATUS_OK ||
        context == 0U)
    {
        throw std::runtime_error("context create failed");
    }
    WisteriaRuntimeCreationOptionsV1 options;
    FillDefaultOptions(options);
    WisteriaEntity entity = 0U;
    if (wisteria_stable_entity_create(
            context,
            &options,
            PathUtf8(modelPath).c_str(),
            &entity
        ) != WISTERIA_STATUS_OK)
    {
        throw std::runtime_error("entity create failed");
    }
    if (!vmdPath.empty() &&
        wisteria_stable_entity_load_motion(
            context,
            entity,
            PathUtf8(vmdPath).c_str()
        ) != WISTERIA_STATUS_OK)
    {
        throw std::runtime_error("motion load failed");
    }
    if (wisteria_stable_entity_prepare_frame_zero(context, entity) !=
            WISTERIA_STATUS_OK ||
        wisteria_stable_entity_replay_exact(context, entity, frame) !=
            WISTERIA_STATUS_OK)
    {
        throw std::runtime_error("replay to N failed");
    }

    WisteriaCheckpoint checkpointN = 0U;
    if (wisteria_stable_checkpoint_create(
            context,
            entity,
            &checkpointN
        ) != WISTERIA_STATUS_OK)
    {
        throw std::runtime_error("checkpoint N create failed");
    }
    WriteFile(outN, SerializeCheckpointBytes(context, checkpointN));

    if (wisteria_stable_entity_step_exact(
            context,
            entity,
            frame + 1U
        ) != WISTERIA_STATUS_OK)
    {
        throw std::runtime_error("step N+1 failed");
    }
    WisteriaCheckpoint checkpointN1 = 0U;
    if (wisteria_stable_checkpoint_create(
            context,
            entity,
            &checkpointN1
        ) != WISTERIA_STATUS_OK)
    {
        throw std::runtime_error("checkpoint N+1 create failed");
    }
    WriteFile(outN1, SerializeCheckpointBytes(context, checkpointN1));

    (void)wisteria_stable_checkpoint_destroy(context, checkpointN);
    (void)wisteria_stable_checkpoint_destroy(context, checkpointN1);
    (void)wisteria_stable_entity_destroy(context, entity);
    (void)wisteria_stable_context_destroy(context);
    return 0;
}

int LoadMode(
    const std::filesystem::path& modelPath,
    const std::filesystem::path& vmdPath,
    std::uint64_t frame,
    const std::filesystem::path& inN,
    const std::filesystem::path& outN,
    const std::filesystem::path& outN1
)
{
    const std::vector<std::uint8_t> wireN = ReadFile(inN);

    WisteriaStableContext context = 0U;
    if (wisteria_stable_context_create(&context) != WISTERIA_STATUS_OK ||
        context == 0U)
    {
        throw std::runtime_error("context create failed");
    }
    WisteriaRuntimeCreationOptionsV1 options;
    FillDefaultOptions(options);
    WisteriaEntity entity = 0U;
    if (wisteria_stable_entity_create(
            context,
            &options,
            PathUtf8(modelPath).c_str(),
            &entity
        ) != WISTERIA_STATUS_OK)
    {
        throw std::runtime_error("entity create failed");
    }
    if (!vmdPath.empty() &&
        wisteria_stable_entity_load_motion(
            context,
            entity,
            PathUtf8(vmdPath).c_str()
        ) != WISTERIA_STATUS_OK)
    {
        throw std::runtime_error("motion load failed");
    }

    WisteriaCheckpoint wireCheckpoint = 0U;
    if (wisteria_stable_checkpoint_deserialize(
            context,
            wireN.data(),
            wireN.size(),
            &wireCheckpoint
        ) != WISTERIA_STATUS_OK)
    {
        throw std::runtime_error("deserialize failed");
    }
    if (wisteria_stable_checkpoint_restore(
            context,
            wireCheckpoint,
            entity
        ) != WISTERIA_STATUS_OK)
    {
        throw std::runtime_error("restore failed");
    }

    WisteriaCheckpoint restoredN = 0U;
    if (wisteria_stable_checkpoint_create(
            context,
            entity,
            &restoredN
        ) != WISTERIA_STATUS_OK)
    {
        throw std::runtime_error("checkpoint N after restore failed");
    }
    WriteFile(outN, SerializeCheckpointBytes(context, restoredN));

    if (wisteria_stable_entity_step_exact(
            context,
            entity,
            frame + 1U
        ) != WISTERIA_STATUS_OK)
    {
        throw std::runtime_error("step N+1 after restore failed");
    }
    WisteriaCheckpoint restoredN1 = 0U;
    if (wisteria_stable_checkpoint_create(
            context,
            entity,
            &restoredN1
        ) != WISTERIA_STATUS_OK)
    {
        throw std::runtime_error("checkpoint N+1 after restore failed");
    }
    WriteFile(outN1, SerializeCheckpointBytes(context, restoredN1));

    (void)wisteria_stable_checkpoint_destroy(context, wireCheckpoint);
    (void)wisteria_stable_checkpoint_destroy(context, restoredN);
    (void)wisteria_stable_checkpoint_destroy(context, restoredN1);
    (void)wisteria_stable_entity_destroy(context, entity);
    (void)wisteria_stable_context_destroy(context);
    return 0;
}
}  // namespace

int main(int argc, char** argv)
{
    try
    {
        const std::string mode = argc > 1 ? argv[1] : "";
        const std::string modelId = argc > 2 ? argv[2] : "";
        const std::string vmdId = argc > 3 ? argv[3] : "-";
        const std::uint64_t frame =
            argc > 4 ? static_cast<std::uint64_t>(std::stoull(argv[4]))
                     : 0U;
        if (modelId.empty())
        {
            std::cerr
                << "usage: stable_checkpoint_wire_cli "
                   "<dump|load> <model_id> <vmd_id|-> <frame> "
                   "<in/out_n> [out_n1]\n";
            return 2;
        }
        const std::filesystem::path modelPath =
            ResolveFixture(modelId);
        std::filesystem::path vmdPath;
        if (vmdId != "-")
        {
            vmdPath = ResolveFixture(vmdId);
        }
        if (mode == "dump")
        {
            if (argc != 7)
            {
                std::cerr << "dump requires out_n and out_n1\n";
                return 2;
            }
            return DumpMode(
                modelPath,
                vmdPath,
                frame,
                argv[5],
                argv[6]
            );
        }
        if (mode == "load")
        {
            if (argc != 8)
            {
                std::cerr << "load requires in_n, out_n and out_n1\n";
                return 2;
            }
            return LoadMode(
                modelPath,
                vmdPath,
                frame,
                argv[5],
                argv[6],
                argv[7]
            );
        }
        std::cerr << "unknown mode: " << mode << '\n';
        return 2;
    }
    catch (const std::exception& error)
    {
        std::cerr << "ERROR: " << error.what() << '\n';
        return 2;
    }
}
