#include "poster.h"

#include <algorithm>

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

bool decodePosterImageContain(
    const std::string& encoded,
    int maximumWidth,
    int maximumHeight,
    PosterImage& image) {
    if (encoded.empty() || maximumWidth <= 0 || maximumHeight <= 0)
        return false;
    int sourceWidth = 0;
    int sourceHeight = 0;
    int channels = 0;
    if (!stbi_info_from_memory(
            reinterpret_cast<const stbi_uc*>(encoded.data()),
            static_cast<int>(encoded.size()), &sourceWidth, &sourceHeight,
            &channels) || sourceWidth <= 0 || sourceHeight <= 0)
        return false;
    stbi_uc* decoded = stbi_load_from_memory(
        reinterpret_cast<const stbi_uc*>(encoded.data()),
        static_cast<int>(encoded.size()), &sourceWidth, &sourceHeight,
        &channels, 4);
    if (!decoded) return false;
    int cropLeft = sourceWidth, cropTop = sourceHeight;
    int cropRight = -1, cropBottom = -1;
    for (int y = 0; y < sourceHeight; ++y) {
        for (int x = 0; x < sourceWidth; ++x) {
            if (decoded[(static_cast<size_t>(y) * sourceWidth + x) * 4 + 3] > 12) {
                cropLeft = std::min(cropLeft, x);
                cropRight = std::max(cropRight, x);
                cropTop = std::min(cropTop, y);
                cropBottom = std::max(cropBottom, y);
            }
        }
    }
    if (cropRight < cropLeft || cropBottom < cropTop) {
        stbi_image_free(decoded);
        return false;
    }
    const int cropWidth = cropRight - cropLeft + 1;
    const int cropHeight = cropBottom - cropTop + 1;
    int width = maximumWidth;
    int height = cropHeight * maximumWidth / cropWidth;
    if (height > maximumHeight) {
        height = maximumHeight;
        width = cropWidth * maximumHeight / cropHeight;
    }
    width = std::max(1, width);
    height = std::max(1, height);
    image = {};
    image.width = width;
    image.height = height;
    image.pixels.resize(static_cast<size_t>(width) * height);
    for (int y = 0; y < height; ++y) {
        const int sourceY = cropTop + y * cropHeight / height;
        for (int x = 0; x < width; ++x) {
            const int sourceX = cropLeft + x * cropWidth / width;
            const stbi_uc* pixel = decoded +
                (static_cast<size_t>(sourceY) * sourceWidth + sourceX) * 4;
            image.pixels[static_cast<size_t>(y) * width + x] =
                (static_cast<uint32_t>(pixel[3]) << 24) |
                (static_cast<uint32_t>(pixel[0]) << 16) |
                (static_cast<uint32_t>(pixel[1]) << 8) | pixel[2];
        }
    }
    stbi_image_free(decoded);
    return true;
}

void preparePosterPresentation(
    PosterImage& poster, int cornerRadius,
    int previewWidth, int previewHeight) {
    if (!poster.valid() || cornerRadius <= 0 ||
        previewWidth <= 0 || previewHeight <= 0) return;
    auto insideRounded = [](int x, int y, int width, int height, int radius) {
        const int cx = x < radius ? radius :
            (x >= width - radius ? width - radius - 1 : x);
        const int cy = y < radius ? radius :
            (y >= height - radius ? height - radius - 1 : y);
        const int dx = x - cx;
        const int dy = y - cy;
        return dx * dx + dy * dy <= radius * radius;
    };
    for (int y = 0; y < poster.height; ++y) {
        for (int x = 0; x < poster.width; ++x) {
            if (!insideRounded(x, y, poster.width, poster.height, cornerRadius))
                poster.pixels[static_cast<size_t>(y) * poster.width + x] =
                    0x01000000u;
        }
    }
    poster.previewWidth = previewWidth;
    poster.previewHeight = previewHeight;
    poster.previewPixels.resize(
        static_cast<size_t>(previewWidth) * previewHeight);
    constexpr uint32_t opacity = 72;
    constexpr uint32_t inverse = 255 - opacity;
    for (int y = 0; y < previewHeight; ++y) {
        const int sourceY = y * poster.height / previewHeight;
        for (int x = 0; x < previewWidth; ++x) {
            const size_t destination = static_cast<size_t>(y) * previewWidth + x;
            if (!insideRounded(x, y, previewWidth, previewHeight, 16)) {
                poster.previewPixels[destination] =
                    (18u << 16) | (18u << 8) | 24u;
                continue;
            }
            const uint32_t source = poster.pixels[
                static_cast<size_t>(sourceY) * poster.width +
                x * poster.width / previewWidth];
            if ((source & 0xff000000u) != 0) {
                poster.previewPixels[destination] =
                    (18u << 16) | (18u << 8) | 24u;
                continue;
            }
            const uint32_t red = (((source >> 16) & 255) * opacity +
                18 * inverse) / 255;
            const uint32_t green = (((source >> 8) & 255) * opacity +
                18 * inverse) / 255;
            const uint32_t blue = ((source & 255) * opacity +
                24 * inverse) / 255;
            poster.previewPixels[destination] =
                (red << 16) | (green << 8) | blue;
        }
    }
}
