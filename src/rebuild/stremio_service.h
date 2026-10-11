#pragma once

#include "../catalog.h"
#include <string>
#include <vector>

namespace rebuild {
class SonyPlatform;

class StremioService {
public:
    explicit StremioService(SonyPlatform& platform) : platform_(platform) {}
    int catalog(const std::string& type, const std::string& search,
        std::vector<CatalogItem>& result);
    int details(const std::string& type, const CatalogItem& item,
        MetaDetails& result);

private:
    SonyPlatform& platform_;
};
}
