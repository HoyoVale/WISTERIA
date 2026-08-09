#include "wisteria/common/pch.hpp"

#include "wisteria/platform/headless_context.hpp"
#include "wisteria/rendering/graphics_device.hpp"
#include "glfw_lifetime.hpp"

#include <GLFW/glfw3.h>
#include <glad/gl.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#if defined(WISTERIA_ENABLE_EGL)
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <dlfcn.h>

#include <glad/gl.h>

// Mesa exposes the surfaceless platform through a client extension; older
// EGL headers do not define the platform token. 0x31DD is the frozen Mesa
// value (EGL_MESA_platform_surfaceless).
#ifndef EGL_PLATFORM_SURFACELESS_MESA
#define EGL_PLATFORM_SURFACELESS_MESA 0x31DD
#endif

// EGL_KHR_no_config_context: a context may be created without an EGLConfig
// (promoted behavior on Mesa). Guard against older headers.
#ifndef EGL_NO_CONFIG_KHR
#define EGL_NO_CONFIG_KHR ((EGLConfig)0)
#endif

namespace wisteria
{
namespace
{
bool HasExtension(std::string_view extensions, std::string_view name)
{
    std::size_t position = 0U;
    while (position <= extensions.size())
    {
        const std::size_t end = extensions.find(' ', position);
        const std::size_t length =
            (end == std::string_view::npos) ? extensions.size() - position
                                            : end - position;
        if (extensions.substr(position, length) == name)
            return true;
        if (end == std::string_view::npos)
            break;
        position = end + 1U;
    }
    return false;
}

std::string EglErrorString(EGLint error)
{
    switch (error)
    {
    case EGL_SUCCESS:
        return "EGL_SUCCESS";
    case EGL_NOT_INITIALIZED:
        return "EGL_NOT_INITIALIZED";
    case EGL_BAD_ACCESS:
        return "EGL_BAD_ACCESS";
    case EGL_BAD_ALLOC:
        return "EGL_BAD_ALLOC";
    case EGL_BAD_ATTRIBUTE:
        return "EGL_BAD_ATTRIBUTE";
    case EGL_BAD_CONFIG:
        return "EGL_BAD_CONFIG";
    case EGL_BAD_CONTEXT:
        return "EGL_BAD_CONTEXT";
    case EGL_BAD_CURRENT_SURFACE:
        return "EGL_BAD_CURRENT_SURFACE";
    case EGL_BAD_DISPLAY:
        return "EGL_BAD_DISPLAY";
    case EGL_BAD_MATCH:
        return "EGL_BAD_MATCH";
    case EGL_BAD_NATIVE_PIXMAP:
        return "EGL_BAD_NATIVE_PIXMAP";
    case EGL_BAD_NATIVE_WINDOW:
        return "EGL_BAD_NATIVE_WINDOW";
    case EGL_BAD_PARAMETER:
        return "EGL_BAD_PARAMETER";
    case EGL_BAD_SURFACE:
        return "EGL_BAD_SURFACE";
    case EGL_CONTEXT_LOST:
        return "EGL_CONTEXT_LOST";
    default:
        return "EGL_ERROR(" + std::to_string(error) + ")";
    }
}

using QueryDevicesEXTProc = EGLBoolean(EGLAPIENTRY*)(
    EGLint max_devices,
    EGLDeviceEXT* devices,
    EGLint* num_devices
);
using QueryDeviceStringEXTProc = const char*(EGLAPIENTRY*)(
    EGLDeviceEXT device,
    EGLint name
);
using GetPlatformDisplayEXTProc = EGLDisplay(EGLAPIENTRY*)(
    EGLenum platform,
    void* native_display,
    const EGLint* attrib_list
);

const char* NullableString(const char* value)
{
    return value != nullptr ? value : "";
}

// Multiple headless sessions may obtain the same EGLDisplay (same platform
// parameters / device). eglTerminate invalidates every context of that
// display, so the provider reference-counts displays and only terminates
// the last owner.
std::mutex gEglDisplayMutex;
std::unordered_map<EGLDisplay, std::size_t> gEglDisplayRefCounts;

void AcquireEglDisplay(EGLDisplay display) noexcept
{
    std::lock_guard<std::mutex> lock(gEglDisplayMutex);
    ++gEglDisplayRefCounts[display];
}

// Returns true when this release must terminate the display (last owner).
bool ReleaseEglDisplay(EGLDisplay display) noexcept
{
    std::lock_guard<std::mutex> lock(gEglDisplayMutex);
    const auto iterator = gEglDisplayRefCounts.find(display);
    if (iterator == gEglDisplayRefCounts.end())
        return false;
    if (iterator->second > 1U)
    {
        --iterator->second;
        return false;
    }
    gEglDisplayRefCounts.erase(iterator);
    return true;
}

class EglHeadlessContext final : public IHeadlessContext
{
public:
    explicit EglHeadlessContext(const HeadlessContextOptions& options)
        : options(options)
    {
        try
        {
            this->Initialize();
        }
        catch (...)
        {
            // The destructor does not run for a failed construction; release
            // any EGL display/context created before the exception.
            this->DestroyResources();
            throw;
        }
    }

