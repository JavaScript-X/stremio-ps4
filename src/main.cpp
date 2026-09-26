#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cwchar>
#include <sstream>
#include <string>
#include <vector>
#include <sys/stat.h>

#include <orbis/libkernel.h>
#include <orbis/Http.h>
#include <orbis/CommonDialog.h>
#include <orbis/ImeDialog.h>
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
constexpr const char* kCatalogBaseUrl =
    "https://cinemeta-catalogs.strem.io/top/catalog/";
constexpr const char* kMetaBaseUrl = "https://v3-cinemeta.strem.io/meta/";
constexpr const char* kPublicDomainBaseUrl =
    "https://caching.stremio.net/publicdomainmovies.now.sh/";
constexpr const char* kLegalRemoteVideoUrl =
    "https://media.w3.org/2010/05/sintel/trailer.mp4";
constexpr const char* kLegalRemoteCachePath = "/data/stremio-remote-sintel.mp4";
constexpr size_t kMaximumDemoVideoBytes = 16 * 1024 * 1024;
constexpr size_t kLegalRemoteVideoBytes = 4372373;
constexpr size_t kMaximumCatalogBytes = 1024 * 1024;
constexpr size_t kMaximumPosterBytes = 2 * 1024 * 1024;
constexpr int kPosterWidth = 310;
constexpr int kPosterHeight = 410;
constexpr int kCatalogBatchSize = 8;
constexpr int kTopTabCount = 5;
const char* const kTopTabs[kTopTabCount] = {
    "MOVIES", "SERIES", "PUBLIC DOMAIN", "SEARCH", "SETTINGS"};

int networkPoolId = 0;
int sslContextId = 0;
int httpContextId = 0;
PosterImage headerLogo;

void drawBrand(Scene2D& scene, Color text) {
    if (headerLogo.valid()) {
        scene.BlitRgbScaledRounded(42, 27, 64, 64, 14,
            headerLogo.pixels.data(), headerLogo.width, headerLogo.height);
    }
    scene.DrawText(125, 42, "STREMIO", text, 3);
}

std::string shortTitle(const std::string& title) {
    constexpr size_t kMaximumCharacters = 18;
    if (title.size() <= kMaximumCharacters) return title;
    return title.substr(0, kMaximumCharacters - 3) + "...";
}

std::vector<std::string> wrapText(
    const std::string& text, size_t maximumCharacters, size_t maximumLines) {
    std::string normalized = text;
    for (char& character : normalized) {
        if (std::isspace(static_cast<unsigned char>(character))) character = ' ';
    }
    std::vector<std::string> lines;
    size_t position = 0;
    while (position < normalized.size() && lines.size() < maximumLines) {
        while (position < normalized.size() && normalized[position] == ' ') ++position;
        size_t end = std::min(normalized.size(), position + maximumCharacters);
        if (end < normalized.size()) {
            const size_t space = normalized.rfind(' ', end);
            if (space != std::string::npos && space > position) end = space;
        }
        if (end <= position) break;
        lines.push_back(normalized.substr(position, end - position));
        position = end;
    }
    if (position < normalized.size() && !lines.empty() && lines.back().size() > 3) {
        lines.back().replace(lines.back().size() - 3, 3, "...");
    }
    return lines;
}

void drawHardwareProbe(
    Scene2D& scene,
    int focusedCard,
    int page,
    const std::string& catalogType,
    const std::string& status,
    const std::vector<CatalogItem>& items,
    const std::vector<PosterImage>& posters,
    int activeTab,
    int indicatorX,
    int animationFrame,
    int catalogMotion) {
    const Color background = {18, 18, 24};
    const Color header = {29, 29, 39};
    const Color stremioPurple = {123, 91, 214};
    const Color card = {45, 45, 58};
    const Color cardMuted = {35, 35, 46};
    const Color focus = {196, 174, 255};
    const Color text = {235, 232, 244};
    const Color mutedText = {164, 158, 181};

    scene.FrameBufferFill(background);
    scene.DrawRectangle(0, 0, kWidth, 145, header);
    drawBrand(scene, text);
    const int tabX[kTopTabCount] = {350, 570, 790, 1130, 1380};
    for (int index = 0; index < kTopTabCount; ++index) {
        scene.DrawText(tabX[index], 52, kTopTabs[index],
            index == activeTab ? text : mutedText, 2);
    }
    scene.DrawRectangle(indicatorX, 105, activeTab == 2 ? 255 : 170, 8,
        stremioPurple);
    scene.DrawText(1690, 52, "R1/R2", mutedText, 2);

    scene.DrawRectangle(120, 170, 690, 48, stremioPurple);
    scene.DrawText(
        142, 182,
        activeTab == 3 ? "SEARCH RESULTS" :
        (catalogType == "series" ? "TOP SERIES" :
        (catalogType == "publicdomain" ? "PUBLIC DOMAIN" : "TOP MOVIES")),
        text, 3);
    const int cardX[] = {120, 565, 1010, 1455};
    const int cardY = 260 + catalogMotion;
    for (int index = 0; index < 4; ++index) {
        const int itemIndex = page * 4 + index;
        if (index == focusedCard) {
            const int pulse = (animationFrame / 8) % 2;
            scene.DrawRoundedRectangle(cardX[index] - 8 - pulse,
                cardY - 8 - pulse,
                326 + pulse * 2, 426 + pulse * 2, 22, focus);
        }
        scene.DrawRoundedRectangle(cardX[index], cardY, 310, 410, 18,
            itemIndex < static_cast<int>(items.size()) ? card : cardMuted);
        if (itemIndex < static_cast<int>(posters.size()) &&
            posters[itemIndex].valid()) {
            scene.BlitRgbRounded(cardX[index], cardY, posters[itemIndex].width,
                posters[itemIndex].height, 18, posters[itemIndex].pixels.data());
        }
        if (itemIndex < static_cast<int>(items.size())) {
            const std::string title = shortTitle(items[itemIndex].name);
            scene.DrawText(cardX[index], cardY + 430, title.c_str(), text, 2);
        }
    }

    const int nextPage = page + 1;
    const int previewY = 820 + static_cast<int>(8.0 *
        std::sin(static_cast<double>(animationFrame) * 0.06));
    for (int index = 0; index < 4; ++index) {
        const int itemIndex = nextPage * 4 + index;
        const int x = 170 + index * 430;
        scene.DrawRoundedRectangle(x, previewY, 220, 290, 16, cardMuted);
        if (itemIndex < static_cast<int>(posters.size()) &&
            posters[itemIndex].valid()) {
            scene.BlitRgbScaledRounded(x, previewY, 220, 290, 16,
                posters[itemIndex].pixels.data(), posters[itemIndex].width,
                posters[itemIndex].height, 72);
        }
    }

    scene.DrawText(120, 740, status.c_str(), mutedText, 2);
    scene.DrawText(
        120, 775,
        "R1/R2 TABS   LEFT/RIGHT SELECT   UP/DOWN CONTINUOUS SCROLL",
        mutedText, 2);
    scene.DrawText(
        1150, 775, "CROSS DETAILS   SQUARE LOCAL VIDEO",
        mutedText, 2);
    char pageText[64];
    snprintf(pageText, sizeof(pageText), "PAGE %d   %d LOADED", page + 1,
        static_cast<int>(items.size()));
    scene.DrawText(1540, 182, pageText, mutedText, 2);
}

