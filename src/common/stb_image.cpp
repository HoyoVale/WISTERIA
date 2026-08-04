// WISTERIA: stb_image v2.30's internal zlib decoder is replaced with miniz's
// tinfl (see docs/architecture/NATIVE_ABI_PLAN.md). The stock decoder
// crashes on some valid PNG streams (e.g. 2048x2048 RGB textures with a
// single maximum-compression 4MB IDAT), while tinfl decodes them correctly.

#define WISTERIA_STBI_USE_TINFL
#define STB_IMAGE_IMPLEMENTATION
#include "wisteria/vendor/stb_image.h"

#include "wisteria/vendor/miniz.h"

extern "C"
{
char* stbi_zlib_decode_malloc_guesssize(
    const char* buffer,
    int len,
    int initial_size,
    int* outlen
)
{
    (void)initial_size;
    std::size_t decodedLength = 0U;
    void* result = tinfl_decompress_mem_to_heap(
        buffer,
        static_cast<std::size_t>(len),
        &decodedLength,
        TINFL_FLAG_PARSE_ZLIB_HEADER
    );
    if (result == nullptr)
        return nullptr;
    if (outlen != nullptr)
        *outlen = static_cast<int>(decodedLength);
    return static_cast<char*>(result);
}

char* stbi_zlib_decode_malloc(const char* buffer, int len, int* outlen)
{
    return stbi_zlib_decode_malloc_guesssize(buffer, len, 16384, outlen);
}

char* stbi_zlib_decode_malloc_guesssize_headerflag(
    const char* buffer,
    int len,
    int initial_size,
    int* outlen,
    int parse_header
)
{
    (void)initial_size;
    std::size_t decodedLength = 0U;
    const int flags =
        parse_header != 0 ? TINFL_FLAG_PARSE_ZLIB_HEADER : 0;
    void* result = tinfl_decompress_mem_to_heap(
        buffer,
        static_cast<std::size_t>(len),
        &decodedLength,
        flags
    );
    if (result == nullptr)
        return nullptr;
    if (outlen != nullptr)
        *outlen = static_cast<int>(decodedLength);
    return static_cast<char*>(result);
}

int stbi_zlib_decode_buffer(
    char* obuffer,
    int olen,
    const char* ibuffer,
    int ilen
)
{
    const std::size_t written = tinfl_decompress_mem_to_mem(
        obuffer,
        static_cast<std::size_t>(olen),
        ibuffer,
        static_cast<std::size_t>(ilen),
        TINFL_FLAG_PARSE_ZLIB_HEADER
    );
    return written == TINFL_DECOMPRESS_MEM_TO_MEM_FAILED
        ? -1
        : static_cast<int>(written);
}

char* stbi_zlib_decode_noheader_malloc(
    const char* buffer,
    int len,
    int* outlen
)
{
    std::size_t decodedLength = 0U;
    void* result = tinfl_decompress_mem_to_heap(
        buffer,
        static_cast<std::size_t>(len),
        &decodedLength,
        0
    );
    if (result == nullptr)
        return nullptr;
    if (outlen != nullptr)
        *outlen = static_cast<int>(decodedLength);
    return static_cast<char*>(result);
}

int stbi_zlib_decode_noheader_buffer(
    char* obuffer,
    int olen,
    const char* ibuffer,
    int ilen
)
{
    const std::size_t written = tinfl_decompress_mem_to_mem(
        obuffer,
        static_cast<std::size_t>(olen),
        ibuffer,
        static_cast<std::size_t>(ilen),
        0
    );
    return written == TINFL_DECOMPRESS_MEM_TO_MEM_FAILED
        ? -1
        : static_cast<int>(written);
}
}
