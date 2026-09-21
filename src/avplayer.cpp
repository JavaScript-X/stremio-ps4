#include "avplayer.h"

#include <cstdlib>
#include <cstring>

#include <orbis/Sysmodule.h>

namespace {
void* allocateAligned(void*, uint32_t alignment, uint32_t size) {
    if (alignment < sizeof(void*)) {
        alignment = sizeof(void*);
    }
    const size_t alignedSize =
        (static_cast<size_t>(size) + alignment - 1) & ~(alignment - 1);
    void* result = nullptr;
    return posix_memalign(&result, alignment, alignedSize) == 0 ? result : nullptr;
}

void release(void*, void* pointer) {
    free(pointer);
}
}  // namespace

AvPlayerProbe::~AvPlayerProbe() {
    stop();
}

bool AvPlayerProbe::start(const char* url) {
    stop();
    errorStage_ = 0;
    width_ = 0;
    height_ = 0;

    if (sceSysmoduleLoadModule(ORBIS_SYSMODULE_AV_PLAYER) < 0) {
        errorStage_ = 1;
        state_ = State::Failed;
        return false;
    }

    SceAvPlayerInitData init = {};
    init.memoryReplacement.allocate = allocateAligned;
    init.memoryReplacement.deallocate = release;
    init.memoryReplacement.allocateTexture = allocateAligned;
    init.memoryReplacement.deallocateTexture = release;
    init.basePriority = 160;
    init.numOutputVideoFrameBuffers = 4;
    init.autoStart = 1;
    init.defaultLanguage = "en";

    handle_ = sceAvPlayerInit(&init);
    if (!handle_) {
        errorStage_ = 2;
        state_ = State::Failed;
        return false;
    }

    if (sceAvPlayerAddSource(handle_, url) < 0) {
        errorStage_ = 3;
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
    if (!sceAvPlayerIsActive(handle_)) {
        return;
    }

    state_ = State::Decoding;
    SceAvPlayerFrameInfoEx frame = {};
    if (sceAvPlayerGetVideoDataEx(handle_, &frame) == 0 && frame.pData) {
        width_ = frame.details.video.width;
        height_ = frame.details.video.height;
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
}
