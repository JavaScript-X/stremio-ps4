#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>
#include <pthread.h>

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

struct SceAvPlayerStreamInfo {
    uint32_t type;
    uint8_t reserved[4];
    SceAvPlayerStreamDetails details;
    uint64_t duration;
    uint64_t startTime;
};

SceAvPlayerHandle sceAvPlayerInit(SceAvPlayerInitData* data);
int32_t sceAvPlayerAddSource(SceAvPlayerHandle handle, const char* filename);
int32_t sceAvPlayerClose(SceAvPlayerHandle handle);
bool sceAvPlayerGetVideoData(
    SceAvPlayerHandle handle,
    SceAvPlayerFrameInfo* frameInfo);
bool sceAvPlayerGetVideoDataEx(
    SceAvPlayerHandle handle,
    SceAvPlayerFrameInfoEx* frameInfo);
uint8_t sceAvPlayerIsActive(SceAvPlayerHandle handle);
int32_t sceAvPlayerStreamCount(SceAvPlayerHandle handle);
int32_t sceAvPlayerGetStreamInfo(
    SceAvPlayerHandle handle,
    uint32_t streamId,
    SceAvPlayerStreamInfo* info);
int32_t sceAvPlayerEnableStream(SceAvPlayerHandle handle, uint32_t streamId);
int32_t sceAvPlayerStart(SceAvPlayerHandle handle);
int32_t sceAvPlayerPause(SceAvPlayerHandle handle);
int32_t sceAvPlayerResume(SceAvPlayerHandle handle);
uint64_t sceAvPlayerCurrentTime(SceAvPlayerHandle handle);
int32_t sceAvPlayerStop(SceAvPlayerHandle handle);

}  // extern "C"

class AvPlayerProbe {
public:
    enum class State { Idle, Opening, Decoding, Passed, Failed };

    AvPlayerProbe();
    ~AvPlayerProbe();
    bool start(const char* url);
    void update();
    void stop();
    void togglePause();

    State state() const { return state_; }
    int errorStage() const { return errorStage_; }
    int32_t errorCode() const { return errorCode_; }
    uint32_t width() const;
    uint32_t height() const;
    bool copyPreview(std::vector<uint32_t>& pixels, uint32_t& width, uint32_t& height) const;
    bool paused() const { return paused_; }
    uint64_t decodedFrames() const;
    uint64_t currentTime() const;

private:
    static void* decoderThreadEntry(void* argument);
    void decoderLoop();
    SceAvPlayerHandle handle_ = nullptr;
    State state_ = State::Idle;
    int errorStage_ = 0;
    int32_t errorCode_ = 0;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    bool started_ = false;
    bool paused_ = false;
    std::vector<uint32_t> preview_;
    uint32_t previewWidth_ = 0;
    uint32_t previewHeight_ = 0;
    uint64_t decodedFrames_ = 0;
    pthread_t decoderThread_ = {};
    mutable pthread_mutex_t previewMutex_ = {};
    volatile bool decoderThreadRunning_ = false;
    volatile bool stopDecoderThread_ = false;
};
