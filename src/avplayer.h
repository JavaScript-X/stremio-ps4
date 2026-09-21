#pragma once

#include <cstddef>
#include <cstdint>

// OpenOrbis v0.5.4 exposes the AVPlayer symbols, but its public header omits
// their ABI. These declarations cover only the subset used by our diagnostic.
extern "C" {

struct SceAvPlayerImpl;
using SceAvPlayerHandle = SceAvPlayerImpl*;

using SceAvPlayerAllocate = void* (*)(void*, uint32_t, uint32_t);
using SceAvPlayerDeallocate = void (*)(void*, void*);

struct SceAvPlayerMemAllocator {
    void* objectPointer;
    SceAvPlayerAllocate allocate;
    SceAvPlayerDeallocate deallocate;
    SceAvPlayerAllocate allocateTexture;
    SceAvPlayerDeallocate deallocateTexture;
};

struct SceAvPlayerFileReplacement {
    void* objectPointer;
    int (*open)(void*, const char*);
    int (*close)(void*);
    int (*readOffset)(void*, uint8_t*, uint64_t, uint32_t);
    uint64_t (*size)(void*);
};

struct SceAvPlayerEventReplacement {
    void* objectPointer;
    void (*eventCallback)(void*, int32_t, int32_t, void*);
};

struct SceAvPlayerInitData {
    SceAvPlayerMemAllocator memoryReplacement;
    SceAvPlayerFileReplacement fileReplacement;
    SceAvPlayerEventReplacement eventReplacement;
    int32_t debugLevel;
    uint32_t basePriority;
    int32_t numOutputVideoFrameBuffers;
    int32_t autoStart;
    uint8_t reserved[3];
    const char* defaultLanguage;
};

struct SceAvPlayerVideoEx {
    uint32_t width;
    uint32_t height;
    float aspectRatio;
    uint8_t languageCode[4];
    uint32_t framerate;
    uint32_t cropLeftOffset;
    uint32_t cropRightOffset;
    uint32_t cropTopOffset;
    uint32_t cropBottomOffset;
    uint32_t pitch;
    uint8_t lumaBitDepth;
    uint8_t chromaBitDepth;
    uint8_t videoFullRangeFlag;
    uint8_t reserved[37];
};

union SceAvPlayerStreamDetailsEx {
    SceAvPlayerVideoEx video;
    uint8_t reserved[80];
};

struct SceAvPlayerFrameInfoEx {
    void* pData;
    uint8_t reserved[4];
    uint64_t timeStamp;
    SceAvPlayerStreamDetailsEx details;
};

struct SceAvPlayerVideo {
    uint32_t width;
    uint32_t height;
    float aspectRatio;
    uint8_t languageCode[4];
};

union SceAvPlayerStreamDetails {
    uint8_t reserved[16];
    SceAvPlayerVideo video;
};

struct SceAvPlayerFrameInfo {
    uint8_t* pData;
    uint32_t reserved;
    uint64_t timeStamp;
    SceAvPlayerStreamDetails details;
};

SceAvPlayerHandle sceAvPlayerInit(SceAvPlayerInitData* data);
int32_t sceAvPlayerAddSource(SceAvPlayerHandle handle, const char* filename);
int32_t sceAvPlayerClose(SceAvPlayerHandle handle);
bool sceAvPlayerGetVideoData(
    SceAvPlayerHandle handle,
    SceAvPlayerFrameInfo* frameInfo);
uint8_t sceAvPlayerIsActive(SceAvPlayerHandle handle);
int32_t sceAvPlayerStart(SceAvPlayerHandle handle);
int32_t sceAvPlayerStop(SceAvPlayerHandle handle);

}  // extern "C"

class AvPlayerProbe {
public:
    enum class State { Idle, Opening, Decoding, Passed, Failed };

    ~AvPlayerProbe();
    bool start(const char* url);
    void update();
    void stop();

    State state() const { return state_; }
    int errorStage() const { return errorStage_; }
    int32_t errorCode() const { return errorCode_; }
    uint32_t width() const { return width_; }
    uint32_t height() const { return height_; }

private:
    SceAvPlayerHandle handle_ = nullptr;
    State state_ = State::Idle;
    int errorStage_ = 0;
    int32_t errorCode_ = 0;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    bool started_ = false;
};
