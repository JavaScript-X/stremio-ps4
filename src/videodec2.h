#pragma once

#include <cstdint>
#include <pthread.h>

// A deliberately small, dynamically-loaded Videodec2 diagnostic. Keeping the
// ABI private prevents the incomplete OpenOrbis Videodec2 header from being
// linked into normal application startup.
class VideoDec2Probe {
public:
    enum class State { Idle, Loading, Decoding, Passed, Finished, Failed };

    VideoDec2Probe();
    ~VideoDec2Probe();
    bool start(const char* annexBPath);
    void stop();

    State state() const { return state_; }
    int errorStage() const { return errorStage_; }
    int32_t errorCode() const { return errorCode_; }
    uint64_t decodedFrames() const { return decodedFrames_; }
    uint32_t measuredFpsTimesTen() const { return measuredFpsTimesTen_; }
    uint32_t width() const { return width_; }
    uint32_t height() const { return height_; }

private:
    static void* threadEntry(void* value);
    void decodeLoop();

    State state_ = State::Idle;
    int errorStage_ = 0;
    int32_t errorCode_ = 0;
    uint64_t decodedFrames_ = 0;
    uint32_t measuredFpsTimesTen_ = 0;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    const char* path_ = nullptr;
    pthread_t thread_ = {};
    volatile bool running_ = false;
    volatile bool stopRequested_ = false;
};
