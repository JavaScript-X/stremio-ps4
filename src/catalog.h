#pragma once

#include <cstddef>
#include <string>
#include <vector>

struct CatalogItem {
    std::string id;
    std::string name;
    std::string poster;
};

struct MetaDetails {
    struct Episode {
        std::string id;
        std::string title;
        int season = 0;
        int episode = 0;
    };

    std::string id;
    std::string name;
    std::string description;
    std::string releaseInfo;
    std::string runtime;
    std::string imdbRating;
    std::string genres;
    std::vector<Episode> episodes;
};

bool parseCatalogItems(
    const std::string& json,
    std::vector<CatalogItem>& items,
    size_t maximumItems);

bool parseMetaDetails(const std::string& json, MetaDetails& details);