    ~EglHeadlessContext() override
    {
        this->DestroyResources();
    }

    EglHeadlessContext(const EglHeadlessContext&) = delete;
    EglHeadlessContext& operator=(const EglHeadlessContext&) = delete;

    void MakeCurrent() override
    {
        if (this->display == EGL_NO_DISPLAY ||
            this->context == EGL_NO_CONTEXT)
        {
            throw std::logic_error(
                "EGL headless context is not initialized"
            );
        }
        const EGLSurface draw =
            this->surface != EGL_NO_SURFACE ? this->surface : EGL_NO_SURFACE;
        if (!eglMakeCurrent(this->display, draw, draw, this->context))
        {
            throw std::runtime_error(
                "eglMakeCurrent failed: " +
                EglErrorString(eglGetError())
            );
        }
        // R1.7 Final Fix: register both identities so GraphicsDevice can
        // reason about context-local vs shared object ownership.
        GraphicsDevice::SetCurrentContext(this->ContextToken());
        GraphicsDevice::SetCurrentShareGroup(this->ShareGroupToken());
    }

    void ReleaseCurrent() override
    {
        if (this->display == EGL_NO_DISPLAY)
            return;
        if (!eglMakeCurrent(
            this->display,
            EGL_NO_SURFACE,
            EGL_NO_SURFACE,
            EGL_NO_CONTEXT
        ))
        {
            throw std::runtime_error(
                "eglMakeCurrent(EGL_NO_CONTEXT) failed: " +
                EglErrorString(eglGetError())
            );
        }
        GraphicsDevice::SetCurrentContext(nullptr);
        GraphicsDevice::SetCurrentShareGroup(nullptr);
    }

    GraphicsContextToken ContextToken() const noexcept override
    {
        return &this->contextIdentity;
    }

    GraphicsShareGroupToken ShareGroupToken() const noexcept override
    {
        return this->shareGroupToken;
    }

    std::string_view ProviderName() const noexcept override
    {
        return "EGL";
    }

    std::string_view PlatformName() const noexcept override
    {
        return this->platformName;
    }

    std::string_view EglVersion() const noexcept override
    {
        return this->eglVersion;
    }

    std::string_view EglVendor() const noexcept override
    {
        return this->eglVendor;
    }

    std::string_view Vendor() const noexcept override
    {
        return this->glVendor;
    }

    std::string_view Renderer() const noexcept override
    {
        return this->glRenderer;
    }

    std::string_view Version() const noexcept override
    {
        return this->glVersion;
    }

