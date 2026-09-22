#pragma once

#include <cstddef>
#include <cstdint>
#include <sys/types.h>

#include <orbis/VideoOut.h>
#include <orbis/libkernel.h>

struct Color {
    uint8_t r;
    uint8_t g;
    uint8_t b;
};

class Scene2D {
public:
    Scene2D(int width, int height, int pixelDepth);
    ~Scene2D();

    bool Init(size_t memorySize, int frameBufferCount);
    void SetActiveFrameBuffer(int index);
    void SubmitFlip(int frameId);
    void FrameWait(int frameId);
    void FrameBufferSwap();
    void FrameBufferFill(Color color);
    void DrawPixel(int x, int y, Color color);
    void DrawRectangle(int x, int y, int width, int height, Color color);
    void BlitRgb(int x, int y, int width, int height, const uint32_t* pixels);
    void DrawText(
        int x, int y, const char* text, Color color, int scale = 2);

private:
    bool initializeFlipQueue();
    bool allocateVideoMemory(size_t size, int alignment);
    bool allocateFrameBuffers(int count);
    char* allocateDisplayMemory(size_t size);

    int width_;
    int height_;
    int depth_;
    int video_ = -1;
    int frameBufferSize_;
    int frameBufferCount_ = 0;
    int activeFrameBuffer_ = 0;
    off_t directMemoryOffset_ = 0;
    size_t directMemorySize_ = 0;
    void* videoMemory_ = nullptr;
    uintptr_t videoMemoryCursor_ = 0;
    char** frameBuffers_ = nullptr;
    OrbisKernelEqueue flipQueue_ = 0;
    OrbisVideoOutBufferAttribute attribute_ = {};
};
