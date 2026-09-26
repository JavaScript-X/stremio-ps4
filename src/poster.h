#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct PosterImage {
    int width = 0;
    int height = 0;
    std::vector<uint32_t> pixels;
    int previewWidth = 0;
    int previewHeight = 0;
    std::vector<uint32_t> previewPixels;

    bool valid() const {
        return width > 0 && height > 0 &&
            pixels.size() == static_cast<size_t>(width * height);
    }

    bool previewValid() const {
        return previewWidth > 0 && previewHeight > 0 &&
            previewPixels.size() ==
                static_cast<size_t>(previewWidth * previewHeight);
    }
};

bool decodePosterJpeg(
    const std::string& encoded,
    int outputWidth,
    int outputHeight,
    PosterImage& poster);

void preparePosterPresentation(
    PosterImage& poster, int cornerRadius,
    int previewWidth, int previewHeight);