    bool IsSoftware() const noexcept override
    {
        return HasExtension(this->glRenderer, "llvmpipe") ||
            HasExtension(this->glRenderer, "softpipe") ||
            HasExtension(this->glRenderer, "swrast");
    }

private:
    void Initialize()
    {
        this->ChooseDisplay();
        this->CreateContext();
        this->LoadGlFunctions();
        this->CaptureDiagnostics();
        if (this->options.forceSoftware && !this->IsSoftware())
        {
            throw std::runtime_error(
                "forceSoftware: GL_RENDERER is not a software renderer "
                "(renderer=\"" + this->glRenderer + "\")"
            );
        }
        // Final Micro Fix: CreateHeadlessContext must return with no native
        // context current and no registered trackers. Context creation made
        // the context current to load GL entry points and read diagnostics;
        // the public MakeCurrent() transaction owns the "current" state from
        // now on. Otherwise a factory-created context-local object could be
        // recorded with a null owner and bypass the ownership gate.
        if (!eglMakeCurrent(
                this->display,
                EGL_NO_SURFACE,
                EGL_NO_SURFACE,
                EGL_NO_CONTEXT
            ))
        {
            throw std::runtime_error(
                "eglMakeCurrent(EGL_NO_CONTEXT) failed after initialization: "
                + EglErrorString(eglGetError())
            );
        }
        GraphicsDevice::SetCurrentContext(nullptr);
        GraphicsDevice::SetCurrentShareGroup(nullptr);
    }

    static std::string QueryClientExtensions()
    {
        // EGL 1.5 allows querying client extensions with EGL_NO_DISPLAY.
        // Older implementations return null; treat that as "no extensions".
        return NullableString(eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS));
    }

    static EGLDeviceEXT FindDevice(bool software)
    {
        const auto queryDevices =
            reinterpret_cast<QueryDevicesEXTProc>(
                eglGetProcAddress("eglQueryDevicesEXT")
            );
        const auto queryDeviceString =
            reinterpret_cast<QueryDeviceStringEXTProc>(
                eglGetProcAddress("eglQueryDeviceStringEXT")
            );
        if (queryDevices == nullptr || queryDeviceString == nullptr)
            return EGL_NO_DEVICE_EXT;

        EGLint count = 0;
        if (!queryDevices(0, nullptr, &count) || count <= 0)
            return EGL_NO_DEVICE_EXT;

        std::vector<EGLDeviceEXT> devices(
            static_cast<std::size_t>(count),
            EGL_NO_DEVICE_EXT
        );
        EGLint actual = 0;
        if (!queryDevices(count, devices.data(), &actual))
            return EGL_NO_DEVICE_EXT;

        for (EGLint index = 0; index < actual; ++index)
        {
            if (devices[static_cast<std::size_t>(index)] == EGL_NO_DEVICE_EXT)
                continue;
            const char* extensions =
                queryDeviceString(
                    devices[static_cast<std::size_t>(index)],
                    EGL_EXTENSIONS
                );
            const bool isSoftware = extensions != nullptr &&
                HasExtension(extensions, "EGL_MESA_device_software");
            if (isSoftware == software)
                return devices[static_cast<std::size_t>(index)];
        }
        return EGL_NO_DEVICE_EXT;
    }

    static EGLDisplay CreateDeviceDisplay(EGLDeviceEXT device)
    {
        const auto getPlatformDisplayExt =
            reinterpret_cast<GetPlatformDisplayEXTProc>(
                eglGetProcAddress("eglGetPlatformDisplayEXT")
            );
        if (getPlatformDisplayExt != nullptr)
        {
            const EGLDisplay extDisplay = getPlatformDisplayExt(
                EGL_PLATFORM_DEVICE_EXT,
                reinterpret_cast<void*>(device),
                nullptr
            );
            if (extDisplay != EGL_NO_DISPLAY)
                return extDisplay;
        }
        return eglGetPlatformDisplay(
            EGL_PLATFORM_DEVICE_EXT,
            reinterpret_cast<void*>(device),
            nullptr
        );
    }

