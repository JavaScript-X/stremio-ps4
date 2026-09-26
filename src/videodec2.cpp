#include "videodec2.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include <orbis/libkernel.h>

namespace {
constexpr size_t kDirectAlignment = 0x200000;
constexpr int kFrameBuffers = 4;

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

void releaseDirect(DirectBlock& block) {
    if (!block.address) return;
    sceKernelMunmap(block.address, block.size);
    sceKernelReleaseDirectMemory(block.offset, block.size);
    block = {};
}

bool resolve(int module, const char* name, void** target) {
    return sceKernelDlsym(module, name, target) >= 0 && *target;
}

bool loadApi(Api& api, int32_t& code) {
    const char* dependencies[] = {
        "/system/common/lib/libSceVdecCore.sprx",
        "/system/common/lib/libSceVdecSavc.sprx",
        "/system/common/lib/libSceVdecSavc2.sprx",
        "/system/common/lib/libSceVdecwrap.sprx"};
    for (const char* dependency : dependencies)
        sceKernelLoadStartModule(dependency, 0, nullptr, 0, nullptr, nullptr);
    const int module = sceKernelLoadStartModule(
        "/system/common/lib/libSceVideodec2.sprx", 0, nullptr, 0,
        nullptr, nullptr);
    if (module < 0) { code = module; return false; }
#define VD2_RESOLVE(field, symbol) \
    if (!resolve(module, symbol, reinterpret_cast<void**>(&api.field))) { \
        code = -1; return false; \
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
}  // namespace

VideoDec2Probe::VideoDec2Probe() = default;
VideoDec2Probe::~VideoDec2Probe() { stop(); }

bool VideoDec2Probe::start(const char* annexBPath) {
    stop();
    path_ = annexBPath;
    state_ = State::Loading;
    errorStage_ = 0;
    errorCode_ = 0;
    decodedFrames_ = 0;
    measuredFpsTimesTen_ = 0;
    width_ = 0;
    height_ = 0;
    stopRequested_ = false;
    running_ = pthread_create(&thread_, nullptr, threadEntry, this) == 0;
    if (!running_) { state_ = State::Failed; errorStage_ = 1; return false; }
    return true;
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
    if (!loadApi(api, errorCode_)) { errorStage_ = 2; state_ = State::Failed; return; }
    std::vector<uint8_t> stream;
    if (!readFile(path_, stream)) { errorStage_ = 3; errorCode_ = -1; state_ = State::Failed; return; }
    const std::vector<size_t> accessUnits = findAccessUnits(stream);
    if (accessUnits.size() < 3) { errorStage_ = 4; errorCode_ = -1; state_ = State::Failed; return; }
    // Videodec2 reads access units from CPU/GPU coherent Onion memory. A
    // normal std::vector is not a valid source buffer on retail hardware.
    DirectBlock inputBlock;
    if (!allocateDirect(stream.size(), 0, inputBlock)) {
        errorStage_ = 5; errorCode_ = -1; state_ = State::Failed; return;
    }
    std::memcpy(inputBlock.address, stream.data(), stream.size());
    stream.clear();

    ComputeMemory compute = {}; compute.thisSize = sizeof(compute);
    errorCode_ = api.queryCompute(&compute);
    if (errorCode_ < 0) {
        releaseDirect(inputBlock); errorStage_ = 6; state_ = State::Failed; return;
    }
    DirectBlock computeBlock;
    if (!allocateDirect(compute.cpuGpuMemorySize, 0, computeBlock)) {
        releaseDirect(inputBlock); errorStage_ = 7; errorCode_ = -1;
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
        errorStage_ = 8; state_ = State::Failed; return;
    }

    DecoderConfig config = {};
    config.thisSize = sizeof(config);
    config.resourceType = 1;
    config.codecType = 1;
    config.profile = 100;
    config.maxLevel = 51;
    config.maxFrameWidth = 1920;
    config.maxFrameHeight = 1088;
    config.maxDpbFrameCount = 4;
    config.decodePipelineDepth = 2;
    config.computeQueue = queue;
    config.cpuAffinityMask = 0x3f;
    config.cpuThreadPriority = 700;
    config.optimizeProgressiveVideo = true;
    config.checkMemoryType = true;
    DecoderMemory memory = {}; memory.thisSize = sizeof(memory);
    errorCode_ = api.queryDecoder(&config, &memory);
    if (errorCode_ < 0) {
        api.releaseQueue(queue); releaseDirect(computeBlock);
        releaseDirect(inputBlock); errorStage_ = 9; state_ = State::Failed; return;
    }
    void* cpuMemory = nullptr;
    if (posix_memalign(&cpuMemory, 0x4000, memory.cpuMemorySize) != 0)
        cpuMemory = nullptr;
    DirectBlock gpuBlock, sharedBlock, frameBlock;
    const bool allocations = cpuMemory &&
        allocateDirect(memory.gpuMemorySize, 3, gpuBlock) &&
        allocateDirect(memory.cpuGpuMemorySize, 0, sharedBlock) &&
        allocateDirect(memory.maxFrameBufferSize * kFrameBuffers, 3, frameBlock);
    if (!allocations) {
        free(cpuMemory); releaseDirect(gpuBlock); releaseDirect(sharedBlock);
        releaseDirect(frameBlock); api.releaseQueue(queue); releaseDirect(computeBlock);
        releaseDirect(inputBlock); errorStage_ = 10; errorCode_ = -1;
        state_ = State::Failed; return;
    }
    std::memset(cpuMemory, 0, memory.cpuMemorySize);
    memory.cpuMemory = cpuMemory;
    memory.gpuMemory = gpuBlock.address;
    memory.cpuGpuMemory = sharedBlock.address;
    void* decoder = nullptr;
    errorCode_ = api.createDecoder(&config, &memory, &decoder);
    if (errorCode_ < 0) errorStage_ = 11;
    if (errorCode_ >= 0) {
        state_ = State::Decoding;
        const uint64_t start = sceKernelGetProcessTime();
        int frameIndex = 0;
        for (size_t index = 0; index + 1 < accessUnits.size() &&
             !stopRequested_; ++index) {
            InputData input = {};
            input.thisSize = sizeof(input);
            input.auData = static_cast<uint8_t*>(inputBlock.address) +
                accessUnits[index];
            input.auSize = accessUnits[index + 1] - accessUnits[index];
            input.ptsData = index * 1000 / 24;
            input.dtsData = input.ptsData;
            FrameBuffer frame = {};
            frame.thisSize = sizeof(frame);
            frame.frameBuffer = static_cast<uint8_t*>(frameBlock.address) +
                (frameIndex % kFrameBuffers) * memory.maxFrameBufferSize;
            frame.frameBufferSize = memory.maxFrameBufferSize;
            OutputInfo output = {}; output.thisSize = sizeof(output);
            errorCode_ = api.decode(decoder, &input, &frame, &output);
            if (errorCode_ < 0) { errorStage_ = 12; state_ = State::Failed; break; }
            ++frameIndex;
            if (output.isValid) {
                ++decodedFrames_;
                width_ = output.frameWidth;
                height_ = output.frameHeight;
                const uint64_t elapsed = sceKernelGetProcessTime() - start;
                if (elapsed) measuredFpsTimesTen_ =
                    static_cast<uint32_t>(decodedFrames_ * 10000000 / elapsed);
                if (decodedFrames_ == 1) state_ = State::Passed;
            }
        }
        if (!stopRequested_ && state_ != State::Failed) state_ = State::Finished;
        api.deleteDecoder(decoder);
    } else {
        state_ = State::Failed;
    }
    free(cpuMemory);
    releaseDirect(frameBlock);
    releaseDirect(sharedBlock);
    releaseDirect(gpuBlock);
    api.releaseQueue(queue);
    releaseDirect(computeBlock);
    releaseDirect(inputBlock);
}
