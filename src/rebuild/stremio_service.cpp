#include "stremio_service.h"
#include "sony_platform.h"

#include <cctype>

namespace rebuild {
namespace {
std::string encode(const std::string& value) {
    static const char hex[] = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : value) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.') out += c;
        else { out += '%'; out += hex[c >> 4]; out += hex[c & 15]; }
    }
    return out;
}
bool safe(const std::string& value) {
    if (value.empty() || value.size() > 96) return false;
    for (unsigned char c : value)
        if (!std::isalnum(c) && c != '-' && c != '_' && c != ':' && c != '.') return false;
    return true;
}
}

int StremioService::catalog(const std::string& type, const std::string& search,
        std::vector<CatalogItem>& result) {
    if (type != "movie" && type != "series") return -10;
    std::string url = search.empty()
        ? "https://cinemeta-catalogs.strem.io/top/catalog/" + type + "/top.json"
        : "https://v3-cinemeta.strem.io/catalog/" + type +
          "/top/search=" + encode(search) + ".json";
    std::string body;
    const int bytes = platform_.get(url, 1024 * 1024, body);
    if (bytes < 0) return bytes;
    return parseCatalogItems(body, result, 30) ? static_cast<int>(result.size()) : -20;
}

int StremioService::details(const std::string& type, const CatalogItem& item,
        MetaDetails& result) {
    if (!safe(item.id)) return -10;
    const std::string resource = type == "series" ? "series" : "movie";
    std::string body;
    const int bytes = platform_.get("https://v3-cinemeta.strem.io/meta/" +
        resource + "/" + item.id + ".json", 1024 * 1024, body);
    if (bytes < 0) return bytes;
    return parseMetaDetails(body, result) ? 1 : -20;
}
}
