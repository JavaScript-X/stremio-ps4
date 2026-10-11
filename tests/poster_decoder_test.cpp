#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

#include "poster.h"

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    std::ifstream input(argv[1], std::ios::binary);
    std::string encoded{
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()};
    PosterImage poster;
    if (!decodePosterJpeg(encoded, 310, 410, poster) || !poster.valid()) return 1;
    std::cout << poster.width << 'x' << poster.height
              << "; pixels=" << poster.pixels.size() << '\n';
    return 0;
}
