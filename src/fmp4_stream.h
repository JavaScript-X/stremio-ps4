#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct Fmp4VideoConfig {
    uint8_t nalLengthBytes = 0;
    std::vector<std::vector<uint8_t>> parameterSets;
};

bool parseFmp4VideoConfig(const std::string& init, Fmp4VideoConfig& config);
bool convertFmp4VideoSegment(const std::string& segment,
    const Fmp4VideoConfig& config, const std::string& outputPath,
    uint32_t& sampleCount);
