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
#include <orbis/AudioOut.h>
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
#include "videodec2.h"
#include "mp4_demux.h"
#include "fmp4_stream.h"

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
    kLegalVideoPath,
    "/app0/assets/sintel-720p.mp4",
    "/app0/assets/sintel-1080p.mp4",
    "/app0/assets/sintel-1080p-high.mp4"};
const char* const kVideoTestNames[] = {
    "360P", "854X480", "720P", "1080P BASELINE", "1080P HIGH"};
constexpr const char* kCatalogBaseUrl =
    "https://cinemeta-catalogs.strem.io/top/catalog/";
constexpr const char* kMetaBaseUrl = "https://v3-cinemeta.strem.io/meta/";
constexpr const char* kPublicDomainBaseUrl =
    "https://caching.stremio.net/publicdomainmovies.now.sh/";
constexpr const char* kLegalRemoteVideoUrl =
    "https://media.w3.org/2010/05/sintel/trailer.mp4";
constexpr const char* kLegalRemoteCachePath = "/data/stremio-remote-sintel.mp4";
constexpr const char* kDirectVideoTestPath =
    "/app0/assets/sintel-1080p-high-annexb.h264";
constexpr size_t kMaximumDemoVideoBytes = 16 * 1024 * 1024;
constexpr size_t kLegalRemoteVideoBytes = 4372373;
constexpr size_t kMaximumCatalogBytes = 1024 * 1024;
constexpr size_t kMaximumPosterBytes = 2 * 1024 * 1024;
constexpr size_t kMaximumStreamBytes = size_t(1900) * 1024 * 1024;
constexpr const char* kCompanionConfigPath = "/data/stremio-companion.txt";
constexpr const char* kAuthKeyPath = "/data/stremio-auth-key.txt";
constexpr const char* kAddonCollectionPath = "/data/stremio-addons.json";
constexpr const char* kNavigationConfigPath = "/data/stremio-navigation.txt";
constexpr const char* kLinkCreateUrl =
    "https://link.stremio.com/api/v2/create?type=Create";
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
PosterImage creatorAvatar;
volatile sig_atomic_t exitRequested = 0;
bool imeDialogInitialized = false;
bool useSideNavigation = true;
bool navigationFocused = false;

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

void drawSideNavigation(Scene2D& scene, int activeTab) {
    const Color rail = {12, 11, 19};
    const Color selected = {87, 61, 150};
    const Color purple = {123, 91, 214};
    const Color text = {235, 232, 244};
    const Color muted = {135, 130, 151};
    const int railWidth = navigationFocused ? 330 : 96;
    scene.DrawRectangle(0, 0, railWidth, kHeight, rail);
    scene.DrawVerticalFade(railWidth - 18, 0, 18, kHeight,
        Color{65, 43, 110}, 110, 25);
    if (navigationFocused) drawBrand(scene, text);
    else if (headerLogo.valid()) scene.BlitRgbScaledRounded(18, 27, 60, 60, 14,
        headerLogo.pixels.data(), headerLogo.width, headerLogo.height);
    const char* labels[] = {"Movies", "Series", "Public domain", "Search", "Settings"};
    const char* glyphs[] = {"M", "TV", "P", "Q", "S"};
    for (int index = 0; index < kTopTabCount; ++index) {
        const int y = 190 + index * 112;
        if (index == activeTab) {
            scene.DrawRoundedRectangle(22, y,
                navigationFocused ? 286 : 58, 76, 38, selected);
        }
        scene.DrawRoundedRectangle(34, y + 10, 56, 56, 28,
            index == activeTab ? purple : Color{31, 29, 42});
        const int glyphWidth = scene.MeasureText(glyphs[index],
            index == 1 ? 1 : 2);
        scene.DrawText(62 - glyphWidth / 2, y + 28, glyphs[index], text,
            index == 1 ? 1 : 2);
        if (navigationFocused) {
            const int labelWidth = scene.MeasureText(labels[index], 2);
            scene.DrawText(196 - labelWidth / 2, y + 29, labels[index],
                index == activeTab ? text : muted, 2);
        }
    }
    if (navigationFocused) {
        scene.DrawText(55, 885, "Navigate", text, 2);
        scene.DrawText(55, 925, "UP / DOWN   Sections", muted, 1);
        scene.DrawText(55, 957, "RIGHT / CROSS   Open", muted, 1);
    }
}

void drawStickHint(Scene2D& scene, int x, int y, const char* label) {
    const Color panel = {35, 35, 46};
    const Color rim = {164, 158, 181};
    const Color text = {235, 232, 244};
    scene.DrawRoundedRectangle(x, y, 44, 44, 22, rim);
    scene.DrawRoundedRectangle(x + 5, y + 5, 34, 34, 17, panel);
    scene.DrawText(x + 12, y + 13, "L3", text, 1);
    scene.DrawText(x + 56, y + 8, label, text, 2);
}

void drawTopNavigation(Scene2D& scene, int activeTab) {
    const Color panel = {14, 14, 21};
    const Color selected = {123, 91, 214};
    const Color text = {235, 232, 244};
    const Color muted = {135, 130, 151};
    scene.DrawRectangle(0, 0, kWidth, 108, panel);
    drawBrand(scene, text);
    const int x[] = {520, 720, 920, 1240, 1450};
    for (int index = 0; index < kTopTabCount; ++index) {
        if (index == activeTab)
            scene.DrawRoundedRectangle(x[index] - 22, 25, 184, 58, 18,
                selected);
        scene.DrawText(x[index], 43, kTopTabs[index],
            index == activeTab ? text : muted, 2);
    }
}

void drawNavigation(Scene2D& scene, int activeTab) {
    if (useSideNavigation) drawSideNavigation(scene, activeTab);
    else drawTopNavigation(scene, activeTab);
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
    const Color background = {11, 10, 17};
    const Color header = {24, 22, 33};
    const Color stremioPurple = {123, 91, 214};
    const Color cardMuted = {35, 35, 46};
    const Color focus = {196, 174, 255};
    const Color text = {235, 232, 244};
    const Color mutedText = {164, 158, 181};

    scene.FrameBufferFill(background);
    const int cardX[] = {300, 700, 1100, 1500};
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
            // Keep focus stable. A flashing/pulsing border distracts from the
            // poster artwork and costs redraw work on every catalog frame.
            scene.DrawRoundedRectangle(x - 9, rowY - 9,
                cardWidth + 18, cardHeight + 18, 24, focus);
            scene.DrawRoundedRectangle(x + 16, rowY + 16, 112, 38, 19,
                stremioPurple);
            scene.DrawText(x + 34, rowY + 25, "SELECTED", text, 1);
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
    scene.DrawRectangle(250, 0, kWidth - 250, 240, background);
    scene.DrawVerticalFade(250, 0, kWidth - 250, 210, header, 220, 0);
    scene.DrawText(300, 40,
        catalogType == "series" ? "SERIES / POPULAR" :
        (catalogType == "publicdomain" ? "PUBLIC DOMAIN / FEATURED" :
        (activeTab == 3 ? "SEARCH / RESULTS" : "MOVIES / POPULAR")),
        stremioPurple, 2);
    const int selectedIndex = page * 4 + focusedCard;
    std::string featured = selectedIndex < static_cast<int>(items.size())
        ? items[selectedIndex].name : "Discover something to watch";
    if (featured.size() > 38) featured = featured.substr(0, 35) + "...";
    scene.DrawText(300, 82, featured.c_str(), text, 5);
    scene.DrawText(300, 155,
        "Browse with the D-pad or L3 stick", mutedText, 2);
    char pageText[64];
    snprintf(pageText, sizeof(pageText), "PAGE %d  /  %d TITLES", page + 1,
        static_cast<int>(items.size()));
    scene.DrawRoundedRectangle(1510, 46, 330, 62, 31, Color{35, 32, 47});
    scene.DrawText(1550, 66, pageText, mutedText, 2);
    scene.DrawText(300, 215, "Recommended for you", text, 3);
    scene.DrawText(300, 740, status.c_str(), mutedText, 2);
    scene.DrawVerticalFade(250, 930, kWidth - 250, 150, header, 0, 230);
    drawButtonHint(scene, 1370, 1020, 'S', "VIDEO TEST");
    drawButtonHint(scene, 1630, 1020, 'X', "DETAILS");
    drawStickHint(scene, 300, 1015, "NAVIGATE");
    drawNavigation(scene, activeTab);
}

void drawSearch(Scene2D& scene, const std::string& query, int filter,
    int indicatorX, int) {
    const Color background = {18, 18, 24};
    const Color header = {29, 29, 39};
    const Color purple = {123, 91, 214};
    const Color text = {235, 232, 244};
    const Color muted = {164, 158, 181};
    scene.FrameBufferFill(background);
    scene.DrawVerticalFade(250, 0, kWidth - 250, 210, header, 220, 0);
    scene.DrawText(300, 55, "DISCOVER", muted, 2);
    scene.DrawText(300, 100, "SEARCH", text, 5);
    scene.DrawText(300, 175,
        "SEARCH MOVIES, SERIES, OR THE PUBLIC DOMAIN COLLECTION", muted, 2);
    const char* filters[] = {"MOVIES", "SERIES", "PUBLIC DOMAIN"};
    const int filterWidths[] = {250, 250, 360};
    int filterX = 300;
    for (int index = 0; index < 3; ++index) {
        scene.DrawRoundedRectangle(filterX, 225, filterWidths[index], 62, 20,
            index == filter ? purple : header);
        scene.DrawText(filterX + 30, 244, filters[index],
            index == filter ? text : muted, 2);
        filterX += filterWidths[index] + 24;
    }
    scene.DrawRoundedRectangle(300, 325, 1500, 130, 30, header);
    scene.DrawRoundedRectangle(335, 355, 68, 68, 20, purple);
    scene.DrawText(356, 369, "?", text, 4);
    scene.DrawText(435, 347, "TITLE", muted, 2);
    const std::string shown = query.empty() ? "TYPE A TITLE..." : query + "_";
    scene.DrawText(435, 392, shown.c_str(), query.empty() ? muted : text, 3);
    scene.DrawRoundedRectangle(300, 505, 720, 250, 26, header);
    scene.DrawText(345, 550, "PS4 SYSTEM KEYBOARD", text, 3);
    scene.DrawText(345, 610, "CROSS   TYPE / SELECT", muted, 2);
    scene.DrawText(345, 650, "SQUARE  DELETE    TRIANGLE  SPACE", muted, 2);
    scene.DrawText(345, 690, "R2      SEARCH    CIRCLE    CLOSE", muted, 2);
    scene.DrawRoundedRectangle(1060, 505, 740, 250, 26, header);
    scene.DrawText(1105, 550, "SEARCH TIPS", text, 3);
    scene.DrawText(1105, 610, "LEFT / RIGHT CHANGES THE FILTER", muted, 2);
    scene.DrawText(1105, 650, "UP TO 24 RESULTS WITH CACHED POSTERS", muted, 2);
    scene.DrawText(1105, 690, "OPEN RESULTS FOR DETAILS AND STREAMS", muted, 2);
    scene.DrawVerticalFade(250, 930, kWidth - 250, 150, header, 0, 230);
    drawButtonHint(scene, 900, 1020, '<', "FILTER");
    drawButtonHint(scene, 1080, 1020, '>', "FILTER");
    drawButtonHint(scene, 1250, 1020, 'O', "CLEAR / BACK");
    drawButtonHint(scene, 1435, 1020, 'T', "SEARCH");
    drawButtonHint(scene, 1660, 1020, 'X', "KEYBOARD");
    drawStickHint(scene, 300, 1015, "NAVIGATE");
    drawNavigation(scene, 3);
}

