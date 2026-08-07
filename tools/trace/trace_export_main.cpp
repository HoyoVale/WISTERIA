// R1.3B Phase 0B Step 9: WISTERIA canonical trace exporter.
//
// Replays a PMX/VMD corpus asset on the Saba runtime under the frozen
// deterministic profile (MMD_RAW preset, deterministic-cold-step-v1) and
// writes one JSONL row per sampled canonical boundary (contract §4.1:
// motionFrame 0 = prepared boundary, motionFrame N = exactly 4 x 120Hz
// ticks, physicsTick = 4N).
//
// usage:
//   wisteria_trace_export --model <pmx> [--motion <vmd>]
//       [--out <trace.jsonl>] [--frames N] [--sample-interval M]
//       [--linked-body PmxMaskOnly|DisableConstraintLinkedPairs]
//       [--mode2 PreserveAnimatedTranslation|FullTransformDiagnostic]

#include "wisteria/runtime/saba_mmd_runtime_model.hpp"
#include "wisteria/mmd/physics/mmd_physics_configuration.hpp"
#include "trace_jsonl.hpp"

#include <cstdint>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>

namespace
{
std::optional<std::string> TakeArg(
    int argc,
    char** argv,
    const std::string& name,
    std::string& value
)
{
    for (int index = 1; index + 1 < argc; ++index)
    {
        if (name == argv[index])
        {
            value = argv[index + 1];
            return std::nullopt;
        }
    }
    return "missing argument: " + name;
}

std::optional<std::string> TakeInt(
    int argc,
    char** argv,
    const std::string& name,
    std::uint64_t& value
)
{
    std::string text;
    if (const auto error = TakeArg(argc, argv, name, text))
        return error;
    // The whole string must be an unsigned decimal integer; reject signs,
    // garbage and trailing characters instead of relying on library behavior.
    if (text.empty())
        return "empty integer for " + name;
    for (const char character : text)
    {
        if (character < '0' || character > '9')
            return "invalid integer for " + name;
    }
    try
    {
        value = std::stoull(text);
    }
    catch (...)
    {
        return "invalid integer for " + name;
    }
    return std::nullopt;
}
}  // namespace

int main(int argc, char** argv)
{
    std::string modelPath;
    std::string motionPath;
    std::string outPath = "wisteria_trace.jsonl";
    std::uint64_t totalFrames = 300U;
    std::uint64_t sampleInterval = 10U;
    std::string linkedBody;
    std::string mode2;

    if (const auto error = TakeArg(argc, argv, "--model", modelPath))
    {
        std::cerr << *error << "\n";
        return 2;
    }
    TakeArg(argc, argv, "--motion", motionPath);
    TakeArg(argc, argv, "--out", outPath);
    if (const auto error = TakeInt(argc, argv, "--frames", totalFrames))
    {
        std::cerr << *error << "\n";
        return 2;
    }
    if (const auto error =
            TakeInt(argc, argv, "--sample-interval", sampleInterval))
    {
        std::cerr << *error << "\n";
        return 2;
    }
    TakeArg(argc, argv, "--linked-body", linkedBody);
    TakeArg(argc, argv, "--mode2", mode2);
    if (sampleInterval == 0U)
    {
        std::cerr << "--sample-interval must be >= 1\n";
        return 2;
    }

    wisteria::SabaMmdRuntimeModel runtime(
        std::filesystem::path(modelPath),
        motionPath.empty()
            ? std::filesystem::path()
            : std::filesystem::path(motionPath)
    );
    wisteria::MmdPhysicsConfiguration configuration =
        wisteria::BuildPresetConfiguration(wisteria::MmdPhysicsPreset::MmdRaw);
    if (!linkedBody.empty() || !mode2.empty())
    {
        wisteria::MmdPhysicsDiagnosticOverrides overrides;
        if (linkedBody == "DisableConstraintLinkedPairs")
        {
            overrides.linkedBodyCollision =
                wisteria::MmdLinkedBodyCollisionMode::
                    DisableConstraintLinkedPairs;
        }
        else if (linkedBody == "PmxMaskOnly")
        {
            overrides.linkedBodyCollision =
                wisteria::MmdLinkedBodyCollisionMode::PmxMaskOnly;
        }
        else if (!linkedBody.empty())
        {
            std::cerr << "unknown --linked-body value: " << linkedBody
                      << "\n";
            return 2;
        }
        if (mode2 == "FullTransformDiagnostic")
        {
            overrides.mode2 =
                wisteria::MmdMode2WritebackMode::FullTransformDiagnostic;
        }
        else if (mode2 == "PreserveAnimatedTranslation")
        {
            overrides.mode2 =
                wisteria::MmdMode2WritebackMode::
                    PreserveAnimatedTranslation;
        }
        else if (!mode2.empty())
        {
            std::cerr << "unknown --mode2 value: " << mode2 << "\n";
            return 2;
        }
        const wisteria::TimelineStatus deriveStatus =
            wisteria::DeriveDiagnosticConfiguration(
                configuration,
                overrides,
                configuration
            );
        if (deriveStatus != wisteria::TimelineStatus::Ok)
        {
            std::cerr << "deriving A/B configuration failed\n";
            return 1;
        }
    }
    const wisteria::TimelineStatus configStatus =
        runtime.SetMmdPhysicsConfiguration(configuration);
    if (configStatus != wisteria::TimelineStatus::Ok)
    {
        std::cerr << "configuration rejected: "
                  << static_cast<int>(configStatus) << "\n";
        return 1;
    }
    if (!runtime.Initialize())
    {
        std::cerr << "runtime initialization failed\n";
        return 1;
    }
    auto* stepper =
        dynamic_cast<wisteria::IDeterministicFrameStepper*>(&runtime);
    if (stepper == nullptr)
    {
        std::cerr << "runtime lost the deterministic stepper\n";
        return 1;
    }
    if (stepper->PrepareFrameZero({}) != wisteria::TimelineStatus::Ok)
    {
        std::cerr << "PrepareFrameZero failed\n";
        return 1;
    }

    std::ofstream output(outPath);
    if (!output.is_open())
    {
        std::cerr << "cannot open output: " << outPath << "\n";
        return 1;
    }

    const auto emit = [&](wisteria::MotionFrameIndex frame) -> bool
    {
        wisteria::MmdPhysicsTraceFrame trace;
        if (!runtime.CapturePhysicsTraceFrame(trace))
            return false;
        return wisteria::trace::WriteTraceFrameJson(trace, output);
    };

    if (!emit(0U))
    {
        std::cerr << "frame 0 capture failed\n";
        return 1;
    }
    std::uint64_t emitted = 1U;
    for (wisteria::MotionFrameIndex frame = 1U;
         frame <= totalFrames;
         ++frame)
    {
        if (stepper->StepMotionFrameExact(frame, {}) !=
            wisteria::TimelineStatus::Ok)
        {
            std::cerr << "StepMotionFrameExact failed at frame " << frame
                      << "\n";
            return 1;
        }
        if (frame % sampleInterval == 0U)
        {
            if (!emit(frame))
            {
                std::cerr << "capture failed at frame " << frame << "\n";
                return 1;
            }
            ++emitted;
        }
    }
    output.close();
    std::cout << "wrote " << outPath << " rows: " << emitted << "\n";
    return 0;
}
