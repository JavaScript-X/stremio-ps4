#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct PosterImage {
    int width = 0;
    int height = 0;
    std::vector<uint32_t> pixels;

    bool valid() const {
        return width > 0 && height > 0 &&
            pixels.size() == static_cast<size_t>(width * height);
    }
};

bool decodePosterJpeg(
    const std::string& encoded,
    int outputWidth,
    int outputHeight,
    PosterImage& poster);
