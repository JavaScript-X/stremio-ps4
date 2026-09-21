#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#include "catalog.h"

int main(int argc, char** argv) {
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
    if (!parseCatalogItems(json, items, 4) || items.empty()) return 1;
    if (argc != 2 &&
        (items.size() != 2 || items[0].name != "Movie \"One\"" ||
         items[1].id != "tt2")) return 2;
    std::cout << items.size() << " items; first=" << items[0].name << '\n';
    return 0;
}
