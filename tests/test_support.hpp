#pragma once

// Shared test infrastructure for the WISTERIA test pyramid. Internal to
// tests/ so each tier (unit / runtime / integration / render) builds and
// runs independently instead of recompiling one monolithic test file.

#include "wisteria/animation/animation.hpp"
#include "wisteria/animation/animator.hpp"
#include "wisteria/scene/behaviour.hpp"
#include "wisteria/scene/demo_scene.hpp"
#include "wisteria/rendering/primitives/cube.hpp"
#include "wisteria/scene/entity.hpp"
#include "wisteria/assets/importer.hpp"
#include "wisteria/platform/input.hpp"
#include "wisteria/assets/manager.hpp"
#include "wisteria/assets/model_asset.hpp"
#include "wisteria/runtime/runtime_model_base.hpp"
#include "wisteria/runtime/mmd_runtime_model.hpp"
#include "wisteria/runtime/saba_mmd_runtime_model.hpp"
#if defined(WISTERIA_TEST_NATIVE_ABI)
#include "wisteria/native/wisteria_native.h"
#endif
#include "wisteria/assets/saba_mmd_importer.hpp"
#include "wisteria/animation/pose.hpp"
#include "wisteria/physics/physics_instance.hpp"
#include "wisteria/physics/physics_world.hpp"
#include "wisteria/rendering/light.hpp"
#include "wisteria/rendering/renderer.hpp"
#include "wisteria/scene/scene.hpp"
#include "wisteria/mmd/vmd_importer.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <exception>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_set>
#include <vector>

using namespace wisteria;

namespace
{
constexpr float Epsilon = 0.0001f;

const std::filesystem::path TestAssetDirectory = WISTERIA_TEST_ASSET_DIR;

const std::filesystem::path ProjectAssetDirectory = WISTERIA_PROJECT_ASSET_DIR;

class TestSkipped final : public std::exception
{
public:
    explicit TestSkipped(std::string reason)
        : reason(std::move(reason))
    {
    }

    const char* what() const noexcept override
    {
        return this->reason.c_str();
    }

private:
    std::string reason;
};

[[noreturn]] void SkipTest(std::string reason)
{
    throw TestSkipped(std::move(reason));
}

// FULL_ASSETS fixtures are optional unless the tier explicitly enables
// WISTERIA_TEST_FULL_ASSETS. Report them separately from SKIP so a missing
// full-asset fixture is never mistaken for a real PASS.
class TestNotConfigured final : public std::exception
{
public:
    explicit TestNotConfigured(std::string reason)
        : reason(std::move(reason))
    {
    }

    const char* what() const noexcept override
    {
        return this->reason.c_str();
    }

private:
    std::string reason;
};

[[noreturn]] void NotConfigured(std::string reason)
{
    throw TestNotConfigured(std::move(reason));
}

void Require(bool condition, const std::string& message)
{
    if (!condition)
        throw std::runtime_error(message);
}

bool NearlyEqual(float left, float right)
{
    return std::abs(left - right) <= Epsilon;
}

bool NearlyEqual(const glm::vec3& left, const glm::vec3& right)
{
    return NearlyEqual(left.x, right.x) &&
        NearlyEqual(left.y, right.y) &&
        NearlyEqual(left.z, right.z);
}

bool NearlyEqual(const glm::vec4& left, const glm::vec4& right)
{
    return NearlyEqual(left.x, right.x) &&
        NearlyEqual(left.y, right.y) &&
        NearlyEqual(left.z, right.z) &&
        NearlyEqual(left.w, right.w);
}

bool NearlySameRotation(const glm::quat& left, const glm::quat& right)
{
    return NearlyEqual(std::abs(glm::dot(
        glm::normalize(left),
        glm::normalize(right)
    )), 1.0f);
}

bool NearlyEqual(const glm::mat4& left, const glm::mat4& right)
{
    for (glm::length_t column = 0; column < 4; ++column)
    {
        for (glm::length_t row = 0; row < 4; ++row)
        {
            if (!NearlyEqual(left[column][row], right[column][row]))
                return false;
        }
    }
    return true;
}

float MatrixIdentityDeviation(const glm::mat4& matrix)
{
    float result = 0.0f;
    for (glm::length_t column = 0; column < 4; ++column)
    {
        for (glm::length_t row = 0; row < 4; ++row)
        {
            result = std::max(
                result,
                std::abs(matrix[column][row] -
                    (column == row ? 1.0f : 0.0f))
            );
        }
    }
    return result;
}

// Shared physics-lifecycle fixtures used by the accumulator (unit) and the
// PhysicsInstance lifecycle (runtime) tests.
struct PhysicsLifecycleCounters
{
    int prepareCount = 0;
    int substepCount = 0;
    int finishCount = 0;
    int resetCount = 0;
    float lastDeltaTime = 0.0f;
    float lastSubstepAlpha = 0.0f;
    float lastFixedTimeStep = 0.0f;
    std::vector<float> substepAlphas;
};

class CountingPhysicsInstance final : public PhysicsInstance
{
public:
    explicit CountingPhysicsInstance(PhysicsLifecycleCounters& counters)
        : counters(&counters)
    {
    }

    void PrepareSimulation(float deltaTime) override
    {
        ++this->counters->prepareCount;
        this->counters->lastDeltaTime = deltaTime;
    }

    void PrepareSimulationSubstep(
        float alpha,
        float fixedTimeStep
    ) override
    {
        ++this->counters->substepCount;
        this->counters->lastSubstepAlpha = alpha;
        this->counters->lastFixedTimeStep = fixedTimeStep;
        this->counters->substepAlphas.push_back(alpha);
    }

    void FinishSimulation() override
    {
        ++this->counters->finishCount;
    }

    void ResetSimulation() override
    {
        ++this->counters->resetCount;
    }

private:
    PhysicsLifecycleCounters* counters = nullptr;
};

template<typename Function>
bool RunTest(const char* name, Function&& function)
{
    try
    {
        function();
        std::cout << "[PASS] " << name << '\n';
        return true;
    }
    catch (const TestSkipped& skipped)
    {
        std::cout << "[SKIP] " << name << ": " << skipped.what() << '\n';
        return true;
    }
    catch (const TestNotConfigured& notConfigured)
    {
        std::cout << "[NOT_CONFIGURED] " << name << ": "
                  << notConfigured.what() << '\n';
        return true;
    }
    catch (const std::exception& error)
    {
        std::cerr << "[FAIL] " << name << ": " << error.what() << '\n';
        return false;
    }
}
}
