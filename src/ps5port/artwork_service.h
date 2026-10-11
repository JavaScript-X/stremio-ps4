// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ui_model.h"

namespace rebuild { class SonyPlatform; }

namespace ps5port {

class ArtworkService {
public:
    explicit ArtworkService(rebuild::SonyPlatform& platform) : platform_(platform) {}
    void populate(MediaRow& row, int maximum = 12);
    bool loadBackdrop(const CatalogItem& item, PosterImage& image);
    void loadInterfaceAssets(AppModel& model);

private:
    bool load(const std::string& url, int width, int height, PosterImage& image);
    bool loadFile(const char* path, int width, int height, PosterImage& image);
    rebuild::SonyPlatform& platform_;
};

}
