#include <cstdio>
#include <cstdint>
#include <sstream>

#include <orbis/libkernel.h>
#include <orbis/Pad.h>
#include <orbis/SystemService.h>
#include <orbis/UserService.h>

#include "graphics.h"
#include "log.h"

std::stringstream debugLogStream;

// OpenOrbis' shared Scene2D helper declares this destructor but v0.5.4 does
// not provide its definition. The application owns the scene for its entire
// process lifetime, so the default implementation is sufficient here.
Scene2D::~Scene2D() = default;

extern "C" int sceSysUtilSendSystemNotificationWithText(
    int messageType,
    const char* message);

namespace {
constexpr int kWidth = 1920;
constexpr int kHeight = 1080;
constexpr int kPixelDepth = 4;
constexpr int kFrameBuffers = 2;
// Match the direct-memory pool used by OpenOrbis' working graphics sample.
constexpr size_t kVideoMemory = 0xC000000;

void drawHardwareProbe(Scene2D& scene, int focusedCard) {
    const Color background = {18, 18, 24};
    const Color sidebar = {29, 29, 39};
    const Color stremioPurple = {123, 91, 214};
    const Color card = {45, 45, 58};
    const Color cardMuted = {35, 35, 46};
    const Color focus = {196, 174, 255};

    scene.FrameBufferFill(background);
    scene.DrawRectangle(0, 0, 310, kHeight, sidebar);
    scene.DrawRectangle(48, 54, 214, 54, stremioPurple);
    scene.DrawRectangle(48, 170, 214, 18, card);
    scene.DrawRectangle(48, 224, 170, 18, cardMuted);
    scene.DrawRectangle(48, 278, 194, 18, cardMuted);

    scene.DrawRectangle(370, 76, 690, 48, stremioPurple);
    const int cardX[] = {370, 718, 1066, 1414};
    for (int index = 0; index < 4; ++index) {
        if (index == focusedCard) {
            scene.DrawRectangle(cardX[index] - 8, 172, 326, 426, focus);
        }
        scene.DrawRectangle(cardX[index], 180, 310, 410, card);
    }

    scene.DrawRectangle(370, 654, 1354, 28, cardMuted);
    scene.DrawRectangle(370, 720, 1030, 28, cardMuted);
    scene.DrawRectangle(370, 786, 1180, 28, cardMuted);
}

void notify(const char* message) {
    sceSysUtilSendSystemNotificationWithText(222, message);
}

int initializeController() {
    OrbisUserServiceInitializeParams userParams = {};
    userParams.priority = ORBIS_KERNEL_PRIO_FIFO_LOWEST;
    int userId = -1;

    sceUserServiceInitialize(&userParams);
    if (sceUserServiceGetInitialUser(&userId) != 0 || scePadInit() != 0) {
        return -1;
    }

    return scePadOpen(userId, 0, 0, nullptr);
}

uint32_t readButtons(int pad) {
    if (pad < 0) {
        return 0;
    }

    OrbisPadData padData = {};
    return scePadReadState(pad, &padData) == 0 ? padData.buttons : 0;
}
}  // namespace

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    DEBUGLOG << "Stremio PS4 M0 starting";
    notify("Stremio PS4 1.03: interactive UI test");

    const int pad = initializeController();
    notify(pad >= 0
        ? "Stremio PS4: controller ready"
        : "Stremio PS4: controller initialization failed");

    Scene2D scene(kWidth, kHeight, kPixelDepth);
    if (!scene.Init(kVideoMemory, kFrameBuffers)) {
        DEBUGLOG << "Video initialization failed";
        notify("Stremio PS4: VIDEO INITIALIZATION FAILED");
        for (;;) {
            if ((readButtons(pad) & ORBIS_PAD_BUTTON_OPTIONS) != 0) {
                sceSystemServiceNavigateToGoHome();
                sceKernelUsleep(1000000);
            }
            sceKernelUsleep(16000);
        }
    }

    DEBUGLOG << "Video initialized at 1920x1080; rendering hardware probe";
    notify("Stremio PS4: video ready");

    int frameId = 1;
    int focusedCard = 0;
    uint32_t previousButtons = 0;
    scene.SetActiveFrameBuffer(0);

    for (;;) {
        const uint32_t buttons = readButtons(pad);
        const uint32_t pressed = buttons & ~previousButtons;
        previousButtons = buttons;

        if ((pressed & ORBIS_PAD_BUTTON_OPTIONS) != 0) {
            DEBUGLOG << "Options pressed; navigating home";
            sceSystemServiceNavigateToGoHome();
            sceKernelUsleep(1000000);
        }

        if ((pressed & ORBIS_PAD_BUTTON_LEFT) != 0 && focusedCard > 0) {
            --focusedCard;
        }
        if ((pressed & ORBIS_PAD_BUTTON_RIGHT) != 0 && focusedCard < 3) {
            ++focusedCard;
        }
        if ((pressed & ORBIS_PAD_BUTTON_CROSS) != 0) {
            notify("Stremio PS4: focused card activated");
        }
        if ((pressed & ORBIS_PAD_BUTTON_TRIANGLE) != 0) {
            notify("Stremio PS4: network probe comes next");
        }

        drawHardwareProbe(scene, focusedCard);
        scene.SubmitFlip(frameId);
        scene.FrameWait(frameId);
        scene.FrameBufferSwap();
        ++frameId;
    }
}
