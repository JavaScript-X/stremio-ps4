#include "graphics.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>

namespace {
uint32_t encode(Color color) {
    return 0x80000000u |
        (static_cast<uint32_t>(color.r) << 16) |
        (static_cast<uint32_t>(color.g) << 8) |
        color.b;
}

bool insideRounded(int px, int py, int width, int height, int radius) {
    if (radius <= 0) return true;
    const int cx = px < radius ? radius :
        (px >= width - radius ? width - radius - 1 : px);
    const int cy = py < radius ? radius :
        (py >= height - radius ? height - radius - 1 : py);
    const int dx = px - cx;
    const int dy = py - cy;
    return dx * dx + dy * dy <= radius * radius;
}

constexpr int kFontAtlasSize = 512;
uint8_t fontAtlas[kFontAtlasSize * kFontAtlasSize] = {};
stbtt_bakedchar fontCharacters[96] = {};
bool fontAttempted = false;
bool fontReady = false;

bool initializeFont() {
    if (fontAttempted) return fontReady;
    fontAttempted = true;
    FILE* file = fopen("/app0/assets/Gontserrat-Regular.ttf", "rb");
    if (!file) return false;
    fseek(file, 0, SEEK_END);
    const long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (size <= 0 || size > 2 * 1024 * 1024) {
        fclose(file);
        return false;
    }
    std::vector<uint8_t> data(static_cast<size_t>(size));
    const bool read = fread(data.data(), 1, data.size(), file) == data.size();
    fclose(file);
    if (!read) return false;
    fontReady = stbtt_BakeFontBitmap(data.data(), 0, 24.0f, fontAtlas,
        kFontAtlasSize, kFontAtlasSize, 32, 96, fontCharacters) > 0;
    return fontReady;
}
}  // namespace

Scene2D::Scene2D(int width, int height, int pixelDepth)
    : width_(width),
      height_(height),
      depth_(pixelDepth),
      frameBufferSize_(width * height * pixelDepth) {}

Scene2D::~Scene2D() = default;

bool Scene2D::Init(size_t memorySize, int frameBufferCount) {
    video_ = sceVideoOutOpen(ORBIS_VIDEO_USER_MAIN, ORBIS_VIDEO_OUT_BUS_MAIN, 0, 0);
    if (video_ < 0 || !initializeFlipQueue() ||
        !allocateVideoMemory(memorySize, 0x200000) ||
        !allocateFrameBuffers(frameBufferCount)) {
        return false;
    }
    sceVideoOutSetFlipRate(video_, 0);
    return true;
}

bool Scene2D::initializeFlipQueue() {
    if (sceKernelCreateEqueue(&flipQueue_, "stremio flip queue") < 0) {
        return false;
    }
    return sceVideoOutAddFlipEvent(flipQueue_, video_, 0) >= 0;
}

bool Scene2D::allocateVideoMemory(size_t size, int alignment) {
    directMemorySize_ = (size + alignment - 1) / alignment * alignment;
    if (sceKernelAllocateDirectMemory(
            0, sceKernelGetDirectMemorySize(), directMemorySize_, alignment,
            3, &directMemoryOffset_) < 0) {
        return false;
    }
    if (sceKernelMapDirectMemory(
            &videoMemory_, directMemorySize_, 0x33, 0,
            directMemoryOffset_, alignment) < 0) {
        sceKernelReleaseDirectMemory(directMemoryOffset_, directMemorySize_);
        return false;
    }
    videoMemoryCursor_ = reinterpret_cast<uintptr_t>(videoMemory_);
    return true;
}

char* Scene2D::allocateDisplayMemory(size_t size) {
    char* result = reinterpret_cast<char*>(videoMemoryCursor_);
    videoMemoryCursor_ += size;
    return result;
}

bool Scene2D::allocateFrameBuffers(int count) {
    frameBufferCount_ = count;
    frameBuffers_ = new char*[count];
    for (int index = 0; index < count; ++index) {
        frameBuffers_[index] = allocateDisplayMemory(frameBufferSize_);
    }
    sceVideoOutSetBufferAttribute(
        &attribute_, 0x80000000, 1, 0, width_, height_, width_);
    return sceVideoOutRegisterBuffers(
        video_, 0, reinterpret_cast<void**>(frameBuffers_),
        count, &attribute_) == 0;
}

