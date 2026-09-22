#include "poster.h"

#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace {
constexpr int kMaximumSourceDimension = 2048;
}

bool decodePosterJpeg(
    const std::string& encoded,
    int outputWidth,
    int outputHeight,
    PosterImage& poster) {
    poster = {};
    if (encoded.empty() || outputWidth <= 0 || outputHeight <= 0) return false;

    int sourceWidth = 0;
    int sourceHeight = 0;
    int channels = 0;
    stbi_uc* decoded = stbi_load_from_memory(
        reinterpret_cast<const stbi_uc*>(encoded.data()),
        static_cast<int>(encoded.size()),
        &sourceWidth,
        &sourceHeight,
        &channels,
        3);
    if (!decoded || sourceWidth <= 0 || sourceHeight <= 0 ||
        sourceWidth > kMaximumSourceDimension ||
        sourceHeight > kMaximumSourceDimension) {
        stbi_image_free(decoded);
        return false;
    }

    poster.width = outputWidth;
    poster.height = outputHeight;
    poster.pixels.resize(static_cast<size_t>(outputWidth) * outputHeight);
    for (int y = 0; y < outputHeight; ++y) {
        const int sourceY = y * sourceHeight / outputHeight;
        for (int x = 0; x < outputWidth; ++x) {
            const int sourceX = x * sourceWidth / outputWidth;
            const stbi_uc* pixel = decoded +
                (static_cast<size_t>(sourceY) * sourceWidth + sourceX) * 3;
            poster.pixels[static_cast<size_t>(y) * outputWidth + x] =
                (static_cast<uint32_t>(pixel[0]) << 16) |
                (static_cast<uint32_t>(pixel[1]) << 8) |
                static_cast<uint32_t>(pixel[2]);
        }
    }
    stbi_image_free(decoded);
    return true;
}
