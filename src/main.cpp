#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <csignal>
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
const char* const kVideoTestPaths[] = {
    "/app0/assets/sintel-360p.mp4",
    "/app0/assets/sintel-720p.mp4",
    "/app0/assets/sintel-1080p.mp4",
    kLegalVideoPath};
const char* const kVideoTestNames[] = {"360P", "720P", "1080P", "854X480"};
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
volatile sig_atomic_t exitRequested = 0;
bool imeDialogInitialized = false;

void requestExit(int) {
    exitRequested = 1;
}

void drawBrand(Scene2D& scene, Color text) {
    if (headerLogo.valid()) {
        scene.BlitRgbScaledRounded(42, 27, 64, 64, 14,
            headerLogo.pixels.data(), headerLogo.width, headerLogo.height);
    }
    scene.DrawText(125, 42, "STREMIO", text, 3);
}

void drawLine(
    Scene2D& scene, int x0, int y0, int x1, int y1, Color color,
    int thickness = 3) {
    const int dx = std::abs(x1 - x0);
    const int sx = x0 < x1 ? 1 : -1;
    const int dy = -std::abs(y1 - y0);
    const int sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;
    for (;;) {
        scene.DrawRectangle(x0 - thickness / 2, y0 - thickness / 2,
            thickness, thickness, color);
        if (x0 == x1 && y0 == y1) break;
        const int doubled = error * 2;
        if (doubled >= dy) { error += dy; x0 += sx; }
        if (doubled <= dx) { error += dx; y0 += sy; }
    }
}

void drawButtonHint(
    Scene2D& scene, int x, int y, char button, const char* label) {
    const Color text = {235, 232, 244};
    const Color blue = {86, 170, 255};
    const Color red = {255, 105, 120};
    const Color green = {95, 220, 145};
    const Color pink = {235, 125, 225};
    const Color panel = {35, 35, 46};
    scene.DrawRoundedRectangle(x, y, 38, 38, 19, panel);
    if (button == 'X') {
        drawLine(scene, x + 11, y + 11, x + 27, y + 27, blue);
        drawLine(scene, x + 27, y + 11, x + 11, y + 27, blue);
    } else if (button == 'O') {
        scene.DrawRoundedRectangle(x + 8, y + 8, 22, 22, 11, red);
        scene.DrawRoundedRectangle(x + 12, y + 12, 14, 14, 7, panel);
    } else if (button == 'S') {
        scene.DrawRectangle(x + 9, y + 9, 20, 20, pink);
        scene.DrawRectangle(x + 13, y + 13, 12, 12, panel);
    } else if (button == 'T') {
        drawLine(scene, x + 19, y + 7, x + 7, y + 29, green);
        drawLine(scene, x + 7, y + 29, x + 31, y + 29, green);
        drawLine(scene, x + 31, y + 29, x + 19, y + 7, green);
    }
    scene.DrawText(x + 50, y + 5, label, text, 2);
}