    void UseDisplay(EGLDisplay candidate, std::string name, bool software)
    {
        if (candidate == EGL_NO_DISPLAY)
            return;
        if (!eglInitialize(candidate, nullptr, nullptr))
            return;
        AcquireEglDisplay(candidate);
        this->display = candidate;
        this->platformName = std::move(name);
        this->softwareSelected = software;
        this->displayInitialized = true;
    }

    void ChooseDisplay()
    {
        const std::string clientExtensions = QueryClientExtensions();

        if (this->options.forceSoftware)
        {
            const EGLDeviceEXT device = FindDevice(true);
            if (device == EGL_NO_DEVICE_EXT)
            {
                throw std::runtime_error(
                    "forceSoftware: no EGL_MESA_device_software device found"
                );
            }
            this->UseDisplay(
                CreateDeviceDisplay(device),
                "device-software",
                true
            );
            if (this->display == EGL_NO_DISPLAY)
            {
                throw std::runtime_error(
                    "forceSoftware: software EGL display failed to initialize"
                );
            }
            return;
        }

        if (HasExtension(clientExtensions, "EGL_MESA_platform_surfaceless"))
        {
            this->UseDisplay(
                eglGetPlatformDisplay(
                    EGL_PLATFORM_SURFACELESS_MESA,
                    EGL_DEFAULT_DISPLAY,
                    nullptr
                ),
                "surfaceless",
                false
            );
        }
        if (this->display != EGL_NO_DISPLAY)
            return;

        const EGLDeviceEXT hardwareDevice = FindDevice(false);
        if (hardwareDevice != EGL_NO_DEVICE_EXT)
        {
            this->UseDisplay(
                CreateDeviceDisplay(hardwareDevice),
                "device-hardware",
                false
            );
        }
        if (this->display != EGL_NO_DISPLAY)
            return;

        const EGLDeviceEXT softwareDevice = FindDevice(true);
        if (softwareDevice != EGL_NO_DEVICE_EXT)
        {
            this->UseDisplay(
                CreateDeviceDisplay(softwareDevice),
                "device-software",
                true
            );
        }
        if (this->display == EGL_NO_DISPLAY)
        {
            throw std::runtime_error(
                "no usable EGL display "
                "(EGL_MESA_platform_surfaceless / EGL_EXT_platform_device)"
            );
        }
    }

    EGLConfig ChooseConfig()
    {
        const EGLint attribs[] = {
            EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
            EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
            EGL_RED_SIZE, 8,
            EGL_GREEN_SIZE, 8,
            EGL_BLUE_SIZE, 8,
            EGL_ALPHA_SIZE, 8,
            EGL_DEPTH_SIZE, 24,
            EGL_STENCIL_SIZE, 8,
            EGL_NONE
        };
        EGLConfig result = nullptr;
        EGLint count = 0;
        if (eglChooseConfig(
                this->display,
                attribs,
                &result,
                1,
                &count
            ) && count >= 1)
        {
            return result;
        }

        const EGLint looseAttribs[] = {
            EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
            EGL_NONE
        };
        count = 0;
        if (eglChooseConfig(
                this->display,
                looseAttribs,
                &result,
                1,
                &count
            ) && count >= 1)
        {
            return result;
        }
        throw std::runtime_error(
            "eglChooseConfig failed: " + EglErrorString(eglGetError())
        );
    }

