#include "videodec2.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <emmintrin.h>

#include <orbis/libkernel.h>
#include <orbis/Sysmodule.h>

namespace {
constexpr size_t kDirectAlignment = 0x200000;
constexpr int kFrameBuffers = 8;

struct ComputeConfig {
    uint64_t thisSize;
    uint16_t computePipeId;
    uint16_t computeQueueId;
    bool checkMemoryType;
    uint8_t reserved0;
    uint16_t reserved1;
};
struct ComputeMemory {
    uint64_t thisSize;
    uint64_t cpuGpuMemorySize;
    void* cpuGpuMemory;
};
struct DecoderConfig {
    uint64_t thisSize;
    uint32_t resourceType;
    uint32_t codecType;
    uint32_t profile;
    uint32_t maxLevel;
    int32_t maxFrameWidth;
    int32_t maxFrameHeight;
    int32_t maxDpbFrameCount;
    uint32_t decodePipelineDepth;
    void* computeQueue;
    uint64_t cpuAffinityMask;
    int32_t cpuThreadPriority;
    bool optimizeProgressiveVideo;
    bool checkMemoryType;
    uint8_t reserved0;
    uint8_t reserved1;
    void* extraConfigInfo;
};
struct DecoderMemory {
    uint64_t thisSize;
    uint64_t cpuMemorySize;
    void* cpuMemory;
    uint64_t gpuMemorySize;
    void* gpuMemory;
    uint64_t cpuGpuMemorySize;
    void* cpuGpuMemory;
    uint64_t maxFrameBufferSize;
    uint32_t frameBufferAlignment;
    uint32_t reserved;
};
struct InputData {
    uint64_t thisSize;
    void* auData;
    uint64_t auSize;
    uint64_t ptsData;
    uint64_t dtsData;
    uint64_t attachedData;
};
struct FrameBuffer {
    uint64_t thisSize;
    void* frameBuffer;
    uint64_t frameBufferSize;
    bool isAccepted;
};
struct OutputInfo {
    uint64_t thisSize;
    bool isValid;
    bool isErrorFrame;
    uint8_t pictureCount;
    uint32_t codecType;
    uint32_t frameWidth;
    uint32_t framePitch;
    uint32_t frameHeight;
    void* frameBuffer;
    uint64_t frameBufferSize;
    uint32_t frameFormat;
    uint32_t framePitchInBytes;
};

using QueryCompute = int32_t (*)(ComputeMemory*);
using AllocateQueue = int32_t (*)(const ComputeConfig*, const ComputeMemory*, void**);
using ReleaseQueue = int32_t (*)(void*);
using QueryDecoder = int32_t (*)(const DecoderConfig*, DecoderMemory*);
using CreateDecoder = int32_t (*)(const DecoderConfig*, const DecoderMemory*, void**);
using DeleteDecoder = int32_t (*)(void*);
using Decode = int32_t (*)(void*, const InputData*, FrameBuffer*, OutputInfo*);
using Flush = int32_t (*)(void*, FrameBuffer*, OutputInfo*);

struct Api {
    QueryCompute queryCompute = nullptr;
    AllocateQueue allocateQueue = nullptr;
    ReleaseQueue releaseQueue = nullptr;
    QueryDecoder queryDecoder = nullptr;
    CreateDecoder createDecoder = nullptr;
    DeleteDecoder deleteDecoder = nullptr;
    Decode decode = nullptr;
    Flush flush = nullptr;
};

struct DirectBlock {
    void* address = nullptr;
    off_t offset = 0;
    size_t size = 0;
};

bool allocateDirect(size_t requested, int memoryType, DirectBlock& block) {
    block.size = (requested + kDirectAlignment - 1) & ~(kDirectAlignment - 1);
    if (!block.size) return true;
    int result = sceKernelAllocateDirectMemory(0,
        sceKernelGetDirectMemorySize(), block.size, kDirectAlignment,
        memoryType, &block.offset);
    if (result < 0) return false;
    result = sceKernelMapDirectMemory(&block.address, block.size, 0x33, 0,
        block.offset, kDirectAlignment);
    if (result < 0) {
        sceKernelReleaseDirectMemory(block.offset, block.size);
        block = {};
        return false;
    }
    std::memset(block.address, 0, block.size);
    return true;
}

bool allocateDecoderFrames(size_t requested, DirectBlock& block) {
    block.size = (requested + kDirectAlignment - 1) & ~(kDirectAlignment - 1);
    if (sceKernelAllocateDirectMemory(0, sceKernelGetDirectMemorySize(),
            block.size, kDirectAlignment, 3, &block.offset) < 0) return false;
    // A cacheable Onion mapping of decoder-visible physical memory avoids the
    // 50-70 ms/frame WC-Garlic CPU readback penalty.
    if (sceKernelMapDirectMemory2(&block.address, block.size, 0, 0x33, 0,
            block.offset, kDirectAlignment) < 0) {
        sceKernelReleaseDirectMemory(block.offset, block.size);
        block = {};
        return false;
    }
    return true;
}

void releaseDirect(DirectBlock& block) {
    if (!block.address) return;
    sceKernelMunmap(block.address, block.size);
    sceKernelReleaseDirectMemory(block.offset, block.size);
    block = {};
}

int32_t resolve(int module, const char* name, void** target) {
    const int32_t result = sceKernelDlsym(module, name, target);
    return result >= 0 && *target ? 0 : (result < 0 ? result : -1);
}

bool loadApi(Api& api, int& stage, int32_t& code) {
    const char* dependencies[] = {
        "/system/common/lib/libSceVdecCore.sprx",
        "/system/common/lib/libSceVdecSavc.sprx",
        "/system/common/lib/libSceVdecSavc2.sprx",
        "/system/common/lib/libSceVdecwrap.sprx"};
    for (const char* dependency : dependencies)
        sceKernelLoadStartModule(dependency, 0, nullptr, 0, nullptr, nullptr);
    // VdecCore must also be registered through Sysmodule on retail firmware.
    // Loading only its path is insufficient on newer system software.
    sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_VDECCORE);
    int module = sceKernelLoadStartModule(
        "/system/common/lib/libSceVideodec2.sprx", 0, nullptr, 0,
        nullptr, nullptr);
    if (module < 0) {
        // Sandboxed applications on some firmwares resolve the system module
        // by basename even though the absolute path returns ENOENT.
        module = sceKernelLoadStartModule("libSceVideodec2.sprx", 0,
            nullptr, 0, nullptr, nullptr);
    }
    if (module < 0) { stage = 2; code = module; return false; }
#define VD2_RESOLVE(field, symbol) \
    code = resolve(module, symbol, reinterpret_cast<void**>(&api.field)); \
    if (code < 0) { \
        stage = 3; return false; \
    }
    VD2_RESOLVE(queryCompute, "sceVideodec2QueryComputeMemoryInfo");
    VD2_RESOLVE(allocateQueue, "sceVideodec2AllocateComputeQueue");
    VD2_RESOLVE(releaseQueue, "sceVideodec2ReleaseComputeQueue");
    VD2_RESOLVE(queryDecoder, "sceVideodec2QueryDecoderMemoryInfo");
    VD2_RESOLVE(createDecoder, "sceVideodec2CreateDecoder");
    VD2_RESOLVE(deleteDecoder, "sceVideodec2DeleteDecoder");
    VD2_RESOLVE(decode, "sceVideodec2Decode");
    VD2_RESOLVE(flush, "sceVideodec2Flush");
#undef VD2_RESOLVE
    return true;
}

