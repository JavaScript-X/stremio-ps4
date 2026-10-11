#pragma once

#include "../catalog.h"
#include "../graphics.h"
#include "theme.h"
#include <string>
#include <vector>

namespace rebuild {

enum class Page { Home, Search, Library, Settings, Details };

struct ViewModel {
    Page page = Page::Home;
    int navigation = 0;
    int selected = 0;
    bool rail = false;
    bool busy = true;
    std::string status = "CONNECTING TO STREMIO";
    std::string query;
    std::string type = "movie";
    std::vector<CatalogItem> items;
    MetaDetails details;
};

class PlayStationShell {
public:
    void draw(Scene2D& scene, const ViewModel& model, int pulse);

private:
    Theme theme_ = midnightTheme();
    void header(Scene2D& scene, const char* section);
    void footer(Scene2D& scene, const char* controls);
    void rail(Scene2D& scene, int selected, bool expanded);
    void home(Scene2D& scene, const ViewModel& model);
    void search(Scene2D& scene, const ViewModel& model);
    void settings(Scene2D& scene, const ViewModel& model);
    void details(Scene2D& scene, const ViewModel& model);
};
}
