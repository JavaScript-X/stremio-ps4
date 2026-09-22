#include <cstdio>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

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
#include "catalog.h"
#include "poster.h"

std::stringstream debugLogStream;

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
constexpr const char* kLegalVideoPath = "/app0/assets/sintel-trailer.mp4";
constexpr const char* kCatalogUrl =
    "https://cinemeta-catalogs.strem.io/top/catalog/movie/top.json";
constexpr size_t kMaximumCatalogBytes = 512 * 1024;
constexpr size_t kMaximumPosterBytes = 2 * 1024 * 1024;
constexpr int kPosterWidth = 310;
constexpr int kPosterHeight = 410;

int networkPoolId = 0;
int sslContextId = 0;
int httpContextId = 0;

std::string shortTitle(const std::string& title) {
    constexpr size_t kMaximumCharacters = 18;
    if (title.size() <= kMaximumCharacters) return title;
    return title.substr(0, kMaximumCharacters - 3) + "...";
}

void drawHardwareProbe(
    Scene2D& scene,
    int focusedCard,
    const std::vector<CatalogItem>& items,
    const std::vector<PosterImage>& posters) {
    const Color background = {18, 18, 24};
    const Color sidebar = {29, 29, 39};
    const Color stremioPurple = {123, 91, 214};
    const Color card = {45, 45, 58};
    const Color cardMuted = {35, 35, 46};
    const Color focus = {196, 174, 255};
    const Color text = {235, 232, 244};
    const Color mutedText = {164, 158, 181};

    scene.FrameBufferFill(background);
    scene.DrawRectangle(0, 0, 310, kHeight, sidebar);
    scene.DrawRectangle(48, 54, 214, 54, stremioPurple);
    scene.DrawRectangle(48, 170, 214, 18, card);
    scene.DrawRectangle(48, 224, 170, 18, cardMuted);
    scene.DrawRectangle(48, 278, 194, 18, cardMuted);
    scene.DrawText(68, 70, "STREMIO", text, 3);
    scene.DrawText(68, 168, "HOME", mutedText, 2);
    scene.DrawText(68, 222, "DISCOVER", mutedText, 2);
    scene.DrawText(68, 276, "LIBRARY", mutedText, 2);

    scene.DrawRectangle(370, 76, 690, 48, stremioPurple);
    scene.DrawText(392, 88, "TOP MOVIES", text, 3);
    const int cardX[] = {370, 718, 1066, 1414};
    for (int index = 0; index < 4; ++index) {
        if (index == focusedCard) {
            scene.DrawRectangle(cardX[index] - 8, 172, 326, 426, focus);
        }
        scene.DrawRectangle(cardX[index], 180, 310, 410, card);
        if (index < static_cast<int>(posters.size()) && posters[index].valid()) {
            scene.BlitRgb(
                cardX[index], 180, posters[index].width, posters[index].height,
                posters[index].pixels.data());
        }
        if (index < static_cast<int>(items.size())) {
            const std::string title = shortTitle(items[index].name);
            scene.DrawText(cardX[index], 610, title.c_str(), text, 2);
        }
    }

    scene.DrawRectangle(370, 654, 1354, 28, cardMuted);
    scene.DrawRectangle(370, 720, 1030, 28, cardMuted);
    scene.DrawRectangle(370, 786, 1180, 28, cardMuted);
}