    void CreateContext()
    {
        if (!eglBindAPI(EGL_OPENGL_API))
        {
            throw std::runtime_error(
                "eglBindAPI(EGL_OPENGL_API) failed: " +
                EglErrorString(eglGetError())
            );
        }

        const EGLint contextAttribs[] = {
            EGL_CONTEXT_MAJOR_VERSION, this->options.major,
            EGL_CONTEXT_MINOR_VERSION, this->options.minor,
            EGL_CONTEXT_OPENGL_PROFILE_MASK,
                EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
            EGL_NONE
        };

        this->context = eglCreateContext(
            this->display,
            EGL_NO_CONFIG_KHR,
            EGL_NO_CONTEXT,
            contextAttribs
        );
        if (this->context == EGL_NO_CONTEXT)
        {
            // No-config contexts are a Mesa extension; fall back to an
            // explicit config.
            this->config = this->ChooseConfig();
            this->context = eglCreateContext(
                this->display,
                this->config,
                EGL_NO_CONTEXT,
                contextAttribs
            );
        }
        if (this->context == EGL_NO_CONTEXT)
        {
            throw std::runtime_error(
                "eglCreateContext failed: " + EglErrorString(eglGetError())
            );
        }

        if (!eglMakeCurrent(
                this->display,
                EGL_NO_SURFACE,
                EGL_NO_SURFACE,
                this->context
            ))
        {
            // Surfaceless current unsupported: use a 1x1 pbuffer as the
            // drawable. All engine rendering goes to FBOs anyway.
            if (this->config == nullptr)
                this->config = this->ChooseConfig();
            const EGLint surfaceAttribs[] = {
                EGL_WIDTH, 1,
                EGL_HEIGHT, 1,
                EGL_NONE
            };
            this->surface = eglCreatePbufferSurface(
                this->display,
                this->config,
                surfaceAttribs
            );
            if (this->surface == EGL_NO_SURFACE ||
                !eglMakeCurrent(
                    this->display,
                    this->surface,
                    this->surface,
                    this->context
                ))
            {
                throw std::runtime_error(
                    "EGL surfaceless current failed and pbuffer fallback "
                    "failed: " + EglErrorString(eglGetError())
                );
            }
        }
    }

    static GLADapiproc LoadGlProc(const char* name) noexcept
    {
        void* proc = reinterpret_cast<void*>(eglGetProcAddress(name));
        if (proc == nullptr)
            proc = reinterpret_cast<void*>(dlsym(RTLD_DEFAULT, name));
        return reinterpret_cast<GLADapiproc>(proc);
    }

    void LoadGlFunctions()
    {
        if (gladLoadGL(&LoadGlProc) == 0)
        {
            throw std::runtime_error(
                "gladLoadGL failed: no OpenGL 3.3 core entry points available"
            );
        }
    }

    void CaptureDiagnostics()
    {
        this->eglVersion =
            NullableString(eglQueryString(this->display, EGL_VERSION));
        this->eglVendor =
            NullableString(eglQueryString(this->display, EGL_VENDOR));
        this->glVendor = NullableString(
            reinterpret_cast<const char*>(glGetString(GL_VENDOR))
        );
        this->glRenderer = NullableString(
            reinterpret_cast<const char*>(glGetString(GL_RENDERER))
        );
        this->glVersion = NullableString(
            reinterpret_cast<const char*>(glGetString(GL_VERSION))
        );
        // The token is a provider-owned identity object, never a native
        // context handle: contexts sharing resources map to one identity.
        this->shareGroupToken = &this->shareGroupIdentity;
    }

    void DestroyResources() noexcept
    {
        if (this->display == EGL_NO_DISPLAY)
            return;
        if (this->context != EGL_NO_CONTEXT)
        {
            eglMakeCurrent(
                this->display,
                EGL_NO_SURFACE,
                EGL_NO_SURFACE,
                EGL_NO_CONTEXT
            );
            eglDestroyContext(this->display, this->context);
            this->context = EGL_NO_CONTEXT;
        }
        if (this->surface != EGL_NO_SURFACE)
        {
            eglDestroySurface(this->display, this->surface);
            this->surface = EGL_NO_SURFACE;
        }
        if (this->displayInitialized)
        {
            if (ReleaseEglDisplay(this->display))
                eglTerminate(this->display);
            this->displayInitialized = false;
        }
        GraphicsDevice::SetCurrentContext(nullptr);
        GraphicsDevice::SetCurrentShareGroup(nullptr);
        this->display = EGL_NO_DISPLAY;
        this->shareGroupToken = nullptr;
    }