void Scene2D::SetActiveFrameBuffer(int index) {
    activeFrameBuffer_ = index;
}

void Scene2D::SubmitFlip(int frameId) {
    sceVideoOutSubmitFlip(
        video_, activeFrameBuffer_, ORBIS_VIDEO_OUT_FLIP_VSYNC, frameId);
}

void Scene2D::FrameWait(int frameId) {
    for (;;) {
        OrbisVideoOutFlipStatus status = {};
        sceVideoOutGetFlipStatus(video_, &status);
        if (status.flipArg == frameId) return;
        OrbisKernelEvent event = {};
        int count = 0;
        if (sceKernelWaitEqueue(flipQueue_, &event, 1, &count, nullptr) != 0) return;
    }
}

void Scene2D::FrameBufferSwap() {
    activeFrameBuffer_ = (activeFrameBuffer_ + 1) % frameBufferCount_;
}

void Scene2D::FrameBufferFill(Color color) {
    uint32_t* buffer = reinterpret_cast<uint32_t*>(frameBuffers_[activeFrameBuffer_]);
    std::fill_n(buffer, static_cast<size_t>(width_) * height_, encode(color));
}

void Scene2D::DrawPixel(int x, int y, Color color) {
    if (x < 0 || y < 0 || x >= width_ || y >= height_) return;
    uint32_t* buffer = reinterpret_cast<uint32_t*>(frameBuffers_[activeFrameBuffer_]);
    buffer[static_cast<size_t>(y) * width_ + x] = encode(color);
}

void Scene2D::DrawRectangle(int x, int y, int width, int height, Color color) {
    const int left = std::max(0, x);
    const int top = std::max(0, y);
    const int right = std::min(width_, x + width);
    const int bottom = std::min(height_, y + height);
    if (left >= right || top >= bottom) return;
    uint32_t* buffer = reinterpret_cast<uint32_t*>(frameBuffers_[activeFrameBuffer_]);
    const uint32_t value = encode(color);
    for (int row = top; row < bottom; ++row) {
        std::fill(buffer + static_cast<size_t>(row) * width_ + left,
                  buffer + static_cast<size_t>(row) * width_ + right,
                  value);
    }
}

void Scene2D::DrawVerticalFade(
    int x, int y, int width, int height, Color color,
    uint8_t topOpacity, uint8_t bottomOpacity) {
    if (width <= 0 || height <= 0) return;
    const int left = std::max(0, x);
    const int right = std::min(width_, x + width);
    const int top = std::max(0, y);
    const int bottom = std::min(height_, y + height);
    if (left >= right || top >= bottom) return;
    // Twelve pre-blended bands preserve the feathered appearance without
    // doing several integer multiplies for ~630,000 pixels every frame.
    constexpr int kBands = 12;
    constexpr Color base = {18, 18, 24};
    for (int band = 0; band < kBands; ++band) {
        const int bandTop = y + height * band / kBands;
        const int bandBottom = y + height * (band + 1) / kBands;
        const uint32_t opacity = topOpacity +
            (static_cast<int>(bottomOpacity) - topOpacity) * band /
                (kBands - 1);
        const uint32_t inverse = 255 - opacity;
        const Color blended = {
            static_cast<uint8_t>((color.r * opacity + base.r * inverse) / 255),
            static_cast<uint8_t>((color.g * opacity + base.g * inverse) / 255),
            static_cast<uint8_t>((color.b * opacity + base.b * inverse) / 255)};
        DrawRectangle(left, std::max(top, bandTop), right - left,
            std::min(bottom, bandBottom) - std::max(top, bandTop), blended);
    }
}