void drawShoulderHint(
    Scene2D& scene, int x, int y, const char* button, const char* label) {
    const Color panel = {52, 52, 68};
    const Color text = {235, 232, 244};
    scene.DrawRoundedRectangle(x, y, 56, 38, 10, panel);
    scene.DrawText(x + 12, y + 5, button, text, 2);
    scene.DrawText(x + 68, y + 5, label, text, 2);
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
    const Color cardMuted = {35, 35, 46};
    const Color focus = {196, 174, 255};
    const Color text = {235, 232, 244};
    const Color mutedText = {164, 158, 181};

    scene.FrameBufferFill(background);
    const int tabX[kTopTabCount] = {350, 570, 790, 1130, 1380};
    const int cardX[] = {120, 565, 1010, 1455};
    const int cardY = 260 + catalogMotion;
    auto drawCatalogRow = [&](int rowPage, int rowY, bool selected,
                              bool showTitles, int scalePercent) {
        if (rowPage < 0 || rowY >= 1000 || rowY + 460 <= 145) return;
        const int cardWidth = 310 * scalePercent / 100;
        const int cardHeight = 410 * scalePercent / 100;
        for (int index = 0; index < 4; ++index) {
        const int itemIndex = rowPage * 4 + index;
        const int x = cardX[index] + (310 - cardWidth) / 2;
        if (selected && index == focusedCard) {
            const int pulse = (animationFrame / 8) % 2;
            scene.DrawRoundedRectangle(x - 8 - pulse,
                rowY - 8 - pulse,
                cardWidth + 16 + pulse * 2,
                cardHeight + 16 + pulse * 2, 22, focus);
        }
        const bool posterReady = itemIndex < static_cast<int>(posters.size()) &&
            posters[itemIndex].valid();
        if (posterReady && scalePercent == 100) {
            scene.BlitRgbMasked(x, rowY, posters[itemIndex].width,
                posters[itemIndex].height, posters[itemIndex].pixels.data());
        } else if (posterReady && posters[itemIndex].previewValid()) {
            scene.BlitRgbMasked(x, rowY, posters[itemIndex].previewWidth,
                posters[itemIndex].previewHeight,
                posters[itemIndex].previewPixels.data());
        } else {
            scene.DrawRoundedRectangle(x, rowY, cardWidth, cardHeight, 18,
                stremioPurple);
            const int logoSize = 96 * scalePercent / 100;
            const int logoX = x + (cardWidth - logoSize) / 2;
            const int logoY = rowY + (cardHeight - logoSize) / 2;
            scene.DrawRoundedRectangle(logoX - 12, logoY - 12,
                logoSize + 24, logoSize + 24, 28, cardMuted);
            if (headerLogo.valid() && rowY >= 0 && rowY + 410 <= kHeight) {
                scene.BlitRgbScaledRounded(logoX, logoY,
                    logoSize, logoSize, 20, headerLogo.pixels.data(),
                    headerLogo.width, headerLogo.height);
            }
        }
        if (showTitles && itemIndex < static_cast<int>(items.size())) {
            const std::string title = shortTitle(items[itemIndex].name);
            scene.DrawText(x, rowY + cardHeight + 20, title.c_str(), text, 2);
        }
        }
    };

    // During paging, preserve the old row and move it out while the exact
    // cards that were visible below move into the selected position.
    if (catalogMotion > 0)
        drawCatalogRow(page - 1, cardY - 560, false, true, 100);
    else if (catalogMotion < 0)
        drawCatalogRow(page + 1, cardY + 560, false, true, 100);
    // Use the precomputed 90% texture for most of the rise and switch to the
    // full poster near its destination. Avoid resampling four JPEGs per frame.
    const int selectedScale = catalogMotion > 48 ? 90 : 100;
    drawCatalogRow(page, cardY, true, true, selectedScale);

    const int nextPage = page + 1;
    // The next row stays still. Movement belongs to page selection, not an
    // always-running animation that consumes GPU time and looks like jumping.
    const int previewY = 820 + (catalogMotion > 0 ? catalogMotion : 0);
    if (catalogMotion >= 0)
        drawCatalogRow(nextPage, previewY, false, false, 90);

    // Paint the navigation after moving rows. This is a hard content viewport:
    // outgoing cards disappear behind it instead of crossing the top menu.
    scene.DrawRectangle(0, 0, kWidth, 240, background);
    scene.DrawVerticalFade(0, 0, kWidth, 180, header, 220, 0);
    drawBrand(scene, text);
    for (int index = 0; index < kTopTabCount; ++index) {
        const int travel = index == activeTab
            ? std::abs(tabX[activeTab] - indicatorX) : 0;
        const int lift = std::min(8, travel / 18);
        if (index == activeTab)
            scene.DrawRectangle(tabX[index] - 12, 38 - lift,
                activeTab == 2 ? 280 : 195, 48, cardMuted);
        scene.DrawText(tabX[index], 52 - lift, kTopTabs[index],
            index == activeTab ? text : mutedText, 2);
    }
    scene.DrawRectangle(indicatorX, 105, activeTab == 2 ? 255 : 170, 8,
        stremioPurple);
    scene.DrawText(1670, 52, "L1 / R1", mutedText, 2);
    scene.DrawRectangle(120, 170, 690, 48, stremioPurple);
    scene.DrawText(142, 182,
        activeTab == 3 ? "SEARCH RESULTS" :
        (catalogType == "series" ? "TOP SERIES" :
        (catalogType == "publicdomain" ? "PUBLIC DOMAIN" : "TOP MOVIES")),
        text, 3);
    char pageText[64];
    snprintf(pageText, sizeof(pageText), "PAGE %d   %d LOADED", page + 1,
        static_cast<int>(items.size()));
    scene.DrawText(1540, 182, pageText, mutedText, 2);
    scene.DrawText(120, 740, status.c_str(), mutedText, 2);
    scene.DrawVerticalFade(0, 930, kWidth, 150, header, 0, 230);
    const int shimmerX = (animationFrame * 7) % (kWidth + 180) - 180;
    scene.DrawRectangle(shimmerX, 998, 180, 3, stremioPurple);
    drawShoulderHint(scene, 40, 1020, "L1", "LEFT TAB");
    drawShoulderHint(scene, 260, 1020, "R1", "RIGHT TAB");
    drawButtonHint(scene, 1370, 1020, 'S', "VIDEO TEST");
    drawButtonHint(scene, 1630, 1020, 'X', "DETAILS");
}

