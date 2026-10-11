#include "playstation_shell.h"
#include "sony_platform.h"
#include "stremio_service.h"

#include <orbis/Pad.h>
#include <orbis/libkernel.h>

using namespace rebuild;

namespace {
bool pressed(uint32_t now, uint32_t before, uint32_t key) {
    return (now & key) && !(before & key);
}
}

int main() {
    SonyPlatform platform;
    if (!platform.start()) return 1;
    Scene2D scene(1920, 1080, 4);
    if (!scene.Init(0xC000000, 2)) {
        platform.notify("Stremio rebuild: VideoOut initialization failed");
        return 2;
    }
    PlayStationShell shell;
    StremioService stremio(platform);
    ViewModel view;
    int frame = 1, pulse = 0;
    uint32_t previous = 0;
    shell.draw(scene, view, pulse); scene.SubmitFlip(frame); scene.FrameWait(frame); scene.FrameBufferSwap();
    const int loaded = stremio.catalog("movie", "", view.items);
    view.busy = false;
    view.status = loaded > 0 ? "READY" : "STREMIO SERVICE UNAVAILABLE";
    if (loaded < 0) platform.notify("Stremio rebuild: catalog unavailable");

    for (;;) {
        const uint32_t now = platform.buttons();
        bool redraw = now != previous;
        if (view.rail) {
            if (pressed(now, previous, ORBIS_PAD_BUTTON_UP)) view.navigation = (view.navigation + 3) % 4;
            if (pressed(now, previous, ORBIS_PAD_BUTTON_DOWN)) view.navigation = (view.navigation + 1) % 4;
            if (pressed(now, previous, ORBIS_PAD_BUTTON_RIGHT) || pressed(now, previous, ORBIS_PAD_BUTTON_CROSS)) {
                view.page = static_cast<Page>(view.navigation);
                view.selected = 0; view.rail = false;
            }
            if (pressed(now, previous, ORBIS_PAD_BUTTON_CIRCLE)) view.rail = false;
        } else if (view.page == Page::Details) {
            if (pressed(now, previous, ORBIS_PAD_BUTTON_CIRCLE)) view.page = Page::Home;
        } else {
            if (pressed(now, previous, ORBIS_PAD_BUTTON_LEFT)) {
                if (view.selected > 0 && view.page != Page::Settings) --view.selected;
                else view.rail = true;
            }
            if (pressed(now, previous, ORBIS_PAD_BUTTON_RIGHT)) {
                if (view.page == Page::Settings) view.selected = std::min(5, view.selected + 1);
                else if (!view.items.empty())
                    view.selected = std::min<int>(view.selected + 1, view.items.size() - 1);
            }
            if (pressed(now, previous, ORBIS_PAD_BUTTON_UP) && view.page == Page::Settings)
                view.selected = std::max(0, view.selected - 3);
            if (pressed(now, previous, ORBIS_PAD_BUTTON_DOWN) && view.page == Page::Settings)
                view.selected = std::min(5, view.selected + 3);
            if (pressed(now, previous, ORBIS_PAD_BUTTON_CROSS)) {
                if (view.page == Page::Search) {
                    if (platform.keyboard("Search Stremio", view.query)) {
                        view.busy = true; shell.draw(scene, view, pulse);
                        scene.SubmitFlip(++frame); scene.FrameWait(frame); scene.FrameBufferSwap();
                        stremio.catalog(view.type, view.query, view.items);
                        view.selected = 0; view.busy = false;
                        redraw = true;
                    }
                } else if ((view.page == Page::Home || view.page == Page::Library) && !view.items.empty()) {
                    view.busy = true; view.status = "LOADING DETAILS";
                    shell.draw(scene, view, pulse); scene.SubmitFlip(++frame); scene.FrameWait(frame); scene.FrameBufferSwap();
                    if (stremio.details(view.type, view.items[view.selected], view.details) > 0)
                        view.page = Page::Details;
                    view.busy = false;
                    redraw = true;
                }
            }
        }
        // EVO-style state-driven rendering: static screens do not continuously
        // burn CPU/GPU. Input, data and animation transitions mark a new frame.
        if (redraw) {
            shell.draw(scene, view, pulse++);
            scene.SubmitFlip(++frame); scene.FrameWait(frame); scene.FrameBufferSwap();
        }
        previous = now;
        sceKernelUsleep(16000);
    }
    platform.stop();
    return 0;
}
