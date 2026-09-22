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
    std::string id;
    std::string name;
    std::string description;
    std::string releaseInfo;
    std::string runtime;
};

bool parseCatalogItems(
    const std::string& json,
    std::vector<CatalogItem>& items,
    size_t maximumItems);

bool parseMetaDetails(const std::string& json, MetaDetails& details);
