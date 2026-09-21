#include "avplayer.h"

#include <cstdlib>
#include <cstring>

#include <orbis/Sysmodule.h>
#include <orbis/libkernel.h>

namespace {
constexpr size_t kTexturePoolSize = 64 * 1024 * 1024;
constexpr size_t kDirectMemoryAlignment = 0x200000;

void* texturePool = nullptr;
size_t texturePoolOffset = 0;
off_t texturePoolDirectOffset = 0;
volatile int32_t latestPlayerEvent = 0;

void playerEvent(void*, int32_t eventId, int32_t, void*) {
    latestPlayerEvent = eventId;
}

void* allocateAligned(void*, uint32_t alignment, uint32_t size) {
    if (alignment < sizeof(void*)) {
        alignment = sizeof(void*);
    }
    const size_t alignedSize =
        (static_cast<size_t>(size) + alignment - 1) & ~(alignment - 1);
    void* result = nullptr;
    return posix_memalign(&result, alignment, alignedSize) == 0 ? result : nullptr;
}

bool initializeTexturePool() {
    if (texturePool) {
        texturePoolOffset = 0;
        return true;
    }

    if (sceKernelAllocateDirectMemory(
            0,
            sceKernelGetDirectMemorySize(),
            kTexturePoolSize,
            kDirectMemoryAlignment,
            3,
            &texturePoolDirectOffset) < 0) {
        return false;
    }

    if (sceKernelMapDirectMemory(
            &texturePool,
            kTexturePoolSize,
            0x33,
            0,
            texturePoolDirectOffset,
            kDirectMemoryAlignment) < 0) {
        sceKernelReleaseDirectMemory(texturePoolDirectOffset, kTexturePoolSize);
        texturePool = nullptr;
        return false;
    }
    texturePoolOffset = 0;
    return true;
}

void* allocateTexture(void*, uint32_t alignment, uint32_t size) {
    if (!texturePool || alignment == 0) {
        return nullptr;
    }
    const size_t alignedOffset =
        (texturePoolOffset + alignment - 1) & ~(static_cast<size_t>(alignment) - 1);
    if (alignedOffset + size > kTexturePoolSize) {
        return nullptr;
    }
    void* result = static_cast<uint8_t*>(texturePool) + alignedOffset;
    texturePoolOffset = alignedOffset + size;
    return result;
}

void releaseTexture(void*, void*) {
    // The arena is reset only after AVPlayer has closed, so its worker threads
    // can never observe recycled decoder buffers during shutdown.
}

void release(void*, void* pointer) {
    free(pointer);
}

uint8_t clampColor(int value) {
    if (value < 0) return 0;
    if (value > 255) return 255;
    return static_cast<uint8_t>(value);
}
}  // namespace

AvPlayerProbe::~AvPlayerProbe() {
    stop();
}

bool AvPlayerProbe::start(const char* url) {
    stop();
    errorStage_ = 0;
    errorCode_ = 0;
    width_ = 0;
    height_ = 0;
    preview_.clear();
    previewWidth_ = 0;
    previewHeight_ = 0;
    started_ = false;
    latestPlayerEvent = 0;

    if (!initializeTexturePool()) {
        errorStage_ = 1;
        errorCode_ = -1;
        state_ = State::Failed;
        return false;
    }

    errorCode_ = sceSysmoduleLoadModule(ORBIS_SYSMODULE_AV_PLAYER);
    if (errorCode_ < 0) {
        errorStage_ = 2;
        state_ = State::Failed;
        return false;
    }

    SceAvPlayerInitData init = {};
    init.memoryReplacement.allocate = allocateAligned;
    init.memoryReplacement.deallocate = release;
    init.memoryReplacement.allocateTexture = allocateTexture;
    init.memoryReplacement.deallocateTexture = releaseTexture;
    init.eventReplacement.eventCallback = playerEvent;
    init.basePriority = 160;
    init.numOutputVideoFrameBuffers = 4;
    init.autoStart = 0;
    init.defaultLanguage = "en";

    handle_ = sceAvPlayerInit(&init);
    if (!handle_) {
        errorStage_ = 3;
        state_ = State::Failed;
        return false;
    }

    errorCode_ = sceAvPlayerAddSource(handle_, url);
    if (errorCode_ < 0) {
        errorStage_ = 4;
        state_ = State::Failed;
        sceAvPlayerClose(handle_);
        handle_ = nullptr;
        return false;
    }

    state_ = State::Opening;
    return true;
}

void AvPlayerProbe::update() {
    if (!handle_ || state_ == State::Failed || state_ == State::Passed) {
        return;
    }
    constexpr int32_t kReadyEvent = 0x02;
    constexpr uint32_t kVideoStream = 0;
    if (!started_) {
        if (latestPlayerEvent != kReadyEvent) {
            return;
        }

        bool videoEnabled = false;
        const int32_t streamCount = sceAvPlayerStreamCount(handle_);
        for (int32_t index = 0; index < streamCount; ++index) {
            SceAvPlayerStreamInfo info = {};
            if (sceAvPlayerGetStreamInfo(handle_, index, &info) >= 0 &&
                info.type == kVideoStream &&
                sceAvPlayerEnableStream(handle_, index) >= 0) {
                width_ = info.details.video.width;
                height_ = info.details.video.height;
                videoEnabled = true;
            }
        }
        if (!videoEnabled) {
            errorStage_ = 5;
            errorCode_ = streamCount;
            state_ = State::Failed;
            return;
        }

        errorCode_ = sceAvPlayerStart(handle_);
        if (errorCode_ < 0) {
            errorStage_ = 6;
            state_ = State::Failed;
            return;
        }
        started_ = true;
        state_ = State::Decoding;
    }

    if (!sceAvPlayerIsActive(handle_)) {
        return;
    }

    SceAvPlayerFrameInfo frame = {};
    if (sceAvPlayerGetVideoData(handle_, &frame) && frame.pData) {
        width_ = frame.details.video.width;
        height_ = frame.details.video.height;
        previewWidth_ = width_ / 2;
        previewHeight_ = height_ / 2;
        preview_.resize(static_cast<size_t>(previewWidth_) * previewHeight_);

        const uint8_t* luma = frame.pData;
        const uint8_t* chroma = luma + static_cast<size_t>(width_) * height_;
        for (uint32_t py = 0; py < previewHeight_; ++py) {
            const uint32_t y = py * 2;
            for (uint32_t px = 0; px < previewWidth_; ++px) {
                const uint32_t x = px * 2;
                const int yy = luma[static_cast<size_t>(y) * width_ + x] - 16;
                const size_t uv = static_cast<size_t>(y / 2) * width_ + x;
                const int u = chroma[uv] - 128;
                const int v = chroma[uv + 1] - 128;
                const int r = (298 * yy + 409 * v + 128) >> 8;
                const int g = (298 * yy - 100 * u - 208 * v + 128) >> 8;
                const int b = (298 * yy + 516 * u + 128) >> 8;
                preview_[static_cast<size_t>(py) * previewWidth_ + px] =
                    (static_cast<uint32_t>(clampColor(r)) << 16) |
                    (static_cast<uint32_t>(clampColor(g)) << 8) |
                    clampColor(b);
            }
        }
        state_ = State::Passed;
    }
}

void AvPlayerProbe::stop() {
    if (handle_) {
        sceAvPlayerStop(handle_);
        sceAvPlayerClose(handle_);
        handle_ = nullptr;
    }
    state_ = State::Idle;
    started_ = false;
}
