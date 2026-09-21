#include "graphics.h"

#include <algorithm>

namespace {
uint32_t encode(Color color) {
    return 0x80000000u |
        (static_cast<uint32_t>(color.r) << 16) |
        (static_cast<uint32_t>(color.g) << 8) |
        color.b;
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

void Scene2D::BlitRgb(
    int x, int y, int width, int height, const uint32_t* pixels) {
    if (!pixels || x < 0 || y < 0 || x + width > width_ || y + height > height_) {
        return;
    }
    uint32_t* buffer = reinterpret_cast<uint32_t*>(frameBuffers_[activeFrameBuffer_]);
    for (int row = 0; row < height; ++row) {
        uint32_t* destination = buffer + static_cast<size_t>(y + row) * width_ + x;
        const uint32_t* source = pixels + static_cast<size_t>(row) * width;
        for (int column = 0; column < width; ++column) {
            destination[column] = 0x80000000u | source[column];
        }
    }
}