void drawSearch(Scene2D& scene, const std::string& query, int,
    int indicatorX, int animationFrame) {
    const Color background = {18, 18, 24};
    const Color header = {29, 29, 39};
    const Color purple = {123, 91, 214};
    const Color text = {235, 232, 244};
    const Color muted = {164, 158, 181};
    scene.FrameBufferFill(background);
    scene.DrawRectangle(0, 0, kWidth, 145, header);
    drawBrand(scene, text);
    const int tabX[kTopTabCount] = {350, 570, 790, 1130, 1380};
    for (int index = 0; index < kTopTabCount; ++index) {
        scene.DrawText(tabX[index], 52, kTopTabs[index],
            index == 3 ? text : muted, 2);
    }
    scene.DrawRectangle(indicatorX, 105, 170, 8, purple);
    scene.DrawText(120, 185, "SEARCH CINEMETA", text, 3);
    scene.DrawRoundedRectangle(220, 300, 1480, 110, 24, header);
    const std::string shown = query.empty() ? "TYPE A TITLE..." : query + "_";
    scene.DrawText(270, 335, shown.c_str(), query.empty() ? muted : text, 3);
    const int pulse = (animationFrame / 10) % 2;
    scene.DrawRoundedRectangle(630 - pulse, 510 - pulse,
        660 + pulse * 2, 115 + pulse * 2, 28, purple);
    scene.DrawText(760, 548, "OPEN PS4 KEYBOARD", text, 3);
    scene.DrawText(510, 720,
        "PRESS CROSS TO TYPE WITH THE SYSTEM KEYBOARD", muted, 2);
    scene.DrawText(610, 790,
        "TRIANGLE SEARCH   CIRCLE CLEAR", muted, 2);
}

void drawSettings(Scene2D& scene, int selected, int indicatorX) {
    const Color background = {18, 18, 24};
    const Color header = {29, 29, 39};
    const Color purple = {123, 91, 214};
    const Color focus = {196, 174, 255};
    const Color text = {235, 232, 244};
    const Color muted = {164, 158, 181};
    scene.FrameBufferFill(background);
    scene.DrawRectangle(0, 0, kWidth, 145, header);
    drawBrand(scene, text);
    const int tabX[kTopTabCount] = {350, 570, 790, 1130, 1380};
    for (int index = 0; index < kTopTabCount; ++index) {
        scene.DrawText(tabX[index], 52, kTopTabs[index],
            index == 4 ? text : muted, 2);
    }
    scene.DrawRectangle(indicatorX, 105, 170, 8, purple);
    scene.DrawText(120, 190, "SETTINGS & DIAGNOSTICS", text, 3);
    const char* rows[] = {
        "LOCAL H.264 PLAYBACK TEST",
        "CACHED HTTPS PLAYBACK TEST",
        "CLEAR SEARCH QUERY",
        "ABOUT STREMIO PS4  v2.00"};
    for (int index = 0; index < 4; ++index) {
        const int y = 300 + index * 125;
        if (index == selected) scene.DrawRectangle(112, y - 8, 1696, 86, focus);
        scene.DrawRectangle(120, y, 1680, 70, header);
        scene.DrawText(155, y + 22, rows[index], text, 2);
    }
    scene.DrawText(120, 900,
        "UP/DOWN SELECT   CROSS RUN   R1/R2 CHANGE TAB", muted, 2);
}

