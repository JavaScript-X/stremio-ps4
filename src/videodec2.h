#pragma once

#include <cstdint>
#include <pthread.h>
#include <string>
#include <vector>

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
    void togglePause() { paused_ = !paused_; }
    bool paused() const { return paused_; }
    void restart();

    State state() const { return state_; }
    int errorStage() const { return errorStage_; }
    int32_t errorCode() const { return errorCode_; }
    uint64_t decodedFrames() const { return decodedFrames_; }
    uint32_t measuredFpsTimesTen() const { return measuredFpsTimesTen_; }
    uint32_t width() const { return width_; }
    uint32_t height() const { return height_; }
    uint32_t submittedAccessUnits() const { return submittedAccessUnits_; }
    uint64_t currentTime() const { return decodedFrames_ * 1000 / 24; }
    uint64_t duration() const { return durationMs_; }
    bool copyPreview(std::vector<uint32_t>& pixels,
        uint32_t& width, uint32_t& height) const;

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
    uint32_t submittedAccessUnits_ = 0;
    uint64_t durationMs_ = 0;
    std::vector<uint32_t> preview_;
    uint32_t previewWidth_ = 0;
    uint32_t previewHeight_ = 0;
    mutable uint64_t deliveredFrame_ = 0;
    mutable pthread_mutex_t previewMutex_ = {};
    std::string path_;
    pthread_t thread_ = {};
    volatile bool running_ = false;
    volatile bool stopRequested_ = false;
    volatile bool paused_ = false;
};