void drawSearch(Scene2D& scene, const std::string& query, int,
    int indicatorX, int) {
    const Color background = {18, 18, 24};
    const Color header = {29, 29, 39};
    const Color purple = {123, 91, 214};
    const Color text = {235, 232, 244};
    const Color muted = {164, 158, 181};
    scene.FrameBufferFill(background);
    scene.DrawVerticalFade(0, 0, kWidth, 180, header, 220, 0);
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
    scene.DrawText(270, 455,
        "PRESS CROSS TO TYPE WITH THE PS4 KEYBOARD", muted, 2);
    scene.DrawText(270, 500,
        "CROSS TYPE   SQUARE DELETE   TRIANGLE SPACE   R2 SEARCH   CIRCLE CLOSE",
        text, 2);
    scene.DrawVerticalFade(0, 930, kWidth, 150, header, 0, 230);
    drawShoulderHint(scene, 40, 1020, "L1", "LEFT TAB");
    drawShoulderHint(scene, 260, 1020, "R1", "RIGHT TAB");
    drawButtonHint(scene, 1170, 1020, 'O', "CLEAR / BACK");
    drawButtonHint(scene, 1435, 1020, 'T', "SEARCH");
    drawButtonHint(scene, 1660, 1020, 'X', "KEYBOARD");
}