void Scene2D::DrawRoundedRectangle(
    int x, int y, int width, int height, int radius, Color color) {
    const int left = std::max(0, x);
    const int top = std::max(0, y);
    const int right = std::min(width_, x + width);
    const int bottom = std::min(height_, y + height);
    if (left >= right || top >= bottom) return;
    uint32_t* buffer = reinterpret_cast<uint32_t*>(frameBuffers_[activeFrameBuffer_]);
    const uint32_t value = encode(color);
    for (int row = top; row < bottom; ++row) {
        for (int column = left; column < right; ++column) {
            if (insideRounded(column - x, row - y, width, height, radius))
                buffer[static_cast<size_t>(row) * width_ + column] = value;
        }
    }
}

void Scene2D::BlitRgb(
    int x, int y, int width, int height, const uint32_t* pixels) {
    if (!pixels || width <= 0 || height <= 0) return;
    const int firstRow = std::max(0, -y);
    const int lastRow = std::min(height, height_ - y);
    const int firstColumn = std::max(0, -x);
    const int lastColumn = std::min(width, width_ - x);
    if (firstRow >= lastRow || firstColumn >= lastColumn) return;
    uint32_t* buffer = reinterpret_cast<uint32_t*>(frameBuffers_[activeFrameBuffer_]);
    for (int row = firstRow; row < lastRow; ++row) {
        uint32_t* destination = buffer + static_cast<size_t>(y + row) * width_ + x;
        const uint32_t* source = pixels + static_cast<size_t>(row) * width;
        for (int column = firstColumn; column < lastColumn; ++column) {
            destination[column] = 0x80000000u | source[column];
        }
    }
}

void Scene2D::BlitRgbMasked(
    int x, int y, int width, int height, const uint32_t* pixels) {
    if (!pixels || width <= 0 || height <= 0) return;
    const int firstRow = std::max(0, -y);
    const int lastRow = std::min(height, height_ - y);
    const int firstColumn = std::max(0, -x);
    const int lastColumn = std::min(width, width_ - x);
    if (firstRow >= lastRow || firstColumn >= lastColumn) return;
    uint32_t* buffer = reinterpret_cast<uint32_t*>(frameBuffers_[activeFrameBuffer_]);
    for (int row = firstRow; row < lastRow; ++row) {
        uint32_t* destination = buffer + static_cast<size_t>(y + row) * width_ + x;
        const uint32_t* source = pixels + static_cast<size_t>(row) * width;
        // Rounded poster masks only trim a run at each edge. Find those runs
        // once, then copy the solid span without a branch for every pixel.
        int first = firstColumn;
        while (first < lastColumn && (source[first] & 0xff000000u) != 0) ++first;
        int last = lastColumn;
        while (last > first && (source[last - 1] & 0xff000000u) != 0) --last;
        for (int column = first; column < last; ++column)
            destination[column] = 0x80000000u | source[column];
    }
}

void Scene2D::BlitRgbRounded(
    int x, int y, int width, int height, int radius,
    const uint32_t* pixels, uint8_t opacity) {
    if (!pixels || x < 0 || y < 0 || x + width > width_ || y + height > height_)
        return;
    uint32_t* buffer = reinterpret_cast<uint32_t*>(frameBuffers_[activeFrameBuffer_]);
    for (int row = 0; row < height; ++row) {
        uint32_t* destination = buffer + static_cast<size_t>(y + row) * width_ + x;
        const uint32_t* source = pixels + static_cast<size_t>(row) * width;
        for (int column = 0; column < width; ++column) {
            if (!insideRounded(column, row, width, height, radius)) continue;
            if (opacity == 255) {
                destination[column] = 0x80000000u | source[column];
            } else {
                const uint32_t src = source[column];
                const uint32_t dst = destination[column];
                const uint32_t inverse = 255 - opacity;
                const uint32_t r = (((src >> 16) & 255) * opacity +
                    ((dst >> 16) & 255) * inverse) / 255;
                const uint32_t g = (((src >> 8) & 255) * opacity +
                    ((dst >> 8) & 255) * inverse) / 255;
                const uint32_t b = ((src & 255) * opacity +
                    (dst & 255) * inverse) / 255;
                destination[column] = 0x80000000u | (r << 16) | (g << 8) | b;
            }
        }
    }
}

