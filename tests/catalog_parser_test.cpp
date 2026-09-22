#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#include "catalog.h"

int main(int argc, char** argv) {
    if (argc == 3 && std::string(argv[1]) == "--meta") {
        std::ifstream input(argv[2]);
        const std::string json{
            std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>()};
        MetaDetails details;
        if (!parseMetaDetails(json, details)) return 4;
        std::cout << "meta=" << details.name << "; "
                  << details.releaseInfo << "; " << details.runtime << '\n';
        return 0;
    }
    std::string json;
    if (argc == 2) {
        std::ifstream input(argv[1]);
        json.assign(
            std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>());
    } else {
        json = R"({"metas":[
            {"id":"tt1","name":"Movie \"One\"","poster":"https://one"},
            {"id":"tt2","name":"Movie Two","behaviorHints":{"x":true}}
        ]})";
    }

    std::vector<CatalogItem> items;
    if (!parseCatalogItems(json, items, argc == 2 ? 8 : 4) || items.empty()) return 1;
    if (argc != 2 &&
        (items.size() != 2 || items[0].name != "Movie \"One\"" ||
         items[1].id != "tt2")) return 2;
    MetaDetails details;
    const std::string meta = R"({"meta":{"id":"tt1","name":"One","description":"A test.","releaseInfo":"2026","runtime":"90 min","imdbRating":"8.1","genres":["Drama","Sci-Fi"],"videos":[{"id":"tt1:1:1","title":"Pilot","season":1,"episode":1}],"nested":{"name":"wrong"}}})";
    if (!parseMetaDetails(meta, details) || details.name != "One" ||
        details.description != "A test." || details.runtime != "90 min" ||
        details.imdbRating != "8.1" || details.genres != "Drama / Sci-Fi" ||
        details.episodes.size() != 1 || details.episodes[0].episode != 1) return 3;
    std::cout << items.size() << " items; first=" << items[0].name << '\n';
    return 0;
}