bool readFile(const char* path, std::vector<uint8_t>& data) {
    FILE* file = std::fopen(path, "rb");
    if (!file) return false;
    std::fseek(file, 0, SEEK_END);
    const long length = std::ftell(file);
    std::rewind(file);
    if (length <= 0) { std::fclose(file); return false; }
    data.resize(static_cast<size_t>(length));
    const bool ok = std::fread(data.data(), 1, data.size(), file) == data.size();
    std::fclose(file);
    return ok;
}

// The packaged elementary stream has an Access Unit Delimiter before every
// frame. Return byte offsets for whole access units, including SPS/PPS/SEI.
std::vector<size_t> findAccessUnits(const std::vector<uint8_t>& data) {
    std::vector<size_t> offsets;
    for (size_t index = 0; index + 5 < data.size();) {
        size_t header = 0;
        size_t startCodeBytes = 0;
        if (data[index] == 0 && data[index + 1] == 0 &&
            data[index + 2] == 0 && data[index + 3] == 1) {
            header = index + 4;
            startCodeBytes = 4;
        } else if (data[index] == 0 && data[index + 1] == 0 &&
            data[index + 2] == 1) {
            header = index + 3;
            startCodeBytes = 3;
        }
        if (header && (data[header] & 0x1f) == 9) offsets.push_back(index);
        index += startCodeBytes ? startCodeBytes : 1;
    }
    if (!offsets.empty() && offsets[0] != 0) offsets[0] = 0;
    offsets.push_back(data.size());
    return offsets;
}

