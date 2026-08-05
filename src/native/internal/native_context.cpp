#include "native_context.hpp"

#include "wisteria/platform/application.hpp"
#include "../windows_path.hpp"

#include <algorithm>
#include <cstring>
#include <mutex>
#include <utility>

using namespace wisteria;

namespace wisteria::native
{
namespace
{
std::unordered_map<WisteriaContext, ContextLease> gContexts;
std::mutex gContextMutex;
std::atomic<std::uint64_t> gNextOpaqueHandle{1U};
}

Context::Context() = default;
Context::~Context() = default;

std::uint64_t AllocateOpaqueHandle() noexcept
{
    return gNextOpaqueHandle.fetch_add(1U, std::memory_order_relaxed);
}

WisteriaContext RegisterContext()
{
    auto context = std::make_shared<Context>();
    const WisteriaContext handle = AllocateOpaqueHandle();
    std::lock_guard<std::mutex> lock(gContextMutex);
    gContexts.emplace(handle, std::move(context));
    return handle;
}

ContextLease FindContext(WisteriaContext handle)
{
    std::lock_guard<std::mutex> lock(gContextMutex);
    const auto iterator = gContexts.find(handle);
    return iterator == gContexts.end() ? ContextLease{} : iterator->second;
}

bool UnregisterContext(WisteriaContext handle)
{
    std::lock_guard<std::mutex> lock(gContextMutex);
    return gContexts.erase(handle) != 0U;
}

ModelEntry* FindModel(Context& context, WisteriaModel handle)
{
    const auto iterator = context.models.find(handle);
    return iterator == context.models.end() ? nullptr : iterator->second.get();
}

WindowEntry* FindWindow(Context& context, WisteriaWindow handle)
{
    const auto iterator = context.windows.find(handle);
    return iterator == context.windows.end() ? nullptr : iterator->second.get();
}

SceneEntry* FindScene(Context& context, WisteriaScene handle)
{
    const auto iterator = context.scenes.find(handle);
    return iterator == context.scenes.end() ? nullptr : iterator->second.get();
}

Entity* FindEntity(SceneEntry& scene, WisteriaEntity handle)
{
    const auto iterator = scene.entities.find(handle);
    return iterator == scene.entities.end() ? nullptr : iterator->second;
}

MmdRuntimeModel* FindEntityMmdRuntime(
    SceneEntry& scene,
    WisteriaEntity handle
)
{
    Entity* entity = FindEntity(scene, handle);
    if (entity == nullptr)
        return nullptr;
    ModelInstance* instance = entity->TryGetModelInstance();
    return instance != nullptr ? instance->TryGetMmdRuntime() : nullptr;
}

void TrySetError(Context* context, std::string_view message) noexcept
{
    if (context == nullptr)
        return;
    const std::size_t copyLength = std::min(
        message.size(),
        sizeof(context->lastError) - 1U
    );
    std::memcpy(context->lastError, message.data(), copyLength);
    context->lastError[copyLength] = '\0';
}

enum WisteriaStatus InvalidHandle(Context& context, const char* message)
{
    TrySetError(&context, message);
    return WISTERIA_ERROR_NOT_FOUND;
}

bool CopyErrorMessage(
    std::string_view message,
    char* buffer,
    size_t bufferSize
)
{
    if (buffer == nullptr || bufferSize == 0U)
        return false;
    const size_t copyLength = std::min(message.size(), bufferSize - 1U);
    if (copyLength > 0U && message.data() != nullptr)
        std::memcpy(buffer, message.data(), copyLength);
    buffer[copyLength] = '\0';
    return true;
}

std::filesystem::path PathFromUtf8(const char* utf8)
{
#ifdef _WIN32
    if (utf8 == nullptr)
        return {};
    const std::wstring wide = WisteriaNativeUtf8ToWide(utf8);
    if (wide.empty())
        return {};
    return std::filesystem::path(wide);
#else
    return std::filesystem::path(utf8 != nullptr ? utf8 : "");
#endif
}

bool ValidKey(int key) noexcept
{
    return key >= 0 && key < WISTERIA_KEY_COUNT;
}

bool ValidMouseButton(int button) noexcept
{
    return button >= 0 && button < WISTERIA_MOUSE_COUNT;
}
}
