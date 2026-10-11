// SPDX-License-Identifier: GPL-3.0-or-later
// Native PS4 adaptation of Sp9nky's unofficial Stremio PS5 port.

#include "port_shell.h"
#include "artwork_service.h"
#include "../rebuild/sony_platform.h"
#include "../rebuild/stremio_service.h"

#include <algorithm>
#include <orbis/Pad.h>
#include <orbis/libkernel.h>

namespace {
bool edge(uint32_t now, uint32_t before, uint32_t mask) {
    return (now & mask) && !(before & mask);
}
const CatalogItem* selected(const ps5port::AppModel& m) {
    if (m.rows.empty()) return nullptr;
    const int r = std::max(0, std::min<int>(m.row, m.rows.size() - 1));
    if (m.rows[r].items.empty()) return nullptr;
    const int c = std::max(0, std::min<int>(m.column, m.rows[r].items.size() - 1));
    return &m.rows[r].items[c];
}
}

int main() {
    rebuild::SonyPlatform platform;
    if (!platform.start()) return 1;
    Scene2D scene(1920, 1080, 4);
    if (!scene.Init(0xC000000, 2)) {
        platform.notify("Stremio PS5 port: VideoOut failed");
        return 2;
    }
    rebuild::StremioService api(platform);
    ps5port::ArtworkService artwork(platform);
    ps5port::PortShell shell;
    ps5port::AppModel model;
    artwork.loadInterfaceAssets(model);
    int frame = 1, tick = 0;
    uint32_t previous = 0;

    shell.draw(scene, model, tick++);
    scene.SubmitFlip(frame); scene.FrameWait(frame); scene.FrameBufferSwap();
    ps5port::MediaRow movies{"Popular movies", "movie/top", {}};
    ps5port::MediaRow series{"Popular series", "series/top", {}};
    const int movieResult = api.catalog("movie", "", movies.items);
    const int seriesResult = api.catalog("series", "", series.items);
    if (movieResult > 0) artwork.populate(movies);
    if (seriesResult > 0) artwork.populate(series);
    if (movieResult > 0) model.rows.push_back(movies);
    if (seriesResult > 0) model.rows.push_back(series);
    if (!model.rows.empty() && !model.rows[0].items.empty())
        artwork.loadBackdrop(model.rows[0].items[0], model.heroArtwork);
    model.loading = false;
    model.status = model.rows.empty() ? "Stremio catalogs are unavailable." : "Ready";

    int dirtyBuffers = 2;
    for (;;) {
        const uint32_t now = platform.buttons();
        bool redraw = now != previous;
        if (model.detailsOpen) {
            if (edge(now, previous, ORBIS_PAD_BUTTON_CIRCLE)) model.detailsOpen = false;
        } else if (model.railFocused) {
            if (edge(now, previous, ORBIS_PAD_BUTTON_UP)) model.railCursor = (model.railCursor + 4) % 5;
            if (edge(now, previous, ORBIS_PAD_BUTTON_DOWN)) model.railCursor = (model.railCursor + 1) % 5;
            if (edge(now, previous, ORBIS_PAD_BUTTON_RIGHT) || edge(now, previous, ORBIS_PAD_BUTTON_CROSS)) {
                model.page = static_cast<ps5port::Page>(model.railCursor);
                model.row = model.column = 0;
                model.railFocused = false;
            }
            if (edge(now, previous, ORBIS_PAD_BUTTON_CIRCLE)) model.railFocused = false;
        } else {
            if (edge(now, previous, ORBIS_PAD_BUTTON_LEFT)) {
                if (model.column > 0) --model.column;
                else model.railFocused = true;
            }
            if (edge(now, previous, ORBIS_PAD_BUTTON_RIGHT)) {
                int maximum = 0;
                if (model.page == ps5port::Page::Settings) maximum = 5;
                else if (model.page == ps5port::Page::Addons) maximum = 3;
                else if (!model.rows.empty()) maximum = static_cast<int>(model.rows[std::min<int>(model.row, model.rows.size() - 1)].items.size()) - 1;
                model.column = std::min(maximum, model.column + 1);
            }
            if (edge(now, previous, ORBIS_PAD_BUTTON_UP) && model.page == ps5port::Page::Board && model.row > 0) {
                --model.row; model.column = 0;
            }
            if (edge(now, previous, ORBIS_PAD_BUTTON_DOWN) && model.page == ps5port::Page::Board && model.row + 1 < static_cast<int>(model.rows.size())) {
                ++model.row; model.column = 0;
            }
            if (edge(now, previous, ORBIS_PAD_BUTTON_TRIANGLE) || edge(now, previous, ORBIS_PAD_BUTTON_OPTIONS)) {
                std::string query = model.query;
                if (platform.keyboard("Search Stremio", query) && !query.empty()) {
                    model.loading = true; model.status = "Searching Stremio...";
                    shell.draw(scene, model, tick++);
                    scene.SubmitFlip(++frame); scene.FrameWait(frame); scene.FrameBufferSwap();
                    ps5port::MediaRow results{"Search results", "search/" + query, {}};
                    api.catalog("movie", query, results.items);
                    artwork.populate(results);
                    model.rows.clear(); model.rows.push_back(results);
                    model.query = query; model.page = ps5port::Page::Discover;
                    model.row = model.column = 0; model.loading = false; redraw = true;
                }
            }
            if (edge(now, previous, ORBIS_PAD_BUTTON_CROSS)) {
                const CatalogItem* item = selected(model);
                if (item && (model.page == ps5port::Page::Board || model.page == ps5port::Page::Discover || model.page == ps5port::Page::Library)) {
                    model.loading = true; model.status = "Loading title...";
                    shell.draw(scene, model, tick++);
                    scene.SubmitFlip(++frame); scene.FrameWait(frame); scene.FrameBufferSwap();
                    const std::string type = model.row == 1 ? "series" : "movie";
                    if (api.details(type, *item, model.details) > 0) model.detailsOpen = true;
                    model.loading = false; redraw = true;
                }
            }
        }
        if (redraw) dirtyBuffers = 2;
        if (dirtyBuffers > 0) {
            shell.draw(scene, model, tick++);
            --dirtyBuffers;
        }
        scene.SubmitFlip(++frame); scene.FrameWait(frame); scene.FrameBufferSwap();
        previous = now;
    }
}