void inspectAvcConfiguration(const std::vector<uint8_t>& data,
        uint32_t& profile, uint32_t& level) {
    profile = 66;
    level = 40;
    for (size_t index = 0; index + 8 < data.size(); ++index) {
        size_t header = 0;
        if (data[index] == 0 && data[index + 1] == 0 &&
            data[index + 2] == 0 && data[index + 3] == 1) header = index + 4;
        else if (data[index] == 0 && data[index + 1] == 0 &&
            data[index + 2] == 1) header = index + 3;
        if (header && (data[header] & 0x1f) == 7) {
            profile = data[header + 1];
            level = data[header + 3];
            return;
        }
    }
}
}  // namespace

VideoDec2Probe::VideoDec2Probe() {
    pthread_mutex_init(&previewMutex_, nullptr);
}
VideoDec2Probe::~VideoDec2Probe() {
    stop();
    pthread_mutex_destroy(&previewMutex_);
}

bool VideoDec2Probe::start(const char* annexBPath) {
    stop();
    path_ = annexBPath ? annexBPath : "";
    state_ = State::Loading;
    errorStage_ = 0;
    errorCode_ = 0;
    decodedFrames_ = 0;
    measuredFpsTimesTen_ = 0;
    width_ = 0;
    height_ = 0;
    submittedAccessUnits_ = 0;
    durationMs_ = 0;
    preview_.clear();
    previewWidth_ = 0;
    previewHeight_ = 0;
    deliveredFrame_ = 0;
    stopRequested_ = false;
    paused_ = false;
    running_ = pthread_create(&thread_, nullptr, threadEntry, this) == 0;
    if (!running_) { state_ = State::Failed; errorStage_ = 1; return false; }
    return true;
}

void VideoDec2Probe::restart() {
    const std::string path = path_;
    stop();
    start(path.c_str());
}

void VideoDec2Probe::stop() {
    if (running_) {
        stopRequested_ = true;
        pthread_join(thread_, nullptr);
        running_ = false;
    }
    state_ = State::Idle;
}

void* VideoDec2Probe::threadEntry(void* value) {
    static_cast<VideoDec2Probe*>(value)->decodeLoop();
    return nullptr;
}

