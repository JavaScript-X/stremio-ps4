#include "mp4_demux.h"

#include <cstdio>

int main(int argc, char** argv) {
    if (argc != 3) return 2;
    Mp4MediaInfo info;
    const int result = demuxMp4AvcToAnnexB(argv[1], argv[2], info);
    if (result) {
        std::fprintf(stderr, "demux stage %d\n", result);
        return result;
    }
    std::printf("%ux%u %u.%02u fps %u samples %llums %s\n",
        info.width, info.height, info.fpsTimes100 / 100,
        info.fpsTimes100 % 100, info.samples,
        static_cast<unsigned long long>(info.durationMs),
        info.videoCodec.c_str());
    return info.width && info.height && info.samples ? 0 : 20;
}