void drawDecodedPreview(
    Scene2D& scene,
    const std::vector<uint32_t>& pixels,
    uint32_t previewWidth,
    uint32_t previewHeight,
    bool paintBackground) {
    const Color background = {8, 8, 12};
    const Color border = {196, 174, 255};
    const int width = static_cast<int>(previewWidth);
    const int height = static_cast<int>(previewHeight);
    const int startX = (kWidth - width) / 2;
    const int startY = (kHeight - height) / 2;
    if (paintBackground) {
        scene.FrameBufferFill(background);
        scene.DrawRectangle(startX - 8, startY - 8, width + 16, height + 16, border);
    }

    scene.BlitRgb(startX, startY, width, height, pixels.data());
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

int downloadUrl(const char* url, size_t maximumBytes, std::string& body) {
    body.clear();
    if (!url || !initializeHttp()) return -1;
    int templateId = sceHttpCreateTemplate(
        httpContextId,
        "StremioPS4/1.22",
        ORBIS_HTTP_VERSION_1_1,
        1);
    if (templateId < 0) return -2;

    sceHttpSetResolveTimeOut(templateId, kHttpTimeoutUsec);
    sceHttpSetConnectTimeOut(templateId, kHttpTimeoutUsec);
    sceHttpSetSendTimeOut(templateId, kHttpTimeoutUsec);

    int connectionId = sceHttpCreateConnectionWithURL(templateId, url, false);
    if (connectionId < 0) {
        sceHttpDeleteTemplate(templateId);
        return -3;
    }

    int requestId = sceHttpCreateRequestWithURL(
        connectionId,
        ORBIS_METHOD_GET,
        url,
        0);
    if (requestId < 0) {
        sceHttpDeleteConnection(connectionId);
        sceHttpDeleteTemplate(templateId);
        return -4;
    }

    int result = -5;
    if (sceHttpSendRequest(requestId, nullptr, 0) >= 0) {
        int status = 0;
        if (sceHttpGetStatusCode(requestId, &status) >= 0 && status == 200) {
            body.reserve(64 * 1024);
            char chunk[8192];
            for (;;) {
                const int bytes = sceHttpReadData(requestId, chunk, sizeof(chunk));
                if (bytes < 0) {
                    result = -6;
                    break;
                }
                if (bytes == 0) {
                    result = static_cast<int>(body.size());
                    break;
                }
                if (body.size() + static_cast<size_t>(bytes) > maximumBytes) {
                    result = -7;
                    break;
                }
                body.append(chunk, static_cast<size_t>(bytes));
            }
        } else if (status > 0) {
            result = -status;
        }
    }

    sceHttpDeleteRequest(requestId);
    sceHttpDeleteConnection(connectionId);
    sceHttpDeleteTemplate(templateId);
    return result;
}

int fetchCatalog(std::vector<CatalogItem>& items) {
    std::string body;
    const int bytes = downloadUrl(kCatalogUrl, kMaximumCatalogBytes, body);
    if (bytes < 0) return bytes;
    return parseCatalogItems(body, items, 4)
        ? static_cast<int>(items.size()) : -8;
}

int fetchPosters(
    const std::vector<CatalogItem>& items,
    std::vector<PosterImage>& posters) {
    posters.clear();
    posters.resize(items.size());
    int loaded = 0;
    for (size_t index = 0; index < items.size(); ++index) {
        if (items[index].poster.compare(0, 8, "https://") != 0) continue;
        std::string posterUrl = items[index].poster;
        posterUrl += posterUrl.find('?') == std::string::npos
            ? "?format=jpg" : "&format=jpg";
        std::string encoded;
        if (downloadUrl(
                posterUrl.c_str(), kMaximumPosterBytes, encoded) >= 0 &&
            decodePosterJpeg(
                encoded, kPosterWidth, kPosterHeight, posters[index])) {
            ++loaded;
        }
    }
    return loaded;
}
}  // namespace

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    DEBUGLOG << "Stremio PS4 M0 starting";
    notify("Stremio PS4 1.24: native catalog titles");

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
                notify("Stremio PS4: use the PS button to go Home");
                sceKernelUsleep(500000);
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
    bool previewVisible = false;
    int previewBackgroundFrames = 0;
    uint64_t playbackWallStart = 0;
    bool playbackTimingReported = false;
    std::vector<uint32_t> previewPixels;
    uint32_t previewWidth = 0;
    uint32_t previewHeight = 0;
    std::vector<CatalogItem> catalogItems;
    std::vector<PosterImage> catalogPosters;
    scene.SetActiveFrameBuffer(0);

    for (;;) {
        const uint32_t buttons = readButtons(pad);
        const uint32_t pressed = buttons & ~previousButtons;
        previousButtons = buttons;

        if ((pressed & ORBIS_PAD_BUTTON_OPTIONS) != 0) {
            if (previewVisible ||
                avPlayer.state() != AvPlayerProbe::State::Idle) {
                DEBUGLOG << "Options pressed during playback; returning to shell";
                avPlayer.stop();
                previewVisible = false;
                notify("Stremio PS4: playback stopped; Options again exits");
            } else {
                DEBUGLOG << "Options pressed from shell; Home API disabled";
                notify("Stremio PS4: use the PS button to go Home");
            }
        }

        if ((pressed & ORBIS_PAD_BUTTON_LEFT) != 0 && focusedCard > 0) {
            --focusedCard;
        }
        if ((pressed & ORBIS_PAD_BUTTON_RIGHT) != 0 && focusedCard < 3) {
            ++focusedCard;
        }
        if ((pressed & ORBIS_PAD_BUTTON_CROSS) != 0 && previewVisible) {
            avPlayer.togglePause();
            notify(avPlayer.paused()
                ? "Stremio PS4: playback paused"
                : "Stremio PS4: playback resumed");
        } else if ((pressed & ORBIS_PAD_BUTTON_CROSS) != 0) {
            if (focusedCard < static_cast<int>(catalogItems.size())) {
                char selected[192];
                snprintf(selected, sizeof(selected),
                    "Stremio PS4: selected %s",
                    catalogItems[focusedCard].name.c_str());
                notify(selected);
            } else {
                notify("Stremio PS4: load Cinemeta first with Triangle");
            }
        }
        if ((pressed & ORBIS_PAD_BUTTON_TRIANGLE) != 0) {
            notify("Stremio PS4: loading Cinemeta top movies...");
            const int catalogResult = fetchCatalog(catalogItems);
            char result[192];
            if (catalogResult > 0) {
                const int posterCount = fetchPosters(catalogItems, catalogPosters);
                snprintf(
                    result,
                    sizeof(result),
                    "Stremio PS4: loaded %d cards, %d posters; first is %s",
                    catalogResult,
                    posterCount,
                    catalogItems[0].name.c_str());
            } else {
                snprintf(
                    result,
                    sizeof(result),
                    "Stremio PS4: catalog failed at stage %d",
                    -catalogResult);
            }
            notify(result);
        }
        if ((pressed & ORBIS_PAD_BUTTON_SQUARE) != 0) {
            previewVisible = false;
            previewBackgroundFrames = 0;
            playbackWallStart = 0;
            playbackTimingReported = false;
            notify("Stremio PS4: opening packaged Sintel H.264 trailer...");
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
        if ((pressed & ORBIS_PAD_BUTTON_CIRCLE) != 0 && previewVisible) {
            avPlayer.stop();
            avPlayerProbeFrames = 0;
            previewVisible = false;
            notify("Stremio PS4: playback stopped");
        } else if ((pressed & ORBIS_PAD_BUTTON_CIRCLE) != 0 &&
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
                previewVisible = true;
                previewBackgroundFrames = kFrameBuffers;
                playbackWallStart = sceKernelGetProcessTime();
            } else if (avPlayer.state() == AvPlayerProbe::State::Failed) {
                char result[128];
                snprintf(result, sizeof(result),
                    "Stremio PS4: AVPlayer stage %d, code 0x%08x",
                    avPlayer.errorStage(),
                    static_cast<unsigned int>(avPlayer.errorCode()));
                notify(result);
                avPlayer.stop();
            }
            previousPlayerState = avPlayer.state();
        }

        if (previewVisible) {
            avPlayer.copyPreview(previewPixels, previewWidth, previewHeight);
            drawDecodedPreview(
                scene, previewPixels, previewWidth, previewHeight,
                previewBackgroundFrames > 0);
            if (previewBackgroundFrames > 0) {
                --previewBackgroundFrames;
            }
            if (!playbackTimingReported && avPlayer.decodedFrames() >= 48) {
                const uint64_t wallMs =
                    (sceKernelGetProcessTime() - playbackWallStart) / 1000;
                const uint64_t fpsTimesTen = wallMs > 0
                    ? avPlayer.decodedFrames() * 10000 / wallMs
                    : 0;
                char timing[128];
                snprintf(timing, sizeof(timing),
                    "Stremio PS4: decode %llu.%llu fps, media %llums",
                    static_cast<unsigned long long>(fpsTimesTen / 10),
                    static_cast<unsigned long long>(fpsTimesTen % 10),
                    static_cast<unsigned long long>(avPlayer.currentTime()));
                notify(timing);
                playbackTimingReported = true;
            }
        } else {
            drawHardwareProbe(scene, focusedCard, catalogItems, catalogPosters);
        }
        scene.SubmitFlip(frameId);
        scene.FrameWait(frameId);
        scene.FrameBufferSwap();
        ++frameId;

    }
}
