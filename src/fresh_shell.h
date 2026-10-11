#pragma once

#include "catalog.h"
#include "graphics.h"
#include "poster.h"

#include <string>
#include <vector>

namespace freshui {

struct Assets {
    const PosterImage* brand = nullptr;
    const PosterImage* creator = nullptr;
};

struct Preferences {
    bool highContrast = false;
    bool showHints = true;
};

void configure(const Assets& assets, const Preferences& preferences);
void loading(Scene2D& scene, int phase, const char* message);
void home(Scene2D& scene, const std::vector<CatalogItem>& items,
    const std::vector<PosterImage>& posters, int selected, int activeTab,
    const PosterImage* backdrop, const PosterImage* titleLogo,
    bool menuOpen, const std::string& status);
void search(Scene2D& scene, const std::string& query, int filter,
    bool menuOpen);
void dashboard(Scene2D& scene, int selected, bool menuOpen);
void list(Scene2D& scene, const char* section, const char* const* rows,
    int count, int selected, bool menuOpen, const char* action);
void details(Scene2D& scene, const MetaDetails& meta, const PosterImage* poster,
    const PosterImage* backdrop, const PosterImage* titleLogo,
    const std::string& type, int episode);
void streams(Scene2D& scene, const MetaDetails& meta,
    const std::vector<StreamItem>& streams, int selected);
void waitForStreams(Scene2D& scene, const MetaDetails& meta, int phase);
void account(Scene2D& scene, const std::string& code, const std::string& link,
    const std::string& status, int addonCount, bool signedIn);
void about(Scene2D& scene);
void progress(Scene2D& scene, int stage, uint64_t downloaded,
    uint64_t expected, uint64_t elapsedUs, int errorCode);
void player(Scene2D& scene, const std::vector<uint32_t>& pixels,
    uint32_t width, uint32_t height, const std::string& title,
    bool hud, bool fill, bool audio, bool paused, uint64_t current,
    uint64_t duration, uint64_t frames, uint64_t fpsTimesTen,
    int audioOffsetMs);

}