    HeadlessContextOptions options;
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLConfig config = nullptr;
    EGLContext context = EGL_NO_CONTEXT;
    EGLSurface surface = EGL_NO_SURFACE;
    bool displayInitialized = false;
    bool softwareSelected = false;

    std::string platformName;
    std::string eglVersion;
    std::string eglVendor;
    std::string glVendor;
    std::string glRenderer;
    std::string glVersion;
    struct ShareGroupIdentity
    {
    };
    struct ContextIdentity
    {
    };
    ContextIdentity contextIdentity;
    ShareGroupIdentity shareGroupIdentity;
    GraphicsShareGroupToken shareGroupToken = nullptr;
};
}  // namespace
}  // namespace wisteria

#endif  // WISTERIA_ENABLE_EGL

// R1.9 Phase 0D: GLFW hidden-window provider. Used on Windows (no EGL) and
// as a fallback on Linux when EGL is unavailable. It still respects the R1.7
// factory invariant: CreateHeadlessContext returns with no native context
// current and no tracker registered.
namespace wisteria
{
namespace
{
bool HasExtensionLocal(std::string_view extensions, std::string_view name)
{
    std::size_t position = 0U;
    while (position <= extensions.size())
    {
        const std::size_t end = extensions.find(' ', position);
        const std::size_t length =
            (end == std::string_view::npos) ? extensions.size() - position
                                            : end - position;
        if (extensions.substr(position, length) == name)
            return true;
        if (end == std::string_view::npos)
            break;
        position = end + 1U;
    }
    return false;
}

const char* NullableLocal(const char* value) noexcept
{
    return value != nullptr ? value : "";
}

bool RendererIsSoftware(std::string_view renderer) noexcept
{
    return HasExtensionLocal(renderer, "llvmpipe") ||
        HasExtensionLocal(renderer, "softpipe") ||
        HasExtensionLocal(renderer, "swrast");
}

class GlfwHeadlessContext final : public IHeadlessContext
{
public:
    explicit GlfwHeadlessContext(const HeadlessContextOptions& options)
    {
        if (!wisteria::platform::AcquireGlfwLifetime())
            throw std::runtime_error("GLFW initialization failed");
        try
        {
            glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
            glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, options.major);
            glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, options.minor);
            glfwWindowHint(
                GLFW_OPENGL_PROFILE,
                GLFW_OPENGL_CORE_PROFILE
            );
            this->window = glfwCreateWindow(
                1,
                1,
                "WISTERIA headless",
                nullptr,
                nullptr
            );
            if (this->window == nullptr)
            {
                throw std::runtime_error(
                    "GLFW hidden window creation failed"
                );
            }
            glfwMakeContextCurrent(this->window);
            if (gladLoadGL(glfwGetProcAddress) == 0)
            {
                throw std::runtime_error(
                    "gladLoadGL failed on GLFW hidden context"
                );
            }
            this->glVendor = NullableLocal(
                reinterpret_cast<const char*>(glGetString(GL_VENDOR))
            );
            this->glRenderer = NullableLocal(
                reinterpret_cast<const char*>(glGetString(GL_RENDERER))
            );
            this->glVersion = NullableLocal(
                reinterpret_cast<const char*>(glGetString(GL_VERSION))
            );
            if (options.forceSoftware && !this->IsSoftware())
            {
                throw std::runtime_error(
                    "forceSoftware: GLFW hidden renderer is not software"
                );
            }
            // Factory invariant: return with no context current.
            glfwMakeContextCurrent(nullptr);
            wisteria::GraphicsDevice::SetCurrentContext(nullptr);
            wisteria::GraphicsDevice::SetCurrentShareGroup(nullptr);
        }
        catch (...)
        {
            if (this->window != nullptr)
                glfwDestroyWindow(this->window);
            wisteria::platform::ReleaseGlfwLifetime();
            throw;
        }
    }