void drawDetails(
    Scene2D& scene,
    const MetaDetails& details,
    const PosterImage* poster,
    const std::string& catalogType,
    int episodeIndex) {
    const Color background = {18, 18, 24};
    const Color panel = {29, 29, 39};
    const Color purple = {123, 91, 214};
    const Color text = {235, 232, 244};
    const Color muted = {164, 158, 181};
    scene.FrameBufferFill(background);
    scene.DrawRectangle(90, 70, 1740, 90, purple);
    scene.DrawText(125, 96, "DETAILS", text, 4);
    scene.DrawRectangle(90, 190, 390, 610, panel);
    if (poster && poster->valid()) {
        scene.BlitRgb(130, 230, poster->width, poster->height,
            poster->pixels.data());
    }
    const std::string title = details.name.size() > 42
        ? details.name.substr(0, 39) + "..." : details.name;
    scene.DrawText(540, 220, title.c_str(), text, 3);
    const std::string facts =
        (catalogType == "series" ? "SERIES   " : "MOVIE   ") +
        details.releaseInfo + "   " + details.runtime +
        (details.imdbRating.empty() ? "" : "   IMDB " + details.imdbRating);
    scene.DrawText(540, 285, facts.c_str(), muted, 2);
    if (!details.genres.empty()) {
        scene.DrawText(540, 325, details.genres.c_str(), muted, 2);
    }
    const std::vector<std::string> lines =
        wrapText(details.description, 68, 13);
    for (size_t line = 0; line < lines.size(); ++line) {
        scene.DrawText(540, 375 + static_cast<int>(line) * 35,
            lines[line].c_str(), text, 2);
    }
    if (!details.episodes.empty() && episodeIndex >= 0 &&
        episodeIndex < static_cast<int>(details.episodes.size())) {
        const MetaDetails::Episode& episode = details.episodes[episodeIndex];
        char episodeText[192];
        snprintf(episodeText, sizeof(episodeText),
            "EPISODE S%d E%d   %d/%d%s%s",
            episode.season, episode.episode, episodeIndex + 1,
            static_cast<int>(details.episodes.size()),
            episode.title.empty() ? "" : "   ", episode.title.c_str());
        scene.DrawRectangle(520, 845, 1190, 60, panel);
        scene.DrawText(545, 865, episodeText, text, 2);
    }
    scene.DrawText(
        130, 945,
        catalogType == "series"
            ? "LEFT/RIGHT EPISODE   CROSS SELECT   CIRCLE BACK"
            : "CIRCLE BACK   R1/R2 TOP TABS",
        muted, 2);
}

void drawStreams(
    Scene2D& scene,
    const MetaDetails& details,
    const std::vector<StreamItem>& streams,
    int focusedStream) {
    const Color background = {18, 18, 24};
    const Color panel = {35, 35, 46};
    const Color purple = {123, 91, 214};
    const Color focus = {196, 174, 255};
    const Color text = {235, 232, 244};
    const Color muted = {164, 158, 181};
    scene.FrameBufferFill(background);
    scene.DrawRectangle(90, 70, 1740, 90, purple);
    scene.DrawText(125, 96, "STREAMS", text, 4);
    scene.DrawText(125, 190, details.name.c_str(), text, 3);
    for (int index = 0; index < static_cast<int>(streams.size()) && index < 6;
         ++index) {
        const int y = 280 + index * 100;
        if (index == focusedStream) {
            scene.DrawRectangle(115, y - 8, 1690, 76, focus);
        }
        scene.DrawRectangle(125, y, 1670, 60, panel);
        const StreamItem& stream = streams[index];
        const std::string label = !stream.name.empty() ? stream.name :
            (!stream.title.empty() ? stream.title : "STREAM");
        scene.DrawText(155, y + 18, label.c_str(), text, 2);
        scene.DrawText(900, y + 18,
            stream.url.empty() ? "TORRENT - COMPANION REQUIRED" : "DIRECT HTTPS",
            stream.url.empty() ? muted : text, 2);
    }
    scene.DrawText(125, 930,
        "UP/DOWN SELECT   CROSS PLAY/INSPECT   CIRCLE DETAILS",
        muted, 2);
}

void drawDecodedPreview(
    Scene2D& scene,
    const std::vector<uint32_t>& pixels,
    uint32_t previewWidth,
    uint32_t previewHeight,
    bool paintBackground,
    uint64_t currentTime,
    uint64_t duration,
    bool paused,
    uint32_t decoderFpsTimesTen) {
    const Color background = {8, 8, 12};
    const Color border = {196, 174, 255};
    const Color track = {45, 45, 58};
    const Color purple = {123, 91, 214};
    const Color text = {235, 232, 244};
    const int width = static_cast<int>(previewWidth);
    const int height = static_cast<int>(previewHeight);
    const int startX = (kWidth - width) / 2;
    const int startY = (kHeight - height) / 2;
    if (paintBackground) {
        scene.FrameBufferFill(background);
        scene.DrawRectangle(startX - 8, startY - 8, width + 16, height + 16, border);
    }

    scene.BlitRgb(startX, startY, width, height, pixels.data());
    const int timelineX = 480;
    const int timelineWidth = 960;
    scene.DrawRectangle(420, 835, 1080, 125, background);
    scene.DrawRectangle(timelineX, 870, timelineWidth, 14, track);
    if (duration > 0) {
        const int progress = static_cast<int>(
            std::min<uint64_t>(currentTime, duration) * timelineWidth / duration);
        scene.DrawRectangle(timelineX, 870, progress, 14, purple);
    }
    char clock[96];
    snprintf(clock, sizeof(clock),
        "%s   %02llu:%02llu / %02llu:%02llu   DECODE %u.%u FPS",
        paused ? "PAUSED" : "PLAYING",
        static_cast<unsigned long long>(currentTime / 60000),
        static_cast<unsigned long long>((currentTime / 1000) % 60),
        static_cast<unsigned long long>(duration / 60000),
        static_cast<unsigned long long>((duration / 1000) % 60),
        decoderFpsTimesTen / 10, decoderFpsTimesTen % 10);
    scene.DrawText(480, 910, clock, text, 2);
}

void notify(const char* message) {
    sceSysUtilSendSystemNotificationWithText(222, message);
}

int initializeController(int& userId) {
    OrbisUserServiceInitializeParams userParams = {};
    userParams.priority = ORBIS_KERNEL_PRIO_FIFO_LOWEST;
    userId = -1;

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
    if (scePadReadState(pad, &padData) != 0) return 0;
    uint32_t buttons = padData.buttons;
    constexpr uint8_t kStickLow = 58;
    constexpr uint8_t kStickHigh = 198;
    constexpr uint8_t kTriggerPressed = 96;
    if (padData.leftStick.x < kStickLow) buttons |= ORBIS_PAD_BUTTON_LEFT;
    if (padData.leftStick.x > kStickHigh) buttons |= ORBIS_PAD_BUTTON_RIGHT;
    if (padData.leftStick.y < kStickLow) buttons |= ORBIS_PAD_BUTTON_UP;
    if (padData.leftStick.y > kStickHigh) buttons |= ORBIS_PAD_BUTTON_DOWN;
    if (padData.analogButtons.r2 > kTriggerPressed)
        buttons |= ORBIS_PAD_BUTTON_R2;
    if (padData.analogButtons.l2 > kTriggerPressed)
        buttons |= ORBIS_PAD_BUTTON_L2;
    return buttons;
}

