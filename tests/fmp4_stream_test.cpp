#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <sys/stat.h>

#include "fmp4_stream.h"

static std::string readFile(const char* path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()};
}

int main(int argc, char** argv) {
    if (argc != 4) return 2;
    Fmp4VideoConfig config;
    const std::string init = readFile(argv[1]);
    const std::string segment = readFile(argv[2]);
    if (!parseFmp4VideoConfig(init, config) || !config.timescale) return 3;
    uint32_t samples = 0;
    if (!convertFmp4VideoSegment(segment, config, argv[3], samples) ||
        !samples) return 4;
    struct stat timing = {};
    if (stat((std::string(argv[3]) + ".pts").c_str(), &timing) != 0 ||
        timing.st_size != static_cast<off_t>(samples * 16)) return 5;
    std::cout << "samples=" << samples << " timescale=" << config.timescale
              << " timing-bytes=" << timing.st_size << '\n';
    return 0;
}
