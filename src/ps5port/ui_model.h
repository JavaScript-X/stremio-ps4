// SPDX-License-Identifier: GPL-3.0-or-later
// PS4 adaptation of the content model in Sp9nky/unofficial-stremio-ps5-port.
#pragma once

#include "../catalog.h"
#include "../poster.h"
#include <string>
#include <vector>

namespace ps5port {

enum class Page { Board, Discover, Library, Addons, Settings };

struct MediaRow {
    std::string title;
    std::string key;
    std::vector<CatalogItem> items;
    std::vector<PosterImage> artwork;
};

struct AppModel {
    Page page = Page::Board;
    bool railFocused = false;
    int railCursor = 0;
    int row = 0;
    int column = 0;
    int detailSection = 0;
    int detailCursor = 0;
    bool loading = true;
    bool detailsOpen = false;
    std::string status = "Loading your catalogs...";
    std::string query;
    std::vector<MediaRow> rows;
    PosterImage heroArtwork;
    PosterImage shellLogo;
    std::vector<PosterImage> navigationIcons;
    MetaDetails details;
};

}