void VideoDec2Probe::decodeLoop() {
    Api api;
    if (!loadApi(api, errorStage_, errorCode_)) {
        state_ = State::Failed; return;
    }
    std::vector<uint8_t> stream;
    if (!readFile(path_.c_str(), stream)) { errorStage_ = 10; errorCode_ = -1; state_ = State::Failed; return; }
    const std::vector<size_t> accessUnits = findAccessUnits(stream);
    if (accessUnits.size() < 3) { errorStage_ = 11; errorCode_ = -1; state_ = State::Failed; return; }
    durationMs_ = (accessUnits.size() - 1) * 1000 / 24;
    struct SampleTime { uint64_t pts; uint64_t dts; };
    std::vector<SampleTime> sampleTimes;
    FILE* timing = std::fopen((path_ + ".pts").c_str(), "rb");
    if (timing) {
        SampleTime value = {};
        while (std::fread(&value, sizeof(value), 1, timing) == 1)
            sampleTimes.push_back(value);
        std::fclose(timing);
        if (sampleTimes.size() + 1 < accessUnits.size()) sampleTimes.clear();
    }
    uint32_t avcProfile = 66, avcLevel = 40;
    inspectAvcConfiguration(stream, avcProfile, avcLevel);
    // Videodec2 reads access units from CPU/GPU coherent Onion memory. A
    // normal std::vector is not a valid source buffer on retail hardware.
    DirectBlock inputBlock;
    if (!allocateDirect(stream.size(), 0, inputBlock)) {
        errorStage_ = 12; errorCode_ = -1; state_ = State::Failed; return;
    }
    std::memcpy(inputBlock.address, stream.data(), stream.size());
    stream.clear();

    ComputeMemory compute = {}; compute.thisSize = sizeof(compute);
    errorCode_ = api.queryCompute(&compute);
    if (errorCode_ < 0) {
        releaseDirect(inputBlock); errorStage_ = 13; state_ = State::Failed; return;
    }
    DirectBlock computeBlock;
    if (!allocateDirect(compute.cpuGpuMemorySize, 0, computeBlock)) {
        releaseDirect(inputBlock); errorStage_ = 14; errorCode_ = -1;
        state_ = State::Failed; return;
    }
    compute.cpuGpuMemory = computeBlock.address;
    ComputeConfig computeConfig = {};
    computeConfig.thisSize = sizeof(computeConfig);
    computeConfig.computePipeId = 0;
    computeConfig.computeQueueId = 0;
    computeConfig.checkMemoryType = true;
    void* queue = nullptr;
    errorCode_ = api.allocateQueue(&computeConfig, &compute, &queue);
    if (errorCode_ < 0) {
        releaseDirect(computeBlock); releaseDirect(inputBlock);
        errorStage_ = 15; state_ = State::Failed; return;
    }

    DecoderConfig config = {};
    config.thisSize = sizeof(config);
    config.resourceType = 1;
    config.codecType = 1;
    // Match the actual SPS instead of imposing the original Baseline test
    // profile on Main/High streams obtained from addons.
    config.profile = avcProfile;
    config.maxLevel = avcLevel;
    config.maxFrameWidth = 1920;
    config.maxFrameHeight = 1088;
    // Streaming encodes commonly use several reference and B-frames. Four
    // DPB surfaces can make presentation oscillate around the current frame.
    config.maxDpbFrameCount = 16;
    config.decodePipelineDepth = 4;
    config.computeQueue = queue;
    config.cpuAffinityMask = 0x3f;
    config.cpuThreadPriority = 700;
    config.optimizeProgressiveVideo = true;
    config.checkMemoryType = false;
    DecoderMemory memory = {}; memory.thisSize = sizeof(memory);
    errorCode_ = api.queryDecoder(&config, &memory);
    if (errorCode_ < 0) {
        api.releaseQueue(queue); releaseDirect(computeBlock);
        releaseDirect(inputBlock); errorStage_ = 16; state_ = State::Failed; return;
    }
    DirectBlock cpuBlock, gpuBlock, sharedBlock, frameBlock;
    const bool allocations =
        allocateDirect(memory.cpuMemorySize, 0, cpuBlock) &&
        allocateDirect(memory.gpuMemorySize, 3, gpuBlock) &&
        allocateDirect(memory.cpuGpuMemorySize, 0, sharedBlock) &&
        allocateDecoderFrames(memory.maxFrameBufferSize * kFrameBuffers,
            frameBlock);
    if (!allocations) {
        releaseDirect(cpuBlock); releaseDirect(gpuBlock); releaseDirect(sharedBlock);
        releaseDirect(frameBlock); api.releaseQueue(queue); releaseDirect(computeBlock);
        releaseDirect(inputBlock); errorStage_ = 17; errorCode_ = -1;
        state_ = State::Failed; return;
    }
    memory.cpuMemory = cpuBlock.address;
    memory.gpuMemory = gpuBlock.address;
    memory.cpuGpuMemory = sharedBlock.address;
    void* decoder = nullptr;
    errorCode_ = api.createDecoder(&config, &memory, &decoder);
    if (errorCode_ < 0) errorStage_ = 18;
    if (errorCode_ >= 0) {
        state_ = State::Decoding;
        const uint64_t start = sceKernelGetProcessTime();
        uint64_t pausedUsec = 0;
        int frameIndex = 0;
        for (size_t index = 0; index + 1 < accessUnits.size() &&
             !stopRequested_; ++index) {
            if (paused_) {
                const uint64_t pauseStart = sceKernelGetProcessTime();
                while (paused_ && !stopRequested_) sceKernelUsleep(16000);
                pausedUsec += sceKernelGetProcessTime() - pauseStart;
                if (stopRequested_) break;
            }
            InputData input = {};
            input.thisSize = sizeof(input);
            input.auData = static_cast<uint8_t*>(inputBlock.address) +
                accessUnits[index];
            input.auSize = accessUnits[index + 1] - accessUnits[index];
            input.ptsData = sampleTimes.empty()
                ? index * 1000000 / 24 : sampleTimes[index].pts;
            input.dtsData = sampleTimes.empty()
                ? input.ptsData : sampleTimes[index].dts;
            FrameBuffer frame = {};
            frame.thisSize = sizeof(frame);
            frame.frameBuffer = static_cast<uint8_t*>(frameBlock.address) +
                (frameIndex % kFrameBuffers) * memory.maxFrameBufferSize;
            frame.frameBufferSize = memory.maxFrameBufferSize;
            OutputInfo output = {}; output.thisSize = sizeof(output);
            submittedAccessUnits_ = static_cast<uint32_t>(index + 1);
            errorCode_ = api.decode(decoder, &input, &frame, &output);
            if (errorCode_ < 0) { errorStage_ = 19; state_ = State::Failed; break; }
            ++frameIndex;
            if (output.isValid) {
                const uint32_t pitch = output.framePitchInBytes > 0
                    ? output.framePitchInBytes : output.framePitch;
                const uint32_t sourceHeight = output.frameHeight;
                constexpr uint32_t previewWidth = 960;
                constexpr uint32_t previewHeight = 540;
                std::vector<uint32_t> converted(
                    static_cast<size_t>(previewWidth) * previewHeight);
                const uint8_t* luma = static_cast<const uint8_t*>(
                    output.frameBuffer);
                const size_t nv12Bytes = static_cast<size_t>(pitch) *
                    sourceHeight * 3 / 2;
                for (size_t offset = 0; offset < nv12Bytes; offset += 64)
                    _mm_clflush(luma + offset);
                _mm_mfence();
                const uint8_t* chroma = luma +
                    static_cast<size_t>(pitch) * sourceHeight;
                for (uint32_t y = 0; y < previewHeight; ++y) {
                    const uint32_t sy = y * 2;
                    for (uint32_t x = 0; x < previewWidth; ++x) {
                        const uint32_t sx = x * 2;
                        const int yy = luma[static_cast<size_t>(sy) * pitch + sx] - 16;
                        const size_t uv = static_cast<size_t>(sy / 2) * pitch + sx;
                        const int u = chroma[uv] - 128;
                        const int v = chroma[uv + 1] - 128;
                        auto clamp = [](int value) -> uint8_t {
                            return value < 0 ? 0 : (value > 255 ? 255 : value);
                        };
                        converted[static_cast<size_t>(y) * previewWidth + x] =
                            (static_cast<uint32_t>(clamp((298 * yy + 409 * v + 128) >> 8)) << 16) |
                            (static_cast<uint32_t>(clamp((298 * yy - 100 * u - 208 * v + 128) >> 8)) << 8) |
                            clamp((298 * yy + 516 * u + 128) >> 8);
                    }
                }
                pthread_mutex_lock(&previewMutex_);
                preview_.swap(converted);
                previewWidth_ = previewWidth;
                previewHeight_ = previewHeight;
                ++decodedFrames_;
                pthread_mutex_unlock(&previewMutex_);
                width_ = output.frameWidth;
                height_ = output.frameHeight;
                const uint64_t elapsed = sceKernelGetProcessTime() - start;
                if (elapsed) measuredFpsTimesTen_ =
                    static_cast<uint32_t>(decodedFrames_ * 10000000 / elapsed);
                if (decodedFrames_ == 1) state_ = State::Passed;
                // Present at the stream cadence instead of finishing the
                // complete 52-second clip as a 140 FPS benchmark.
                const uint64_t target = start + pausedUsec +
                    decodedFrames_ * 1000000 / 24;
                const uint64_t now = sceKernelGetProcessTime();
                if (target > now) sceKernelUsleep(target - now);
            }
        }
        if (!stopRequested_ && state_ != State::Failed) state_ = State::Finished;
        api.deleteDecoder(decoder);
    } else {
        state_ = State::Failed;
    }
    releaseDirect(cpuBlock);
    releaseDirect(frameBlock);
    releaseDirect(sharedBlock);
    releaseDirect(gpuBlock);
    api.releaseQueue(queue);
    releaseDirect(computeBlock);
    releaseDirect(inputBlock);
}

bool VideoDec2Probe::copyPreview(std::vector<uint32_t>& pixels,
    uint32_t& width, uint32_t& height) const {
    pthread_mutex_lock(&previewMutex_);
    if (decodedFrames_ == deliveredFrame_ || preview_.empty()) {
        pthread_mutex_unlock(&previewMutex_);
        return false;
    }
    pixels = preview_;
    width = previewWidth_;
    height = previewHeight_;
    deliveredFrame_ = decodedFrames_;
    pthread_mutex_unlock(&previewMutex_);
    return true;
}
