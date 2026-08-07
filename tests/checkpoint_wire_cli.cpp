/*
 * R1.4 Phase 0A Step 8: minimal checkpoint wire CLI for the cross-process
 * regression. One process dumps a canonical checkpoint to bytes; a second
 * process deserializes those bytes and restores them on a fresh runtime.
 * Both sides print the exact physics hash of the checkpoint payload so the
 * harness can prove the wire bytes survived the process boundary.
 */

#include "wisteria/mmd/mmd_determinism.hpp"
#include "wisteria/runtime/checkpoint_serialization.hpp"
#include "wisteria/runtime/saba_mmd_runtime_model.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
using namespace wisteria;

std::unique_ptr<SabaMmdRuntimeModel> CreateRuntime(
    const std::filesystem::path& modelPath
)
{
    SabaPhysicsSettings settings;
    settings.fixedTimeStep = 1.0f / 120.0f;
    settings.maxSubSteps = 10;
    settings.gravity = glm::vec3(0.0f, -98.0f, 0.0f);
    settings.enabled = true;
    auto runtime = std::make_unique<SabaMmdRuntimeModel>(
        modelPath,
        std::filesystem::path{},
        settings
    );
    if (!runtime->Initialize())
    {
        throw std::runtime_error("runtime Initialize failed");
    }
    return runtime;
}

void CaptureCanonicalAt(
    SabaMmdRuntimeModel& runtime,
    MotionFrameIndex frame
)
{
    auto* stepper = dynamic_cast<IDeterministicFrameStepper*>(&runtime);
    if (stepper == nullptr)
    {
        throw std::runtime_error("runtime has no deterministic stepper");
    }
    if (stepper->PrepareFrameZero({}) != TimelineStatus::Ok)
    {
        throw std::runtime_error("PrepareFrameZero failed");
    }
    for (MotionFrameIndex current = 1U; current <= frame; ++current)
    {
        if (stepper->StepMotionFrameExact(current, {}) !=
            TimelineStatus::Ok)
        {
            throw std::runtime_error("StepMotionFrameExact failed");
        }
    }
}

FrameCheckpoint CreateCheckpointAt(
    SabaMmdRuntimeModel& runtime,
    MotionFrameIndex frame
)
{
    auto* mmd = dynamic_cast<MmdRuntimeModel*>(&runtime);
    if (mmd == nullptr)
    {
        throw std::runtime_error("runtime has no MMD surface");
    }
    mmd->SetMotionLooping(false);
    CaptureCanonicalAt(runtime, frame);
    FrameCheckpoint checkpoint;
    if (mmd->CreateCheckpoint(checkpoint) != TimelineStatus::Ok)
    {
        throw std::runtime_error("CreateCheckpoint failed");
    }
    return checkpoint;
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

int DumpMode(
    const std::filesystem::path& modelPath,
    MotionFrameIndex frame,
    const std::filesystem::path& outputPath
)
{
    auto runtime = CreateRuntime(modelPath);
    const FrameCheckpoint checkpoint =
        CreateCheckpointAt(*runtime, frame);
    const std::vector<std::uint8_t> bytes =
        SerializeCheckpoint(checkpoint);
    WriteFile(outputPath, bytes);

    const DeterminismHashes hashes = HashPhysics(checkpoint.physics);
    if (!hashes.valid)
    {
        throw std::runtime_error("dump produced invalid physics hash");
    }
    std::cout << "HASH=" << std::hex << hashes.exactHash << '\n';
    std::cout << "BYTES=" << std::dec << bytes.size() << '\n';
    return 0;
}

int LoadMode(
    const std::filesystem::path& modelPath,
    MotionFrameIndex frame,
    const std::filesystem::path& inputPath
)
{
    const std::vector<std::uint8_t> bytes = ReadFile(inputPath);
    FrameCheckpoint decoded;
    if (DeserializeCheckpoint(
            bytes.data(),
            bytes.size(),
            {},
            decoded
        ) != TimelineStatus::Ok)
    {
        throw std::runtime_error("deserialize failed");
    }

    auto runtime = CreateRuntime(modelPath);
    CaptureCanonicalAt(*runtime, frame + 30U);
    auto* mmd = dynamic_cast<MmdRuntimeModel*>(runtime.get());
    auto* stepper = dynamic_cast<IDeterministicFrameStepper*>(
        runtime.get()
    );
    if (mmd == nullptr || stepper == nullptr)
    {
        throw std::runtime_error("runtime lost MMD/stepper surface");
    }
    if (mmd->ReplayFromCheckpoint(decoded, frame) != TimelineStatus::Ok)
    {
        throw std::runtime_error("ReplayFromCheckpoint failed");
    }
    if (stepper->StepMotionFrameExact(frame + 1U, {}) !=
        TimelineStatus::Ok)
    {
        throw std::runtime_error("step after restore failed");
    }

    const DeterminismHashes hashes = HashPhysics(decoded.physics);
    if (!hashes.valid)
    {
        throw std::runtime_error("load produced invalid physics hash");
    }
    std::cout << "HASH=" << std::hex << hashes.exactHash << '\n';
    std::cout << "RESTORE_OK\n";
    return 0;
}
}  // namespace

int main(int argc, char** argv)
{
    try
    {
        if (argc != 5)
        {
            std::cerr
                << "usage: checkpoint_wire_cli "
                   "<dump|load> <model_path> <frame> <wire_file>\n";
            return 2;
        }
        const std::string mode = argv[1];
        const std::filesystem::path modelPath = argv[2];
        const wisteria::MotionFrameIndex frame =
            static_cast<wisteria::MotionFrameIndex>(
                std::stoull(argv[3])
            );
        const std::filesystem::path wirePath = argv[4];
        if (mode == "dump")
        {
            return DumpMode(modelPath, frame, wirePath);
        }
        if (mode == "load")
        {
            return LoadMode(modelPath, frame, wirePath);
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