bool openSystemSearchKeyboard(int userId, std::string& query) {
    static bool initialized = false;
    if (!initialized) {
        if (sceSysmoduleLoadModuleInternal(
                ORBIS_SYSMODULE_INTERNAL_COMMON_DIALOG) < 0 ||
            sceSysmoduleLoadModule(ORBIS_SYSMODULE_IME_DIALOG) < 0) {
            return false;
        }
        sceCommonDialogInitialize();
        initialized = true;
    }

    // The PS4 IME ABI uses UTF-16 even though this target's wchar_t is 32-bit.
    uint16_t buffer[65] = {};
    size_t outputIndex = 0;
    for (size_t index = 0; index < query.size() && outputIndex < 64;) {
        const unsigned char first = query[index];
        uint32_t codepoint = '?';
        if (first < 0x80) {
            codepoint = first;
            ++index;
        } else if ((first >> 5) == 0x6 && index + 1 < query.size()) {
            codepoint = ((first & 0x1f) << 6) |
                (static_cast<unsigned char>(query[index + 1]) & 0x3f);
            index += 2;
        } else if ((first >> 4) == 0xe && index + 2 < query.size()) {
            codepoint = ((first & 0x0f) << 12) |
                ((static_cast<unsigned char>(query[index + 1]) & 0x3f) << 6) |
                (static_cast<unsigned char>(query[index + 2]) & 0x3f);
            index += 3;
        } else {
            ++index;
        }
        buffer[outputIndex++] = static_cast<uint16_t>(
            codepoint <= 0xffff ? codepoint : '?');
    }
    static const uint16_t title[] = {
        'S','e','a','r','c','h',' ','S','t','r','e','m','i','o',0};
    static const uint16_t placeholder[] = {
        'M','o','v','i','e',' ','o','r',' ','s','e','r','i','e','s',' ',
        't','i','t','l','e',0};
    OrbisImeDialogSetting settings = {};
    settings.userId = static_cast<uint32_t>(userId);
    settings.type = ORBIS_TYPE_DEFAULT;
    settings.enterLabel = ORBIS_BUTTON_LABEL_SEARCH;
    settings.inputMethod = ORBIS__DEFAULT;
    settings.maxTextLength = 64;
    settings.inputTextBuffer = reinterpret_cast<wchar_t*>(buffer);
    settings.horizontalAlignment = ORBIS_H_CENTER;
    settings.verticalAlignment = ORBIS_V_CENTER;
    settings.placeholder = reinterpret_cast<const wchar_t*>(placeholder);
    settings.title = reinterpret_cast<const wchar_t*>(title);
    if (sceImeDialogInit(&settings, nullptr) < 0) return false;

    OrbisDialogStatus status = sceImeDialogGetStatus();
    while (status == ORBIS_DIALOG_STATUS_RUNNING) {
        sceKernelUsleep(16000);
        status = sceImeDialogGetStatus();
    }
    OrbisDialogResult result = {};
    const bool accepted = status == ORBIS_DIALOG_STATUS_STOPPED &&
        sceImeDialogGetResult(&result) >= 0 &&
        result.endstatus == ORBIS_DIALOG_OK;
    sceImeDialogTerm();
    if (!accepted) return false;
    query.clear();
    for (size_t index = 0; index < 64 && buffer[index] != 0; ++index) {
        const uint32_t character = buffer[index];
        if (character < 0x80) {
            query.push_back(static_cast<char>(character));
        } else if (character < 0x800) {
            query.push_back(static_cast<char>(0xc0 | (character >> 6)));
            query.push_back(static_cast<char>(0x80 | (character & 0x3f)));
        } else {
            query.push_back(static_cast<char>(0xe0 | (character >> 12)));
            query.push_back(static_cast<char>(0x80 | ((character >> 6) & 0x3f)));
            query.push_back(static_cast<char>(0x80 | (character & 0x3f)));
        }
    }
    return true;
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

int cacheLegalRemoteTrailer(bool& reused) {
    reused = false;
    struct stat fileInfo = {};
    if (stat(kLegalRemoteCachePath, &fileInfo) == 0 &&
        static_cast<size_t>(fileInfo.st_size) == kLegalRemoteVideoBytes) {
        reused = true;
        return static_cast<int>(fileInfo.st_size);
    }

    std::string video;
    const int bytes = downloadUrl(
        kLegalRemoteVideoUrl, kMaximumDemoVideoBytes, video);
    if (bytes < 0 || static_cast<size_t>(bytes) != kLegalRemoteVideoBytes) {
        return bytes < 0 ? bytes : -11;
    }
    FILE* output = fopen(kLegalRemoteCachePath, "wb");
    if (!output) return -12;
    const size_t written = fwrite(video.data(), 1, video.size(), output);
    const int closeResult = fclose(output);
    if (written != video.size() || closeResult != 0) {
        remove(kLegalRemoteCachePath);
        return -13;
    }
    return bytes;
}

bool loadBundledImage(
    const char* path, int width, int height, PosterImage& image) {
    FILE* file = fopen(path, "rb");
    if (!file) return false;
    fseek(file, 0, SEEK_END);
    const long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (size <= 0 || size > 4 * 1024 * 1024) {
        fclose(file);
        return false;
    }
    std::string encoded(static_cast<size_t>(size), '\0');
    const bool read = fread(&encoded[0], 1, encoded.size(), file) == encoded.size();
    fclose(file);
    return read && decodePosterJpeg(encoded, width, height, image);
}

std::string urlEncode(const std::string& value) {
    std::string encoded;
    const char* hex = "0123456789ABCDEF";
    for (unsigned char character : value) {
        if (std::isalnum(character) || character == '-' || character == '_' ||
            character == '.' || character == '~') {
            encoded.push_back(static_cast<char>(character));
        } else {
            encoded.push_back('%');
            encoded.push_back(hex[character >> 4]);
            encoded.push_back(hex[character & 15]);
        }
    }
    return encoded;
}

int fetchCatalogPage(
    const std::string& catalogType,
    int skip,
    const std::string& searchQuery,
    std::vector<CatalogItem>& items) {
    if (catalogType != "movie" && catalogType != "series" &&
        catalogType != "publicdomain") return -10;
    std::string url;
    if (!searchQuery.empty()) {
        url = std::string("https://v3-cinemeta.strem.io/catalog/") +
            catalogType + "/top/search=" + urlEncode(searchQuery) + ".json";
    } else if (catalogType == "publicdomain") {
        url = std::string(kPublicDomainBaseUrl) +
            "catalog/movie/publicdomainmovies";
    } else {
        url = std::string(kCatalogBaseUrl) + catalogType + "/top";
    }
    if (searchQuery.empty() && skip > 0) {
        char pagination[48];
        snprintf(pagination, sizeof(pagination), "/skip=%d", skip);
        url += pagination;
    }
    if (searchQuery.empty()) url += ".json";
    std::string body;
    const int bytes =
        downloadUrl(url.c_str(), kMaximumCatalogBytes, body);
    if (bytes < 0) return bytes;
    return parseCatalogItems(body, items, kCatalogBatchSize)
        ? static_cast<int>(items.size()) : -8;
}

bool isSafeMetadataId(const std::string& id) {
    if (id.empty() || id.size() > 96) return false;
    for (char character : id) {
        const unsigned char value = static_cast<unsigned char>(character);
        if (!std::isalnum(value) && character != '-' && character != '_' &&
            character != ':' && character != '.') return false;
    }
    return true;
}

int fetchDetails(
    const std::string& catalogType,
    const CatalogItem& item,
    MetaDetails& details) {
    if ((catalogType != "movie" && catalogType != "series" &&
         catalogType != "publicdomain") ||
        !isSafeMetadataId(item.id)) return -10;
    const std::string resourceType =
        catalogType == "series" ? "series" : "movie";
    const std::string url = std::string(kMetaBaseUrl) + resourceType + "/" +
        item.id + ".json";
    std::string body;
    const int bytes = downloadUrl(url.c_str(), kMaximumCatalogBytes, body);
    if (bytes < 0) return bytes;
    return parseMetaDetails(body, details) ? 1 : -8;
}

int fetchPublicDomainStreams(
    const std::string& videoId,
    std::vector<StreamItem>& streams) {
    if (!isSafeMetadataId(videoId)) return -10;
    const std::string url = std::string(kPublicDomainBaseUrl) +
        "stream/movie/" + videoId + ".json";
    std::string body;
    const int bytes = downloadUrl(url.c_str(), kMaximumCatalogBytes, body);
    if (bytes < 0) return bytes;
    return parseStreamItems(body, streams, 8)
        ? static_cast<int>(streams.size()) : -8;
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

int appendCatalogPage(
    const std::string& catalogType,
    std::vector<CatalogItem>& items,
    std::vector<PosterImage>& posters) {
    std::vector<CatalogItem> nextItems;
    const int result = fetchCatalogPage(
        catalogType, static_cast<int>(items.size()), "", nextItems);
    if (result <= 0) return result;
    std::vector<PosterImage> nextPosters;
    fetchPosters(nextItems, nextPosters);
    items.insert(items.end(), nextItems.begin(), nextItems.end());
    posters.insert(posters.end(), nextPosters.begin(), nextPosters.end());
    return result;
}
}  // namespace

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    DEBUGLOG << "Stremio native client starting";
    notify("Stremio 2.00: system keyboard and polished catalog UI");

    int userId = -1;
    const int pad = initializeController(userId);
    notify(pad >= 0
        ? "Stremio: controller ready"
        : "Stremio: controller initialization failed");

    Scene2D scene(kWidth, kHeight, kPixelDepth);
    if (!scene.Init(kVideoMemory, kFrameBuffers)) {
        DEBUGLOG << "Video initialization failed";
        notify("Stremio: VIDEO INITIALIZATION FAILED");
        for (;;) {
            if ((readButtons(pad) & ORBIS_PAD_BUTTON_OPTIONS) != 0) {
                notify("Stremio: use the PS button to go Home");
                sceKernelUsleep(500000);
            }
            sceKernelUsleep(16000);
        }
    }

    DEBUGLOG << "Video initialized at 1920x1080; rendering hardware probe";
    notify("Stremio: video ready");

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
    std::string catalogType = "movie";
    std::string catalogStatus = "LOADING CINEMETA...";
    int catalogPage = 0;
    int activeTab = 0;
    int indicatorX = 350;
    int animationFrame = 0;
    int catalogMotion = 0;
    int searchKey = 0;
    int settingsSelection = 0;
    bool searchShowingResults = false;
    std::string searchQuery;
    bool detailVisible = false;
    bool streamVisible = false;
    int detailPosterIndex = -1;
    int detailEpisodeIndex = 0;
    int focusedStream = 0;
    MetaDetails details;
    std::vector<StreamItem> streams;
    scene.SetActiveFrameBuffer(0);
    loadBundledImage(
        "/app0/assets/stremio-official.png", 96, 96, headerLogo);

    auto loadActiveCatalog = [&]() {
        char loading[96];
        snprintf(loading, sizeof(loading), "Stremio: loading top %s...",
            catalogType == "series" ? "series" : "movies");
        notify(loading);
        catalogStatus = "LOADING CINEMETA...";
        catalogPosters.clear();
        const int catalogResult = fetchCatalogPage(
            catalogType, 0, "", catalogItems);
        char result[192];
        if (catalogResult > 0) {
            int posterCount = fetchPosters(catalogItems, catalogPosters);
            const size_t beforePreload = catalogPosters.size();
            const int preloaded = appendCatalogPage(
                catalogType, catalogItems, catalogPosters);
            if (preloaded > 0) {
                for (size_t index = beforePreload;
                     index < catalogPosters.size(); ++index) {
                    if (catalogPosters[index].valid()) ++posterCount;
                }
            }
            snprintf(result, sizeof(result),
                "Stremio: loaded %d cards and %d posters",
                static_cast<int>(catalogItems.size()), posterCount);
            char visibleStatus[128];
            snprintf(visibleStatus, sizeof(visibleStatus),
                "%d ITEMS   %d POSTERS   PRELOADED",
                static_cast<int>(catalogItems.size()), posterCount);
            catalogStatus = visibleStatus;
        } else {
            catalogItems.clear();
            catalogPosters.clear();
            snprintf(result, sizeof(result),
                "Stremio: catalog failed at stage %d", -catalogResult);
            catalogStatus = "CATALOG LOAD FAILED - PRESS TRIANGLE TO RETRY";
        }
        focusedCard = 0;
        catalogPage = 0;
        detailVisible = false;
        streamVisible = false;
        detailEpisodeIndex = 0;
        notify(result);
    };

    auto loadSearchResults = [&]() {
        if (searchQuery.empty()) {
            notify("Stremio: enter a search title first");
            return;
        }
        notify("Stremio: searching Cinemeta movies...");
        catalogItems.clear();
        catalogPosters.clear();
        const int result = fetchCatalogPage(
            "movie", 0, searchQuery, catalogItems);
        if (result > 0) {
            const int posterCount = fetchPosters(catalogItems, catalogPosters);
            char status[128];
            snprintf(status, sizeof(status),
                "SEARCH: %s   %d RESULTS   %d POSTERS",
                searchQuery.c_str(), result, posterCount);
            catalogStatus = status;
            catalogPage = 0;
            focusedCard = 0;
            catalogType = "movie";
            searchShowingResults = true;
            notify("Stremio: search results ready");
        } else {
            catalogStatus = "NO SEARCH RESULTS - CIRCLE TO EDIT";
            searchShowingResults = true;
            notify("Stremio: no search results");
        }
    };

    auto selectTab = [&](int tab) {
        activeTab = (tab + kTopTabCount) % kTopTabCount;
        detailVisible = false;
        streamVisible = false;
        if (activeTab == 0) {
            catalogType = "movie";
            loadActiveCatalog();
        } else if (activeTab == 1) {
            catalogType = "series";
            loadActiveCatalog();
        } else if (activeTab == 2) {
            catalogType = "publicdomain";
            loadActiveCatalog();
        }
    };

    // Present both framebuffers before the synchronous first load so the user
    // sees a responsive loading shell instead of a black screen.
    for (int initialFrame = 0; initialFrame < kFrameBuffers; ++initialFrame) {
        drawHardwareProbe(
            scene, focusedCard, catalogPage, catalogType, catalogStatus,
            catalogItems, catalogPosters, activeTab, indicatorX,
            animationFrame, catalogMotion);
        scene.SubmitFlip(frameId);
        scene.FrameWait(frameId);
        scene.FrameBufferSwap();
        ++frameId;
    }
    loadActiveCatalog();

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
                notify("Stremio: playback stopped; Options again exits");
            } else {
                DEBUGLOG << "Options pressed from shell; Home API disabled";
                notify("Stremio: use the PS button to go Home");
            }
        }

        const bool shellVisible =
            !previewVisible && !detailVisible && !streamVisible;
        const bool catalogScreen = activeTab < 3 ||
            (activeTab == 3 && searchShowingResults);
        if (shellVisible && (pressed & ORBIS_PAD_BUTTON_R1) != 0) {
            selectTab(activeTab + 1);
        } else if (shellVisible && (pressed & ORBIS_PAD_BUTTON_R2) != 0) {
            selectTab(activeTab - 1);
        }

        const int tabTargets[kTopTabCount] = {350, 570, 790, 1130, 1380};
        const int indicatorDelta = tabTargets[activeTab] - indicatorX;
        if (indicatorDelta != 0) {
            if (indicatorDelta >= -4 && indicatorDelta <= 4) {
                indicatorX = tabTargets[activeTab];
            } else {
                indicatorX += indicatorDelta / 4;
            }
        }

        if (shellVisible && activeTab == 3 && !searchShowingResults) {
            if ((pressed & ORBIS_PAD_BUTTON_CROSS) != 0) {
                if (openSystemSearchKeyboard(userId, searchQuery)) {
                    loadSearchResults();
                } else {
                    notify("Stremio: system keyboard cancelled or unavailable");
                }
                previousButtons = readButtons(pad);
            }
            if ((pressed & ORBIS_PAD_BUTTON_CIRCLE) != 0) searchQuery.clear();
            if ((pressed & ORBIS_PAD_BUTTON_TRIANGLE) != 0)
                loadSearchResults();
        } else if (shellVisible && catalogScreen) {
            if ((pressed & ORBIS_PAD_BUTTON_LEFT) != 0 && focusedCard > 0)
                --focusedCard;
            if ((pressed & ORBIS_PAD_BUTTON_RIGHT) != 0 && focusedCard < 3 &&
                catalogPage * 4 + focusedCard + 1 <
                    static_cast<int>(catalogItems.size())) ++focusedCard;
            if ((pressed & ORBIS_PAD_BUTTON_DOWN) != 0) {
                const int nextPage = catalogPage + 1;
                if ((nextPage + 1) * 4 >=
                        static_cast<int>(catalogItems.size()) &&
                    activeTab < 3) {
                    catalogStatus = "LOADING MORE ITEMS...";
                    const int added = appendCatalogPage(
                        catalogType, catalogItems, catalogPosters);
                    char status[128];
                    snprintf(status, sizeof(status),
                        added > 0 ? "%d ITEMS LOADED   CONTINUOUS SCROLL" :
                            "END REACHED   %d ITEMS LOADED",
                        static_cast<int>(catalogItems.size()));
                    catalogStatus = status;
                }
                if (nextPage * 4 < static_cast<int>(catalogItems.size())) {
                    catalogPage = nextPage;
                    catalogMotion = 90;
                    if (catalogPage * 4 + focusedCard >=
                        static_cast<int>(catalogItems.size())) focusedCard = 0;
                }
            }
            if ((pressed & ORBIS_PAD_BUTTON_UP) != 0 && catalogPage > 0) {
                --catalogPage;
                catalogMotion = -90;
            }
            if (activeTab == 3 &&
                (pressed & ORBIS_PAD_BUTTON_CIRCLE) != 0) {
                searchShowingResults = false;
                catalogItems.clear();
                catalogPosters.clear();
            }
        } else if (shellVisible && activeTab == 4) {
            if ((pressed & ORBIS_PAD_BUTTON_UP) != 0 && settingsSelection > 0)
                --settingsSelection;
            if ((pressed & ORBIS_PAD_BUTTON_DOWN) != 0 && settingsSelection < 3)
                ++settingsSelection;
            if ((pressed & ORBIS_PAD_BUTTON_CROSS) != 0 &&
                settingsSelection == 2) {
                searchQuery.clear();
                searchShowingResults = false;
                notify("Stremio: search query cleared");
            } else if ((pressed & ORBIS_PAD_BUTTON_CROSS) != 0 &&
                settingsSelection == 3) {
                notify("Stremio PS4 v2.00 - JavaScript-X community build");
            }
        }
        if (!previewVisible && detailVisible && catalogType == "series" &&
            (pressed & ORBIS_PAD_BUTTON_LEFT) != 0 && detailEpisodeIndex > 0) {
            --detailEpisodeIndex;
        }
        if (!previewVisible && detailVisible && catalogType == "series" &&
            (pressed & ORBIS_PAD_BUTTON_RIGHT) != 0 &&
            detailEpisodeIndex + 1 < static_cast<int>(details.episodes.size())) {
            ++detailEpisodeIndex;
        }
        if (!previewVisible && streamVisible &&
            (pressed & ORBIS_PAD_BUTTON_UP) != 0 && focusedStream > 0) {
            --focusedStream;
        }
        if (!previewVisible && streamVisible &&
            (pressed & ORBIS_PAD_BUTTON_DOWN) != 0 &&
            focusedStream + 1 < static_cast<int>(streams.size())) {
            ++focusedStream;
        }
        if ((pressed & ORBIS_PAD_BUTTON_CROSS) != 0 && previewVisible) {
            avPlayer.togglePause();
            notify(avPlayer.paused()
                ? "Stremio: playback paused"
                : "Stremio: playback resumed");
        } else if ((pressed & ORBIS_PAD_BUTTON_CROSS) != 0 &&
            !detailVisible && !streamVisible && catalogScreen) {
            const int selectedIndex = catalogPage * 4 + focusedCard;
            if (selectedIndex < static_cast<int>(catalogItems.size())) {
                notify("Stremio: loading metadata details...");
                const int detailResult = fetchDetails(
                    catalogType, catalogItems[selectedIndex], details);
                if (detailResult > 0) {
                    detailPosterIndex = selectedIndex;
                    detailEpisodeIndex = 0;
                    detailVisible = true;
                    notify("Stremio: metadata details ready");
                } else {
                    char failure[96];
                    snprintf(failure, sizeof(failure),
                        "Stremio: metadata failed at stage %d",
                        -detailResult);
                    notify(failure);
                }
            } else {
                notify("Stremio: no item in this slot");
            }
        } else if ((pressed & ORBIS_PAD_BUTTON_CROSS) != 0 && detailVisible &&
            catalogType == "series" && !details.episodes.empty()) {
            const MetaDetails::Episode& episode =
                details.episodes[detailEpisodeIndex];
            char selected[128];
            snprintf(selected, sizeof(selected),
                "Stremio: selected season %d episode %d",
                episode.season, episode.episode);
            notify(selected);
        } else if ((pressed & ORBIS_PAD_BUTTON_CROSS) != 0 && detailVisible &&
            !streamVisible && catalogType == "publicdomain") {
            notify("Stremio: resolving public-domain streams...");
            const int streamResult =
                fetchPublicDomainStreams(details.id, streams);
            if (streamResult > 0) {
                focusedStream = 0;
                streamVisible = true;
                notify("Stremio: stream results ready");
            } else {
                char failure[96];
                snprintf(failure, sizeof(failure),
                    "Stremio: stream lookup failed at stage %d", -streamResult);
                notify(failure);
            }
        } else if ((pressed & ORBIS_PAD_BUTTON_CROSS) != 0 && streamVisible &&
            focusedStream < static_cast<int>(streams.size())) {
            const StreamItem& stream = streams[focusedStream];
            if (stream.url.empty()) {
                notify("Stremio: torrent stream needs the companion service");
            } else if (stream.url.compare(0, 8, "https://") == 0) {
                notify("Stremio: HTTPS streams need the cache/companion bridge");
            } else {
                notify("Stremio: rejected non-HTTPS direct stream");
            }
        }
        if (!previewVisible && activeTab < 3 &&
            (pressed & ORBIS_PAD_BUTTON_TRIANGLE) != 0) {
            loadActiveCatalog();
        }
        const bool localPlaybackRequested =
            ((pressed & ORBIS_PAD_BUTTON_SQUARE) != 0 && catalogScreen) ||
            (shellVisible && activeTab == 4 && settingsSelection == 0 &&
             (pressed & ORBIS_PAD_BUTTON_CROSS) != 0);
        if (localPlaybackRequested) {
            previewVisible = false;
            previewBackgroundFrames = 0;
            playbackWallStart = 0;
            playbackTimingReported = false;
            notify("Stremio: opening packaged Sintel H.264 trailer...");
            avPlayerProbeFrames = 0;
            if (!avPlayer.start(kLegalVideoPath)) {
                char result[128];
                snprintf(result, sizeof(result),
                    "Stremio: AVPlayer stage %d, code 0x%08x",
                    avPlayer.errorStage(),
                    static_cast<unsigned int>(avPlayer.errorCode()));
                notify(result);
            }
        }
        const bool remotePlaybackRequested = shellVisible && activeTab == 4 &&
            settingsSelection == 1 &&
            (pressed & ORBIS_PAD_BUTTON_CROSS) != 0;
        if (remotePlaybackRequested) {
            previewVisible = false;
            previewBackgroundFrames = 0;
            playbackWallStart = 0;
            playbackTimingReported = false;
            avPlayerProbeFrames = 0;
            notify("Stremio: checking verified remote trailer cache...");
            bool reused = false;
            const int cacheResult = cacheLegalRemoteTrailer(reused);
            if (cacheResult < 0) {
                char result[128];
                snprintf(result, sizeof(result),
                    "Stremio: remote cache failed at stage %d",
                    -cacheResult);
                notify(result);
            } else {
                notify(reused
                    ? "Stremio: using cached HTTPS trailer"
                    : "Stremio: HTTPS trailer downloaded and cached");
            }
            if (cacheResult >= 0 && !avPlayer.start(kLegalRemoteCachePath)) {
                char result[128];
                snprintf(result, sizeof(result),
                    "Stremio: cached AVPlayer stage %d, code 0x%08x",
                    avPlayer.errorStage(),
                    static_cast<unsigned int>(avPlayer.errorCode()));
                notify(result);
            }
        }
        if ((pressed & ORBIS_PAD_BUTTON_CIRCLE) != 0 && previewVisible) {
            avPlayer.stop();
            avPlayerProbeFrames = 0;
            previewVisible = false;
            notify("Stremio: playback stopped");
        } else if ((pressed & ORBIS_PAD_BUTTON_CIRCLE) != 0 &&
            avPlayer.state() != AvPlayerProbe::State::Idle) {
            avPlayer.stop();
            avPlayerProbeFrames = 0;
            notify("Stremio: AVPlayer probe cancelled");
        } else if ((pressed & ORBIS_PAD_BUTTON_CIRCLE) != 0 && streamVisible) {
            streamVisible = false;
            notify("Stremio: returned to details");
        } else if ((pressed & ORBIS_PAD_BUTTON_CIRCLE) != 0 && detailVisible) {
            detailVisible = false;
            notify("Stremio: returned to catalog");
        }

        avPlayer.update();
        if (avPlayer.state() == AvPlayerProbe::State::Opening ||
            avPlayer.state() == AvPlayerProbe::State::Decoding) {
            ++avPlayerProbeFrames;
            if (avPlayerProbeFrames > 1200) {
                avPlayer.stop();
                notify("Stremio: AVPlayer timed out before first frame");
            }
        }
        if (avPlayer.state() != previousPlayerState) {
            if (avPlayer.state() == AvPlayerProbe::State::Decoding) {
                notify("Stremio: AVPlayer active; waiting for frame");
            } else if (avPlayer.state() == AvPlayerProbe::State::Passed) {
                char result[96];
                snprintf(result, sizeof(result),
                    "Stremio: decoded frame %ux%u",
                    avPlayer.width(), avPlayer.height());
                notify(result);
                previewVisible = true;
                previewBackgroundFrames = kFrameBuffers;
                playbackWallStart = sceKernelGetProcessTime();
            } else if (avPlayer.state() == AvPlayerProbe::State::Failed) {
                char result[128];
                snprintf(result, sizeof(result),
                    "Stremio: AVPlayer stage %d, code 0x%08x",
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
                previewBackgroundFrames > 0, avPlayer.currentTime(),
                avPlayer.duration(), avPlayer.paused(),
                avPlayer.measuredFpsTimesTen());
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
                    "Stremio: decode %llu.%llu fps, media %llums",
                    static_cast<unsigned long long>(fpsTimesTen / 10),
                    static_cast<unsigned long long>(fpsTimesTen % 10),
                    static_cast<unsigned long long>(avPlayer.currentTime()));
                notify(timing);
                playbackTimingReported = true;
            }
        } else if (streamVisible) {
            drawStreams(scene, details, streams, focusedStream);
        } else if (detailVisible) {
            const PosterImage* poster =
                detailPosterIndex >= 0 &&
                detailPosterIndex < static_cast<int>(catalogPosters.size())
                ? &catalogPosters[detailPosterIndex] : nullptr;
            drawDetails(
                scene, details, poster, catalogType, detailEpisodeIndex);
        } else if (activeTab == 3 && !searchShowingResults) {
            drawSearch(scene, searchQuery, searchKey, indicatorX,
                animationFrame);
        } else if (activeTab == 4) {
            drawSettings(scene, settingsSelection, indicatorX);
        } else {
            drawHardwareProbe(
                scene, focusedCard, catalogPage, catalogType, catalogStatus,
                catalogItems, catalogPosters, activeTab, indicatorX,
                animationFrame, catalogMotion);
        }
        scene.SubmitFlip(frameId);
        scene.FrameWait(frameId);
        scene.FrameBufferSwap();
        ++frameId;
        ++animationFrame;
        if (catalogMotion != 0) {
            catalogMotion = catalogMotion * 3 / 4;
            if (catalogMotion > -2 && catalogMotion < 2) catalogMotion = 0;
        }

    }
}
