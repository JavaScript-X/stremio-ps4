#pragma once

#include <cstddef>
#include <string>
#include <vector>

struct CatalogItem {
    std::string id;
    std::string name;
    std::string poster;
};

bool parseCatalogItems(
    const std::string& json,
    std::vector<CatalogItem>& items,
    size_t maximumItems);
