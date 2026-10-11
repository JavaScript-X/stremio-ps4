// SPDX-License-Identifier: GPL-3.0-or-later
#include "artwork_service.h"
#include "../rebuild/sony_platform.h"

#include <algorithm>
#include <cstdio>

namespace ps5port {

bool ArtworkService::load(const std::string& url, int width, int height,
        PosterImage& image) {
    if (url.compare(0, 8, "https://") != 0) return false;
    std::string encoded;
    if (platform_.get(url, 3 * 1024 * 1024, encoded) <= 0) return false;
    return decodePosterJpeg(encoded, width, height, image);
}

bool ArtworkService::loadFile(const char* path, int width, int height,
        PosterImage& image) {
    FILE* file = std::fopen(path, "rb");
    if (!file) return false;
    std::fseek(file, 0, SEEK_END);
    const long size = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);
    if (size <= 0 || size > 4 * 1024 * 1024) { std::fclose(file); return false; }
    std::string encoded(static_cast<size_t>(size), '\0');
    const bool read = std::fread(&encoded[0], 1, encoded.size(), file) == encoded.size();
    std::fclose(file);
    return read && decodePosterJpeg(encoded, width, height, image);
}

void ArtworkService::loadInterfaceAssets(AppModel& model) {
    loadFile("/app0/assets/logo_l.png", 64, 64, model.shellLogo);
    static const char* files[] = {
        "nav_board.png", "nav_discover.png", "nav_library.png",
        "nav_addons.png", "nav_settings.png"
    };
    model.navigationIcons.resize(5);
    for (int i = 0; i < 5; ++i) {
        const std::string path = std::string("/app0/assets/") + files[i];
        loadFile(path.c_str(), 46, 46, model.navigationIcons[static_cast<size_t>(i)]);
    }
}

void ArtworkService::populate(MediaRow& row, int maximum) {
    row.artwork.clear();
    const int count = std::min<int>(maximum, row.items.size());
    row.artwork.resize(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        std::string url = row.items[i].poster;
        if (url.empty() && !row.items[i].id.empty())
            url = "https://images.metahub.space/poster/medium/" +
                row.items[i].id + "/img";
        load(url, 232, 300, row.artwork[static_cast<size_t>(i)]);
    }
}

bool ArtworkService::loadBackdrop(const CatalogItem& item, PosterImage& image) {
    std::string url = item.background;
    if (url.empty() && !item.id.empty())
        url = "https://images.metahub.space/background/medium/" + item.id + "/img";
    return load(url, 960, 540, image);
}

}