void drawSettingsHeader(Scene2D& scene, int indicatorX, const char* title) {
    const Color background = {18, 18, 24};
    const Color header = {29, 29, 39};
    const Color purple = {123, 91, 214};
    const Color text = {235, 232, 244};
    const Color muted = {164, 158, 181};
    scene.FrameBufferFill(background);
    scene.DrawVerticalFade(250, 0, kWidth - 250, 210, header, 220, 0);
    scene.DrawText(300, 55, "STREMIO", muted, 2);
    scene.DrawText(300, 105, title, text, 5);
}

void drawSettingsRows(Scene2D& scene, const char* const* rows, int rowCount,
    int selected, int indicatorX, const char* title, const char* action) {
    const Color header = {29, 29, 39};
    const Color focus = {196, 174, 255};
    const Color text = {235, 232, 244};
    drawSettingsHeader(scene, indicatorX, title);
    scene.DrawRoundedRectangle(280, 245, 1540, 650, 30, header);
    for (int index = 0; index < rowCount; ++index) {
        const int y = 275 + index * 92;
        const bool focused = index == selected;
        scene.DrawRoundedRectangle(310, y, 1480, 72, 18,
            focused ? focus : Color{35, 35, 46});
        scene.DrawRoundedRectangle(330, y + 12, 48, 48, 14,
            focused ? Color{123, 91, 214} : Color{52, 52, 68});
        char number[8];
        snprintf(number, sizeof(number), "%02d", index + 1);
        scene.DrawText(338, y + 23, number, text, 2);
        scene.DrawText(410, y + 23, rows[index],
            focused ? Color{18, 18, 24} : text, 2);
        if (focused) {
            scene.DrawText(1710, y + 23, ">", Color{18, 18, 24}, 2);
        }
    }
    scene.DrawVerticalFade(0, 930, kWidth, 150, header, 0, 230);
    drawButtonHint(scene, 1390, 1020, 'O', "BACK");
    drawButtonHint(scene, 1640, 1020, 'X', action);
    drawStickHint(scene, 360, 1015, "NAVIGATE");
    drawNavigation(scene, 4);
}

void drawSettings(Scene2D& scene, int selected, int indicatorX) {
    const char* rows[] = {
        "STREMIO ACCOUNT AND SYNCED ADDONS",
        "PLAYBACK TESTS",
        "AUDIO OUTPUT TEST  48KHZ STEREO",
        "COMPANION SERVER  PC-IP:11470",
        "CLEAR SEARCH QUERY",
        useSideNavigation ? "NAVIGATION LAYOUT  LEFT SIDEBAR" :
            "NAVIGATION LAYOUT  CLASSIC TOP BAR",
        "ABOUT STREMIO  v3.60"};
    drawSettingsRows(scene, rows, 7, selected, indicatorX,
        "SETTINGS", "OPEN");
}

void drawAccount(Scene2D& scene, int indicatorX, const std::string& code,
        const std::string& link, const std::string& status,
        int addonCount, bool signedIn) {
    const Color panel = {29, 29, 39};
    const Color purple = {123, 91, 214};
    const Color text = {235, 232, 244};
    const Color muted = {164, 158, 181};
    drawSettingsHeader(scene, indicatorX, "STREMIO ACCOUNT");
    scene.DrawRoundedRectangle(300, 230, 1440, 650, 32, panel);
    scene.DrawText(360, 300, signedIn ? "ACCOUNT LINKED" :
        "LINK THIS PS4 TO YOUR STREMIO ACCOUNT", text, 4);
    if (!code.empty() && !signedIn) {
        scene.DrawText(360, 405, "OPEN ON YOUR PHONE OR COMPUTER", muted, 2);
        scene.DrawText(360, 455, link.c_str(), purple, 3);
        scene.DrawText(360, 535, "LINK CODE", muted, 2);
        scene.DrawText(360, 580, code.c_str(), text, 6);
    } else if (signedIn) {
        char addons[96];
        snprintf(addons, sizeof(addons), "%d SYNCED ADDON ENDPOINTS", addonCount);
        scene.DrawText(360, 430, addons, purple, 3);
        scene.DrawText(360, 500,
            "STREAM RESULTS WILL BE REQUESTED FROM YOUR ADDONS", text, 2);
    } else {
        scene.DrawText(360, 430,
            "PRESS CROSS TO GENERATE A SECURE LINK CODE", purple, 3);
    }
    scene.DrawText(360, 740, status.c_str(), muted, 2);
    scene.DrawVerticalFade(0, 930, kWidth, 150, panel, 0, 230);
    drawButtonHint(scene, 40, 1020, 'O', "BACK");
    drawButtonHint(scene, 1530, 1020, 'X',
        signedIn ? "SYNC ADDONS" : "LINK ACCOUNT");
    drawStickHint(scene, 360, 1015, "NAVIGATE");
    drawNavigation(scene, 4);
}

void drawPlayerSelection(Scene2D& scene, int selected, int indicatorX) {
    const char* rows[] = {
        "SONY AVPLAYER + RGB PREVIEW",
        "SONY AVPLAYER DECODE-ONLY",
        "SONY AVPLAYER PERFORMANCE / LEGACY API",
        "DIRECT VIDEODEC2 GPU / 1080P PLAYBACK"};
    drawSettingsRows(scene, rows, 4, selected, indicatorX,
        "PLAYBACK TESTS / PLAYER MODE", "SELECT");
}

void drawVideoDec2Test(Scene2D& scene, const VideoDec2Probe& decoder,
    const std::vector<uint32_t>& pixels, uint32_t previewWidth,
    uint32_t previewHeight) {
    const Color background = {8, 8, 12};
    const Color panel = {29, 29, 39};
    const Color purple = {123, 91, 214};
    const Color text = {235, 232, 244};
    const Color muted = {164, 158, 181};
    scene.FrameBufferFill(background);
    if (!pixels.empty() && previewWidth > 0 && previewHeight > 0)
        scene.BlitRgbScaled(0, 0, kWidth, kHeight, pixels.data(),
            previewWidth, previewHeight);
    scene.DrawVerticalFade(0, 0, kWidth, 220, panel, 235, 0);
    drawBrand(scene, text);
    scene.DrawText(120, 190, "DIRECT VIDEODEC2 GPU PLAYER", text, 4);
    char metrics[160];
    snprintf(metrics, sizeof(metrics),
        "OUTPUT %ux%u   AU %u   FRAMES %llu   DECODE %u.%u FPS",
        decoder.width(), decoder.height(),
        decoder.submittedAccessUnits(),
        static_cast<unsigned long long>(decoder.decodedFrames()),
        decoder.measuredFpsTimesTen() / 10,
        decoder.measuredFpsTimesTen() % 10);
    if (pixels.empty()) {
        scene.DrawRoundedRectangle(280, 315, 1360, 430, 34, panel);
        scene.DrawRoundedRectangle(330, 370, 120, 120, 30, purple);
        scene.DrawText(365, 405, "GPU", text, 3);
        scene.DrawText(505, 370,
            "H.264 BASELINE L4.0   1920x1080 / 24 FPS", text, 3);
        scene.DrawText(505, 430,
            "INITIALIZING DIRECT HARDWARE PLAYBACK...", muted, 2);
        scene.DrawRoundedRectangle(505, 555, 1035, 90, 22, background);
        scene.DrawText(545, 585, metrics, text, 2);
    } else {
        scene.DrawRoundedRectangle(420, 870, 1080, 74, 18, panel);
        scene.DrawText(470, 895, metrics, text, 2);
        const uint64_t duration = decoder.duration();
        const int progress = duration ? static_cast<int>(
            std::min<uint64_t>(1000, decoder.currentTime() * 1000 / duration)) : 0;
        scene.DrawRoundedRectangle(460, 960, 1000, 12, 6, panel);
        scene.DrawRoundedRectangle(460, 960, progress, 12, 6, purple);
    }
    scene.DrawVerticalFade(0, 930, kWidth, 150, panel, 0, 235);
    drawButtonHint(scene, 40, 1020, 'X',
        decoder.paused() ? "RESUME" : "PAUSE");
    drawButtonHint(scene, 310, 1020, 'T', "RESTART");
    drawButtonHint(scene, 1640, 1020, 'O', "STOP");
}

void drawQualitySelection(Scene2D& scene, int selected, int indicatorX,
    bool decodeOnly, bool legacyApi, bool directVideoDec2) {
    if (directVideoDec2) {
        const char* directRows[] = {
            "1080P BASELINE L4.0 / DIRECT GPU PLAYBACK"};
        drawSettingsRows(scene, directRows, 1, selected, indicatorX,
            "DIRECT VIDEODEC2 / SELECT TEST", "RUN");
        return;
    }
    const char* rows[] = {
        "LOCAL H.264  640x360",
        "LOCAL H.264  854x480",
        "LOCAL H.264  1280x720",
        "LOCAL H.264  1920x1080 BASELINE L4.0",
        "LOCAL H.264  1920x1080 HIGH L4.1",
        "CACHED HTTPS H.264  854x480"};
    drawSettingsRows(scene, rows, 6, selected, indicatorX,
        legacyApi ? "PERFORMANCE LEGACY API / SELECT QUALITY" :
        (decodeOnly ? "DECODE-ONLY / SELECT QUALITY" :
            "RGB PREVIEW / SELECT QUALITY"), "RUN");
}

