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
constexpr const char* kUiConfigPath = "/data/stremio-ui-mode.txt";
constexpr const char* kLinkCreateUrl =
    "https://link.stremio.com/api/v2/create?type=Create";
constexpr int kPosterWidth = 235;
constexpr int kPosterHeight = 330;
constexpr int kCatalogBatchSize = 8;
constexpr int kCatalogPageSize = 6;
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
bool androidTvMode = true;
constexpr int kSideRailCollapsed = 118;
constexpr int kSideRailExpanded = 360;
int navigationWidth = kSideRailCollapsed;

void drawNavigationIcon(
    Scene2D& scene, int index, int centerX, int centerY, Color color);

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
    const Color rail = {10, 9, 17};
    const Color selected = {70, 48, 126};
    const Color purple = {123, 91, 214};
    const Color text = {235, 232, 244};
    const Color muted = {135, 130, 151};
    const int railWidth = navigationWidth;
    scene.DrawRectangle(0, 0, railWidth, kHeight, rail);
    scene.DrawVerticalFade(0, 0, railWidth, 160,
        Color{42, 29, 74}, 190, 0);
    scene.DrawRectangle(railWidth - 2, 0, 2, kHeight, Color{39, 32, 55});
    if (railWidth > 250) {
        drawBrand(scene, text);
        scene.DrawText(42, 116, "WATCH  DISCOVER  ENJOY", muted, 1);
        scene.DrawRectangle(34, 151, railWidth - 68, 2, Color{51, 46, 66});
    }
    else if (headerLogo.valid()) scene.BlitRgbScaledRounded(
        (railWidth - 60) / 2, 27, 60, 60, 14,
        headerLogo.pixels.data(), headerLogo.width, headerLogo.height);
    const char* labels[] = {"Movies", "Series", "Public domain", "Search", "Settings"};
    for (int index = 0; index < kTopTabCount; ++index) {
        const int y = 190 + index * 112;
        if (index == activeTab) {
            scene.DrawRoundedRectangle(20, y,
                railWidth > 250 ? railWidth - 40 : railWidth - 40,
                76, 38, selected);
        }
        const int iconX = railWidth > 250 ? 34 : (railWidth - 56) / 2;
        const int iconCenter = iconX + 28;
        scene.DrawRoundedRectangle(iconX, y + 10, 56, 56, 28,
            index == activeTab ? purple : Color{31, 29, 42});
        drawNavigationIcon(scene, index, iconCenter, y + 38,
            index == activeTab ? text : muted);
        if (railWidth > 210) {
            const int labelWidth = scene.MeasureText(labels[index], 2);
            scene.DrawText(220 - labelWidth / 2, y + 29, labels[index],
                index == activeTab ? text : muted, 2);
        }
    }
    if (railWidth > 250) {
        scene.DrawRoundedRectangle(34, 864, railWidth - 68, 150, 24,
            Color{20, 18, 29});
        scene.DrawText(55, 888, "NAVIGATION", purple, 1);
        scene.DrawText(55, 925, "UP / DOWN   SECTIONS", text, 1);
        scene.DrawText(55, 957, "RIGHT / CROSS   OPEN", text, 1);
        scene.DrawText(55, 990, "CIRCLE   RETURN", muted, 1);
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

void drawNavigationIcon(
    Scene2D& scene, int index, int centerX, int centerY, Color color) {
    if (index == 0) {
        // Film frame with two sprocket holes.
        scene.DrawRoundedRectangle(centerX - 17, centerY - 13, 34, 26, 5,
            color);
        scene.DrawRectangle(centerX - 12, centerY - 8, 24, 16,
            Color{31, 29, 42});
        scene.DrawRectangle(centerX - 14, centerY - 9, 4, 4, color);
        scene.DrawRectangle(centerX + 10, centerY + 5, 4, 4, color);
    } else if (index == 1) {
        // Television / series.
        scene.DrawRoundedRectangle(centerX - 18, centerY - 13, 36, 25, 5,
            color);
        scene.DrawRectangle(centerX - 13, centerY - 8, 26, 15,
            Color{31, 29, 42});
        drawLine(scene, centerX - 7, centerY + 17,
            centerX + 7, centerY + 17, color, 3);
    } else if (index == 2) {
        // Public-domain play mark.
        scene.DrawRoundedRectangle(centerX - 17, centerY - 17, 34, 34, 17,
            color);
        drawLine(scene, centerX - 5, centerY - 8,
            centerX + 9, centerY, Color{31, 29, 42}, 4);
        drawLine(scene, centerX + 9, centerY,
            centerX - 5, centerY + 8, Color{31, 29, 42}, 4);
    } else if (index == 3) {
        // Search glass.
        scene.DrawRoundedRectangle(centerX - 14, centerY - 14, 23, 23, 12,
            color);
        scene.DrawRoundedRectangle(centerX - 9, centerY - 9, 13, 13, 7,
            Color{31, 29, 42});
        drawLine(scene, centerX + 7, centerY + 7,
            centerX + 17, centerY + 17, color, 5);
    } else {
        // Settings cog, kept geometric for the bitmap renderer.
        scene.DrawRoundedRectangle(centerX - 15, centerY - 15, 30, 30, 15,
            color);
        scene.DrawRoundedRectangle(centerX - 7, centerY - 7, 14, 14, 7,
            Color{31, 29, 42});
        scene.DrawRectangle(centerX - 3, centerY - 20, 6, 8, color);
        scene.DrawRectangle(centerX - 3, centerY + 12, 6, 8, color);
        scene.DrawRectangle(centerX - 20, centerY - 3, 8, 6, color);
        scene.DrawRectangle(centerX + 12, centerY - 3, 8, 6, color);
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
    int catalogMotion,
    int catalogHorizontalMotion,
    const PosterImage* heroArtwork,
    const PosterImage* heroLogo) {
    const Color background = {11, 10, 17};
    const Color header = {24, 22, 33};
    const Color stremioPurple = {123, 91, 214};
    const Color cardMuted = {35, 35, 46};
    const Color focus = {82, 58, 142};
    const Color text = {235, 232, 244};
    const Color mutedText = {164, 158, 181};

    scene.FrameBufferFill(background);
    const int contentLeft = useSideNavigation ? kSideRailCollapsed : 0;
    if (androidTvMode && heroArtwork && heroArtwork->valid())
        scene.BlitRgbMasked(contentLeft, 0, heroArtwork->width,
            heroArtwork->height, heroArtwork->pixels.data());
    const int cardX[] = {150, 440, 730, 1020, 1310, 1600};
    const int cardY = (androidTvMode ? 590 : 320) + catalogMotion;
    const int selectedIndex = page * kCatalogPageSize + focusedCard;
    auto drawCatalogRow = [&](int rowStartIndex, int rowY, bool selected,
                              bool showTitles, int scalePercent, int xOffset) {
        if (rowStartIndex < 0 || rowY >= 1000 || rowY + 390 <= 145) return;
        const int cardWidth = 235 * scalePercent / 100;
        const int cardHeight = 330 * scalePercent / 100;
        for (int index = 0; index < kCatalogPageSize; ++index) {
        const int itemIndex = rowStartIndex + index;
        const int x = cardX[index] + (235 - cardWidth) / 2 + xOffset;
        if (selected && itemIndex == selectedIndex) {
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
            if (headerLogo.valid() && rowY >= 0 && rowY + cardHeight <= kHeight) {
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
    if (androidTvMode) {
        // Keep the selected title near the third slot and slide one continuous
        // catalog underneath it. There are no visible six-item "pages".
        const int rowStart = std::max(0, selectedIndex - 2);
        drawCatalogRow(rowStart, cardY, true, true, 100,
            catalogHorizontalMotion);
    } else {
        if (catalogMotion > 0)
            drawCatalogRow((page - 1) * kCatalogPageSize, cardY - 480,
                false, true, 100, 0);
        else if (catalogMotion < 0)
            drawCatalogRow((page + 1) * kCatalogPageSize, cardY + 480,
                false, true, 100, 0);
        const int selectedScale = catalogMotion > 48 ? 90 : 100;
        drawCatalogRow(page * kCatalogPageSize, cardY, true, true,
            selectedScale, 0);
        const int previewY = 805 + (catalogMotion > 0 ? catalogMotion : 0);
        if (catalogMotion >= 0)
            drawCatalogRow((page + 1) * kCatalogPageSize, previewY,
                false, false, 90, 0);
    }

    // Paint the navigation after moving rows. This is a hard content viewport:
    // outgoing cards disappear behind it instead of crossing the top menu.
    if (!(androidTvMode && heroArtwork && heroArtwork->valid())) {
        scene.DrawRectangle(contentLeft, 0, kWidth - contentLeft, 240, background);
        scene.DrawVerticalFade(contentLeft, 0, kWidth - contentLeft, 210,
            header, 220, 0);
    }
    scene.DrawText(150, 38,
        catalogType == "series" ? "SERIES / POPULAR" :
        (catalogType == "publicdomain" ? "PUBLIC DOMAIN / FEATURED" :
        (activeTab == 3 ? "SEARCH / RESULTS" : "MOVIES / POPULAR")),
        stremioPurple, 2);
    std::string featured = selectedIndex < static_cast<int>(items.size())
        ? items[selectedIndex].name : "Discover something to watch";
    if (featured.size() > 38) featured = featured.substr(0, 35) + "...";
    if (androidTvMode && heroLogo && heroLogo->valid())
        scene.BlitRgba(150, 78, heroLogo->width, heroLogo->height,
            heroLogo->pixels.data());
    else {
        const int titleScale = featured.size() > 30 ? 3 :
            (featured.size() > 18 ? 4 : 5);
        scene.DrawText(150, 90, featured.c_str(), text, titleScale);
    }
    if (androidTvMode && selectedIndex < static_cast<int>(items.size())) {
        const CatalogItem& selectedItem = items[selectedIndex];
        std::string facts;
        if (!selectedItem.releaseInfo.empty()) facts += selectedItem.releaseInfo;
        if (!selectedItem.runtime.empty()) {
            if (!facts.empty()) facts += "   |   ";
            facts += selectedItem.runtime;
        }
        if (!selectedItem.imdbRating.empty()) {
            if (!facts.empty()) facts += "   |   ";
            facts += "IMDB ";
            facts += selectedItem.imdbRating;
        }
        scene.DrawText(150, 175, facts.empty() ? "STREMIO" : facts.c_str(),
            stremioPurple, 2);
        scene.DrawText(150, 218, selectedItem.genres.c_str(), mutedText, 2);
        const std::vector<std::string> description = wrapText(
            selectedItem.description.empty() ?
                "Open for details, episodes and available streams." :
                selectedItem.description, 62, 4);
        int descriptionY = 270;
        for (const std::string& line : description) {
            scene.DrawText(150, descriptionY, line.c_str(), text, 2);
            descriptionY += 38;
        }
        scene.DrawRoundedRectangle(150, 450, 230, 58, 29, stremioPurple);
        scene.DrawText(194, 468, "X  DETAILS", text, 2);
    } else scene.DrawText(150, 155,
        "Browse with the D-pad or L3 stick", mutedText, 2);
    char pageText[64];
    if (androidTvMode)
        snprintf(pageText, sizeof(pageText), "TITLE %d  /  %d",
            selectedIndex + 1, static_cast<int>(items.size()));
    else snprintf(pageText, sizeof(pageText), "PAGE %d  /  %d TITLES", page + 1,
            static_cast<int>(items.size()));
    scene.DrawRoundedRectangle(1510, 36, 330, 62, 31, Color{35, 32, 47});
    scene.DrawText(1550, 56, pageText, mutedText, 2);
    scene.DrawText(150, androidTvMode ? 535 : 215,
        "Recommended for you", text, 3);
    scene.DrawText(150, androidTvMode ? 946 : 740,
        status.c_str(), mutedText, 2);
    scene.DrawRectangle(contentLeft, 978, kWidth - contentLeft, 102,
        Color{16, 14, 23});
    drawStickHint(scene, contentLeft + 42, 1007, "NAVIGATE");
    drawButtonHint(scene, 1650, 1010, 'X', "DETAILS");
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
    const int contentX = useSideNavigation ? std::max(300, navigationWidth + 45) : 300;
    scene.DrawVerticalFade(contentX - 50, 0, kWidth - contentX + 50, 210, header, 220, 0);
    scene.DrawText(contentX, 55, "DISCOVER", muted, 2);
    scene.DrawText(contentX, 100, "SEARCH", text, 5);
    scene.DrawText(contentX, 175,
        "SEARCH MOVIES, SERIES, OR THE PUBLIC DOMAIN COLLECTION", muted, 2);
    const char* filters[] = {"MOVIES", "SERIES", "PUBLIC DOMAIN"};
    const int filterWidths[] = {250, 250, 360};
    int filterX = contentX;
    for (int index = 0; index < 3; ++index) {
        scene.DrawRoundedRectangle(filterX, 225, filterWidths[index], 62, 20,
            index == filter ? purple : header);
        const int filterTextWidth = scene.MeasureText(filters[index], 2);
        scene.DrawText(filterX + (filterWidths[index] - filterTextWidth) / 2,
            244, filters[index],
            index == filter ? text : muted, 2);
        filterX += filterWidths[index] + 24;
    }
    scene.DrawRoundedRectangle(contentX, 325, 1800 - contentX, 130, 30, header);
    scene.DrawRoundedRectangle(contentX + 35, 355, 68, 68, 20, purple);
    scene.DrawText(contentX + 56, 369, "?", text, 4);
    scene.DrawText(contentX + 135, 347, "TITLE", muted, 2);
    const std::string shown = query.empty() ? "TYPE A TITLE..." : query + "_";
    scene.DrawText(contentX + 135, 392, shown.c_str(), query.empty() ? muted : text, 3);
    const int panelWidth = (1760 - contentX - 28) / 2;
    scene.DrawRoundedRectangle(contentX, 505, panelWidth, 250, 26, header);
    scene.DrawText(contentX + 45, 550, "PS4 SYSTEM KEYBOARD", text, 3);
    scene.DrawText(contentX + 45, 610, "CROSS   TYPE / SELECT", muted, 2);
    scene.DrawText(contentX + 45, 650, "SQUARE  DELETE    TRIANGLE  SPACE", muted, 2);
    scene.DrawText(contentX + 45, 690, "R2      SEARCH    CIRCLE    CLOSE", muted, 2);
    const int tipsX = contentX + panelWidth + 28;
    scene.DrawRoundedRectangle(tipsX, 505, panelWidth, 250, 26, header);
    scene.DrawText(tipsX + 45, 550, "SEARCH TIPS", text, 3);
    scene.DrawText(tipsX + 45, 610, "LEFT / RIGHT CHANGES THE FILTER", muted, 2);
    scene.DrawText(tipsX + 45, 650, "UP TO 24 RESULTS WITH CACHED POSTERS", muted, 2);
    scene.DrawText(tipsX + 45, 690, "OPEN RESULTS FOR DETAILS AND STREAMS", muted, 2);
    scene.DrawRectangle(kSideRailCollapsed, 978,
        kWidth - kSideRailCollapsed, 102, Color{16, 14, 23});
    drawStickHint(scene, kSideRailCollapsed + 42, 1007, "NAVIGATE");
    drawButtonHint(scene, 610, 1010, '<', "FILTER");
    drawButtonHint(scene, 810, 1010, '>', "FILTER");
    drawButtonHint(scene, 1030, 1010, 'O', "CLEAR / BACK");
    drawButtonHint(scene, 1325, 1010, 'T', "SEARCH");
    drawButtonHint(scene, 1580, 1010, 'X', "KEYBOARD");
    drawNavigation(scene, 3);
}

void drawSettingsHeader(Scene2D& scene, int indicatorX, const char* title) {
    const Color background = {18, 18, 24};
    const Color header = {29, 29, 39};
    const Color purple = {123, 91, 214};
    const Color text = {235, 232, 244};
    const Color muted = {164, 158, 181};
    scene.FrameBufferFill(background);
    const int contentX = useSideNavigation ? std::max(300, navigationWidth + 45) : 300;
    scene.DrawVerticalFade(contentX - 50, 0, kWidth - contentX + 50, 210, header, 220, 0);
    scene.DrawText(contentX, 55, "STREMIO", muted, 2);
    scene.DrawText(contentX, 105, title, text, 5);
}

void drawSettingsRows(Scene2D& scene, const char* const* rows, int rowCount,
    int selected, int indicatorX, const char* title, const char* action) {
    const Color header = {29, 29, 39};
    const Color focus = {82, 58, 142};
    const Color text = {235, 232, 244};
    drawSettingsHeader(scene, indicatorX, title);
    const int contentX = useSideNavigation ? std::max(380, navigationWidth + 45) : 380;
    scene.DrawRoundedRectangle(contentX, 235, 1760 - contentX, 675, 32, header);
    for (int index = 0; index < rowCount; ++index) {
        const int y = 250 + index * 78;
        const bool focused = index == selected;
        scene.DrawRoundedRectangle(contentX + 35, y, 1690 - contentX, 60, 30,
            focused ? focus : Color{35, 35, 46});
        scene.DrawRoundedRectangle(contentX + 55, y + 6, 48, 48, 24,
            focused ? Color{123, 91, 214} : Color{52, 52, 68});
        char number[8];
        snprintf(number, sizeof(number), "%02d", index + 1);
        scene.DrawText(contentX + 63, y + 17, number, text, 2);
        scene.DrawText(contentX + 135, y + 17, rows[index], text, 2);
        if (focused) {
            scene.DrawText(1640, y + 17, ">", text, 2);
        }
    }
    scene.DrawRectangle(kSideRailCollapsed, 978,
        kWidth - kSideRailCollapsed, 102, Color{16, 14, 23});
    drawStickHint(scene, kSideRailCollapsed + 42, 1007, "NAVIGATE");
    drawButtonHint(scene, 1390, 1010, 'O', "BACK");
    drawButtonHint(scene, 1640, 1010, 'X', action);
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
        androidTvMode ? "HOME LAYOUT  ANDROID TV" :
            "HOME LAYOUT  COMPACT GRID",
        "ABOUT STREMIO  v4.04"};
    drawSettingsRows(scene, rows, 8, selected, indicatorX,
        "SETTINGS", "OPEN");
}

void drawStreamLookup(Scene2D& scene, const MetaDetails& details,
        int animationFrame) {
    const Color background = {11, 10, 17};
    const Color panel = {28, 25, 39};
    const Color purple = {123, 91, 214};
    const Color text = {235, 232, 244};
    const Color muted = {164, 158, 181};
    scene.FrameBufferFill(background);
    scene.DrawRoundedRectangle(410, 245, 1100, 560, 38, panel);
    scene.DrawText(560, 335, "FINDING AVAILABLE STREAMS", text, 4);
    scene.DrawText(560, 405, details.name.c_str(), muted, 2);
    const int phase = (animationFrame / 4) % 12;
    for (int dot = 0; dot < 12; ++dot) {
        const double angle = 6.283185307179586 * dot / 12.0;
        const int x = 960 + static_cast<int>(std::cos(angle) * 105.0);
        const int y = 585 + static_cast<int>(std::sin(angle) * 105.0);
        const int distance = (dot - phase + 12) % 12;
        const uint8_t shade = static_cast<uint8_t>(55 + (11 - distance) * 16);
        scene.DrawRoundedRectangle(x - 11, y - 11, 22, 22, 11,
            distance < 4 ? purple : Color{shade, shade, shade});
    }
    scene.DrawText(660, 735,
        "Contacting your addons. This can take a few seconds.", muted, 2);
    drawButtonHint(scene, 40, 1020, 'O', "BACK");
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
    uint32_t previewHeight, const std::string& title, bool hudVisible,
    int aspectMode, int audioOffsetMs, bool audioActive) {
    const Color background = {8, 8, 12};
    const Color panel = {29, 29, 39};
    const Color purple = {123, 91, 214};
    const Color text = {235, 232, 244};
    const Color muted = {164, 158, 181};
    scene.FrameBufferFill(background);
    if (!pixels.empty() && previewWidth > 0 && previewHeight > 0) {
        int videoX = 0, videoY = 0, videoWidth = kWidth, videoHeight = kHeight;
        if (aspectMode == 0) {
            videoHeight = static_cast<int>(
                static_cast<uint64_t>(kWidth) * previewHeight / previewWidth);
            if (videoHeight > kHeight) {
                videoHeight = kHeight;
                videoWidth = static_cast<int>(
                    static_cast<uint64_t>(kHeight) * previewWidth / previewHeight);
            }
            videoX = (kWidth - videoWidth) / 2;
            videoY = (kHeight - videoHeight) / 2;
        }
        scene.BlitRgbScaled(videoX, videoY, videoWidth, videoHeight,
            pixels.data(), previewWidth, previewHeight);
    }
    char metrics[160];
    snprintf(metrics, sizeof(metrics),
        "OUTPUT %ux%u   AU %u   FRAMES %llu   DECODE %u.%u FPS",
        decoder.width(), decoder.height(),
        decoder.submittedAccessUnits(),
        static_cast<unsigned long long>(decoder.decodedFrames()),
        decoder.measuredFpsTimesTen() / 10,
        decoder.measuredFpsTimesTen() % 10);
    if (pixels.empty()) {
        scene.DrawVerticalFade(0, 0, kWidth, 220, panel, 235, 0);
        drawBrand(scene, text);
        scene.DrawText(120, 190, "DIRECT VIDEODEC2 GPU PLAYER", text, 4);
        scene.DrawRoundedRectangle(280, 315, 1360, 430, 34, panel);
        scene.DrawRoundedRectangle(330, 370, 120, 120, 30, purple);
        scene.DrawText(365, 405, "GPU", text, 3);
        scene.DrawText(505, 370,
            "H.264 BASELINE L4.0   1920x1080 / 24 FPS", text, 3);
        scene.DrawText(505, 430,
            "INITIALIZING DIRECT HARDWARE PLAYBACK...", muted, 2);
        scene.DrawRoundedRectangle(505, 555, 1035, 90, 22, background);
        scene.DrawText(545, 585, metrics, text, 2);
    } else if (hudVisible) {
        scene.DrawVerticalFade(0, 0, kWidth, 158, panel, 232, 0);
        scene.DrawText(64, 28, "NOW PLAYING", purple, 1);
        const std::string displayTitle = title.empty() ? "STREMIO STREAM" :
            (title.size() > 48 ? title.substr(0, 45) + "..." : title);
        scene.DrawText(64, 61, displayTitle.c_str(), text, 3);
        scene.DrawRoundedRectangle(64, 108, 220, 36, 18, panel);
        scene.DrawText(86, 118, "VIDEODEC2  H.264", muted, 1);
        scene.DrawRoundedRectangle(300, 108, 190, 36, 18,
            audioActive ? Color{45, 94, 72} : Color{92, 45, 55});
        scene.DrawText(322, 118,
            audioActive ? "AUDIO  ACTIVE" : "AUDIO  OFF", text, 1);
        scene.DrawRoundedRectangle(506, 108, 190, 36, 18, panel);
        scene.DrawText(532, 118,
            aspectMode == 0 ? "ASPECT  FIT" : "ASPECT  FILL", muted, 1);
        scene.DrawRoundedRectangle(410, 892, 1100, 52, 22, Color{24, 22, 34});
        scene.DrawText(452, 908, metrics, text, 2);
        const uint64_t duration = decoder.duration();
        const int progress = duration ? static_cast<int>(
            std::min<uint64_t>(1000, decoder.currentTime() * 1000 / duration)) : 0;
        scene.DrawRoundedRectangle(180, 965, 1560, 10, 5, panel);
        scene.DrawRoundedRectangle(180, 965, progress * 1560 / 1000,
            10, 5, purple);
        char sync[64];
        snprintf(sync, sizeof(sync), "AUDIO SYNC  %+d MS", audioOffsetMs);
        scene.DrawText(1510, 918, sync, muted, 1);
    }
    if (hudVisible || pixels.empty()) {
        scene.DrawVerticalFade(0, 980, kWidth, 100, panel, 0, 235);
        drawButtonHint(scene, 40, 1025, 'X',
            decoder.paused() ? "RESUME" : "PAUSE");
        drawButtonHint(scene, 285, 1025, 'T', "RESTART");
        drawButtonHint(scene, 520, 1025, 'S', "HIDE HUD");
        scene.DrawText(790, 1040, "UP  ASPECT    L1 / R1  AUDIO SYNC",
            muted, 1);
        drawButtonHint(scene, 1640, 1025, 'O', "STOP");
    }
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
        "SOURCE AVAILABLE FOR NONCOMMERCIAL USE.", muted, 2);
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
    scene.DrawRoundedRectangle(90, 175, 410, 650, 30, panel);
    scene.DrawRoundedRectangle(124, 209, 322, 422, 22, purple);
    if (poster && poster->valid()) {
        scene.BlitRgbScaledRounded(130, 215, 322, 422, 18,
            poster->pixels.data(), poster->width, poster->height);
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
    scene.DrawRoundedRectangle(575, 760, 285, 66, 33, purple);
    scene.DrawText(625, 781,
        catalogType == "series" ? "CHOOSE EPISODE" : "FIND STREAMS",
        text, 2);
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
    const Color focus = {82, 58, 142};
    const Color text = {235, 232, 244};
    const Color muted = {164, 158, 181};
    scene.FrameBufferFill(background);
    scene.DrawVerticalFade(0, 0, kWidth, 210, panel, 235, 0);
    drawBrand(scene, text);
    scene.DrawText(120, 132, "AVAILABLE STREAMS", text, 4);
    scene.DrawText(120, 195, details.name.c_str(), muted, 2);
    scene.DrawRoundedRectangle(120, 230, 210, 48, 24, purple);
    scene.DrawText(158, 245, "BEST MATCH", text, 1);
    scene.DrawRoundedRectangle(348, 230, 220, 48, 24, panel);
    scene.DrawText(388, 245, "MOST PEERS", muted, 1);
    scene.DrawRoundedRectangle(586, 230, 245, 48, 24, panel);
    scene.DrawText(624, 245, "CACHED RESULTS", muted, 1);
    scene.DrawRoundedRectangle(1450, 175, 350, 70, 20, purple);
    char count[64];
    snprintf(count, sizeof(count), "%d SOURCES FOUND",
        static_cast<int>(streams.size()));
    scene.DrawText(1495, 197, count, text, 2);
    constexpr int visibleRows = 6;
    const int maximumStart = std::max(0,
        static_cast<int>(streams.size()) - visibleRows);
    const int firstVisible = std::min(maximumStart,
        std::max(0, focusedStream - visibleRows + 1));
    for (int row = 0; row < visibleRows; ++row) {
        const int index = firstVisible + row;
        if (index >= static_cast<int>(streams.size())) break;
        const int y = 300 + row * 101;
        const bool selected = index == focusedStream;
        scene.DrawRoundedRectangle(120, y, 1270, 86, 18,
            selected ? focus : panel);
        if (selected) scene.DrawRectangle(120, y + 12, 7, 62, purple);
        scene.DrawRoundedRectangle(145, y + 14, 58, 58, 16,
            selected ? purple : Color{52, 52, 68});
        char sourceNumber[8];
        snprintf(sourceNumber, sizeof(sourceNumber), "%02d", index + 1);
        scene.DrawText(158, y + 32, sourceNumber, text, 2);
        const StreamItem& stream = streams[index];
        // Addons commonly put resolution, codec, release, peers and size in
        // title, while name is only the provider badge.
        const std::string label = !stream.title.empty() ? stream.title :
            (!stream.fileName.empty() ? stream.fileName :
            (!stream.name.empty() ? stream.name : "STREAM"));
        const std::vector<std::string> labelLines = wrapText(label, 58, 1);
        if (!labelLines.empty()) scene.DrawText(230, y + 14,
            labelLines[0].c_str(), text, 2);
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
        scene.DrawText(230, y + 55, torrentInfo.c_str(),
            selected ? Color{215, 205, 235} : muted, 1);
        scene.DrawText(1110, y + 34,
            stream.name.empty() ?
                (stream.url.empty() ? "COMPANION" : "DIRECT") :
                shortTitle(stream.name).c_str(),
            selected ? text : muted, 1);
    }
    if (!streams.empty() && focusedStream >= 0 &&
        focusedStream < static_cast<int>(streams.size())) {
        const StreamItem& selected = streams[focusedStream];
        scene.DrawRoundedRectangle(1430, 300, 370, 590, 28, panel);
        scene.DrawText(1470, 342, "SOURCE DETAILS", purple, 2);
        const std::string provider = selected.name.empty() ?
            (selected.url.empty() ? "TORRENT / COMPANION" : "DIRECT URL") :
            selected.name;
        const std::vector<std::string> providerLines = wrapText(provider, 24, 3);
        for (size_t line = 0; line < providerLines.size(); ++line)
            scene.DrawText(1470, 400 + static_cast<int>(line) * 34,
                providerLines[line].c_str(), text, 2);
        scene.DrawText(1470, 525, "AVAILABILITY", muted, 1);
        char availability[96];
        snprintf(availability, sizeof(availability), "SEEDS %d    PEERS %d",
            selected.seeders, selected.peers);
        scene.DrawText(1470, 558, availability, text, 2);
        scene.DrawText(1470, 625, "PLAYBACK", muted, 1);
        scene.DrawText(1470, 658,
            selected.infoHash.empty() ? "DIRECT STREAM" :
            "VIDEODEC2 + SONY AUDIO", text, 2);
        scene.DrawRoundedRectangle(1470, 745, 290, 68, 34, purple);
        scene.DrawText(1525, 767, "X  PLAY NOW", text, 2);
    }
    if (firstVisible > 0)
        scene.DrawText(1360, 305, "^", focus, 3);
    if (firstVisible + visibleRows < static_cast<int>(streams.size()))
        scene.DrawText(1360, 855, "v", focus, 3);
    char position[48];
    snprintf(position, sizeof(position), "%d / %d", focusedStream + 1,
        static_cast<int>(streams.size()));
    scene.DrawText(1700, 850, position, muted, 2);
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

void appendCacheU32(std::string& data, uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8)
        data.push_back(static_cast<char>((value >> shift) & 255));
}

bool readCacheU32(const std::string& data, size_t& position, uint32_t& value) {
    if (position + 4 > data.size()) return false;
    value = 0;
    for (int shift = 0; shift < 32; shift += 8)
        value |= static_cast<uint32_t>(
            static_cast<uint8_t>(data[position++])) << shift;
    return true;
}

void appendCacheString(std::string& data, const std::string& value) {
    appendCacheU32(data, static_cast<uint32_t>(value.size()));
    data.append(value);
}

bool readCacheString(
    const std::string& data, size_t& position, std::string& value) {
    uint32_t length = 0;
    if (!readCacheU32(data, position, length) ||
        length > kMaximumCatalogBytes || position + length > data.size())
        return false;
    value.assign(data, position, length);
    position += length;
    return true;
}

bool saveStreamCache(
    const std::string& key, const std::vector<StreamItem>& streams) {
    std::string data("S4SC1", 5);
    appendCacheU32(data, static_cast<uint32_t>(streams.size()));
    for (const StreamItem& stream : streams) {
        appendCacheString(data, stream.name);
        appendCacheString(data, stream.title);
        appendCacheString(data, stream.url);
        appendCacheString(data, stream.infoHash);
        appendCacheString(data, stream.fileName);
        appendCacheU32(data, static_cast<uint32_t>(stream.videoSize));
        appendCacheU32(data, static_cast<uint32_t>(stream.videoSize >> 32));
        appendCacheU32(data, static_cast<uint32_t>(stream.seeders + 1));
        appendCacheU32(data, static_cast<uint32_t>(stream.peers + 1));
        appendCacheU32(data, static_cast<uint32_t>(stream.fileIndex + 1));
    }
    writeCachedFile(cachePath("streams", key, "bin"), data);
    return true;
}

bool loadStreamCache(
    const std::string& key, std::vector<StreamItem>& streams) {
    std::string data;
    if (!readCachedFile(cachePath("streams", key, "bin"),
            kMaximumCatalogBytes, data) || data.compare(0, 5, "S4SC1") != 0)
        return false;
    size_t position = 5;
    uint32_t count = 0;
    if (!readCacheU32(data, position, count) || count > 64) return false;
    std::vector<StreamItem> loaded;
    for (uint32_t index = 0; index < count; ++index) {
        StreamItem stream;
        uint32_t low = 0, high = 0, seeders = 0, peers = 0, fileIndex = 0;
        if (!readCacheString(data, position, stream.name) ||
            !readCacheString(data, position, stream.title) ||
            !readCacheString(data, position, stream.url) ||
            !readCacheString(data, position, stream.infoHash) ||
            !readCacheString(data, position, stream.fileName) ||
            !readCacheU32(data, position, low) ||
            !readCacheU32(data, position, high) ||
            !readCacheU32(data, position, seeders) ||
            !readCacheU32(data, position, peers) ||
            !readCacheU32(data, position, fileIndex)) return false;
        stream.videoSize = static_cast<uint64_t>(low) |
            (static_cast<uint64_t>(high) << 32);
        stream.seeders = static_cast<int>(seeders) - 1;
        stream.peers = static_cast<int>(peers) - 1;
        stream.fileIndex = static_cast<int>(fileIndex) - 1;
        loaded.push_back(std::move(stream));
    }
    streams.swap(loaded);
    return !streams.empty();
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
            preparePosterPresentation(posters[index], 18, 211, 297);
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
    std::string videoPlaylistUrl;
    std::string audioPlaylistUrl;
    std::string outputPath;
    std::vector<std::string> segmentUrls;
    Fmp4VideoConfig config;
};

std::string hlsAbsoluteUrl(const std::string& baseUrl,
    const std::string& relative) {
    if (relative.compare(0, 8, "https://") == 0 ||
        relative.compare(0, 7, "http://") == 0) return relative;
    return baseUrl + relative;
}

std::string hlsQuotedUri(const std::string& line) {
    const size_t marker = line.find("URI=\"");
    if (marker == std::string::npos) return {};
    const size_t first = marker + 5;
    const size_t last = line.find('"', first);
    return last == std::string::npos ? std::string() :
        line.substr(first, last - first);
}

struct StreamLookupJob {
    pthread_t thread = {};
    std::atomic<bool> running{false};
    std::atomic<bool> completed{false};
    bool showWhenReady = false;
    int result = 0;
    std::string catalogType;
    std::string streamType;
    std::string streamId;
    std::string cacheKey;
    std::vector<std::string> addonUrls;
    std::vector<StreamItem> streams;
};

struct HeroArtworkJob {
    pthread_t thread = {};
    std::atomic<bool> running{false};
    std::atomic<bool> completed{false};
    std::string itemId;
    std::string url;
    std::string logoUrl;
    PosterImage artwork;
    PosterImage logo;
    int result = 0;
};

void* heroArtworkEntry(void* argument) {
    HeroArtworkJob* job = static_cast<HeroArtworkJob*>(argument);
    std::string encoded;
    std::string url = job->url;
    url += url.find('?') == std::string::npos ? "?format=jpg" : "&format=jpg";
    const std::string stored = cachePath("background", url, "jpg");
    const bool cached = readCachedFile(stored, kMaximumPosterBytes, encoded);
    const int bytes = cached ? static_cast<int>(encoded.size()) :
        downloadUrl(url.c_str(), kMaximumPosterBytes, encoded);
    PosterImage decoded;
    job->result = bytes > 0 &&
        decodePosterJpeg(encoded, 1920, 1080, decoded) ? 1 : -1;
    if (job->result > 0 && !cached) writeCachedFile(stored, encoded);
    if (job->result > 0) {
        constexpr int targetWidth = kWidth - kSideRailCollapsed;
        constexpr int targetHeight = kHeight;
        job->artwork.width = targetWidth;
        job->artwork.height = targetHeight;
        job->artwork.pixels.resize(
            static_cast<size_t>(targetWidth) * targetHeight);
        const float scale = std::max(
            static_cast<float>(targetWidth) / decoded.width,
            static_cast<float>(targetHeight) / decoded.height);
        const int sourceWidth = static_cast<int>(targetWidth / scale);
        const int sourceHeight = static_cast<int>(targetHeight / scale);
        const int sourceX = (decoded.width - sourceWidth) / 2;
        const int sourceY = (decoded.height - sourceHeight) / 2;
        std::vector<int> sourceColumns(static_cast<size_t>(targetWidth));
        for (int x = 0; x < targetWidth; ++x)
            sourceColumns[static_cast<size_t>(x)] =
                sourceX + x * sourceWidth / targetWidth;
        for (int y = 0; y < targetHeight; ++y) {
            const int sy = sourceY + y * sourceHeight / targetHeight;
            for (int x = 0; x < targetWidth; ++x) {
                const int sx = sourceColumns[static_cast<size_t>(x)];
                const uint32_t source = decoded.pixels[
                    static_cast<size_t>(sy) * decoded.width + sx];
                // Precompose one continuous cinema-style scrim into the hero
                // once, off the render thread. This removes the center seam
                // and avoids blending millions of pixels during navigation.
                const int horizontal = x < 1050 ? 205 - x * 190 / 1050 : 15;
                // The lower half remains visible behind the catalog while a
                // stronger scrim keeps poster labels readable.
                const int vertical = y > 330 ?
                    std::min(205, (y - 330) * 205 / 520) : 0;
                const int opacity = std::min(232,
                    horizontal + vertical - horizontal * vertical / 255);
                const int inverse = 255 - opacity;
                const uint32_t red = ((((source >> 16) & 255) * inverse) +
                    8 * opacity) / 255;
                const uint32_t green = ((((source >> 8) & 255) * inverse) +
                    7 * opacity) / 255;
                const uint32_t blue = (((source & 255) * inverse) +
                    13 * opacity) / 255;
                job->artwork.pixels[static_cast<size_t>(y) * targetWidth + x] =
                    (red << 16) | (green << 8) | blue;
            }
        }
    }
    if (job->logoUrl.compare(0, 8, "https://") == 0) {
        std::string logoEncoded;
        const std::string logoStored = cachePath("logo", job->logoUrl, "png");
        const bool logoCached = readCachedFile(
            logoStored, kMaximumPosterBytes, logoEncoded);
        const int logoBytes = logoCached ? static_cast<int>(logoEncoded.size()) :
            downloadUrl(job->logoUrl.c_str(), kMaximumPosterBytes, logoEncoded);
        if (logoBytes > 0 && decodePosterImageContain(
                logoEncoded, 440, 82, job->logo) && !logoCached)
            writeCachedFile(logoStored, logoEncoded);
    }
    job->completed.store(true, std::memory_order_release);
    job->running.store(false, std::memory_order_release);
    return nullptr;
}

void* streamLookupEntry(void* argument) {
    StreamLookupJob* job = static_cast<StreamLookupJob*>(argument);
    job->streams.clear();
    if (job->catalogType == "publicdomain")
        job->result = fetchPublicDomainStreams(job->streamId, job->streams);
    else job->result = fetchAddonStreams(job->addonUrls, job->streamType,
        job->streamId, job->streams);
    job->completed.store(true, std::memory_order_release);
    job->running.store(false, std::memory_order_release);
    return nullptr;
}

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
        // Resolve the exact rendition URLs once. The companion carries the
        // torrent/session token in each URI; dropping that query makes Sony
        // AVPlayer reject the audio source at AddSource (0x806a0002).
        std::string master;
        if (downloadUrl((job->baseUrl + "master.m3u8").c_str(),
                1024 * 1024, master) > 0) {
            size_t position = 0;
            bool audioSeen = false;
            while (position < master.size()) {
                size_t end = master.find('\n', position);
                if (end == std::string::npos) end = master.size();
                std::string line = master.substr(position, end - position);
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (line.find("#EXT-X-MEDIA:TYPE=AUDIO") == 0) {
                    const std::string uri = hlsQuotedUri(line);
                    if (!uri.empty()) {
                        job->audioPlaylistUrl = hlsAbsoluteUrl(job->baseUrl, uri);
                        audioSeen = true;
                    }
                } else if (!line.empty() && line[0] != '#' &&
                    line.find(".m3u8") != std::string::npos &&
                    job->videoPlaylistUrl.empty()) {
                    job->videoPlaylistUrl = hlsAbsoluteUrl(job->baseUrl, line);
                }
                position = end + 1;
            }
            (void)audioSeen;
        }
        if (job->videoPlaylistUrl.empty())
            job->videoPlaylistUrl = job->baseUrl + "video0.m3u8";
        std::string playlist;
        int playlistResult = -1;
        for (int attempt = 0; attempt < 6 && job->active; ++attempt) {
            playlist.clear();
            playlistResult = downloadUrl(
                job->videoPlaylistUrl.c_str(), 1024 * 1024,
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
                    job->segmentUrls.push_back(hlsAbsoluteUrl(job->baseUrl, line));
                position = end + 1;
            }
            std::string init;
            std::string initUrl = job->baseUrl + "video0/init.mp4";
            const size_t mapAt = playlist.find("#EXT-X-MAP:");
            if (mapAt != std::string::npos) {
                const size_t mapEnd = playlist.find('\n', mapAt);
                const std::string uri = hlsQuotedUri(playlist.substr(mapAt,
                    mapEnd == std::string::npos ? std::string::npos : mapEnd - mapAt));
                if (!uri.empty()) initUrl = hlsAbsoluteUrl(job->baseUrl, uri);
            }
            if (job->segmentUrls.empty() ||
                downloadUrl(initUrl.c_str(),
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
    AvPlayerProbe streamAudioPlayer;
    bool streamAudioFailureReported = false;
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
    bool playerHudVisible = true;
    int playerAspectMode = 0;
    int playerAudioOffsetMs = 0;
    int playerUiRedrawFrames = 2;
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
    int catalogHorizontalMotion = 0;
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
    std::string uiConfig;
    if (readCachedFile(kUiConfigPath, 16, uiConfig))
        androidTvMode = uiConfig != "grid";
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
    StreamLookupJob streamLookupJob;
    HeroArtworkJob heroJob;
    PosterImage heroArtwork;
    PosterImage heroLogo;
    std::string heroArtworkId;
    std::string heroRequestedId;
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

    auto resetHlsSession = [&]() {
        hlsJob.active = false;
        hlsJob.segmentWaiting = false;
        const bool running = hlsJob.running.load(std::memory_order_acquire);
        if (running) pthread_cancel(hlsJob.thread);
        if (running || hlsJob.completed.load(std::memory_order_acquire))
            pthread_join(hlsJob.thread, nullptr);
        hlsJob.running.store(false, std::memory_order_release);
        hlsJob.completed.store(false, std::memory_order_release);
        hlsJob.segmentUrls.clear();
        hlsJob.videoPlaylistUrl.clear();
        hlsJob.audioPlaylistUrl.clear();
        hlsJob.nextSegment = 0;
        hlsJob.readySegment = -1;
        videoDec2.stop();
        streamAudioPlayer.stop();
        directPreviewPixels.clear();
        directPreviewWidth = 0;
        directPreviewHeight = 0;
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
            animationFrame, catalogMotion, catalogHorizontalMotion,
            nullptr, nullptr);
        scene.SubmitFlip(frameId);
        scene.FrameWait(frameId);
        scene.FrameBufferSwap();
        ++frameId;
    }
    activateCatalog(0, false);

    while (!exitRequested) {
        streamAudioPlayer.update();
        if (streamAudioPlayer.state() == AvPlayerProbe::State::Failed &&
            !streamAudioFailureReported) {
            char failure[112];
            snprintf(failure, sizeof(failure),
                "Stremio: stream audio unavailable, stage %d code 0x%08x",
                streamAudioPlayer.errorStage(),
                static_cast<unsigned int>(streamAudioPlayer.errorCode()));
            notify(failure);
            streamAudioFailureReported = true;
        }
        if (heroJob.completed.exchange(false, std::memory_order_acq_rel)) {
            pthread_join(heroJob.thread, nullptr);
            if (heroJob.result > 0) {
                heroArtwork = std::move(heroJob.artwork);
                heroLogo = std::move(heroJob.logo);
                heroArtworkId = heroJob.itemId;
            } else if (heroRequestedId == heroJob.itemId) {
                // Catalog poster workers share Sony's HTTP context. Retry the
                // hero after they become idle instead of permanently caching
                // a transient concurrent-request failure.
                heroRequestedId.clear();
            }
            staticScreenKey = ~0ull;
        }
        if (streamLookupJob.completed.exchange(false,
                std::memory_order_acq_rel)) {
            pthread_join(streamLookupJob.thread, nullptr);
            if (streamLookupJob.showWhenReady &&
                streamLookupJob.result > 0) {
                streams.swap(streamLookupJob.streams);
                saveStreamCache(streamLookupJob.cacheKey, streams);
                focusedStream = 0;
                streamVisible = true;
                detailVisible = true;
            } else if (streamLookupJob.showWhenReady) {
                char failure[96];
                snprintf(failure, sizeof(failure),
                    "Stremio: stream lookup failed at stage %d",
                    -streamLookupJob.result);
                notify(failure);
            }
            streamLookupJob.showWhenReady = false;
            staticScreenKey = ~0ull;
        }
        if (hlsJob.completed.exchange(false, std::memory_order_acq_rel)) {
            pthread_join(hlsJob.thread, nullptr);
            if (hlsJob.active && hlsJob.result == 0) {
                const VideoDec2Probe::State state = videoDec2.state();
                if (state == VideoDec2Probe::State::Idle ||
                    state == VideoDec2Probe::State::Finished) {
                    if (streamAudioPlayer.state() == AvPlayerProbe::State::Idle) {
                        streamAudioFailureReported = false;
                        if (!hlsJob.audioPlaylistUrl.empty())
                            streamAudioPlayer.start(
                                hlsJob.audioPlaylistUrl.c_str(), false, false, true);
                    }
                    videoDec2.start(hlsJob.outputPath.c_str());
                    playerUiRedrawFrames = kFrameBuffers;
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
                streamAudioPlayer.stop();
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
            streamAudioPlayer.stop();
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
                resetHlsSession();
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
            if ((pressed & ORBIS_PAD_BUTTON_CROSS) != 0) {
                videoDec2.togglePause();
                if (hlsJob.active) streamAudioPlayer.togglePause();
            }
            if ((pressed & ORBIS_PAD_BUTTON_TRIANGLE) != 0) {
                videoDec2.restart();
                if (hlsJob.active) streamAudioPlayer.restart();
            }
            if ((pressed & ORBIS_PAD_BUTTON_SQUARE) != 0) {
                playerHudVisible = !playerHudVisible;
                playerUiRedrawFrames = kFrameBuffers;
            }
            if ((pressed & ORBIS_PAD_BUTTON_UP) != 0) {
                playerAspectMode = (playerAspectMode + 1) % 2;
                playerUiRedrawFrames = kFrameBuffers;
            }
            if (hlsJob.active &&
                (pressed & ORBIS_PAD_BUTTON_L1) != 0) {
                streamAudioPlayer.seekRelative(-250);
                playerAudioOffsetMs -= 250;
                playerUiRedrawFrames = kFrameBuffers;
            }
            if (hlsJob.active &&
                (pressed & ORBIS_PAD_BUTTON_R1) != 0) {
                streamAudioPlayer.seekRelative(250);
                playerAudioOffsetMs += 250;
                playerUiRedrawFrames = kFrameBuffers;
            }
        }

        const bool shellVisible =
            !previewVisible && !detailVisible && !streamVisible &&
            videoDec2.state() == VideoDec2Probe::State::Idle;
        if (shellVisible && androidTvMode && activeTab < 4 &&
            !catalogItems.empty()) {
            bool catalogNetworkBusy = pageJob.running.load(
                std::memory_order_acquire);
            for (int index = 0; index < 3; ++index)
                catalogNetworkBusy = catalogNetworkBusy ||
                    catalogJobs[index].running.load(std::memory_order_acquire);
            const int selectedIndex = catalogPage * kCatalogPageSize + focusedCard;
            if (selectedIndex >= 0 &&
                selectedIndex < static_cast<int>(catalogItems.size())) {
                const CatalogItem& selected = catalogItems[selectedIndex];
                if (selected.id != heroRequestedId &&
                    !catalogNetworkBusy &&
                    !heroJob.running.load(std::memory_order_acquire)) {
                    heroRequestedId = selected.id;
                    heroJob.itemId = selected.id;
                    heroJob.url = selected.background.compare(0, 8, "https://") == 0
                        ? selected.background
                        : (selected.id.compare(0, 2, "tt") == 0
                            ? "https://images.metahub.space/background/medium/" +
                                selected.id + "/img"
                            : selected.poster);
                    heroJob.logoUrl = selected.logo.compare(0, 8, "https://") == 0
                        ? selected.logo
                        : (selected.id.compare(0, 2, "tt") == 0
                            ? "https://images.metahub.space/logo/medium/" +
                                selected.id + "/img" : "");
                    heroJob.result = 0;
                    heroJob.artwork = {};
                    heroJob.logo = {};
                    heroJob.running.store(true, std::memory_order_release);
                    heroJob.completed.store(false, std::memory_order_release);
                    if (pthread_create(&heroJob.thread, nullptr,
                            heroArtworkEntry, &heroJob) != 0) {
                        heroJob.running.store(false, std::memory_order_release);
                        heroRequestedId.clear();
                    }
                }
            }
        }
        const bool catalogScreen = activeTab < 3 ||
            (activeTab == 3 && searchShowingResults);
        if (shellVisible && !navigationFocused &&
            (pressed & ORBIS_PAD_BUTTON_R1) != 0) {
            selectTab(activeTab + 1);
        } else if (shellVisible && !navigationFocused &&
            (pressed & ORBIS_PAD_BUTTON_L1) != 0) {
            selectTab(activeTab - 1);
        }

        bool navigationInputConsumed = false;
        if (shellVisible && useSideNavigation) {
            const int absoluteSelection =
                catalogPage * kCatalogPageSize + focusedCard;
            const bool canEnterRail = catalogScreen ?
                absoluteSelection == 0 : true;
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
                    navigationInputConsumed = true;
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

        if (shellVisible && !navigationFocused && !navigationInputConsumed && activeTab == 3 &&
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
        } else if (shellVisible && !navigationFocused && !navigationInputConsumed && catalogScreen) {
            if (androidTvMode) {
                int absolute = catalogPage * kCatalogPageSize + focusedCard;
                if ((pressed & ORBIS_PAD_BUTTON_LEFT) != 0 && absolute > 0) {
                    const int oldAbsolute = absolute--;
                    catalogPage = absolute / kCatalogPageSize;
                    focusedCard = absolute % kCatalogPageSize;
                    (void)oldAbsolute;
                }
                if ((pressed & ORBIS_PAD_BUTTON_RIGHT) != 0 &&
                    absolute + 1 < static_cast<int>(catalogItems.size())) {
                    ++absolute;
                    catalogPage = absolute / kCatalogPageSize;
                    focusedCard = absolute % kCatalogPageSize;
                    if (static_cast<int>(catalogItems.size()) - absolute < 8)
                        startPagePrefetch();
                }
            } else {
                if ((pressed & ORBIS_PAD_BUTTON_LEFT) != 0 && focusedCard > 0)
                    --focusedCard;
                if ((pressed & ORBIS_PAD_BUTTON_RIGHT) != 0 &&
                    focusedCard < kCatalogPageSize - 1 &&
                    catalogPage * kCatalogPageSize + focusedCard + 1 <
                        static_cast<int>(catalogItems.size())) ++focusedCard;
            }
            if (!androidTvMode && (pressed & ORBIS_PAD_BUTTON_DOWN) != 0) {
                const int nextPage = catalogPage + 1;
                if ((nextPage + 1) * kCatalogPageSize >=
                        static_cast<int>(catalogItems.size()) &&
                    activeTab < 3) {
                    catalogStatus = "LOADING MORE IN BACKGROUND...";
                    startPagePrefetch();
                }
                if (nextPage * kCatalogPageSize <
                    static_cast<int>(catalogItems.size())) {
                    catalogPage = nextPage;
                    // New selection enters from below; the preview row itself
                    // remains stationary until it becomes the active row.
                    catalogMotion = 0;
                    if (catalogPage * kCatalogPageSize + focusedCard >=
                        static_cast<int>(catalogItems.size())) focusedCard = 0;
                }
            }
            if (!androidTvMode && (pressed & ORBIS_PAD_BUTTON_UP) != 0 && catalogPage > 0) {
                --catalogPage;
                catalogMotion = 0;
            }
            if (activeTab == 3 &&
                (pressed & ORBIS_PAD_BUTTON_CIRCLE) != 0) {
                searchShowingResults = false;
                catalogItems.clear();
                catalogPosters.clear();
            }
        } else if (shellVisible && !navigationFocused && !navigationInputConsumed && activeTab == 4) {
            const int maximumSelection = settingsPage == 0 ? 7 :
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
                    androidTvMode = !androidTvMode;
                    writeCachedFile(kUiConfigPath,
                        androidTvMode ? "android-tv" : "grid");
                    notify(androidTvMode
                        ? "Stremio: Android TV home layout saved"
                        : "Stremio: compact grid home layout saved");
                    staticScreenKey = ~0ull;
                } else if (settingsPage == 0 && settingsSelection == 7) {
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
            const int selectedIndex =
                catalogPage * kCatalogPageSize + focusedCard;
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
            !streamVisible && !streamLookupJob.running.load(
                std::memory_order_acquire)) {
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
            if (addonUrls.empty() && catalogType != "publicdomain") {
                notify("Stremio: link account and sync addons in Settings");
            } else {
                const std::string lookupCacheKey = streamType + ":" + streamId;
                std::vector<StreamItem> cachedStreams;
                if (loadStreamCache(lookupCacheKey, cachedStreams)) {
                    streams.swap(cachedStreams);
                    focusedStream = 0;
                    streamVisible = true;
                    staticScreenKey = ~0ull;
                    continue;
                }
                stopBackgroundForPlayback();
                streamLookupJob.catalogType = catalogType;
                streamLookupJob.streamType = streamType;
                streamLookupJob.streamId = streamId;
                streamLookupJob.cacheKey = lookupCacheKey;
                streamLookupJob.addonUrls = addonUrls;
                streamLookupJob.result = 0;
                streamLookupJob.showWhenReady = true;
                streamLookupJob.completed.store(false,
                    std::memory_order_release);
                streamLookupJob.running.store(true,
                    std::memory_order_release);
                if (pthread_create(&streamLookupJob.thread, nullptr,
                        streamLookupEntry, &streamLookupJob) != 0) {
                    streamLookupJob.running.store(false,
                        std::memory_order_release);
                    streamLookupJob.showWhenReady = false;
                    notify("Stremio: stream lookup worker could not start");
                }
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
                    resetHlsSession();
                    stopBackgroundForPlayback();
                    avPlayer.stop();
                    videoDec2.stop();
                    previewVisible = false;
                    hlsJob.active = true;
                    hlsJob.segmentWaiting = false;
                    hlsJob.nextSegment = 0;
                    hlsJob.readySegment = -1;
                    hlsJob.segmentUrls.clear();
                    hlsJob.videoPlaylistUrl.clear();
                    hlsJob.audioPlaylistUrl.clear();
                    hlsJob.config = {};
                    char hlsBase[256];
                    snprintf(hlsBase, sizeof(hlsBase),
                        "http://%s/%s/%d/", companionAddress.c_str(),
                        stream.infoHash.c_str(),
                        stream.fileIndex >= 0 ? stream.fileIndex : -1);
                    hlsJob.baseUrl = hlsBase;
                    playerHudVisible = true;
                    playerAspectMode = 0;
                    playerAudioOffsetMs = 0;
                    playerUiRedrawFrames = kFrameBuffers;
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
        // Direct test playback is settings-only. Square belongs exclusively
        // to the active player's HUD and must never leak into shell shortcuts.
        const bool directPlaybackRequested = playbackDirectVideoDec2 &&
            queuedPlaybackTest == 0;
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
            resetHlsSession();
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
        } else if ((pressed & ORBIS_PAD_BUTTON_CIRCLE) != 0 &&
            streamLookupJob.running.load(std::memory_order_acquire)) {
            streamLookupJob.showWhenReady = false;
            detailVisible = false;
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
            const bool newFrame = videoDec2.copyPreview(directPreviewPixels,
                directPreviewWidth, directPreviewHeight);
            if (newFrame)
                // Keep both alternating display buffers synchronized. The
                // scaler itself is optimized to avoid per-pixel divisions.
                playerUiRedrawFrames = std::max(
                    playerUiRedrawFrames, kFrameBuffers);
            if (playerUiRedrawFrames > 0) {
                drawVideoDec2Test(scene, videoDec2, directPreviewPixels,
                    directPreviewWidth, directPreviewHeight, details.name,
                    playerHudVisible, playerAspectMode, playerAudioOffsetMs,
                    streamAudioPlayer.state() == AvPlayerProbe::State::Passed);
                if (playerUiRedrawFrames > 0) --playerUiRedrawFrames;
            }
            static VideoDec2Probe::State reportedVideoDec2State =
                VideoDec2Probe::State::Idle;
            if (videoDec2.state() != reportedVideoDec2State) {
                if (videoDec2.state() == VideoDec2Probe::State::Failed) {
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
        } else if (streamLookupJob.running.load(std::memory_order_acquire) &&
            streamLookupJob.showWhenReady) {
            staticScreenKey = ~0ull;
            drawStreamLookup(scene, details, animationFrame);
        } else if (streamVisible) {
            const uint64_t key = 0x5700000000000000ull |
                (static_cast<uint64_t>(focusedStream) << 24) |
                static_cast<uint32_t>(streams.size());
            if (key != staticScreenKey) {
                staticScreenKey = key;
                staticScreenFrames = kFrameBuffers;
            }
            if (staticScreenFrames > 0) {
                drawStreams(scene, details, streams, focusedStream);
                --staticScreenFrames;
            }
        } else if (detailVisible) {
            uint64_t key = 0x4400000000000000ull |
                (static_cast<uint64_t>(detailEpisodeIndex) << 24) |
                static_cast<uint32_t>(detailPosterIndex + 1);
            key ^= stableHash(details.name);
            if (key != staticScreenKey) {
                staticScreenKey = key;
                staticScreenFrames = kFrameBuffers;
            }
            const PosterImage* poster =
                detailPosterIndex >= 0 &&
                detailPosterIndex < static_cast<int>(catalogPosters.size())
                ? &catalogPosters[detailPosterIndex] : nullptr;
            if (staticScreenFrames > 0) {
                drawDetails(
                    scene, details, poster, catalogType, detailEpisodeIndex);
                --staticScreenFrames;
            }
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
            uint64_t key = 0x4300000000000000ull |
                (static_cast<uint64_t>(activeTab) << 52) |
                (static_cast<uint64_t>(catalogPage) << 32) |
                (static_cast<uint64_t>(focusedCard) << 24) |
                (static_cast<uint64_t>(displayedPosterCount) << 8) |
                static_cast<uint8_t>(catalogMotion & 0xff);
            key ^= stableHash(catalogStatus);
            key ^= navigationFocused ? 0x100000ull : 0;
            key ^= useSideNavigation ? 0x200000ull : 0;
            key ^= androidTvMode ? 0x400000ull : 0;
            key ^= static_cast<uint64_t>(navigationWidth) << 40;
            key ^= static_cast<uint64_t>(
                static_cast<uint16_t>(catalogHorizontalMotion)) << 16;
            key ^= stableHash(heroArtworkId);
            if (key != staticScreenKey) {
                staticScreenKey = key;
                staticScreenFrames = kFrameBuffers;
            }
            if (staticScreenFrames > 0) {
                const int selectedIndex = catalogPage * kCatalogPageSize + focusedCard;
                const PosterImage* activeHero = androidTvMode &&
                    selectedIndex >= 0 && selectedIndex < static_cast<int>(catalogItems.size()) &&
                    catalogItems[selectedIndex].id == heroArtworkId && heroArtwork.valid()
                    ? &heroArtwork : nullptr;
                const PosterImage* activeLogo = activeHero && heroLogo.valid()
                    ? &heroLogo : nullptr;
                drawHardwareProbe(
                    scene, focusedCard, catalogPage, catalogType, catalogStatus,
                    catalogItems, catalogPosters, activeTab, indicatorX,
                    animationFrame, catalogMotion, catalogHorizontalMotion,
                    activeHero, activeLogo);
                --staticScreenFrames;
            }
        }
        scene.SubmitFlip(frameId);
        scene.FrameWait(frameId);
        scene.FrameBufferSwap();
        ++frameId;
        ++animationFrame;
        const int navigationTarget = useSideNavigation && navigationFocused ?
            kSideRailExpanded : kSideRailCollapsed;
        if (navigationWidth != navigationTarget) {
            // A full 1080p software redraw for every intermediate width made
            // the rail animation run at 8-10 FPS. Switch atomically and let
            // both framebuffers receive the finished layout at 60 Hz.
            navigationWidth = navigationTarget;
            staticScreenKey = ~0ull;
        }
        if (catalogMotion != 0) {
            catalogMotion = catalogMotion * 3 / 4;
            if (catalogMotion > -3 && catalogMotion < 3) catalogMotion = 0;
        }
        catalogHorizontalMotion = 0;
        if (activeTab < 3 && !catalogItems.empty() &&
            static_cast<int>(catalogItems.size()) -
                (catalogPage + 2) * kCatalogPageSize <= kCatalogPageSize)
            startPagePrefetch();

    }

    DEBUGLOG << "Stremio graceful shutdown starting";
    avPlayer.stop();
    streamAudioPlayer.stop();
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
    streamLookupJob.showWhenReady = false;
    if (streamLookupJob.running.load(std::memory_order_acquire) ||
        streamLookupJob.completed.load(std::memory_order_acquire))
        pthread_join(streamLookupJob.thread, nullptr);
    if (heroJob.running.load(std::memory_order_acquire) ||
        heroJob.completed.load(std::memory_order_acquire))
        pthread_join(heroJob.thread, nullptr);
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
