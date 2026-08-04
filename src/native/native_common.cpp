#include "wisteria/native/wisteria_native.h"
#include "internal/native_context.hpp"

using namespace wisteria::native;
using namespace wisteria;

extern "C"
{
const char* wisteria_status_name(enum WisteriaStatus status)
{
    switch (status)
    {
    case WISTERIA_OK: return "OK";
    case WISTERIA_ERROR_INVALID_ARGUMENT: return "INVALID_ARGUMENT";
    case WISTERIA_ERROR_NOT_FOUND: return "NOT_FOUND";
    case WISTERIA_ERROR_IO: return "IO";
    case WISTERIA_ERROR_PARSE: return "PARSE";
    case WISTERIA_ERROR_INITIALIZATION: return "INITIALIZATION";
    case WISTERIA_ERROR_ALREADY_EXISTS: return "ALREADY_EXISTS";
    case WISTERIA_ERROR_INTERNAL: return "INTERNAL";
    }
    return "UNKNOWN";
}

uint32_t wisteria_version_major(void)
{
    return WISTERIA_NATIVE_VERSION_MAJOR;
}

uint32_t wisteria_version_minor(void)
{
    return WISTERIA_NATIVE_VERSION_MINOR;
}

enum WisteriaStatus wisteria_create_context(WisteriaContext* out_context)
{
    if (out_context == nullptr)
        return WISTERIA_ERROR_INVALID_ARGUMENT;

    try
    {
        *out_context = RegisterContext();
        return WISTERIA_OK;
    }
    catch (...)
    {
        *out_context = 0U;
        return WISTERIA_ERROR_INTERNAL;
    }
}

enum WisteriaStatus wisteria_destroy_context(WisteriaContext context)
{
    return UnregisterContext(context)
        ? WISTERIA_OK
        : WISTERIA_ERROR_NOT_FOUND;
}

enum WisteriaStatus wisteria_last_error_message(
    WisteriaContext context,
    char* buffer,
    size_t buffer_size
)
{
    if (buffer == nullptr || buffer_size == 0U)
        return WISTERIA_ERROR_INVALID_ARGUMENT;
    const ContextLease handle = FindContext(context);
    if (handle == nullptr)
    {
        buffer[0] = '\0';
        return WISTERIA_ERROR_NOT_FOUND;
    }
    CopyErrorMessage(handle->lastError, buffer, buffer_size);
    return WISTERIA_OK;
}
} /* extern "C" */