    ~GlfwHeadlessContext() override
    {
        if (this->window != nullptr)
        {
            glfwMakeContextCurrent(nullptr);
            glfwDestroyWindow(this->window);
        }
        wisteria::GraphicsDevice::SetCurrentContext(nullptr);
        wisteria::GraphicsDevice::SetCurrentShareGroup(nullptr);
        wisteria::platform::ReleaseGlfwLifetime();
    }

    GlfwHeadlessContext(const GlfwHeadlessContext&) = delete;
    GlfwHeadlessContext& operator=(const GlfwHeadlessContext&) = delete;

    void MakeCurrent() override
    {
        glfwMakeContextCurrent(this->window);
        wisteria::GraphicsDevice::SetCurrentContext(this->window);
        wisteria::GraphicsDevice::SetCurrentShareGroup(this->window);
    }

    void ReleaseCurrent() override
    {
        glfwMakeContextCurrent(nullptr);
        wisteria::GraphicsDevice::SetCurrentContext(nullptr);
        wisteria::GraphicsDevice::SetCurrentShareGroup(nullptr);
    }

    GraphicsContextToken ContextToken() const noexcept override
    {
        return this->window;
    }

    GraphicsShareGroupToken ShareGroupToken() const noexcept override
    {
        return this->window;
    }

    std::string_view ProviderName() const noexcept override
    {
        return "GLFW-hidden";
    }

    std::string_view PlatformName() const noexcept override
    {
        return "hidden-window";
    }

    std::string_view EglVersion() const noexcept override
    {
        return "-";
    }

    std::string_view EglVendor() const noexcept override
    {
        return "-";
    }

    std::string_view Vendor() const noexcept override
    {
        return this->glVendor;
    }

    std::string_view Renderer() const noexcept override
    {
        return this->glRenderer;
    }

    std::string_view Version() const noexcept override
    {
        return this->glVersion;
    }

    bool IsSoftware() const noexcept override
    {
        return RendererIsSoftware(this->glRenderer);
    }

private:
    GLFWwindow* window = nullptr;
    std::string glVendor;
    std::string glRenderer;
    std::string glVersion;
};
}  // namespace
}  // namespace wisteria

namespace wisteria
{
std::unique_ptr<IHeadlessContext> CreateHeadlessContext(
    const HeadlessContextOptions& options
)
{
#if defined(WISTERIA_ENABLE_EGL)
    const bool eglDisabled =
        std::getenv("WISTERIA_HEADLESS_DISABLE_EGL") != nullptr;
    if (eglDisabled)
    {
        std::fprintf(
            stderr,
            "[headless] EGL disabled by WISTERIA_HEADLESS_DISABLE_EGL\n"
        );
    }
    else
    {
        try
        {
            return std::make_unique<EglHeadlessContext>(options);
        }
        catch (const std::exception& error)
        {
            std::fprintf(
                stderr,
                "[headless] EGL provider failed: %s\n",
                error.what()
            );
            // forceSoftware keeps strict semantics: never silently fall
            // back to a context that is not proven software.
            if (options.forceSoftware)
                return nullptr;
        }
    }
    if (options.forceSoftware)
        return nullptr;
#else
    (void)options;
#endif
    // Fallback / Windows provider (only reached when !forceSoftware).
    try
    {
        return std::make_unique<GlfwHeadlessContext>(options);
    }
    catch (const std::exception& error)
    {
        std::fprintf(
            stderr,
            "[headless] GLFW-hidden provider failed: %s\n",
            error.what()
        );
        return nullptr;
    }
}
}  // namespace wisteria
