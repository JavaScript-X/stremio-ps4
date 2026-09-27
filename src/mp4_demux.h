#pragma once

#include <cstdint>
#include <string>

struct Mp4MediaInfo {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t fpsTimes100 = 0;
    uint64_t durationMs = 0;
    uint32_t samples = 0;
    bool hasAudio = false;
    std::string videoCodec;
    std::string audioCodec;
};

// Extracts an AVC track from a complete, progressive MP4 into an Annex-B
// elementary stream accepted by the PS4 Videodec2 path. Returns zero on
// success and a stable positive diagnostic stage on failure.
int demuxMp4AvcToAnnexB(const std::string& inputPath,
    const std::string& outputPath, Mp4MediaInfo& info);
