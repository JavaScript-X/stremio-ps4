#include "fmp4_stream.h"

#include <cstdio>
#include <cstring>

namespace {
uint32_t be32(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
        (uint32_t(p[2]) << 8) | p[3];
}

size_t findType(const std::string& data, const char* type, size_t from = 4) {
    for (size_t at = from; at + 4 <= data.size(); ++at)
        if (std::memcmp(data.data() + at, type, 4) == 0) return at;
    return std::string::npos;
}

bool writeNal(FILE* output, const uint8_t* data, size_t size) {
    static const uint8_t start[] = {0, 0, 0, 1};
    return std::fwrite(start, 1, sizeof(start), output) == sizeof(start) &&
        std::fwrite(data, 1, size, output) == size;
}
}

bool parseFmp4VideoConfig(const std::string& init, Fmp4VideoConfig& config) {
    config = {};
    const size_t type = findType(init, "avcC");
    if (type == std::string::npos || type < 4 || type + 11 > init.size())
        return false;
    const uint32_t boxSize = be32(reinterpret_cast<const uint8_t*>(
        init.data() + type - 4));
    if (boxSize < 15 || type - 4 + boxSize > init.size()) return false;
    const uint8_t* data = reinterpret_cast<const uint8_t*>(init.data() + type + 4);
    const size_t length = boxSize - 8;
    if (length < 7 || data[0] != 1) return false;
    config.nalLengthBytes = (data[4] & 3) + 1;
    size_t at = 6;
    int count = data[5] & 31;
    for (int group = 0; group < 2; ++group) {
        if (group) {
            if (at >= length) return false;
            count = data[at++];
        }
        for (int index = 0; index < count; ++index) {
            if (at + 2 > length) return false;
            const size_t bytes = (size_t(data[at]) << 8) | data[at + 1];
            at += 2;
            if (!bytes || at + bytes > length) return false;
            config.parameterSets.emplace_back(data + at, data + at + bytes);
            at += bytes;
        }
    }
    return config.nalLengthBytes > 0 && !config.parameterSets.empty();
}

bool convertFmp4VideoSegment(const std::string& segment,
    const Fmp4VideoConfig& config, const std::string& outputPath,
    uint32_t& sampleCount) {
    sampleCount = 0;
    if (config.nalLengthBytes < 1 || config.nalLengthBytes > 4) return false;
    const size_t moofType = findType(segment, "moof");
    const size_t trunType = findType(segment, "trun");
    if (moofType == std::string::npos || trunType == std::string::npos ||
        moofType < 4 || trunType + 12 > segment.size()) return false;
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(segment.data());
    const uint32_t flags = be32(bytes + trunType + 4) & 0x00ffffff;
    const uint32_t count = be32(bytes + trunType + 8);
    if (!count || count > 100000 || !(flags & 0x000200)) return false;
    size_t cursor = trunType + 12;
    int32_t dataOffset = 0;
    if (flags & 0x000001) {
        if (cursor + 4 > segment.size()) return false;
        dataOffset = static_cast<int32_t>(be32(bytes + cursor)); cursor += 4;
    }
    if (flags & 0x000004) cursor += 4;
    std::vector<uint32_t> sizes;
    sizes.reserve(count);
    for (uint32_t index = 0; index < count; ++index) {
        if (flags & 0x000100) cursor += 4;
        if (cursor + 4 > segment.size()) return false;
        sizes.push_back(be32(bytes + cursor)); cursor += 4;
        if (flags & 0x000400) cursor += 4;
        if (flags & 0x000800) cursor += 4;
        if (cursor > segment.size()) return false;
    }
    const int64_t mediaStart = static_cast<int64_t>(moofType - 4) + dataOffset;
    if (mediaStart < 0 || static_cast<size_t>(mediaStart) >= segment.size())
        return false;
    FILE* output = std::fopen(outputPath.c_str(), "wb");
    if (!output) return false;
    static const uint8_t aud[] = {0, 0, 0, 1, 9, 0x10};
    size_t sampleAt = static_cast<size_t>(mediaStart);
    bool okay = true;
    for (uint32_t index = 0; index < count && okay; ++index) {
        const size_t end = sampleAt + sizes[index];
        if (end > segment.size() ||
            std::fwrite(aud, 1, sizeof(aud), output) != sizeof(aud)) {
            okay = false; break;
        }
        if (index == 0)
            for (const auto& set : config.parameterSets)
                if (!writeNal(output, set.data(), set.size())) { okay = false; break; }
        while (okay && sampleAt + config.nalLengthBytes <= end) {
            uint32_t nalSize = 0;
            for (uint8_t byte = 0; byte < config.nalLengthBytes; ++byte)
                nalSize = (nalSize << 8) | bytes[sampleAt + byte];
            sampleAt += config.nalLengthBytes;
            if (!nalSize || sampleAt + nalSize > end ||
                !writeNal(output, bytes + sampleAt, nalSize)) {
                okay = false; break;
            }
            sampleAt += nalSize;
        }
        if (sampleAt != end) okay = false;
    }
    okay = std::fclose(output) == 0 && okay;
    if (!okay) std::remove(outputPath.c_str());
    sampleCount = okay ? count : 0;
    return okay;
}