void drawAbout(Scene2D& scene, int indicatorX) {
    const Color panel = {29, 29, 39};
    const Color purple = {123, 91, 214};
    const Color text = {235, 232, 244};
    const Color muted = {164, 158, 181};
    drawSettingsHeader(scene, indicatorX, "ABOUT STREMIO FOR PS4");
    scene.DrawRoundedRectangle(120, 270, 1680, 610, 30, panel);
    scene.DrawRoundedRectangle(165, 320, 330, 330, 36, purple);
    if (creatorAvatar.valid()) {
        scene.BlitRgbScaledRounded(180, 335, 300, 300, 34,
            creatorAvatar.pixels.data(), creatorAvatar.width,
            creatorAvatar.height);
    }
    scene.DrawText(560, 315, "TAHAR CHTIOUI", text, 4);
    scene.DrawText(560, 380, "GITHUB  @JAVASCRIPT-X", purple, 3);
    scene.DrawText(560, 455,
        "CREATOR, DEVELOPER AND REAL-HARDWARE TESTER", text, 2);
    scene.DrawText(560, 510,
        "A COMMUNITY HOMEBREW CLIENT BUILT FOR JAILBROKEN PS4", muted, 2);
    scene.DrawText(560, 565,
        "TEST PLATFORM  PS4 FIRMWARE 13.02 + GOLDHEN", muted, 2);
    scene.DrawText(560, 640,
        "GITHUB.COM/JAVASCRIPT-X/STREMIO-PS4", text, 2);
    scene.DrawText(560, 705,
        "STREMIO IS A TRADEMARK OF ITS RESPECTIVE OWNER.", muted, 2);
    scene.DrawText(560, 750,
        "THIS PROJECT IS INDEPENDENT AND OPEN SOURCE.", muted, 2);
    scene.DrawVerticalFade(0, 930, kWidth, 150, panel, 0, 230);
    drawButtonHint(scene, 1640, 1020, 'O', "BACK");
    drawStickHint(scene, 360, 1015, "NAVIGATE");
    drawNavigation(scene, 4);
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
    scene.DrawVerticalFade(0, 0, kWidth, 210, panel, 235, 0);
    drawBrand(scene, text);
    scene.DrawText(1450, 48,
        catalogType == "series" ? "SERIES DETAILS" : "MOVIE DETAILS",
        muted, 2);
    scene.DrawRoundedRectangle(90, 175, 430, 650, 30, panel);
    scene.DrawRoundedRectangle(124, 209, 322, 422, 22, purple);
    if (poster && poster->valid()) {
        scene.BlitRgbRounded(130, 215, poster->width, poster->height, 18,
            poster->pixels.data());
    }
    const std::string title = details.name.size() > 42
        ? details.name.substr(0, 39) + "..." : details.name;
    scene.DrawText(575, 200, title.c_str(), text, 4);
    int badgeX = 575;
    auto drawBadge = [&](const std::string& value, int width) {
        if (value.empty()) return;
        scene.DrawRoundedRectangle(badgeX, 280, width, 48, 16, panel);
        scene.DrawText(badgeX + 18, 294, value.c_str(), muted, 2);
        badgeX += width + 16;
    };
    drawBadge(catalogType == "series" ? "SERIES" : "MOVIE", 125);
    drawBadge(details.releaseInfo, 170);
    drawBadge(details.runtime, 170);
    if (!details.imdbRating.empty())
        drawBadge("IMDB " + details.imdbRating, 190);
    if (!details.genres.empty()) {
        scene.DrawText(575, 355, details.genres.c_str(), purple, 2);
    }
    scene.DrawText(575, 415, "OVERVIEW", muted, 2);
    const std::vector<std::string> lines =
        wrapText(details.description, 67, 10);
    for (size_t line = 0; line < lines.size(); ++line) {
        scene.DrawText(575, 465 + static_cast<int>(line) * 36,
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
        scene.DrawRoundedRectangle(575, 840, 1180, 72, 20, panel);
        scene.DrawRectangle(575, 840, 8, 72, purple);
        scene.DrawText(610, 863, episodeText, text, 2);
    }
    scene.DrawText(145, 675, "STREMIO METADATA", muted, 2);
    scene.DrawText(145, 720, "CACHED FOR FASTER REVISITS", muted, 2);
    scene.DrawVerticalFade(0, 930, kWidth, 150, panel, 0, 235);
    drawButtonHint(scene, 40, 1020, 'O', "BACK");
    drawStickHint(scene, 420, 1015, "NAVIGATE");
    drawButtonHint(scene, catalogType == "series" ? 1570 : 1630, 1020, 'X',
        catalogType == "series" ? "SELECT EPISODE" : "FIND STREAMS");
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
    scene.DrawVerticalFade(0, 0, kWidth, 210, panel, 235, 0);
    drawBrand(scene, text);
    scene.DrawText(120, 145, "CHOOSE A STREAM", text, 4);
    scene.DrawText(120, 205, details.name.c_str(), muted, 2);
    scene.DrawRoundedRectangle(1420, 175, 360, 70, 20, purple);
    char count[64];
    snprintf(count, sizeof(count), "%d SOURCES FOUND",
        static_cast<int>(streams.size()));
    scene.DrawText(1470, 197, count, text, 2);
    constexpr int visibleRows = 5;
    const int maximumStart = std::max(0,
        static_cast<int>(streams.size()) - visibleRows);
    const int firstVisible = std::min(maximumStart,
        std::max(0, focusedStream - visibleRows + 1));
    for (int row = 0; row < visibleRows; ++row) {
        const int index = firstVisible + row;
        if (index >= static_cast<int>(streams.size())) break;
        const int y = 270 + row * 126;
        const bool selected = index == focusedStream;
        scene.DrawRoundedRectangle(120, y, 1680, 108, 20,
            selected ? focus : panel);
        scene.DrawRoundedRectangle(145, y + 18, 58, 58, 16,
            selected ? purple : Color{52, 52, 68});
        char sourceNumber[8];
        snprintf(sourceNumber, sizeof(sourceNumber), "%02d", index + 1);
        scene.DrawText(158, y + 36, sourceNumber, text, 2);
        const StreamItem& stream = streams[index];
        // Addons commonly put resolution, codec, release, peers and size in
        // title, while name is only the provider badge.
        const std::string label = !stream.title.empty() ? stream.title :
            (!stream.fileName.empty() ? stream.fileName :
            (!stream.name.empty() ? stream.name : "STREAM"));
        const std::vector<std::string> labelLines = wrapText(label, 52, 2);
        if (!labelLines.empty()) scene.DrawText(230, y + 17,
            labelLines[0].c_str(), selected ? background : text, 2);
        if (labelLines.size() > 1) scene.DrawText(230, y + 49,
            labelLines[1].c_str(), selected ? Color{55, 50, 70} : muted, 1);
        std::string sizeText = "SIZE UNKNOWN";
        if (stream.videoSize > 0) {
            char size[48];
            if (stream.videoSize >= 1073741824ULL)
                snprintf(size, sizeof(size), "%llu.%01llu GB",
                    static_cast<unsigned long long>(stream.videoSize / 1073741824ULL),
                    static_cast<unsigned long long>((stream.videoSize % 1073741824ULL) * 10 / 1073741824ULL));
            else snprintf(size, sizeof(size), "%llu MB",
                static_cast<unsigned long long>(stream.videoSize / 1048576ULL));
            sizeText = size;
        }
        std::string torrentInfo = stream.url.empty() ? "TORRENT   " :
            "DIRECT HTTPS   ";
        torrentInfo += sizeText;
        if (stream.seeders >= 0)
            torrentInfo += "   SEEDS " + std::to_string(stream.seeders);
        if (stream.peers >= 0)
            torrentInfo += "   PEERS " + std::to_string(stream.peers);
        scene.DrawText(230, y + 78, torrentInfo.c_str(),
            selected ? Color{55, 50, 70} : muted, 1);
        scene.DrawText(1510, y + 39,
            stream.name.empty() ?
                (stream.url.empty() ? "COMPANION" : "DIRECT") :
                shortTitle(stream.name).c_str(),
            selected ? background : text, 2);
    }
    if (firstVisible > 0)
        scene.DrawText(1840, 290, "^", focus, 3);
    if (firstVisible + visibleRows < static_cast<int>(streams.size()))
        scene.DrawText(1840, 855, "v", focus, 3);
    char position[48];
    snprintf(position, sizeof(position), "%d / %d", focusedStream + 1,
        static_cast<int>(streams.size()));
    scene.DrawText(1720, 875, position, muted, 2);
    scene.DrawVerticalFade(0, 930, kWidth, 150, panel, 0, 235);
    drawButtonHint(scene, 40, 1020, 'O', "DETAILS");
    drawStickHint(scene, 420, 1015, "SELECT SOURCE");
    drawButtonHint(scene, 1250, 1020, 'S', "DOWNLOAD FULL");
    drawButtonHint(scene, 1580, 1020, 'X', "STREAM VIDEODEC2");
}

void drawStreamProgress(Scene2D& scene, int stage, uint64_t downloaded,
        uint64_t expected, uint64_t startedAt, int nativeError) {
    const Color background = {18, 18, 24};
    const Color panel = {35, 35, 46};
    const Color purple = {123, 91, 214};
    const Color focus = {196, 174, 255};
    const Color text = {235, 232, 244};
    const Color muted = {164, 158, 181};
    scene.FrameBufferFill(background);
    drawBrand(scene, text);
    scene.DrawRoundedRectangle(260, 240, 1400, 570, 34, panel);
    const char* stageName = stage <= 1 ? "CONNECTING TO COMPANION" :
        (stage == 2 ? "COMPANION IS RESOLVING TORRENT" :
        (stage == 3 ? "DOWNLOADING VIDEO TO PS4 CACHE" :
        (stage == 4 ? "PREPARING H.264 VIDEO" : "STARTING PLAYER")));
    scene.DrawText(360, 325, stageName, text, 4);
    const uint64_t elapsedUs = startedAt > 0
        ? sceKernelGetProcessTime() - startedAt : 0;
    const uint64_t speed = elapsedUs > 0
        ? downloaded * 1000000ULL / elapsedUs : 0;
    char transfer[192];
    if (expected > 0) {
        snprintf(transfer, sizeof(transfer),
            "%llu.%01llu MB / %llu.%01llu MB     %llu KB/s",
            static_cast<unsigned long long>(downloaded / 1048576),
            static_cast<unsigned long long>((downloaded % 1048576) * 10 / 1048576),
            static_cast<unsigned long long>(expected / 1048576),
            static_cast<unsigned long long>((expected % 1048576) * 10 / 1048576),
            static_cast<unsigned long long>(speed / 1024));
    } else {
        snprintf(transfer, sizeof(transfer),
            "%llu.%01llu MB RECEIVED     %llu KB/s     %llus ELAPSED",
            static_cast<unsigned long long>(downloaded / 1048576),
            static_cast<unsigned long long>((downloaded % 1048576) * 10 / 1048576),
            static_cast<unsigned long long>(speed / 1024),
            static_cast<unsigned long long>(elapsedUs / 1000000));
    }
    scene.DrawText(360, 445, transfer, focus, 3);
    scene.DrawRoundedRectangle(360, 535, 1200, 28, 14, background);
    int progress = 0;
    if (expected > 0)
        progress = static_cast<int>(std::min<uint64_t>(1200,
            downloaded * 1200 / expected));
    else
        progress = static_cast<int>((elapsedUs / 15000) % 260) + 180;
    scene.DrawRoundedRectangle(360, 535, progress, 28, 14, purple);
    scene.DrawText(360, 625,
        "FIRST PLAY MAY TAKE TIME WHILE THE TORRENT FINDS PEERS",
        muted, 2);
    if (nativeError != 0) {
        char error[64];
        snprintf(error, sizeof(error), "NETWORK CODE 0x%08x",
            static_cast<unsigned int>(nativeError));
        scene.DrawText(360, 680, error, muted, 2);
    }
    scene.DrawVerticalFade(0, 930, kWidth, 150, panel, 0, 235);
    drawButtonHint(scene, 40, 1020, 'O', "CANCEL");
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
    uint32_t sourceHeight,
    bool decodeOnly,
    bool legacyApi) {
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

    if (!decodeOnly) {
        scene.BlitRgb(startX, startY, width, height, pixels.data());
    } else {
        scene.DrawRoundedRectangle(560, 300, 800, 360, 30, track);
        scene.DrawText(700, 390, "SONY AVPLAYER", border, 4);
        scene.DrawText(675, 480, legacyApi ?
            "LEGACY HARDWARE FRAME API" : "HARDWARE DECODE-ONLY TEST",
            text, 3);
        scene.DrawText(650, 555,
            "RGB CONVERSION AND VIDEO BLIT DISABLED", border, 2);
    }
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

int playAudioOutputTest(int, int& failureStage) {
    failureStage = 1;
    static bool initialized = false;
    if (!initialized) {
        // Some HEN builds report 0x809b0001 when the internal PRX is already
        // resident. That loader status is not an AudioOut failure.
        // The OpenOrbis audio reference links AudioOut directly and does not
        // register its internal sysmodule first.
        // 13.xx HEN can return the loader-domain 0x809b0001 even while the
        // service is usable. sceAudioOutOpen supplies the authoritative error.
        sceAudioOutInit();
        initialized = true;
    }
    constexpr int frames = 256;
    constexpr int rate = 48000;
    const int handle = sceAudioOutOpen(ORBIS_USER_SERVICE_USER_ID_SYSTEM,
        ORBIS_AUDIO_OUT_PORT_TYPE_MAIN, 0, frames, rate,
        ORBIS_AUDIO_OUT_PARAM_FORMAT_S16_STEREO);
    if (handle <= 0) { failureStage = 2; return handle; }
    int16_t samples[frames * 2];
    double phase = 0.0;
    int result = 0;
    for (int block = 0; block < 188; ++block) {
        const double frequency = block < 94 ? 440.0 : 660.0;
        for (int frame = 0; frame < frames; ++frame) {
            const int16_t value = static_cast<int16_t>(
                std::sin(phase) * 9000.0);
            samples[frame * 2] = value;
            samples[frame * 2 + 1] = value;
            phase += 6.283185307179586 * frequency / rate;
            if (phase >= 6.283185307179586) phase -= 6.283185307179586;
        }
        sceAudioOutOutput(handle, nullptr);
        result = sceAudioOutOutput(handle, samples);
        if (result < 0) { failureStage = 3; break; }
    }
    sceAudioOutClose(handle);
    if (result >= 0) failureStage = 0;
    return result;
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

bool openSystemSearchKeyboard(int userId, std::string& query,
        bool companionMode = false) {
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
    static const uint16_t companionTitle[] = {
        'C','o','m','p','a','n','i','o','n',' ','s','e','r','v','e','r',0};
    static const uint16_t placeholder[] = {
        'M','o','v','i','e',' ','o','r',' ','s','e','r','i','e','s',' ',
        't','i','t','l','e',0};
    static const uint16_t companionPlaceholder[] = {
        '1','9','2','.','1','6','8','.','1','.','5','0',':','1','1','4','7','0',0};
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
    settings.placeholder = reinterpret_cast<const wchar_t*>(
        companionMode ? companionPlaceholder : placeholder);
    settings.title = reinterpret_cast<const wchar_t*>(
        companionMode ? companionTitle : title);
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

int postJson(const char* url, const std::string& json,
        size_t maximumBytes, std::string& body) {
    body.clear();
    if (!url || !initializeHttp()) return -1;
    int templateId = sceHttpCreateTemplate(httpContextId,
        "StremioPS4/3.10", ORBIS_HTTP_VERSION_1_1, 1);
    if (templateId < 0) return -2;
    sceHttpSetResolveTimeOut(templateId, kHttpTimeoutUsec);
    sceHttpSetConnectTimeOut(templateId, kHttpTimeoutUsec);
    sceHttpSetSendTimeOut(templateId, kHttpTimeoutUsec);
    int connectionId = sceHttpCreateConnectionWithURL(templateId, url, false);
    int requestId = connectionId >= 0 ? sceHttpCreateRequestWithURL(
        connectionId, ORBIS_METHOD_POST, url, json.size()) : -1;
    int result = -3;
    if (requestId >= 0) {
        sceHttpAddRequestHeader(requestId, "Content-Type",
            "application/json", 0);
        if (sceHttpSendRequest(requestId, json.data(), json.size()) >= 0) {
            int status = 0;
            if (sceHttpGetStatusCode(requestId, &status) >= 0 && status == 200) {
                char chunk[8192]; result = 0;
                for (;;) {
                    const int bytes = sceHttpReadData(requestId, chunk, sizeof(chunk));
                    if (bytes < 0) { result = -4; break; }
                    if (!bytes) { result = static_cast<int>(body.size()); break; }
                    if (body.size() + static_cast<size_t>(bytes) > maximumBytes) {
                        result = -5; break;
                    }
                    body.append(chunk, static_cast<size_t>(bytes));
                }
            } else if (status > 0) result = -status;
        }
    }
    if (requestId >= 0) sceHttpDeleteRequest(requestId);
    if (connectionId >= 0) sceHttpDeleteConnection(connectionId);
    sceHttpDeleteTemplate(templateId);
    return result;
}

bool jsonStringValue(const std::string& json, const std::string& key,
        std::string& value, size_t from = 0) {
    const std::string marker = "\"" + key + "\"";
    size_t at = json.find(marker, from);
    if (at == std::string::npos) return false;
    at = json.find(':', at + marker.size());
    if (at == std::string::npos) return false;
    at = json.find('"', at + 1);
    if (at == std::string::npos) return false;
    value.clear();
    for (++at; at < json.size(); ++at) {
        const char c = json[at];
        if (c == '"') return true;
        if (c == '\\' && at + 1 < json.size()) {
            const char escaped = json[++at];
            if (escaped == 'n') value.push_back('\n');
            else if (escaped == 'r') value.push_back('\r');
            else if (escaped == 't') value.push_back('\t');
            else value.push_back(escaped);
        } else value.push_back(c);
    }
    return false;
}

std::vector<std::string> addonTransportUrls(const std::string& json) {
    std::vector<std::string> urls;
    size_t position = 0;
    for (;;) {
        const size_t marker = json.find("\"transportUrl\"", position);
        if (marker == std::string::npos) break;
        std::string url;
        if (jsonStringValue(json, "transportUrl", url, marker) &&
            (url.compare(0, 8, "https://") == 0 ||
             url.compare(0, 7, "http://") == 0) &&
            std::find(urls.begin(), urls.end(), url) == urls.end()) {
            if (url.size() >= 14 &&
                url.compare(url.size() - 14, 14, "/manifest.json") == 0)
                url.erase(url.size() - 14);
            urls.push_back(url);
        }
        position = marker + 14;
    }
    return urls;
}

struct DownloadProgress {
    std::atomic<int> stage{0};
    std::atomic<uint64_t> downloaded{0};
    std::atomic<uint64_t> expected{0};
    std::atomic<int> nativeError{0};
    std::atomic<int> requestId{-1};
    std::atomic<bool> cancel{false};
    uint64_t startedAt = 0;
};

int downloadUrlToFile(const char* url, const std::string& path,
        size_t maximumBytes, DownloadProgress* progress = nullptr) {
    if (!url || !initializeHttp()) return -1;
    if (progress) {
        progress->stage.store(1, std::memory_order_release);
        progress->downloaded.store(0, std::memory_order_release);
        progress->expected.store(0, std::memory_order_release);
        progress->nativeError.store(0, std::memory_order_release);
        progress->requestId.store(-1, std::memory_order_release);
        progress->cancel.store(false, std::memory_order_release);
        progress->startedAt = sceKernelGetProcessTime();
    }
    const std::string temporary = path + ".part";
    FILE* output = fopen(temporary.c_str(), "wb");
    if (!output) return -2;
    int templateId = sceHttpCreateTemplate(httpContextId,
        "StremioPS4/3.00", ORBIS_HTTP_VERSION_1_1, 1);
    if (templateId < 0) { fclose(output); remove(temporary.c_str()); return -3; }
    sceHttpSetResolveTimeOut(templateId, kHttpTimeoutUsec);
    // A torrent companion may need substantially longer than an ordinary API
    // request to discover peers and make the first media file available.
    constexpr uint32_t streamTimeoutUsec = 120 * 1000 * 1000;
    sceHttpSetConnectTimeOut(templateId, streamTimeoutUsec);
    sceHttpSetSendTimeOut(templateId, streamTimeoutUsec);
    int connectionId = sceHttpCreateConnectionWithURL(templateId, url, false);
    int requestId = connectionId >= 0 ? sceHttpCreateRequestWithURL(
        connectionId, ORBIS_METHOD_GET, url, 0) : -1;
    if (progress) progress->requestId.store(
        requestId, std::memory_order_release);
    int result = -4;
    size_t total = 0;
    if (requestId < 0) {
        if (progress) progress->nativeError.store(
            connectionId < 0 ? connectionId : requestId,
            std::memory_order_release);
        result = -10;
    } else {
        if (progress) progress->stage.store(2, std::memory_order_release);
        const int sendResult = sceHttpSendRequest(requestId, nullptr, 0);
        if (sendResult < 0) {
            if (progress) progress->nativeError.store(
                sendResult, std::memory_order_release);
            result = -11;
        } else {
        int status = 0;
        if (sceHttpGetStatusCode(requestId, &status) >= 0 && status == 200) {
            if (progress) {
                int lengthType = 0;
                size_t contentLength = 0;
                if (sceHttpGetResponseContentLength(requestId, &lengthType,
                        &contentLength) >= 0 &&
                    lengthType == ORBIS_HTTP_CONTENTLEN_EXIST)
                    progress->expected.store(
                        contentLength, std::memory_order_release);
                progress->stage.store(3, std::memory_order_release);
            }
            // OpenOrbis worker threads have a much smaller default stack than
            // desktop pthreads. A previous 64 KiB local buffer exhausted it
            // as soon as a torrent-backed download started on real hardware.
            char chunk[8 * 1024];
            result = 0;
            for (;;) {
                if (progress && progress->cancel.load(
                        std::memory_order_acquire)) {
                    result = -13;
                    break;
                }
                const int bytes = sceHttpReadData(requestId, chunk, sizeof(chunk));
                if (bytes < 0) { result = -5; break; }
                if (!bytes) break;
                if (total + static_cast<size_t>(bytes) > maximumBytes ||
                    fwrite(chunk, 1, bytes, output) != static_cast<size_t>(bytes)) {
                    result = -6; break;
                }
                total += static_cast<size_t>(bytes);
                if (progress) progress->downloaded.store(
                    total, std::memory_order_release);
            }
            if (!total && result == 0) result = -7;
        } else if (status > 0) result = -status;
        else {
            if (progress) progress->nativeError.store(
                status, std::memory_order_release);
            result = -12;
        }
        }
    }
    if (requestId >= 0) sceHttpDeleteRequest(requestId);
    if (progress) progress->requestId.store(-1, std::memory_order_release);
    if (connectionId >= 0) sceHttpDeleteConnection(connectionId);
    sceHttpDeleteTemplate(templateId);
    if (fclose(output) != 0 && result == 0) result = -8;
    if (result == 0) {
        remove(path.c_str());
        if (rename(temporary.c_str(), path.c_str()) != 0) result = -9;
    }
    if (result != 0) remove(temporary.c_str());
    return result == 0 ? static_cast<int>(total) : result;
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

bool normalizeCompanionAddress(std::string& address) {
    while (!address.empty() && std::isspace(
            static_cast<unsigned char>(address.back()))) address.pop_back();
    size_t first = 0;
    while (first < address.size() && std::isspace(
            static_cast<unsigned char>(address[first]))) ++first;
    address.erase(0, first);
    const std::string http = "http://";
    if (address.compare(0, http.size(), http) == 0)
        address.erase(0, http.size());
    while (!address.empty() && address.back() == '/') address.pop_back();
    if (address.empty() || address.size() > 80) return false;
    for (unsigned char value : address) {
        if (!std::isalnum(value) && value != '.' && value != ':' &&
            value != '-') return false;
    }
    if (address.find(':') == std::string::npos) address += ":11470";
    return true;
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
    std::vector<CatalogItem>& items,
    size_t maximumItems = kCatalogBatchSize) {
    if (catalogType != "movie" && catalogType != "series" &&
        catalogType != "publicdomain") return -10;
    std::string url;
    if (!searchQuery.empty()) {
        if (catalogType == "publicdomain") {
            url = std::string(kPublicDomainBaseUrl) +
                "catalog/movie/publicdomainmovies/search=" +
                urlEncode(searchQuery) + ".json";
        } else {
            url = std::string("https://v3-cinemeta.strem.io/catalog/") +
                catalogType + "/top/search=" + urlEncode(searchQuery) + ".json";
        }
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
    return parseCatalogItems(body, items, maximumItems)
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

int fetchAddonStreams(
    const std::vector<std::string>& addonUrls,
    const std::string& type,
    const std::string& videoId,
    std::vector<StreamItem>& streams) {
    streams.clear();
    if ((type != "movie" && type != "series") ||
        !isSafeMetadataId(videoId)) return -10;
    int successfulAddons = 0;
    for (const std::string& base : addonUrls) {
        if (streams.size() >= 32) break;
        // Account collections can contain desktop-only local addons. Never
        // redirect a PS4 request to its own loopback address.
        if (base.find("localhost") != std::string::npos ||
            base.find("127.0.0.1") != std::string::npos ||
            base.find("[::1]") != std::string::npos) continue;
        const std::string url = base + "/stream/" + type + "/" +
            urlEncode(videoId) + ".json";
        std::string body;
        if (downloadUrl(url.c_str(), kMaximumCatalogBytes, body) <= 0)
            continue;
        std::vector<StreamItem> addonStreams;
        if (!parseStreamItems(body, addonStreams, 32 - streams.size()))
            continue;
        ++successfulAddons;
        streams.insert(streams.end(), addonStreams.begin(), addonStreams.end());
    }
    if (!streams.empty()) return static_cast<int>(streams.size());
    return successfulAddons ? -8 : -9;
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

struct StreamPrepareJob {
    pthread_t thread = {};
    std::atomic<bool> running{false};
    std::atomic<bool> completed{false};
    std::string url;
    std::string sourcePath;
    std::string annexBPath;
    int result = 0;
    bool sourceReused = false;
    bool demuxReused = false;
    Mp4MediaInfo media;
    DownloadProgress progress;
};

struct HlsSegmentJob {
    pthread_t thread = {};
    std::atomic<bool> running{false};
    std::atomic<bool> completed{false};
    bool active = false;
    bool segmentWaiting = false;
    int result = 0;
    int nextSegment = 0;
    int readySegment = -1;
    uint32_t samples = 0;
    std::string baseUrl;
    std::string outputPath;
    std::vector<std::string> segmentUrls;
    Fmp4VideoConfig config;
};

struct AccountSyncJob {
    pthread_t thread = {};
    std::atomic<bool> running{false};
    std::atomic<bool> completed{false};
    std::atomic<bool> codeReady{false};
    std::atomic<bool> cancel{false};
    std::string code;
    std::string link;
    std::string authKey;
    std::string addonJson;
    std::vector<std::string> addonUrls;
    int result = 0;
};

void* accountSyncEntry(void* argument) {
    AccountSyncJob* job = static_cast<AccountSyncJob*>(argument);
    job->result = 0;
    if (job->authKey.empty()) {
        std::string response;
        if (downloadUrl(kLinkCreateUrl, 64 * 1024, response) <= 0 ||
            !jsonStringValue(response, "code", job->code) ||
            !jsonStringValue(response, "link", job->link)) {
            job->result = 1;
        } else {
            job->codeReady.store(true, std::memory_order_release);
            for (int attempt = 0; attempt < 150 &&
                    !job->cancel.load(std::memory_order_acquire); ++attempt) {
                char url[256];
                snprintf(url, sizeof(url),
                    "https://link.stremio.com/api/v2/read?type=Read&code=%s",
                    job->code.c_str());
                response.clear();
                if (downloadUrl(url, 64 * 1024, response) > 0 &&
                    jsonStringValue(response, "authKey", job->authKey)) break;
                sceKernelUsleep(2000000);
            }
            if (job->authKey.empty() && !job->cancel.load(
                    std::memory_order_acquire)) job->result = 2;
        }
    }
    if (!job->authKey.empty() && !job->cancel.load(
            std::memory_order_acquire)) {
        const std::string body = "{\"authKey\":\"" + job->authKey +
            "\",\"update\":true,\"addFromURL\":[]}";
        if (postJson("https://api.strem.io/api/addonCollectionGet", body,
                4 * 1024 * 1024, job->addonJson) <= 0) {
            job->result = 3;
        } else {
            job->addonUrls = addonTransportUrls(job->addonJson);
            writeCachedFile(kAuthKeyPath, job->authKey);
            writeCachedFile(kAddonCollectionPath, job->addonJson);
        }
    }
    job->completed.store(true, std::memory_order_release);
    job->running.store(false, std::memory_order_release);
    return nullptr;
}

void* streamPrepareEntry(void* argument) {
    StreamPrepareJob* job = static_cast<StreamPrepareJob*>(argument);
    struct stat source = {}, elementary = {};
    job->sourceReused = stat(job->sourcePath.c_str(), &source) == 0 &&
        source.st_size > 16;
    if (!job->sourceReused) {
        const int downloaded = downloadUrlToFile(job->url.c_str(),
            job->sourcePath, kMaximumStreamBytes, &job->progress);
        if (downloaded < 0) job->result = downloaded;
    } else {
        job->progress.startedAt = sceKernelGetProcessTime();
        job->progress.stage.store(4, std::memory_order_release);
    }
    job->demuxReused = job->result == 0 &&
        stat(job->annexBPath.c_str(), &elementary) == 0 &&
        elementary.st_size > 16 && job->sourceReused;
    if (job->result == 0 && !job->demuxReused) {
        job->progress.stage.store(4, std::memory_order_release);
        const int demux = demuxMp4AvcToAnnexB(
            job->sourcePath, job->annexBPath, job->media);
        if (demux != 0) job->result = 100 + demux;
    }
    if (job->result == 0)
        job->progress.stage.store(5, std::memory_order_release);
    job->completed.store(true, std::memory_order_release);
    job->running.store(false, std::memory_order_release);
    return nullptr;
}

void* hlsSegmentEntry(void* argument) {
    HlsSegmentJob* job = static_cast<HlsSegmentJob*>(argument);
    job->result = 0;
    if (job->segmentUrls.empty()) {
        std::string playlist;
        int playlistResult = -1;
        for (int attempt = 0; attempt < 6 && job->active; ++attempt) {
            playlist.clear();
            playlistResult = downloadUrl(
                (job->baseUrl + "video0.m3u8").c_str(), 1024 * 1024,
                playlist);
            if (playlistResult > 0) break;
            sceKernelUsleep(2000000);
        }
        if (playlistResult <= 0) {
            job->result = 11;
        } else {
            size_t position = 0;
            while (position < playlist.size()) {
                size_t end = playlist.find('\n', position);
                if (end == std::string::npos) end = playlist.size();
                std::string line = playlist.substr(position, end - position);
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (!line.empty() && line[0] != '#' &&
                    line.find("segment") != std::string::npos)
                    job->segmentUrls.push_back(job->baseUrl + line);
                position = end + 1;
            }
            std::string init;
            if (job->segmentUrls.empty() ||
                downloadUrl((job->baseUrl + "video0/init.mp4").c_str(),
                    1024 * 1024, init) <= 0 ||
                !parseFmp4VideoConfig(init, job->config)) job->result = 12;
        }
    }
    if (job->result == 0 && job->nextSegment >= 0 &&
        job->nextSegment < static_cast<int>(job->segmentUrls.size())) {
        std::string segment;
        if (downloadUrl(job->segmentUrls[job->nextSegment].c_str(),
                32 * 1024 * 1024, segment) <= 0) {
            job->result = 13;
        } else {
            job->outputPath = job->nextSegment % 2
                ? "/data/stremio-hls-b.h264"
                : "/data/stremio-hls-a.h264";
            if (!convertFmp4VideoSegment(segment, job->config,
                    job->outputPath, job->samples)) job->result = 14;
            else job->readySegment = job->nextSegment++;
        }
    } else if (job->result == 0) {
        job->result = 5;
    }
    job->completed.store(true, std::memory_order_release);
    job->running.store(false, std::memory_order_release);
    return nullptr;
}

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
    int pad = initializeController(userId);
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
    VideoDec2Probe videoDec2;
    AvPlayerProbe::State previousPlayerState = AvPlayerProbe::State::Idle;
    int avPlayerProbeFrames = 0;
    bool previewVisible = false;
    int previewBackgroundFrames = 0;
    uint64_t playbackWallStart = 0;
    bool playbackTimingReported = false;
    std::vector<uint32_t> previewPixels;
    uint32_t previewWidth = 0;
    uint32_t previewHeight = 0;
    uint32_t playbackSourceWidth = 0;
    uint32_t playbackSourceHeight = 0;
    std::vector<uint32_t> directPreviewPixels;
    uint32_t directPreviewWidth = 0;
    uint32_t directPreviewHeight = 0;
    uint64_t staticScreenKey = ~0ull;
    int staticScreenFrames = 2;
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
    int settingsPage = 0;
    bool playbackDecodeOnly = false;
    bool playbackLegacyApi = false;
    bool playbackDirectVideoDec2 = true;
    bool activePlaybackDecodeOnly = false;
    bool activePlaybackLegacyApi = false;
    int queuedPlaybackTest = -1;
    bool searchShowingResults = false;
    std::string searchQuery;
    int searchFilter = 0;
    bool detailVisible = false;
    bool streamVisible = false;
    bool progressiveStreamOpening = false;
    uint64_t progressiveStreamStartedAt = 0;
    int detailPosterIndex = -1;
    int detailEpisodeIndex = 0;
    int focusedStream = 0;
    MetaDetails details;
    std::vector<StreamItem> streams;
    std::string companionAddress;
    readCachedFile(kCompanionConfigPath, 96, companionAddress);
    normalizeCompanionAddress(companionAddress);
    std::string navigationConfig;
    if (readCachedFile(kNavigationConfigPath, 16, navigationConfig))
        useSideNavigation = navigationConfig != "top";
    AccountSyncJob accountJob;
    std::string authKey;
    std::string addonCollectionJson;
    std::string accountStatus = "NOT LINKED";
    readCachedFile(kAuthKeyPath, 4096, authKey);
    readCachedFile(kAddonCollectionPath, 4 * 1024 * 1024,
        addonCollectionJson);
    std::vector<std::string> addonUrls =
        addonTransportUrls(addonCollectionJson);
    if (!authKey.empty()) {
        char status[96];
        snprintf(status, sizeof(status), "LINKED   %d ADDONS CACHED",
            static_cast<int>(addonUrls.size()));
        accountStatus = status;
    }
    scene.SetActiveFrameBuffer(0);
    loadBundledImage(
        "/app0/assets/stremio-official.png", 96, 96, headerLogo);
    loadBundledImage(
        "/app0/assets/javascript-x-avatar.png", 300, 300, creatorAvatar);

    CatalogLoadJob catalogJobs[3];
    CatalogPageJob pageJob;
    StreamPrepareJob streamJob;
    HlsSegmentJob hlsJob;
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
            accountJob.running.load(std::memory_order_acquire) ||
            catalogWorkersBusy() || catalogItems.empty() ||
            avPlayer.state() != AvPlayerProbe::State::Idle) return;
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

    auto startNextHlsSegment = [&]() {
        if (!hlsJob.active || hlsJob.running.load(std::memory_order_acquire) ||
            hlsJob.segmentWaiting) return false;
        hlsJob.result = 0;
        hlsJob.completed.store(false, std::memory_order_release);
        hlsJob.running.store(true, std::memory_order_release);
        pthread_attr_t attributes;
        pthread_attr_init(&attributes);
        pthread_attr_setstacksize(&attributes, 512 * 1024);
        const int result = pthread_create(
            &hlsJob.thread, &attributes, hlsSegmentEntry, &hlsJob);
        pthread_attr_destroy(&attributes);
        if (result != 0) hlsJob.running.store(false, std::memory_order_release);
        return result == 0;
    };

    auto startAccountSync = [&]() {
        if (accountJob.running.load(std::memory_order_acquire)) return;
        if (accountJob.completed.load(std::memory_order_acquire)) {
            pthread_join(accountJob.thread, nullptr);
            accountJob.completed.store(false, std::memory_order_release);
        }
        stopBackgroundForPlayback();
        accountJob.code.clear();
        accountJob.link.clear();
        accountJob.authKey = authKey;
        accountJob.addonJson.clear();
        accountJob.addonUrls.clear();
        accountJob.result = 0;
        accountJob.cancel.store(false, std::memory_order_release);
        accountJob.codeReady.store(false, std::memory_order_release);
        accountJob.completed.store(false, std::memory_order_release);
        accountJob.running.store(true, std::memory_order_release);
        accountStatus = authKey.empty()
            ? "CREATING SECURE LINK CODE..."
            : "REFRESHING ADDON COLLECTION...";
        pthread_attr_t attributes;
        pthread_attr_init(&attributes);
        pthread_attr_setstacksize(&attributes, 512 * 1024);
        if (pthread_create(&accountJob.thread, &attributes,
                accountSyncEntry, &accountJob) != 0) {
            accountJob.running.store(false, std::memory_order_release);
            accountStatus = "ACCOUNT WORKER COULD NOT START";
        }
        pthread_attr_destroy(&attributes);
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
        if (accountJob.running.load(std::memory_order_acquire)) return false;
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
        const char* searchTypes[] = {"movie", "series", "publicdomain"};
        const std::string selectedType = searchTypes[searchFilter];
        int result = 0;
        if (selectedType == "publicdomain") {
            std::vector<CatalogItem> candidates;
            result = fetchCatalogPage(
                selectedType, 0, "", candidates, 64);
            std::string needle = searchQuery;
            std::transform(needle.begin(), needle.end(), needle.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            catalogItems.clear();
            for (const CatalogItem& candidate : candidates) {
                std::string title = candidate.name;
                std::transform(title.begin(), title.end(), title.begin(),
                    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (title.find(needle) != std::string::npos) {
                    catalogItems.push_back(candidate);
                    if (catalogItems.size() == 24) break;
                }
            }
            result = static_cast<int>(catalogItems.size());
        } else {
            result = fetchCatalogPage(
                selectedType, 0, searchQuery, catalogItems, 24);
        }
        if (result > 0) {
            const int posterCount = fetchPosters(catalogItems, catalogPosters);
            char status[128];
            snprintf(status, sizeof(status),
                "SEARCH: %s   %d RESULTS   %d POSTERS",
                searchQuery.c_str(), result, posterCount);
            catalogStatus = status;
            catalogPage = 0;
            focusedCard = 0;
            catalogType = selectedType;
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
        settingsPage = 0;
        settingsSelection = 0;
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
        if (hlsJob.completed.exchange(false, std::memory_order_acq_rel)) {
            pthread_join(hlsJob.thread, nullptr);
            if (hlsJob.active && hlsJob.result == 0) {
                const VideoDec2Probe::State state = videoDec2.state();
                if (state == VideoDec2Probe::State::Idle ||
                    state == VideoDec2Probe::State::Finished) {
                    videoDec2.start(hlsJob.outputPath.c_str());
                    hlsJob.segmentWaiting = false;
                    startNextHlsSegment();
                } else {
                    hlsJob.segmentWaiting = true;
                }
            } else if (hlsJob.active) {
                if (hlsJob.result == 11)
                    notify("Stremio: torrent unresolved - no peers responded; try another source");
                else if (hlsJob.result == 12)
                    notify("Stremio: stream codec is not H.264/AVC; download or choose another source");
                else if (hlsJob.result == 13)
                    notify("Stremio: segment network error; try another source");
                else if (hlsJob.result == 14)
                    notify("Stremio: unsupported fragmented MP4/AVC layout");
                else {
                    char failure[96];
                    snprintf(failure, sizeof(failure),
                        "Stremio: Videodec2 stream stage %d", hlsJob.result);
                    notify(failure);
                }
                hlsJob.active = false;
            }
        }
        if (hlsJob.active && hlsJob.segmentWaiting &&
            videoDec2.state() == VideoDec2Probe::State::Finished) {
            videoDec2.start(hlsJob.outputPath.c_str());
            hlsJob.segmentWaiting = false;
            startNextHlsSegment();
        }
        if (hlsJob.active &&
            videoDec2.state() == VideoDec2Probe::State::Failed) {
            hlsJob.active = false;
            hlsJob.segmentWaiting = false;
        }
        if (accountJob.codeReady.exchange(false,
                std::memory_order_acq_rel)) {
            accountStatus = "WAITING FOR ACCOUNT APPROVAL...";
            notify("Stremio: link code ready in Settings / Account");
            staticScreenKey = ~0ull;
        }
        if (accountJob.completed.exchange(false,
                std::memory_order_acq_rel)) {
            pthread_join(accountJob.thread, nullptr);
            if (accountJob.result == 0 && !accountJob.authKey.empty()) {
                authKey = accountJob.authKey;
                addonCollectionJson = accountJob.addonJson;
                addonUrls = accountJob.addonUrls;
                char status[96];
                snprintf(status, sizeof(status),
                    "SYNC COMPLETE   %d ADDON ENDPOINTS",
                    static_cast<int>(addonUrls.size()));
                accountStatus = status;
                notify("Stremio: account and addons synchronized");
            } else if (accountJob.cancel.load(std::memory_order_acquire)) {
                accountStatus = "ACCOUNT LINK CANCELLED";
            } else {
                char status[96];
                snprintf(status, sizeof(status),
                    "ACCOUNT SYNC FAILED AT STAGE %d", accountJob.result);
                accountStatus = status;
                notify("Stremio: account synchronization failed");
            }
            staticScreenKey = ~0ull;
        }
        if (streamJob.completed.exchange(false, std::memory_order_acq_rel)) {
            pthread_join(streamJob.thread, nullptr);
            if (streamJob.progress.cancel.load(std::memory_order_acquire)) {
                notify("Stremio: stream preparation cancelled");
            } else if (streamJob.result == 0) {
                char ready[160];
                if (streamJob.media.width && streamJob.media.height) {
                    snprintf(ready, sizeof(ready),
                        "Stremio: cached H.264 %ux%u at %u.%02u fps",
                        streamJob.media.width, streamJob.media.height,
                        streamJob.media.fpsTimes100 / 100,
                        streamJob.media.fpsTimes100 % 100);
                } else {
                    snprintf(ready, sizeof(ready),
                        "Stremio: using cached H.264 elementary stream");
                }
                notify(ready);
                streamVisible = false;
                if (!videoDec2.start(streamJob.annexBPath.c_str()))
                    notify("Stremio: Videodec2 worker could not start");
            } else if (streamJob.result >= 100) {
                char failure[128];
                snprintf(failure, sizeof(failure),
                    "Stremio: unsupported MP4/AVC, demux stage %d",
                    streamJob.result - 100);
                notify(failure);
            } else {
                char failure[128];
                const int nativeError = streamJob.progress.nativeError.load(
                    std::memory_order_acquire);
                snprintf(failure, sizeof(failure),
                    "Stremio: stream failed stage %d code 0x%08x",
                    -streamJob.result,
                    static_cast<unsigned int>(nativeError));
                notify(failure);
            }
        }
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
            if (hlsJob.active) {
                hlsJob.active = false;
                hlsJob.segmentWaiting = false;
                videoDec2.stop();
            } else if (videoDec2.state() != VideoDec2Probe::State::Idle) {
                videoDec2.stop();
            } else if (previewVisible ||
                avPlayer.state() != AvPlayerProbe::State::Idle) {
                DEBUGLOG << "Options pressed during playback; returning to shell";
                avPlayer.requestStop();
                previewVisible = false;
                progressiveStreamOpening = false;
            } else {
                DEBUGLOG << "Options pressed from shell; Home API disabled";
            }
        }

        if (videoDec2.state() != VideoDec2Probe::State::Idle) {
            if ((pressed & ORBIS_PAD_BUTTON_CROSS) != 0)
                videoDec2.togglePause();
            if ((pressed & ORBIS_PAD_BUTTON_TRIANGLE) != 0)
                videoDec2.restart();
        }

        const bool shellVisible =
            !previewVisible && !detailVisible && !streamVisible &&
            videoDec2.state() == VideoDec2Probe::State::Idle;
        const bool catalogScreen = activeTab < 3 ||
            (activeTab == 3 && searchShowingResults);
        if (shellVisible && !navigationFocused &&
            (pressed & ORBIS_PAD_BUTTON_R1) != 0) {
            selectTab(activeTab + 1);
        } else if (shellVisible && !navigationFocused &&
            (pressed & ORBIS_PAD_BUTTON_L1) != 0) {
            selectTab(activeTab - 1);
        }

        if (shellVisible && useSideNavigation) {
            const bool canEnterRail = catalogScreen ? focusedCard == 0 : true;
            if (!navigationFocused && canEnterRail &&
                (pressed & ORBIS_PAD_BUTTON_LEFT) != 0) {
                navigationFocused = true;
                staticScreenKey = ~0ull;
            } else if (navigationFocused) {
                if ((pressed & ORBIS_PAD_BUTTON_UP) != 0)
                    selectTab(activeTab - 1);
                if ((pressed & ORBIS_PAD_BUTTON_DOWN) != 0)
                    selectTab(activeTab + 1);
                if ((pressed & (ORBIS_PAD_BUTTON_RIGHT |
                        ORBIS_PAD_BUTTON_CROSS)) != 0) {
                    navigationFocused = false;
                    staticScreenKey = ~0ull;
                }
            }
        } else if (!useSideNavigation) {
            navigationFocused = false;
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

        if (shellVisible && !navigationFocused && activeTab == 3 &&
            !searchShowingResults) {
            if ((pressed & ORBIS_PAD_BUTTON_LEFT) != 0)
                searchFilter = (searchFilter + 2) % 3;
            if ((pressed & ORBIS_PAD_BUTTON_RIGHT) != 0)
                searchFilter = (searchFilter + 1) % 3;
            if ((pressed & ORBIS_PAD_BUTTON_CROSS) != 0) {
                // The same Cross press must not leak into the native IME and
                // immediately accept/close it. Wait for a clean release first.
                while ((readButtons(pad) & ORBIS_PAD_BUTTON_CROSS) != 0 &&
                    !exitRequested) sceKernelUsleep(8000);
                // Give Sony's IME exclusive ownership of the controller. On
                // tested homebrew environments, retaining our open pad handle
                // made the dialog receive Circle as select and Cross as close.
                if (pad >= 0) {
                    scePadClose(pad);
                    pad = -1;
                }
                if (openSystemSearchKeyboard(userId, searchQuery)) {
                    loadSearchResults();
                }
                pad = scePadOpen(userId, 0, 0, nullptr);
                previousButtons = readButtons(pad);
            }
            if ((pressed & ORBIS_PAD_BUTTON_CIRCLE) != 0) searchQuery.clear();
            if ((pressed & ORBIS_PAD_BUTTON_TRIANGLE) != 0)
                loadSearchResults();
        } else if (shellVisible && !navigationFocused && catalogScreen) {
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
        } else if (shellVisible && !navigationFocused && activeTab == 4) {
            const int maximumSelection = settingsPage == 0 ? 6 :
                (settingsPage == 1 ? 3 : (settingsPage == 2 ?
                    (playbackDirectVideoDec2 ? 0 : 5) : 0));
            if ((pressed & ORBIS_PAD_BUTTON_UP) != 0 && settingsSelection > 0)
                --settingsSelection;
            if ((pressed & ORBIS_PAD_BUTTON_DOWN) != 0 &&
                settingsSelection < maximumSelection)
                ++settingsSelection;
            if ((pressed & ORBIS_PAD_BUTTON_CIRCLE) != 0 && settingsPage != 0) {
                settingsPage = settingsPage == 2 ? 1 : 0;
                settingsSelection = 0;
            } else if ((pressed & ORBIS_PAD_BUTTON_CROSS) != 0) {
                if (settingsPage == 0 && settingsSelection == 0) {
                    settingsPage = 4;
                    settingsSelection = 0;
                } else if (settingsPage == 0 && settingsSelection == 1) {
                    settingsPage = 1;
                    // Direct Videodec2 is the validated 1080p default.
                    settingsSelection = 3;
                } else if (settingsPage == 0 && settingsSelection == 2) {
                    notify("Stremio: playing 48 kHz stereo audio test");
                    int audioFailureStage = 0;
                    const int audioResult = playAudioOutputTest(
                        userId, audioFailureStage);
                    if (audioResult < 0) {
                        char failure[96];
                        snprintf(failure, sizeof(failure),
                            "Stremio: audio stage %d failed 0x%08x",
                            audioFailureStage,
                            static_cast<unsigned int>(audioResult));
                        notify(failure);
                    } else {
                        notify("Stremio: stereo audio output test passed");
                    }
                } else if (settingsPage == 0 && settingsSelection == 3) {
                    while ((readButtons(pad) & ORBIS_PAD_BUTTON_CROSS) != 0 &&
                        !exitRequested) sceKernelUsleep(8000);
                    if (pad >= 0) { scePadClose(pad); pad = -1; }
                    std::string entered = companionAddress;
                    const bool accepted = openSystemSearchKeyboard(
                        userId, entered, true);
                    if (accepted && normalizeCompanionAddress(entered)) {
                        companionAddress = entered;
                        writeCachedFile(kCompanionConfigPath, companionAddress);
                        notify("Stremio: companion server address saved");
                    } else if (accepted) {
                        notify("Stremio: invalid companion address");
                    }
                    pad = scePadOpen(userId, 0, 0, nullptr);
                    previousButtons = readButtons(pad);
                } else if (settingsPage == 0 && settingsSelection == 4) {
                    searchQuery.clear();
                    searchShowingResults = false;
                    notify("Stremio: search query cleared");
                } else if (settingsPage == 0 && settingsSelection == 5) {
                    useSideNavigation = !useSideNavigation;
                    navigationFocused = false;
                    writeCachedFile(kNavigationConfigPath,
                        useSideNavigation ? "side" : "top");
                    notify(useSideNavigation
                        ? "Stremio: left sidebar navigation saved"
                        : "Stremio: classic top navigation saved");
                    staticScreenKey = ~0ull;
                } else if (settingsPage == 0 && settingsSelection == 6) {
                    settingsPage = 3;
                    settingsSelection = 0;
                } else if (settingsPage == 4) {
                    startAccountSync();
                } else if (settingsPage == 1) {
                    playbackDecodeOnly = settingsSelection != 0;
                    playbackLegacyApi = settingsSelection == 2;
                    playbackDirectVideoDec2 = settingsSelection == 3;
                    settingsPage = 2;
                    settingsSelection = 0;
                } else if (settingsPage == 2) {
                    queuedPlaybackTest = settingsSelection;
                }
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
            !navigationFocused && !detailVisible && !streamVisible &&
            catalogScreen) {
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
            !streamVisible) {
            std::string streamType = catalogType == "series"
                ? "series" : "movie";
            std::string streamId = details.id;
            if (catalogType == "series") {
                if (details.episodes.empty()) {
                    notify("Stremio: no episodes available");
                    continue;
                }
                streamId = details.episodes[detailEpisodeIndex].id;
            }
            int streamResult = -9;
            if (catalogType == "publicdomain")
                streamResult = fetchPublicDomainStreams(streamId, streams);
            else if (!addonUrls.empty())
                streamResult = fetchAddonStreams(
                    addonUrls, streamType, streamId, streams);
            if (streamResult > 0) {
                focusedStream = 0;
                streamVisible = true;
            } else if (addonUrls.empty() && catalogType != "publicdomain") {
                notify("Stremio: link account and sync addons in Settings");
            } else {
                char failure[96];
                snprintf(failure, sizeof(failure),
                    "Stremio: stream lookup failed at stage %d", -streamResult);
                notify(failure);
            }
        } else if ((pressed & (ORBIS_PAD_BUTTON_CROSS |
                    ORBIS_PAD_BUTTON_SQUARE)) != 0 && streamVisible &&
            focusedStream < static_cast<int>(streams.size())) {
            const StreamItem& stream = streams[focusedStream];
            const bool cacheRequested =
                (pressed & ORBIS_PAD_BUTTON_SQUARE) != 0;
            std::string resolvedUrl = stream.url;
            std::string cacheKey = stream.url;
            if (resolvedUrl.empty() && !stream.infoHash.empty() &&
                !companionAddress.empty()) {
                char endpoint[256];
                snprintf(endpoint, sizeof(endpoint), "http://%s/%s/%d",
                    companionAddress.c_str(), stream.infoHash.c_str(),
                    stream.fileIndex >= 0 ? stream.fileIndex : -1);
                resolvedUrl = endpoint;
                cacheKey = stream.infoHash + ":" +
                    std::to_string(stream.fileIndex);
            }
            if (resolvedUrl.empty() && !stream.infoHash.empty()) {
                notify("Stremio: set Companion Server in Settings first");
            } else if (resolvedUrl.compare(0, 8, "https://") == 0 ||
                resolvedUrl.compare(0, 7, "http://") == 0) {
                if (!cacheRequested && !stream.infoHash.empty()) {
                    stopBackgroundForPlayback();
                    avPlayer.stop();
                    videoDec2.stop();
                    previewVisible = false;
                    hlsJob.active = true;
                    hlsJob.segmentWaiting = false;
                    hlsJob.nextSegment = 0;
                    hlsJob.readySegment = -1;
                    hlsJob.segmentUrls.clear();
                    hlsJob.config = {};
                    char hlsBase[256];
                    snprintf(hlsBase, sizeof(hlsBase),
                        "http://%s/%s/%d/", companionAddress.c_str(),
                        stream.infoHash.c_str(),
                        stream.fileIndex >= 0 ? stream.fileIndex : -1);
                    hlsJob.baseUrl = hlsBase;
                    streamVisible = false;
                    progressiveStreamStartedAt = sceKernelGetProcessTime();
                    notify("Stremio: buffering Videodec2 stream segment...");
                    if (!startNextHlsSegment()) {
                        hlsJob.active = false;
                        notify("Stremio: segmented stream worker failed");
                    }
                } else if (streamJob.running.load(std::memory_order_acquire)) {
                    notify("Stremio: stream is already downloading");
                } else {
                    stopBackgroundForPlayback();
                    streamJob.url = resolvedUrl;
                    streamJob.sourcePath = cachePath("media", cacheKey, "mp4");
                    streamJob.annexBPath = cachePath("video", cacheKey, "h264");
                    streamJob.result = 0;
                    streamJob.media = {};
                    streamJob.progress.stage.store(0,
                        std::memory_order_release);
                    streamJob.progress.downloaded.store(0,
                        std::memory_order_release);
                    streamJob.progress.expected.store(0,
                        std::memory_order_release);
                    streamJob.progress.nativeError.store(0,
                        std::memory_order_release);
                    streamJob.progress.startedAt = sceKernelGetProcessTime();
                    streamJob.completed.store(false, std::memory_order_release);
                    streamJob.running.store(true, std::memory_order_release);
                    pthread_attr_t streamAttributes;
                    pthread_attr_init(&streamAttributes);
                    pthread_attr_setstacksize(&streamAttributes, 512 * 1024);
                    const int threadResult = pthread_create(
                        &streamJob.thread, &streamAttributes,
                        streamPrepareEntry, &streamJob);
                    pthread_attr_destroy(&streamAttributes);
                    if (threadResult != 0) {
                        streamJob.running.store(false, std::memory_order_release);
                        notify("Stremio: stream worker could not start");
                    } else {
                        notify(stream.infoHash.empty()
                            ? "Stremio: caching HTTPS stream in background..."
                            : "Stremio: companion is resolving torrent...");
                    }
                }
            } else {
                notify("Stremio: rejected unsupported stream URL");
            }
        }
        if (!previewVisible && activeTab < 3 &&
            (pressed & ORBIS_PAD_BUTTON_TRIANGLE) != 0) {
            activateCatalog(activeTab, true);
        }
        const bool localPlaybackRequested =
            ((pressed & ORBIS_PAD_BUTTON_SQUARE) != 0 && catalogScreen &&
                !streamVisible && !playbackDirectVideoDec2) ||
            (queuedPlaybackTest >= 0 && queuedPlaybackTest <= 4 &&
                !playbackDirectVideoDec2);
        if (localPlaybackRequested) {
            stopBackgroundForPlayback();
            previewVisible = false;
            previewBackgroundFrames = 0;
            playbackWallStart = 0;
            playbackTimingReported = false;
            playbackSourceWidth = 0;
            playbackSourceHeight = 0;
            const int testIndex = queuedPlaybackTest >= 0
                ? queuedPlaybackTest : 1;
            char opening[96];
            snprintf(opening, sizeof(opening),
                "Stremio test: opening Sintel %s H.264",
                kVideoTestNames[testIndex]);
            notify(opening);
            avPlayerProbeFrames = 0;
            activePlaybackDecodeOnly = queuedPlaybackTest >= 0 &&
                playbackDecodeOnly;
            activePlaybackLegacyApi = queuedPlaybackTest >= 0 &&
                playbackLegacyApi;
            const bool renderPreview = !activePlaybackDecodeOnly;
            if (!avPlayer.start(kVideoTestPaths[testIndex], renderPreview,
                    activePlaybackLegacyApi)) {
                char result[128];
                snprintf(result, sizeof(result),
                    "Stremio: AVPlayer stage %d, code 0x%08x",
                    avPlayer.errorStage(),
                    static_cast<unsigned int>(avPlayer.errorCode()));
                notify(result);
            }
            queuedPlaybackTest = -1;
        }
        const bool directPlaybackRequested = playbackDirectVideoDec2 &&
            (queuedPlaybackTest == 0 ||
             ((pressed & ORBIS_PAD_BUTTON_SQUARE) != 0 && catalogScreen &&
              !streamVisible));
        if (directPlaybackRequested) {
            stopBackgroundForPlayback();
            avPlayer.stop();
            notify("Stremio: starting direct Videodec2 GPU benchmark");
            if (!videoDec2.start(kDirectVideoTestPath)) {
                notify("Stremio: Videodec2 worker could not start");
            }
            queuedPlaybackTest = -1;
        }
        const bool remotePlaybackRequested = queuedPlaybackTest == 5;
        if (remotePlaybackRequested) {
            stopBackgroundForPlayback();
            previewVisible = false;
            previewBackgroundFrames = 0;
            playbackWallStart = 0;
            playbackTimingReported = false;
            playbackSourceWidth = 0;
            playbackSourceHeight = 0;
            avPlayerProbeFrames = 0;
            activePlaybackDecodeOnly = playbackDecodeOnly;
            activePlaybackLegacyApi = playbackLegacyApi;
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
            if (cacheResult >= 0 && !avPlayer.start(
                    kLegalRemoteCachePath, !activePlaybackDecodeOnly,
                    activePlaybackLegacyApi)) {
                char result[128];
                snprintf(result, sizeof(result),
                    "Stremio: cached AVPlayer stage %d, code 0x%08x",
                    avPlayer.errorStage(),
                    static_cast<unsigned int>(avPlayer.errorCode()));
                notify(result);
            }
            queuedPlaybackTest = -1;
        }
        if ((pressed & ORBIS_PAD_BUTTON_CIRCLE) != 0 && hlsJob.active) {
            hlsJob.active = false;
            hlsJob.segmentWaiting = false;
            videoDec2.stop();
            notify("Stremio: segmented Videodec2 stream stopped");
        } else if ((pressed & ORBIS_PAD_BUTTON_CIRCLE) != 0 &&
            streamJob.running.load(std::memory_order_acquire)) {
            streamJob.progress.cancel.store(true, std::memory_order_release);
            const int requestId = streamJob.progress.requestId.load(
                std::memory_order_acquire);
            if (requestId >= 0) sceHttpAbortRequest(requestId);
            notify("Stremio: cancelling stream preparation...");
        } else if ((pressed & ORBIS_PAD_BUTTON_CIRCLE) != 0 && previewVisible) {
            avPlayer.requestStop();
            avPlayerProbeFrames = 0;
            previewVisible = false;
            previewPixels.clear();
            previewWidth = 0;
            previewHeight = 0;
        } else if ((pressed & ORBIS_PAD_BUTTON_CIRCLE) != 0 &&
            videoDec2.state() != VideoDec2Probe::State::Idle) {
            videoDec2.stop();
            previewVisible = false;
        } else if ((pressed & ORBIS_PAD_BUTTON_CIRCLE) != 0 &&
            avPlayer.state() != AvPlayerProbe::State::Idle) {
            avPlayer.requestStop();
            avPlayerProbeFrames = 0;
            progressiveStreamOpening = false;
        } else if ((pressed & ORBIS_PAD_BUTTON_CIRCLE) != 0 && streamVisible) {
            streamVisible = false;
        } else if ((pressed & ORBIS_PAD_BUTTON_CIRCLE) != 0 && detailVisible) {
            detailVisible = false;
        }

        avPlayer.update();
        if (avPlayer.state() == AvPlayerProbe::State::Opening ||
            avPlayer.state() == AvPlayerProbe::State::Decoding) {
            ++avPlayerProbeFrames;
            const int timeoutFrames = progressiveStreamOpening ? 7200 : 1200;
            if (avPlayerProbeFrames > timeoutFrames) {
                avPlayer.requestStop();
                notify("Stremio: AVPlayer timed out before first frame");
                progressiveStreamOpening = false;
            }
        }
        if (avPlayer.state() != previousPlayerState) {
            if (avPlayer.state() == AvPlayerProbe::State::Decoding) {
                notify("Stremio: AVPlayer active; waiting for frame");
            } else if (avPlayer.state() == AvPlayerProbe::State::Passed) {
                progressiveStreamOpening = false;
                char result[96];
                snprintf(result, sizeof(result),
                    "Stremio: decoded frame %ux%u",
                    avPlayer.width(), avPlayer.height());
                notify(result);
                previewVisible = true;
                previewBackgroundFrames = kFrameBuffers;
                playbackWallStart = sceKernelGetProcessTime();
                // Query once on state transition. Re-locking the decoder's
                // preview mutex twice every render frame starved its worker.
                playbackSourceWidth = avPlayer.width();
                playbackSourceHeight = avPlayer.height();
            } else if (avPlayer.state() == AvPlayerProbe::State::Failed) {
                progressiveStreamOpening = false;
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

        if (videoDec2.state() != VideoDec2Probe::State::Idle) {
            staticScreenKey = ~0ull;
            videoDec2.copyPreview(directPreviewPixels,
                directPreviewWidth, directPreviewHeight);
            drawVideoDec2Test(scene, videoDec2, directPreviewPixels,
                directPreviewWidth, directPreviewHeight);
            static VideoDec2Probe::State reportedVideoDec2State =
                VideoDec2Probe::State::Idle;
            if (videoDec2.state() != reportedVideoDec2State) {
                if (videoDec2.state() == VideoDec2Probe::State::Passed) {
                    notify("Stremio: direct Videodec2 produced GPU frames");
                } else if (videoDec2.state() == VideoDec2Probe::State::Finished) {
                    char result[128];
                    snprintf(result, sizeof(result),
                        "Stremio: Videodec2 %u.%u fps / %llu frames",
                        videoDec2.measuredFpsTimesTen() / 10,
                        videoDec2.measuredFpsTimesTen() % 10,
                        static_cast<unsigned long long>(videoDec2.decodedFrames()));
                    notify(result);
                } else if (videoDec2.state() == VideoDec2Probe::State::Failed) {
                    char result[128];
                    snprintf(result, sizeof(result),
                        "Stremio: Videodec2 stage %d code 0x%08x AU %u",
                        videoDec2.errorStage(),
                        static_cast<unsigned int>(videoDec2.errorCode()),
                        videoDec2.submittedAccessUnits());
                    notify(result);
                }
                reportedVideoDec2State = videoDec2.state();
            }
        } else if (previewVisible) {
            staticScreenKey = ~0ull;
            avPlayer.copyPreview(previewPixels, previewWidth, previewHeight);
            drawDecodedPreview(
                scene, previewPixels, previewWidth, previewHeight,
                previewBackgroundFrames > 0, avPlayer.currentTime(),
                avPlayer.duration(), avPlayer.paused(),
                avPlayer.measuredFpsTimesTen(), avPlayer.decodedFrames(),
                playbackSourceWidth, playbackSourceHeight,
                activePlaybackDecodeOnly, activePlaybackLegacyApi);
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
        } else if (hlsJob.active &&
            videoDec2.state() == VideoDec2Probe::State::Idle) {
            staticScreenKey = ~0ull;
            drawStreamProgress(scene, 2, 0, 0,
                progressiveStreamStartedAt, 0);
        } else if (streamJob.running.load(std::memory_order_acquire)) {
            staticScreenKey = ~0ull;
            drawStreamProgress(scene,
                streamJob.progress.stage.load(std::memory_order_acquire),
                streamJob.progress.downloaded.load(std::memory_order_acquire),
                streamJob.progress.expected.load(std::memory_order_acquire),
                streamJob.progress.startedAt,
                streamJob.progress.nativeError.load(std::memory_order_acquire));
        } else if (streamVisible) {
            staticScreenKey = ~0ull;
            drawStreams(scene, details, streams, focusedStream);
        } else if (detailVisible) {
            staticScreenKey = ~0ull;
            const PosterImage* poster =
                detailPosterIndex >= 0 &&
                detailPosterIndex < static_cast<int>(catalogPosters.size())
                ? &catalogPosters[detailPosterIndex] : nullptr;
            drawDetails(
                scene, details, poster, catalogType, detailEpisodeIndex);
        } else if (activeTab == 3 && !searchShowingResults) {
            uint64_t key = 0x5300000000000000ull |
                (static_cast<uint64_t>(indicatorX) << 24) |
                (static_cast<uint64_t>(searchFilter) << 8) |
                static_cast<uint32_t>(searchKey);
            for (char value : searchQuery)
                key = (key ^ static_cast<uint8_t>(value)) * 1099511628211ull;
            if (key != staticScreenKey) {
                staticScreenKey = key;
                staticScreenFrames = kFrameBuffers;
            }
            if (staticScreenFrames > 0) {
                drawSearch(scene, searchQuery, searchFilter, indicatorX,
                    animationFrame);
                --staticScreenFrames;
            }
        } else if (activeTab == 4) {
            uint64_t key = 0x5400000000000000ull |
                (static_cast<uint64_t>(indicatorX) << 24) |
                (static_cast<uint64_t>(settingsPage) << 16) |
                static_cast<uint32_t>(settingsSelection);
            if (settingsPage == 4) {
                key ^= stableHash(accountStatus);
                key ^= stableHash(accountJob.code);
                key ^= static_cast<uint64_t>(addonUrls.size()) << 40;
            }
            if (key != staticScreenKey) {
                staticScreenKey = key;
                staticScreenFrames = kFrameBuffers;
            }
            if (staticScreenFrames > 0) {
                if (settingsPage == 1) {
                    drawPlayerSelection(scene, settingsSelection, indicatorX);
                } else if (settingsPage == 2) {
                    drawQualitySelection(scene, settingsSelection, indicatorX,
                        playbackDecodeOnly, playbackLegacyApi,
                        playbackDirectVideoDec2);
                } else if (settingsPage == 3) {
                    drawAbout(scene, indicatorX);
                } else if (settingsPage == 4) {
                    drawAccount(scene, indicatorX, accountJob.code,
                        accountJob.link, accountStatus,
                        static_cast<int>(addonUrls.size()), !authKey.empty());
                } else {
                    drawSettings(scene, settingsSelection, indicatorX);
                }
                --staticScreenFrames;
            }
        } else {
            staticScreenKey = ~0ull;
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
    videoDec2.stop();
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
    if (streamJob.running.load(std::memory_order_acquire) ||
        streamJob.completed.load(std::memory_order_acquire))
        pthread_join(streamJob.thread, nullptr);
    hlsJob.active = false;
    if (hlsJob.running.load(std::memory_order_acquire) ||
        hlsJob.completed.load(std::memory_order_acquire))
        pthread_join(hlsJob.thread, nullptr);
    if (accountJob.running.load(std::memory_order_acquire))
        accountJob.cancel.store(true, std::memory_order_release);
    if (accountJob.running.load(std::memory_order_acquire) ||
        accountJob.completed.load(std::memory_order_acquire))
        pthread_join(accountJob.thread, nullptr);
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
