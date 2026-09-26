#include "poster.h"

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
        4);
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
                (static_cast<size_t>(sourceY) * sourceWidth + sourceX) * 4;
            const uint32_t alpha = pixel[3];
            const uint32_t inverse = 255 - alpha;
            const uint32_t red = (pixel[0] * alpha + 29 * inverse) / 255;
            const uint32_t green = (pixel[1] * alpha + 29 * inverse) / 255;
            const uint32_t blue = (pixel[2] * alpha + 39 * inverse) / 255;
            poster.pixels[static_cast<size_t>(y) * outputWidth + x] =
                (red << 16) | (green << 8) | blue;
        }
    }
    stbi_image_free(decoded);
    return true;
}
