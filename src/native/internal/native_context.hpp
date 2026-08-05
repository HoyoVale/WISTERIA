#pragma once

#include "wisteria/native/wisteria_native.h"
#include "wisteria/runtime/saba_mmd_runtime_model.hpp"
#include "wisteria/scene/scene.hpp"

#include <filesystem>
#include <memory>
#include <string>
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
    std::unique_ptr<SabaMmdRuntimeModel> runtime;
    WisteriaMotion currentMotion = 0U;
    bool hasMotion = false;
};

struct WindowEntry
{
    Window* window = nullptr;
    bool demoLoaded = false;
};

struct SceneEntry
{
    std::shared_ptr<Scene> scene;
    Window* window = nullptr;
    std::unordered_map<WisteriaSceneModel, ModelAsset*> models;
    WisteriaSceneModel nextModelHandle = 1U;
    std::unordered_map<WisteriaEntity, Entity*> entities;
    WisteriaEntity nextEntityHandle = 1U;
    // Solid-color PBR materials cached per RGB key for set_part_color.
    std::unordered_map<std::uint32_t, Material*> solidMaterials;
    struct LightEntry
    {
        int kind = 0;  // 0 = directional, 1 = point
        void* light = nullptr;
    };
    std::unordered_map<WisteriaLight, LightEntry> lights;
    WisteriaLight nextLightHandle = 1U;
};

struct Context
{
    Context();
    ~Context();

    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;

    std::unordered_map<WisteriaModel, std::unique_ptr<ModelEntry>> models;
    WisteriaModel nextModelHandle = 1U;
    WisteriaMotion nextMotionHandle = 1U;
    std::unique_ptr<Application> application;
    std::unordered_map<WisteriaWindow, std::unique_ptr<WindowEntry>> windows;
    WisteriaWindow nextWindowHandle = 1U;
    std::unordered_map<WisteriaScene, std::unique_ptr<SceneEntry>> scenes;
    WisteriaScene nextSceneHandle = 1U;
    std::string lastError;
};

using ContextLease = std::shared_ptr<Context>;

WisteriaContext RegisterContext();
ContextLease FindContext(WisteriaContext handle);
bool UnregisterContext(WisteriaContext handle);

ModelEntry* FindModel(Context& context, WisteriaModel handle);
WindowEntry* FindWindow(Context& context, WisteriaWindow handle);
SceneEntry* FindScene(Context& context, WisteriaScene handle);
void SetError(Context& context, std::string message);
enum WisteriaStatus InvalidHandle(Context& context, const char* message);
bool CopyErrorMessage(
    const std::string& message,
    char* buffer,
    size_t bufferSize
);
std::filesystem::path PathFromUtf8(const char* utf8);
bool ValidKey(int key) noexcept;
bool ValidMouseButton(int button) noexcept;
}