void Scene2D::BlitRgbScaledRounded(
    int x, int y, int width, int height, int radius,
    const uint32_t* pixels, int sourceWidth, int sourceHeight,
    uint8_t opacity) {
    if (!pixels || width <= 0 || height <= 0 || sourceWidth <= 0 ||
        sourceHeight <= 0) return;
    uint32_t* buffer = reinterpret_cast<uint32_t*>(frameBuffers_[activeFrameBuffer_]);
    for (int row = 0; row < height; ++row) {
        const int py = y + row;
        if (py < 0 || py >= height_) continue;
        for (int column = 0; column < width; ++column) {
            const int px = x + column;
            if (px < 0 || px >= width_ ||
                !insideRounded(column, row, width, height, radius)) continue;
            const uint32_t src = pixels[
                static_cast<size_t>(row * sourceHeight / height) * sourceWidth +
                column * sourceWidth / width];
            uint32_t& dst = buffer[static_cast<size_t>(py) * width_ + px];
            const uint32_t inverse = 255 - opacity;
            const uint32_t red = (((src >> 16) & 255) * opacity +
                ((dst >> 16) & 255) * inverse) / 255;
            const uint32_t green = (((src >> 8) & 255) * opacity +
                ((dst >> 8) & 255) * inverse) / 255;
            const uint32_t blue = ((src & 255) * opacity +
                (dst & 255) * inverse) / 255;
            dst = 0x80000000u | (red << 16) | (green << 8) | blue;
        }
    }
}

void Scene2D::DrawText(
    int x, int y, const char* text, Color color, int scale) {
    if (!text || scale <= 0) return;
    std::string ascii(text);
    if (ascii.empty()) return;
    for (char& character : ascii) {
        const unsigned char value = static_cast<unsigned char>(character);
        if (value < 32 || value > 126) character = '?';
    }

    if (!initializeFont()) return;
    const float displayScale = static_cast<float>(scale) / 2.0f;
    float cursorX = 0.0f;
    float cursorY = 22.0f;
    uint32_t* buffer = reinterpret_cast<uint32_t*>(frameBuffers_[activeFrameBuffer_]);
    for (unsigned char character : ascii) {
        stbtt_aligned_quad quad = {};
        stbtt_GetBakedQuad(fontCharacters, kFontAtlasSize, kFontAtlasSize,
            character - 32, &cursorX, &cursorY, &quad, 1);
        const int glyphWidth = std::max(0, static_cast<int>(quad.x1 - quad.x0));
        const int glyphHeight = std::max(0, static_cast<int>(quad.y1 - quad.y0));
        const int atlasX = static_cast<int>(quad.s0 * kFontAtlasSize);
        const int atlasY = static_cast<int>(quad.t0 * kFontAtlasSize);
        for (int gy = 0; gy < glyphHeight; ++gy) {
            for (int gx = 0; gx < glyphWidth; ++gx) {
                const uint8_t alpha = fontAtlas[
                    (atlasY + gy) * kFontAtlasSize + atlasX + gx];
                if (alpha < 8) continue;
                const int baseX = x + static_cast<int>(quad.x0 * displayScale) +
                    static_cast<int>(gx * displayScale);
                const int baseY = y + static_cast<int>(quad.y0 * displayScale) +
                    static_cast<int>(gy * displayScale);
                const int block = std::max(1, static_cast<int>(std::ceil(displayScale)));
                for (int sy = 0; sy < block; ++sy) for (int sx = 0; sx < block; ++sx) {
                    const int px = baseX + sx;
                    const int py = baseY + sy;
                    if (px < 0 || py < 0 || px >= width_ || py >= height_) continue;
                    uint32_t& destination = buffer[static_cast<size_t>(py) * width_ + px];
                    const uint32_t inverse = 255 - alpha;
                    const uint32_t r = (color.r * alpha +
                        ((destination >> 16) & 255) * inverse) / 255;
                    const uint32_t g = (color.g * alpha +
                        ((destination >> 8) & 255) * inverse) / 255;
                    const uint32_t b = (color.b * alpha +
                        (destination & 255) * inverse) / 255;
                    destination = 0x80000000u | (r << 16) | (g << 8) | b;
                }
            }
        }
    }
}
