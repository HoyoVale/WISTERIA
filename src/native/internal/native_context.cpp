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
WisteriaContext gNextContextHandle = 1U;
}

Context::Context() = default;
Context::~Context() = default;

WisteriaContext RegisterContext()
{
    auto context = std::make_shared<Context>();
    std::lock_guard<std::mutex> lock(gContextMutex);
    const WisteriaContext handle = gNextContextHandle++;
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

void SetError(Context& context, std::string message)
{
    context.lastError = std::move(message);
}

enum WisteriaStatus InvalidHandle(Context& context, const char* message)
{
    SetError(context, message);
    return WISTERIA_ERROR_NOT_FOUND;
}

bool CopyErrorMessage(
    const std::string& message,
    char* buffer,
    size_t bufferSize
)
{
    if (buffer == nullptr || bufferSize == 0U)
        return false;
    const size_t copyLength = std::min(message.size(), bufferSize - 1U);
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
