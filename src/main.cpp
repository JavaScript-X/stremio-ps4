#include <cstdio>
#include <cstdint>
#include <sstream>

#include <orbis/libkernel.h>
#include <orbis/Http.h>
#include <orbis/Net.h>
#include <orbis/Pad.h>
#include <orbis/Ssl.h>
#include <orbis/Sysmodule.h>
#include <orbis/SystemService.h>
#include <orbis/UserService.h>

#include "graphics.h"
#include "log.h"
#include "avplayer.h"

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
constexpr int kNetworkPoolSize = 64 * 1024;
constexpr uint32_t kHttpTimeoutUsec = 8 * 1000 * 1000;
constexpr const char* kLegalVideoPath = "/app0/assets/flower.mp4";

int networkPoolId = 0;
int sslContextId = 0;
int httpContextId = 0;

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

bool initializeHttp() {
    if (httpContextId > 0) {
        return true;
    }

    if (static_cast<int32_t>(
            sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_NET)) < 0 ||
        static_cast<int32_t>(
            sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_HTTP)) < 0 ||
        static_cast<int32_t>(
            sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_SSL)) < 0) {
        return false;
    }

    sceNetInit();
    networkPoolId = sceNetPoolCreate("stremioNetPool", kNetworkPoolSize, 0);
    if (networkPoolId < 0) {
        return false;
    }

    sslContextId = sceSslInit(SSL_POOLSIZE);
    if (sslContextId < 0) {
        return false;
    }

    httpContextId = sceHttpInit(networkPoolId, sslContextId, LIBHTTP_POOLSIZE);
    return httpContextId >= 0;
}

int probeStremioHttps() {
    if (!initializeHttp()) {
        return -1;
    }

    constexpr const char* kProbeUrl = "https://www.stremio.com/";
    int templateId = sceHttpCreateTemplate(
        httpContextId,
        "StremioPS4/1.08",
        ORBIS_HTTP_VERSION_1_1,
        1);
    if (templateId < 0) {
        return -2;
    }

    sceHttpSetResolveTimeOut(templateId, kHttpTimeoutUsec);
    sceHttpSetConnectTimeOut(templateId, kHttpTimeoutUsec);
    sceHttpSetSendTimeOut(templateId, kHttpTimeoutUsec);

    int connectionId = sceHttpCreateConnectionWithURL(templateId, kProbeUrl, false);
    if (connectionId < 0) {
        sceHttpDeleteTemplate(templateId);
        return -3;
    }

    int requestId = sceHttpCreateRequestWithURL(
        connectionId,
        ORBIS_METHOD_GET,
        kProbeUrl,
        0);
    if (requestId < 0) {
        sceHttpDeleteConnection(connectionId);
        sceHttpDeleteTemplate(templateId);
        return -4;
    }

    int statusCode = -5;
    if (sceHttpSendRequest(requestId, nullptr, 0) >= 0) {
        int responseStatus = 0;
        if (sceHttpGetStatusCode(requestId, &responseStatus) >= 0) {
            statusCode = responseStatus;
        }
    }

    sceHttpDeleteRequest(requestId);
    sceHttpDeleteConnection(connectionId);
    sceHttpDeleteTemplate(templateId);
    return statusCode;
}
}  // namespace

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    DEBUGLOG << "Stremio PS4 M0 starting";
    notify("Stremio PS4 1.08: direct-memory AVPlayer test");

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
    AvPlayerProbe avPlayer;
    AvPlayerProbe::State previousPlayerState = AvPlayerProbe::State::Idle;
    int avPlayerProbeFrames = 0;
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
            notify("Stremio PS4: running HTTPS probe...");
            const int statusCode = probeStremioHttps();
            char result[96];
            if (statusCode >= 100) {
                snprintf(
                    result,
                    sizeof(result),
                    "Stremio PS4: HTTPS status %d",
                    statusCode);
            } else {
                snprintf(
                    result,
                    sizeof(result),
                    "Stremio PS4: HTTPS probe failed at stage %d",
                    -statusCode);
            }
            notify(result);
        }
        if ((pressed & ORBIS_PAD_BUTTON_SQUARE) != 0) {
            notify("Stremio PS4: opening packaged CC0 H.264 video...");
            avPlayerProbeFrames = 0;
            if (!avPlayer.start(kLegalVideoPath)) {
                char result[128];
                snprintf(result, sizeof(result),
                    "Stremio PS4: AVPlayer stage %d, code 0x%08x",
                    avPlayer.errorStage(),
                    static_cast<unsigned int>(avPlayer.errorCode()));
                notify(result);
            }
        }
        if ((pressed & ORBIS_PAD_BUTTON_CIRCLE) != 0 &&
            avPlayer.state() != AvPlayerProbe::State::Idle) {
            avPlayer.stop();
            avPlayerProbeFrames = 0;
            notify("Stremio PS4: AVPlayer probe cancelled");
        }

        avPlayer.update();
        if (avPlayer.state() == AvPlayerProbe::State::Opening ||
            avPlayer.state() == AvPlayerProbe::State::Decoding) {
            ++avPlayerProbeFrames;
            if (avPlayerProbeFrames > 1200) {
                avPlayer.stop();
                notify("Stremio PS4: AVPlayer timed out before first frame");
            }
        }
        if (avPlayer.state() != previousPlayerState) {
            if (avPlayer.state() == AvPlayerProbe::State::Decoding) {
                notify("Stremio PS4: AVPlayer active; waiting for frame");
            } else if (avPlayer.state() == AvPlayerProbe::State::Passed) {
                char result[96];
                snprintf(result, sizeof(result),
                    "Stremio PS4: decoded frame %ux%u",
                    avPlayer.width(), avPlayer.height());
                notify(result);
                avPlayer.stop();
            }
            previousPlayerState = avPlayer.state();
        }

        drawHardwareProbe(scene, focusedCard);
        scene.SubmitFlip(frameId);
        scene.FrameWait(frameId);
        scene.FrameBufferSwap();
        ++frameId;
    }
}
