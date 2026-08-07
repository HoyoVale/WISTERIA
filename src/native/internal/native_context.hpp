#pragma once

#include "wisteria/native/wisteria_native.h"
#include "wisteria/runtime/mmd_runtime_model.hpp"
#include "wisteria/scene/scene.hpp"

#include <filesystem>
#include <exception>
#include <stdexcept>
#include <atomic>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>

namespace wisteria
{
class Application;
class Window;
}

namespace wisteria::native
{
struct ModelEntry
{
    std::unique_ptr<MmdRuntimeModel> runtime;
    WisteriaMotion currentMotion = 0U;
    bool hasMotion = false;
};

struct WindowEntry
{
    Window* window = nullptr;
    WisteriaScene boundScene = 0U;
    bool demoLoaded = false;
};

struct SceneEntry
{
    std::shared_ptr<Scene> scene;
    WisteriaWindow windowHandle = 0U;
    std::unordered_map<WisteriaSceneModel, ModelAsset*> models;
    std::unordered_map<WisteriaEntity, Entity*> entities;
    std::unordered_map<WisteriaEntity, WisteriaMotion> entityMotions;
    // Solid-color PBR materials cached per RGB key for set_part_color.
    std::unordered_map<std::uint32_t, Material*> solidMaterials;
    struct LightEntry
    {
        int kind = 0;  // 0 = directional, 1 = point
        void* light = nullptr;
    };
    std::unordered_map<WisteriaLight, LightEntry> lights;
};

struct StableContextState;

struct Context
{
    Context();
    ~Context();

    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;

    // R1.4 Phase 0B: Stable C ABI v1 context-owned state. Always
    // constructed so stable handles work on every registered context.
    std::unique_ptr<StableContextState> stable;

    std::unordered_map<WisteriaModel, std::unique_ptr<ModelEntry>> models;
    std::unique_ptr<Application> application;
    std::unordered_map<WisteriaWindow, std::unique_ptr<WindowEntry>> windows;
    std::unordered_map<WisteriaScene, std::unique_ptr<SceneEntry>> scenes;
    // Fixed-capacity error slot so recording a failure can never allocate
    // and throw inside an exception handler (noexcept ABI boundary).
    char lastError[512] = {};
};

using ContextLease = std::shared_ptr<Context>;

WisteriaContext RegisterContext();
ContextLease FindContext(WisteriaContext handle);
bool UnregisterContext(WisteriaContext handle);

// Global, process-wide, monotonic opaque handle allocator shared by every
// handle family (Context/Model/Motion/Window/Scene/SceneModel/Entity/Light).
// Never reuses a value, so a handle cannot collide across contexts, across
// scenes, across handle types, or with a destroyed object recreated later.
std::uint64_t AllocateOpaqueHandle() noexcept;

ModelEntry* FindModel(Context& context, WisteriaModel handle);
WindowEntry* FindWindow(Context& context, WisteriaWindow handle);
SceneEntry* FindScene(Context& context, WisteriaScene handle);
Entity* FindEntity(SceneEntry& scene, WisteriaEntity handle);
MmdRuntimeModel* FindEntityMmdRuntime(
    SceneEntry& scene,
    WisteriaEntity handle
);
// Never throws: truncates into the fixed error slot. Safe to call from an
// exception handler.
void TrySetError(Context* context, std::string_view message) noexcept;
enum WisteriaStatus InvalidHandle(Context& context, const char* message);

// Compatibility wrapper for functions that still use the older inner-guard
// style. New code should use InvokeAbi; this template remains so existing
// bodies compile until they migrate.
template<typename Function>
enum WisteriaStatus GuardAbi(Context& context, Function&& function) noexcept
{
    try
    {
        function();
        return WISTERIA_OK;
    }
    catch (const std::invalid_argument& error)
    {
        TrySetError(&context, error.what());
        return WISTERIA_ERROR_INVALID_ARGUMENT;
    }
    catch (const std::out_of_range& error)
    {
        TrySetError(&context, error.what());
        return WISTERIA_ERROR_NOT_FOUND;
    }
    catch (const std::exception& error)
    {
        TrySetError(&context, error.what());
        return WISTERIA_ERROR_INTERNAL;
    }
    catch (...)
    {
        TrySetError(&context, "Unknown C++ exception across native boundary");
        return WISTERIA_ERROR_INTERNAL;
    }
}

// The single outermost ABI wrapper. Finds the context, then runs the body
// with the full entry (context lookup, path conversion, filesystem calls,
// handle validation, core work) inside one exception boundary. Expected
// status codes are returned directly by the body; unexpected C++ exceptions
// are mapped here and never cross extern "C".
template<typename Function>
auto InvokeAbi(
    WisteriaContext contextHandle,
    Function&& function
) noexcept
    -> std::invoke_result_t<Function, Context&>
{
    // Resolve the lease outside the try block so the catch handlers can
    // record the error without re-running FindContext (whose mutex lock can
    // itself throw; re-throwing inside noexcept would terminate the process).
    ContextLease context;
    try
    {
        context = FindContext(contextHandle);
        if (context == nullptr)
            return WISTERIA_ERROR_NOT_FOUND;
        return function(*context);
    }
    catch (const std::invalid_argument& error)
    {
        TrySetError(context.get(), error.what());
        return WISTERIA_ERROR_INVALID_ARGUMENT;
    }
    catch (const std::out_of_range& error)
    {
        TrySetError(context.get(), error.what());
        return WISTERIA_ERROR_NOT_FOUND;
    }
    catch (const std::exception& error)
    {
        TrySetError(context.get(), error.what());
        return WISTERIA_ERROR_INTERNAL;
    }
    catch (...)
    {
        TrySetError(
            context.get(),
            "Unknown C++ exception across native boundary"
        );
        return WISTERIA_ERROR_INTERNAL;
    }
}

bool CopyErrorMessage(
    std::string_view message,
    char* buffer,
    size_t bufferSize
);
std::filesystem::path PathFromUtf8(const char* utf8);
bool ValidKey(int key) noexcept;
bool ValidMouseButton(int button) noexcept;
}