void drawSettings(Scene2D& scene, int selected, int indicatorX) {
    const Color background = {18, 18, 24};
    const Color header = {29, 29, 39};
    const Color purple = {123, 91, 214};
    const Color focus = {196, 174, 255};
    const Color text = {235, 232, 244};
    const Color muted = {164, 158, 181};
    scene.FrameBufferFill(background);
    scene.DrawVerticalFade(0, 0, kWidth, 180, header, 220, 0);
    drawBrand(scene, text);
    const int tabX[kTopTabCount] = {350, 570, 790, 1130, 1380};
    for (int index = 0; index < kTopTabCount; ++index) {
        scene.DrawText(tabX[index], 52, kTopTabs[index],
            index == 4 ? text : muted, 2);
    }
    scene.DrawRectangle(indicatorX, 105, 170, 8, purple);
    scene.DrawText(120, 190, "SETTINGS & DIAGNOSTICS", text, 3);
    const char* rows[] = {
        "H.264 PLAYBACK TEST - 640x360",
        "H.264 PLAYBACK TEST - 1280x720",
        "H.264 PLAYBACK TEST - 1920x1080",
        "H.264 PLAYBACK TEST - ORIGINAL 854x480",
        "CACHED HTTPS PLAYBACK TEST - 854x480",
        "CLEAR SEARCH QUERY",
        "ABOUT STREMIO PS4  v2.61"};
    for (int index = 0; index < 7; ++index) {
        const int y = 220 + index * 105;
        if (index == selected) scene.DrawRectangle(112, y - 8, 1696, 86, focus);
        scene.DrawRectangle(120, y, 1680, 70, header);
        scene.DrawText(155, y + 22, rows[index], text, 2);
    }
    scene.DrawVerticalFade(0, 930, kWidth, 150, header, 0, 230);
    drawShoulderHint(scene, 40, 1020, "L1", "LEFT TAB");
    drawShoulderHint(scene, 260, 1020, "R1", "RIGHT TAB");
    drawButtonHint(scene, 1640, 1020, 'X', "RUN");
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
    scene.DrawRectangle(0, 1000, kWidth, 80, panel);
    drawButtonHint(scene, 40, 1020, 'O', "BACK");
    if (catalogType == "series")
        drawButtonHint(scene, 1620, 1020, 'X', "SELECT EPISODE");
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
    scene.DrawRectangle(0, 1000, kWidth, 80, panel);
    drawButtonHint(scene, 40, 1020, 'O', "DETAILS");
    drawButtonHint(scene, 1590, 1020, 'X', "PLAY / INSPECT");
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
    uint32_t decoderFpsTimesTen,
    uint64_t decodedFrames,
    uint32_t sourceWidth,
    uint32_t sourceHeight) {
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
    char debug[128];
    snprintf(debug, sizeof(debug),
        "SOURCE %ux%u   FRAMES %llu   SEEK STEP 5s   HW AVPLAYER",
        sourceWidth, sourceHeight,
        static_cast<unsigned long long>(decodedFrames));
    scene.DrawText(480, 945, debug, text, 2);
    scene.DrawRectangle(0, 1000, kWidth, 80, background);
    drawButtonHint(scene, 40, 1020, 'O', "STOP");
    drawButtonHint(scene, 250, 1020, '<', "-5 SEC");
    drawButtonHint(scene, 500, 1020, '>', "+5 SEC");
    drawButtonHint(scene, 760, 1020, 'T', "RESTART");
    drawButtonHint(scene, 1610, 1020, 'X', paused ? "RESUME" : "PAUSE");
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
    if (!imeDialogInitialized) {
        if (sceSysmoduleLoadModuleInternal(
                ORBIS_SYSMODULE_INTERNAL_COMMON_DIALOG) < 0 ||
            sceSysmoduleLoadModule(ORBIS_SYSMODULE_IME_DIALOG) < 0) {
            return false;
        }
        sceCommonDialogInitialize();
        imeDialogInitialized = true;
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
    // Use Sony's standard search-dialog configuration. The system keyboard
    // owns Cross/Square/Triangle/R2/Circle while this blocking dialog runs.
    settings.type = ORBIS_TYPE_DEFAULT;
    settings.enterLabel = ORBIS_BUTTON_LABEL_SEARCH;
    settings.inputMethod = ORBIS__DEFAULT;
    settings.maxTextLength = 64;
    settings.inputTextBuffer = reinterpret_cast<wchar_t*>(buffer);
    settings.posx = 960.0f;
    settings.posy = 540.0f;
    settings.horizontalAlignment = ORBIS_H_CENTER;
    settings.verticalAlignment = ORBIS_V_CENTER;
    settings.placeholder = reinterpret_cast<const wchar_t*>(placeholder);
    settings.title = reinterpret_cast<const wchar_t*>(title);
    if (sceImeDialogInit(&settings, nullptr) < 0) return false;

    OrbisDialogStatus status = sceImeDialogGetStatus();
    while (status == ORBIS_DIALOG_STATUS_RUNNING && !exitRequested) {
        sceKernelUsleep(16000);
        status = sceImeDialogGetStatus();
    }
    if (exitRequested) {
        sceImeDialogAbort();
        sceImeDialogTerm();
        return false;
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

uint64_t stableHash(const std::string& value) {
    uint64_t hash = 1469598103934665603ULL;
    for (unsigned char byte : value) {
        hash ^= byte;
        hash *= 1099511628211ULL;
    }
    return hash;
}

std::string cachePath(const char* kind, const std::string& key,
                      const char* extension) {
    char path[160];
    snprintf(path, sizeof(path), "/data/stremio-%s-%016llx.%s", kind,
        static_cast<unsigned long long>(stableHash(key)), extension);
    return path;
}

bool readCachedFile(const std::string& path, size_t maximum,
                    std::string& contents) {
    FILE* file = fopen(path.c_str(), "rb");
    if (!file) return false;
    fseek(file, 0, SEEK_END);
    const long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (size <= 0 || static_cast<size_t>(size) > maximum) {
        fclose(file);
        return false;
    }
    contents.resize(static_cast<size_t>(size));
    const bool okay = fread(&contents[0], 1, contents.size(), file) ==
        contents.size();
    fclose(file);
    return okay;
}

void writeCachedFile(const std::string& path, const std::string& contents) {
    FILE* file = fopen(path.c_str(), "wb");
    if (!file) return;
    fwrite(contents.data(), 1, contents.size(), file);
    fclose(file);
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
    const std::string stored = cachePath("catalog", url, "json");
    int bytes = 0;
    if (!readCachedFile(stored, kMaximumCatalogBytes, body)) {
        bytes = downloadUrl(url.c_str(), kMaximumCatalogBytes, body);
        if (bytes >= 0) writeCachedFile(stored, body);
    } else {
        bytes = static_cast<int>(body.size());
    }
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
    const std::string stored = cachePath("meta", url, "json");
    int bytes = 0;
    if (!readCachedFile(stored, kMaximumCatalogBytes, body)) {
        bytes = downloadUrl(url.c_str(), kMaximumCatalogBytes, body);
        if (bytes >= 0) writeCachedFile(stored, body);
    } else {
        bytes = static_cast<int>(body.size());
    }
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
        const std::string stored = cachePath("poster", posterUrl, "jpg");
        const bool cached = readCachedFile(
            stored, kMaximumPosterBytes, encoded);
        int bytes = cached ? static_cast<int>(encoded.size())
            : downloadUrl(posterUrl.c_str(), kMaximumPosterBytes, encoded);
        if (bytes >= 0 &&
            decodePosterJpeg(
                encoded, kPosterWidth, kPosterHeight, posters[index])) {
            if (!cached) writeCachedFile(stored, encoded);
            preparePosterPresentation(posters[index], 18, 279, 369);
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

struct CatalogLoadJob {
    pthread_t thread = {};
    std::atomic<bool> running{false};
    std::atomic<bool> completed{false};
    std::atomic<bool> itemsReady{false};
    std::atomic<int> postersProcessed{0};
    bool loaded = false;
    int result = 0;
    int posterCount = 0;
    std::string type;
    std::string status;
    std::vector<CatalogItem> items;
    std::vector<PosterImage> posters;
};

struct CatalogPageJob {
    pthread_t thread = {};
    std::atomic<bool> running{false};
    std::atomic<bool> completed{false};
    std::string type;
    int skip = 0;
    int result = 0;
    std::vector<CatalogItem> items;
    std::vector<PosterImage> posters;
};

void* catalogPageEntry(void* argument) {
    CatalogPageJob* job = static_cast<CatalogPageJob*>(argument);
    job->items.clear();
    job->posters.clear();
    job->result = fetchCatalogPage(job->type, job->skip, "", job->items);
    if (job->result > 0) fetchPosters(job->items, job->posters);
    job->completed.store(true, std::memory_order_release);
    job->running.store(false, std::memory_order_release);
    return nullptr;
}

void* catalogLoadEntry(void* argument) {
    CatalogLoadJob* job = static_cast<CatalogLoadJob*>(argument);
    job->items.clear();
    job->posters.clear();
    job->posterCount = 0;
    job->itemsReady.store(false, std::memory_order_release);
    job->postersProcessed.store(0, std::memory_order_release);
    job->result = fetchCatalogPage(job->type, 0, "", job->items);
    if (job->result > 0) {
        std::vector<CatalogItem> nextItems;
        if (fetchCatalogPage(job->type,
                static_cast<int>(job->items.size()), "", nextItems) > 0) {
            job->items.insert(job->items.end(), nextItems.begin(), nextItems.end());
        }
        job->posters.clear();
        job->posters.resize(job->items.size());
        job->itemsReady.store(true, std::memory_order_release);
        for (size_t index = 0; index < job->items.size(); ++index) {
            std::vector<CatalogItem> oneItem(1, job->items[index]);
            std::vector<PosterImage> onePoster;
            if (fetchPosters(oneItem, onePoster) > 0 && !onePoster.empty()) {
                job->posters[index] = std::move(onePoster[0]);
                ++job->posterCount;
            }
            job->postersProcessed.store(
                static_cast<int>(index + 1), std::memory_order_release);
        }
        char text[128];
        snprintf(text, sizeof(text), "%d ITEMS   %d POSTERS   PRELOADED",
            static_cast<int>(job->items.size()), job->posterCount);
        job->status = text;
    } else {
        job->status = "CATALOG LOAD FAILED - PRESS TRIANGLE TO RETRY";
    }
    job->loaded = job->result > 0;
    job->completed.store(true, std::memory_order_release);
    job->running.store(false, std::memory_order_release);
    return nullptr;
}
}  // namespace

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    signal(SIGTERM, requestExit);
    signal(SIGINT, requestExit);
    DEBUGLOG << "Stremio native client starting";
    int userId = -1;
    const int pad = initializeController(userId);
    if (pad < 0) notify("Stremio: controller initialization failed");

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

    CatalogLoadJob catalogJobs[3];
    CatalogPageJob pageJob;
    catalogJobs[0].type = "movie";
    catalogJobs[1].type = "series";
    catalogJobs[2].type = "publicdomain";
    int activeCatalogDataTab = -1;
    int displayedPosterCount = 0;

    auto catalogWorkersBusy = [&]() {
        for (int index = 0; index < 3; ++index) {
            if (catalogJobs[index].running.load(std::memory_order_acquire))
                return true;
        }
        return false;
    };

    auto startPagePrefetch = [&]() {
        if (activeTab >= 3 || pageJob.running.load(std::memory_order_acquire) ||
            pageJob.completed.load(std::memory_order_acquire) ||
            catalogWorkersBusy() || catalogItems.empty()) return;
        pageJob.type = catalogType;
        pageJob.skip = static_cast<int>(catalogItems.size());
        pageJob.completed.store(false, std::memory_order_release);
        pageJob.running.store(true, std::memory_order_release);
        if (pthread_create(&pageJob.thread, nullptr,
                catalogPageEntry, &pageJob) != 0) {
            pageJob.running.store(false, std::memory_order_release);
        }
    };

    auto stopBackgroundForPlayback = [&]() {
        const bool pageRunning = pageJob.running.load(std::memory_order_acquire);
        if (pageRunning) pthread_cancel(pageJob.thread);
        if (pageRunning || pageJob.completed.load(std::memory_order_acquire))
            pthread_join(pageJob.thread, nullptr);
        pageJob.running.store(false, std::memory_order_release);
        pageJob.completed.store(false, std::memory_order_release);
        pageJob.items.clear();
        pageJob.posters.clear();
        for (int index = 0; index < 3; ++index) {
            CatalogLoadJob& job = catalogJobs[index];
            if (!job.running.load(std::memory_order_acquire)) continue;
            pthread_cancel(job.thread);
            pthread_join(job.thread, nullptr);
            job.running.store(false, std::memory_order_release);
            job.completed.store(false, std::memory_order_release);
            job.itemsReady.store(false, std::memory_order_release);
            job.postersProcessed.store(0, std::memory_order_release);
            job.loaded = false;
        }
    };

    auto storeActiveCatalog = [&]() {
        if (activeCatalogDataTab < 0 || activeCatalogDataTab >= 3) return;
        CatalogLoadJob& cache = catalogJobs[activeCatalogDataTab];
        if (cache.running.load(std::memory_order_acquire) ||
            cache.completed.load(std::memory_order_acquire)) {
            // The worker owns its vectors until it has been joined.
            catalogItems.clear();
            catalogPosters.clear();
            catalogStatus.clear();
            activeCatalogDataTab = -1;
            return;
        }
        cache.items.swap(catalogItems);
        cache.posters.swap(catalogPosters);
        cache.status.swap(catalogStatus);
        cache.loaded = !cache.items.empty();
        activeCatalogDataTab = -1;
        displayedPosterCount = 0;
    };

    auto startCatalogLoad = [&](int tab, bool force) {
        if (tab < 0 || tab >= 3) return false;
        CatalogLoadJob& job = catalogJobs[tab];
        if (job.running.load(std::memory_order_acquire)) return true;
        if (pageJob.running.load(std::memory_order_acquire)) return false;
        if (!force && job.loaded) return true;
        // Keep the shared PS4 HTTP contexts single-threaded. A rapidly selected
        // tab remains responsive with placeholders and starts next.
        for (int index = 0; index < 3; ++index) {
            if (catalogJobs[index].running.load(std::memory_order_acquire))
                return false;
        }
        job.loaded = false;
        job.itemsReady.store(false, std::memory_order_release);
        job.postersProcessed.store(0, std::memory_order_release);
        job.completed.store(false, std::memory_order_release);
        job.running.store(true, std::memory_order_release);
        if (pthread_create(&job.thread, nullptr, catalogLoadEntry, &job) != 0) {
            job.running.store(false, std::memory_order_release);
            job.status = "CATALOG WORKER FAILED - PRESS TRIANGLE TO RETRY";
            return false;
        }
        return true;
    };

    auto activateCatalog = [&](int tab, bool force) {
        storeActiveCatalog();
        CatalogLoadJob& job = catalogJobs[tab];
        catalogType = job.type;
        catalogItems.clear();
        catalogPosters.clear();
        bool restoredFromCache = false;
        if (!force && job.loaded) {
            job.items.swap(catalogItems);
            job.posters.swap(catalogPosters);
            job.status.swap(catalogStatus);
            job.loaded = false;
            restoredFromCache = true;
        } else {
            if (force && !job.running.load(std::memory_order_acquire) &&
                !job.completed.load(std::memory_order_acquire)) {
                job.items.clear();
                job.posters.clear();
                job.status.clear();
                job.loaded = false;
            }
            catalogStatus = "LOADING - PLACEHOLDERS READY";
            startCatalogLoad(tab, force);
        }
        activeCatalogDataTab = tab;
        displayedPosterCount = restoredFromCache
            ? static_cast<int>(catalogPosters.size()) : 0;
        focusedCard = 0;
        catalogPage = 0;
        detailVisible = false;
        streamVisible = false;
        detailEpisodeIndex = 0;
    };

    auto loadSearchResults = [&]() {
        if (searchQuery.empty()) {
            notify("Stremio: enter a search title first");
            return;
        }
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
        } else {
            catalogStatus = "NO SEARCH RESULTS - CIRCLE TO EDIT";
            searchShowingResults = true;
        }
    };

    auto selectTab = [&](int tab) {
        const int nextTab = (tab + kTopTabCount) % kTopTabCount;
        if (nextTab == activeTab) return;
        if (activeCatalogDataTab >= 0) storeActiveCatalog();
        activeTab = nextTab;
        detailVisible = false;
        streamVisible = false;
        if (activeTab < 3) activateCatalog(activeTab, false);
        else if (activeTab != 3 || !searchShowingResults) {
            catalogItems.clear();
            catalogPosters.clear();
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
    activateCatalog(0, false);

    while (!exitRequested) {
        if (pageJob.completed.exchange(false, std::memory_order_acq_rel)) {
            pthread_join(pageJob.thread, nullptr);
            if (pageJob.result > 0 && pageJob.type == catalogType &&
                pageJob.skip == static_cast<int>(catalogItems.size())) {
                catalogItems.insert(catalogItems.end(),
                    pageJob.items.begin(), pageJob.items.end());
                catalogPosters.insert(catalogPosters.end(),
                    pageJob.posters.begin(), pageJob.posters.end());
                char status[96];
                snprintf(status, sizeof(status),
                    "%d ITEMS LOADED   BACKGROUND PREFETCH",
                    static_cast<int>(catalogItems.size()));
                catalogStatus = status;
            }
            pageJob.items.clear();
            pageJob.posters.clear();
        }
        if (activeTab < 3 && activeCatalogDataTab == activeTab) {
            CatalogLoadJob& activeJob = catalogJobs[activeTab];
            if (activeJob.itemsReady.load(std::memory_order_acquire) &&
                catalogItems.empty()) {
                catalogItems = activeJob.items;
                catalogPosters.resize(catalogItems.size());
            }
            const int ready = activeJob.postersProcessed.load(
                std::memory_order_acquire);
            while (displayedPosterCount < ready &&
                displayedPosterCount < static_cast<int>(catalogPosters.size())) {
                catalogPosters[displayedPosterCount] =
                    activeJob.posters[displayedPosterCount];
                ++displayedPosterCount;
            }
        }
        // Adopt completed content only on the main/render thread. Other tabs
        // retain their completed vectors as an instant in-memory cache.
        for (int index = 0; index < 3; ++index) {
            CatalogLoadJob& job = catalogJobs[index];
            if (!job.completed.exchange(false, std::memory_order_acq_rel))
                continue;
            pthread_join(job.thread, nullptr);
            if (activeCatalogDataTab == index && activeTab == index) {
                catalogItems.swap(job.items);
                catalogPosters.swap(job.posters);
                catalogStatus.swap(job.status);
                job.loaded = false;
                displayedPosterCount = static_cast<int>(catalogPosters.size());
            }
        }
        if (activeTab < 3 && catalogItems.empty() &&
            !catalogJobs[activeTab].running.load(std::memory_order_acquire) &&
            !catalogJobs[activeTab].loaded) {
            startCatalogLoad(activeTab, false);
        }
        const uint32_t buttons = readButtons(pad);
        const uint32_t pressed = buttons & ~previousButtons;
        previousButtons = buttons;

        if ((pressed & ORBIS_PAD_BUTTON_OPTIONS) != 0) {
            if (previewVisible ||
                avPlayer.state() != AvPlayerProbe::State::Idle) {
                DEBUGLOG << "Options pressed during playback; returning to shell";
                avPlayer.stop();
                previewVisible = false;
            } else {
                DEBUGLOG << "Options pressed from shell; Home API disabled";
            }
        }

        const bool shellVisible =
            !previewVisible && !detailVisible && !streamVisible;
        const bool catalogScreen = activeTab < 3 ||
            (activeTab == 3 && searchShowingResults);
        if (shellVisible && (pressed & ORBIS_PAD_BUTTON_R1) != 0) {
            selectTab(activeTab + 1);
        } else if (shellVisible && (pressed & ORBIS_PAD_BUTTON_L1) != 0) {
            selectTab(activeTab - 1);
        }

        const int tabTargets[kTopTabCount] = {350, 570, 790, 1130, 1380};
        const int indicatorDelta = tabTargets[activeTab] - indicatorX;
        if (indicatorDelta != 0) {
            if (indicatorDelta >= -8 && indicatorDelta <= 8) {
                indicatorX = tabTargets[activeTab];
            } else {
                indicatorX += indicatorDelta / 2;
            }
        }

        if (shellVisible && activeTab == 3 && !searchShowingResults) {
            if ((pressed & ORBIS_PAD_BUTTON_CROSS) != 0) {
                // The same Cross press must not leak into the native IME and
                // immediately accept/close it. Wait for a clean release first.
                while ((readButtons(pad) & ORBIS_PAD_BUTTON_CROSS) != 0 &&
                    !exitRequested) sceKernelUsleep(8000);
                if (openSystemSearchKeyboard(userId, searchQuery)) {
                    loadSearchResults();
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
                    catalogStatus = "LOADING MORE IN BACKGROUND...";
                    startPagePrefetch();
                }
                if (nextPage * 4 < static_cast<int>(catalogItems.size())) {
                    catalogPage = nextPage;
                    // New selection enters from below; the preview row itself
                    // remains stationary until it becomes the active row.
                    catalogMotion = 560;
                    if (catalogPage * 4 + focusedCard >=
                        static_cast<int>(catalogItems.size())) focusedCard = 0;
                }
            }
            if ((pressed & ORBIS_PAD_BUTTON_UP) != 0 && catalogPage > 0) {
                --catalogPage;
                catalogMotion = -560;
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
            if ((pressed & ORBIS_PAD_BUTTON_DOWN) != 0 && settingsSelection < 6)
                ++settingsSelection;
            if ((pressed & ORBIS_PAD_BUTTON_CROSS) != 0 &&
                settingsSelection == 5) {
                searchQuery.clear();
                searchShowingResults = false;
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
        } else if ((pressed & ORBIS_PAD_BUTTON_LEFT) != 0 && previewVisible) {
            avPlayer.seekRelative(-5000);
        } else if ((pressed & ORBIS_PAD_BUTTON_RIGHT) != 0 && previewVisible) {
            avPlayer.seekRelative(5000);
        } else if ((pressed & ORBIS_PAD_BUTTON_TRIANGLE) != 0 && previewVisible) {
            avPlayer.restart();
        } else if ((pressed & ORBIS_PAD_BUTTON_CROSS) != 0 &&
            !detailVisible && !streamVisible && catalogScreen) {
            const int selectedIndex = catalogPage * 4 + focusedCard;
            if (selectedIndex < static_cast<int>(catalogItems.size())) {
                const int detailResult = fetchDetails(
                    catalogType, catalogItems[selectedIndex], details);
                if (detailResult > 0) {
                    detailPosterIndex = selectedIndex;
                    detailEpisodeIndex = 0;
                    detailVisible = true;
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
            const int streamResult =
                fetchPublicDomainStreams(details.id, streams);
            if (streamResult > 0) {
                focusedStream = 0;
                streamVisible = true;
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
            activateCatalog(activeTab, true);
        }
        const bool localPlaybackRequested =
            ((pressed & ORBIS_PAD_BUTTON_SQUARE) != 0 && catalogScreen) ||
            (shellVisible && activeTab == 4 && settingsSelection <= 3 &&
             (pressed & ORBIS_PAD_BUTTON_CROSS) != 0);
        if (localPlaybackRequested) {
            stopBackgroundForPlayback();
            previewVisible = false;
            previewBackgroundFrames = 0;
            playbackWallStart = 0;
            playbackTimingReported = false;
            const int testIndex = activeTab == 4 ? settingsSelection : 3;
            char opening[96];
            snprintf(opening, sizeof(opening),
                "Stremio test: opening Sintel %s H.264",
                kVideoTestNames[testIndex]);
            notify(opening);
            avPlayerProbeFrames = 0;
            if (!avPlayer.start(kVideoTestPaths[testIndex])) {
                char result[128];
                snprintf(result, sizeof(result),
                    "Stremio: AVPlayer stage %d, code 0x%08x",
                    avPlayer.errorStage(),
                    static_cast<unsigned int>(avPlayer.errorCode()));
                notify(result);
            }
        }
        const bool remotePlaybackRequested = shellVisible && activeTab == 4 &&
            settingsSelection == 4 &&
            (pressed & ORBIS_PAD_BUTTON_CROSS) != 0;
        if (remotePlaybackRequested) {
            stopBackgroundForPlayback();
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
        } else if ((pressed & ORBIS_PAD_BUTTON_CIRCLE) != 0 &&
            avPlayer.state() != AvPlayerProbe::State::Idle) {
            avPlayer.stop();
            avPlayerProbeFrames = 0;
        } else if ((pressed & ORBIS_PAD_BUTTON_CIRCLE) != 0 && streamVisible) {
            streamVisible = false;
        } else if ((pressed & ORBIS_PAD_BUTTON_CIRCLE) != 0 && detailVisible) {
            detailVisible = false;
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
                avPlayer.measuredFpsTimesTen(), avPlayer.decodedFrames(),
                avPlayer.width(), avPlayer.height());
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
            if (catalogMotion > -3 && catalogMotion < 3) catalogMotion = 0;
        }
        if (activeTab < 3 && !catalogItems.empty() &&
            static_cast<int>(catalogItems.size()) - (catalogPage + 2) * 4 <= 4)
            startPagePrefetch();

    }

    DEBUGLOG << "Stremio graceful shutdown starting";
    avPlayer.stop();
    for (int index = 0; index < 3; ++index) {
        CatalogLoadJob& job = catalogJobs[index];
        const bool wasRunning = job.running.load(std::memory_order_acquire);
        if (wasRunning) pthread_cancel(job.thread);
        if (wasRunning || job.completed.load(std::memory_order_acquire))
            pthread_join(job.thread, nullptr);
    }
    const bool pageWasRunning = pageJob.running.load(std::memory_order_acquire);
    if (pageWasRunning) pthread_cancel(pageJob.thread);
    if (pageWasRunning || pageJob.completed.load(std::memory_order_acquire))
        pthread_join(pageJob.thread, nullptr);
    if (imeDialogInitialized &&
        sceImeDialogGetStatus() == ORBIS_DIALOG_STATUS_RUNNING) {
        sceImeDialogAbort();
        sceImeDialogTerm();
    }
    if (pad >= 0) scePadClose(pad);
    if (httpContextId > 0) {
        sceHttpTerm(httpContextId);
        httpContextId = 0;
    }
    if (sslContextId > 0) {
        sceSslTerm();
        sslContextId = 0;
    }
    if (networkPoolId > 0) {
        sceNetPoolDestroy(networkPoolId);
        networkPoolId = 0;
        sceNetTerm();
    }
    sceUserServiceTerminate();
    DEBUGLOG << "Stremio graceful shutdown complete";
    return 0;
}
