#include "fresh_shell.h"

#include <algorithm>
#include <cstdio>

namespace freshui {
namespace {
constexpr int W = 1920;
constexpr int H = 1080;
const Color bg{7, 8, 12};
const Color surface{25, 26, 34};
const Color surface2{38, 38, 49};
const Color violet{126, 86, 232};
const Color white{245, 243, 249};
Assets gAssets;
Preferences gPrefs;

Color secondary() { return gPrefs.highContrast ? Color{220, 216, 228} : Color{168, 164, 179}; }
void center(Scene2D& s, int x, int y, int w, const char* value, Color color, int scale) {
    s.DrawText(x + (w - s.MeasureText(value, scale)) / 2, y, value, color, scale);
}
void pill(Scene2D& s, int x, int y, int w, int h, const char* value, bool active) {
    s.DrawRoundedRectangle(x, y, w, h, h / 2, active ? white : surface2);
    center(s, x, y + 15, w, value, active ? bg : secondary(), 2);
}
void brand(Scene2D& s) {
    if (gAssets.brand && gAssets.brand->valid())
        s.BlitRgbScaledRounded(54, 25, 58, 58, 15, gAssets.brand->pixels.data(),
            gAssets.brand->width, gAssets.brand->height);
    s.DrawText(132, 40, "STREMIO", white, 3);
}
void top(Scene2D& s, int active) {
    s.DrawRectangle(0, 0, W, 104, Color{8, 8, 12});
    brand(s);
    const char* names[] = {"MOVIES", "SERIES", "PUBLIC", "SEARCH", "SETTINGS"};
    const int widths[] = {150, 150, 150, 150, 175};
    int x = 560;
    for (int i = 0; i < 5; ++i) { pill(s, x, 26, widths[i], 52, names[i], i == active); x += widths[i] + 18; }
}
void footer(Scene2D& s, const char* left, const char* right) {
    s.DrawRectangle(0, 980, W, 100, Color{9, 9, 14});
    if (!gPrefs.showHints) return;
    s.DrawText(62, 1014, left, secondary(), 2);
    const int rw = s.MeasureText(right, 2);
    s.DrawText(W - rw - 62, 1014, right, white, 2);
}
void sideMenu(Scene2D& s, int active) {
    s.DrawRectangle(0, 0, 430, H, Color{6, 6, 10});
    brand(s);
    const char* labels[] = {"Movies", "Series", "Public domain", "Search", "Settings"};
    for (int i = 0; i < 5; ++i) {
        const int y = 190 + i * 112;
        if (i == active) s.DrawRoundedRectangle(36, y, 350, 72, 36, white);
        char index[8]; std::snprintf(index, sizeof(index), "%02d", i + 1);
        s.DrawText(65, y + 23, index, i == active ? bg : violet, 2);
        s.DrawText(135, y + 23, labels[i], i == active ? bg : secondary(), 2);
    }
    s.DrawText(54, 900, "STREMIO FOR PLAYSTATION", secondary(), 2);
    s.DrawText(54, 940, "BY JAVASCRIPTX", violet, 2);
}
std::string trim(const std::string& value, size_t n) {
    return value.size() <= n ? value : value.substr(0, n - 3) + "...";
}
}

void configure(const Assets& assets, const Preferences& preferences) { gAssets = assets; gPrefs = preferences; }

void loading(Scene2D& s, int phase, const char* message) {
    s.FrameBufferFill(bg);
    s.DrawVerticalFade(0, 0, W, H, Color{35, 18, 64}, 185, 0);
    if (gAssets.brand && gAssets.brand->valid())
        s.BlitRgbScaledRounded(850, 310, 220, 220, 48, gAssets.brand->pixels.data(),
            gAssets.brand->width, gAssets.brand->height);
    center(s, 0, 570, W, "STREMIO", white, 6);
    center(s, 0, 652, W, "BY JAVASCRIPTX", violet, 2);
    s.DrawRoundedRectangle(710, 730, 500, 10, 5, surface2);
    s.DrawRoundedRectangle(710, 730, 100 + (phase % 5) * 100, 10, 5, violet);
    center(s, 0, 772, W, message ? message : "LOADING", secondary(), 2);
}

void home(Scene2D& s, const std::vector<CatalogItem>& items,
    const std::vector<PosterImage>& posters, int selected, int activeTab,
    const PosterImage* backdrop, const PosterImage* titleLogo,
    bool menuOpen, const std::string& status) {
    if (items.empty()) { loading(s, selected, status.c_str()); return; }
    s.FrameBufferFill(bg);
    if (backdrop && backdrop->valid()) s.BlitRgbMasked(0, 0, backdrop->width, backdrop->height, backdrop->pixels.data());
    s.DrawVerticalFade(0, 100, W, 520, Color{7, 8, 12}, 210, 10);
    top(s, activeTab);
    const CatalogItem& item = items[std::min<int>(selected, items.size() - 1)];
    if (titleLogo && titleLogo->valid()) s.BlitRgba(82, 170, titleLogo->width, titleLogo->height, titleLogo->pixels.data());
    else s.DrawText(82, 175, trim(item.name, 34).c_str(), white, 5);
    std::string facts = item.releaseInfo + "   " + item.runtime + "   IMDB " + item.imdbRating;
    s.DrawText(82, 285, facts.c_str(), white, 2);
    s.DrawText(82, 330, trim(item.genres, 52).c_str(), secondary(), 2);
    s.DrawText(82, 382, trim(item.description, 82).c_str(), white, 2);
    pill(s, 82, 465, 220, 62, "X  DETAILS", true);
    s.DrawText(82, 565, "RECOMMENDED FOR YOU", white, 3);
    const int start = std::max(0, selected - 2);
    for (int slot = 0; slot < 7; ++slot) {
        const int index = start + slot; if (index >= static_cast<int>(items.size())) break;
        const int x = 82 + slot * 258;
        const bool focused = index == selected;
        if (focused) s.DrawRoundedRectangle(x - 8, 614, 230, 318, 24, white);
        if (index < static_cast<int>(posters.size()) && posters[index].valid())
            s.BlitRgbScaledRounded(x, 622, 214, 302, 18, posters[index].pixels.data(), posters[index].width, posters[index].height);
        else s.DrawRoundedRectangle(x, 622, 214, 302, 18, surface2);
        s.DrawText(x, 940, trim(items[index].name, 20).c_str(), focused ? white : secondary(), 2);
    }
    footer(s, "L3  BROWSE", "X  OPEN");
    if (menuOpen) sideMenu(s, activeTab);
}

void search(Scene2D& s, const std::string& query, int filter, bool menuOpen) {
    s.FrameBufferFill(bg); top(s, 3);
    s.DrawText(82, 150, "SEARCH", white, 5);
    s.DrawText(82, 220, "FIND MOVIES, SERIES AND PUBLIC DOMAIN TITLES", secondary(), 2);
    const char* labels[] = {"MOVIES", "SERIES", "PUBLIC DOMAIN"};
    int x = 82; for (int i = 0; i < 3; ++i) { pill(s, x, 275, i == 2 ? 250 : 190, 58, labels[i], i == filter); x += i == 2 ? 268 : 208; }
    s.DrawRoundedRectangle(82, 375, 1756, 135, 30, surface);
    s.DrawText(125, 398, "TITLE", violet, 2);
    s.DrawText(125, 445, query.empty() ? "TYPE A TITLE..." : query.c_str(), query.empty() ? secondary() : white, 4);
    s.DrawRoundedRectangle(82, 565, 840, 270, 30, surface);
    s.DrawText(125, 610, "PLAYSTATION KEYBOARD", white, 3);
    s.DrawText(125, 675, "CROSS  TYPE     SQUARE  DELETE", secondary(), 2);
    s.DrawText(125, 720, "TRIANGLE  SPACE     R2  SEARCH", secondary(), 2);
    s.DrawRoundedRectangle(950, 565, 888, 270, 30, surface);
    s.DrawText(995, 610, "SEARCH OPTIONS", white, 3);
    s.DrawText(995, 675, "LEFT / RIGHT  CHANGE CATEGORY", secondary(), 2);
    s.DrawText(995, 720, "RESULTS OPEN WITH DETAILS AND STREAMS", secondary(), 2);
    footer(s, "CIRCLE  BACK", "X  KEYBOARD     TRIANGLE  SEARCH");
    if (menuOpen) sideMenu(s, 3);
}

void dashboard(Scene2D& s, int selected, bool menuOpen) {
    s.FrameBufferFill(bg); top(s, 4); s.DrawText(82, 150, "SETTINGS", white, 5);
    const char* title[] = {"ACCOUNT", "PLAYBACK", "APPEARANCE", "NETWORK", "STORAGE", "ABOUT"};
    const char* sub[] = {"Sync and addons", "Video and audio", "Interface and controls", "Companion server", "Cache and search", "Credits and project"};
    for (int i = 0; i < 6; ++i) {
        const int x = 82 + (i % 3) * 590, y = 260 + (i / 3) * 300;
        const bool active = i == selected;
        s.DrawRoundedRectangle(x, y, 550, 250, 30, active ? white : surface);
        char n[8]; std::snprintf(n, sizeof(n), "%02d", i + 1);
        s.DrawText(x + 35, y + 35, n, active ? violet : secondary(), 2);
        s.DrawText(x + 35, y + 105, title[i], active ? bg : white, 3);
        s.DrawText(x + 35, y + 165, sub[i], active ? Color{65, 62, 72} : secondary(), 2);
    }
    footer(s, "D-PAD  SELECT CATEGORY", "X  OPEN"); if (menuOpen) sideMenu(s, 4);
}

void list(Scene2D& s, const char* section, const char* const* rows, int count,
    int selected, bool menuOpen, const char* action) {
    s.FrameBufferFill(bg); top(s, 4); s.DrawText(82, 145, section, white, 4);
    for (int i = 0; i < count; ++i) {
        const int y = 235 + i * 100; const bool active = i == selected;
        s.DrawRoundedRectangle(82, y, 1756, 78, 24, active ? white : surface);
        char n[8]; std::snprintf(n, sizeof(n), "%02d", i + 1);
        s.DrawText(115, y + 25, n, active ? violet : secondary(), 2);
        s.DrawText(190, y + 25, rows[i], active ? bg : white, 2);
        if (active) s.DrawText(1775, y + 25, ">", bg, 2);
    }
    footer(s, "CIRCLE  BACK", action); if (menuOpen) sideMenu(s, 4);
}

void details(Scene2D& s, const MetaDetails& m, const PosterImage* poster,
    const PosterImage* backdrop, const PosterImage* logo, const std::string& type, int episode) {
    s.FrameBufferFill(bg); if (backdrop && backdrop->valid()) s.BlitRgbMasked(0, 0, backdrop->width, backdrop->height, backdrop->pixels.data());
    s.DrawVerticalFade(0, 0, W, H, bg, 210, 60); brand(s);
    if (poster && poster->valid()) s.BlitRgbScaledRounded(92, 190, 330, 465, 24, poster->pixels.data(), poster->width, poster->height);
    if (logo && logo->valid()) s.BlitRgba(520, 185, logo->width, logo->height, logo->pixels.data()); else s.DrawText(520, 190, trim(m.name, 38).c_str(), white, 5);
    std::string facts = (type == "series" ? "SERIES   " : "MOVIE   ") + m.releaseInfo + "   " + m.runtime + "   IMDB " + m.imdbRating;
    s.DrawText(520, 300, facts.c_str(), white, 2); s.DrawText(520, 345, m.genres.c_str(), violet, 2);
    s.DrawText(520, 420, "OVERVIEW", secondary(), 2); s.DrawText(520, 470, trim(m.description, 95).c_str(), white, 2);
    pill(s, 520, 650, 310, 68, type == "series" ? "CHOOSE EPISODE" : "FIND STREAMS", true);
    if (type == "series") { char e[64]; std::snprintf(e, sizeof(e), "EPISODE %d", episode + 1); s.DrawText(860, 672, e, white, 2); }
    footer(s, "CIRCLE  BACK", "X  CONTINUE");
}

void streams(Scene2D& s, const MetaDetails& m, const std::vector<StreamItem>& v, int selected) {
    s.FrameBufferFill(bg); brand(s); s.DrawText(82, 135, "CHOOSE A SOURCE", white, 5); s.DrawText(82, 205, trim(m.name, 60).c_str(), secondary(), 2);
    const int first = std::max(0, selected - 4);
    for (int r = 0; r < 6 && first + r < static_cast<int>(v.size()); ++r) {
        const int i = first + r, y = 280 + r * 105; const bool active = i == selected;
        s.DrawRoundedRectangle(82, y, 1756, 88, 24, active ? white : surface);
        char n[8]; std::snprintf(n, sizeof(n), "%02d", i + 1); s.DrawText(115, y + 30, n, active ? violet : secondary(), 2);
        const std::string label = !v[i].title.empty() ? v[i].title : (!v[i].name.empty() ? v[i].name : "STREAM");
        s.DrawText(190, y + 18, trim(label, 72).c_str(), active ? bg : white, 2);
        char info[128]; std::snprintf(info, sizeof(info), "SEEDS %d   PEERS %d   %s", v[i].seeders, v[i].peers, v[i].url.empty() ? "TORRENT" : "DIRECT");
        s.DrawText(190, y + 53, info, active ? Color{65, 62, 72} : secondary(), 2);
    }
    footer(s, "CIRCLE  DETAILS", "SQUARE  DOWNLOAD     X  STREAM");
}

void waitForStreams(Scene2D& s, const MetaDetails& m, int phase) {
    loading(s, phase, "CONTACTING YOUR ADDONS");
    center(s, 0, 850, W, trim(m.name, 52).c_str(), white, 2);
}

void account(Scene2D& s, const std::string& code, const std::string& link,
    const std::string& status, int addonCount, bool signedIn) {
    s.FrameBufferFill(bg); top(s, 4); s.DrawText(82, 145, "YOUR STREMIO", white, 5);
    s.DrawRoundedRectangle(82, 250, 1756, 590, 34, surface);
    s.DrawText(145, 320, signedIn ? "ACCOUNT CONNECTED" : "CONNECT YOUR ACCOUNT", white, 4);
    if (signedIn) {
        char addons[96]; std::snprintf(addons, sizeof(addons), "%d ADDON ENDPOINTS SYNCED", addonCount);
        s.DrawText(145, 420, addons, violet, 3);
        s.DrawText(145, 485, "YOUR LIBRARY AND ADDONS ARE READY ON THIS CONSOLE.", secondary(), 2);
        pill(s, 145, 650, 300, 66, "SYNC AGAIN", true);
    } else {
        s.DrawText(145, 410, "OPEN THIS ADDRESS ON ANOTHER DEVICE", secondary(), 2);
        s.DrawText(145, 460, link.c_str(), violet, 3);
        s.DrawText(145, 555, "LINK CODE", secondary(), 2);
        s.DrawText(145, 605, code.empty() ? "PRESS X TO CREATE" : code.c_str(), white, 5);
    }
    s.DrawText(145, 770, status.c_str(), secondary(), 2);
    footer(s, "CIRCLE  BACK", "X  CONTINUE");
}

void about(Scene2D& s) {
    s.FrameBufferFill(bg); top(s, 4); s.DrawText(82, 145, "ABOUT", white, 5);
    s.DrawRoundedRectangle(82, 250, 1756, 590, 34, surface);
    if (gAssets.creator && gAssets.creator->valid())
        s.BlitRgbScaledRounded(145, 320, 300, 300, 36, gAssets.creator->pixels.data(), gAssets.creator->width, gAssets.creator->height);
    s.DrawText(520, 320, "STREMIO FOR PLAYSTATION", white, 4);
    s.DrawText(520, 390, "CREATED BY TAHAR CHTIOUI", violet, 3);
    s.DrawText(520, 455, "GITHUB  @JAVASCRIPT-X", white, 2);
    s.DrawText(520, 540, "A COMMUNITY HOMEBREW CLIENT DESIGNED FOR THE PS4.", secondary(), 2);
    s.DrawText(520, 590, "CATALOGS, ADDONS, VIDEODEC2 VIDEO AND SONY AUDIO.", secondary(), 2);
    s.DrawText(520, 690, "VERSION 7.00  /  PS4 FW 13.02 + GOLDHEN", white, 2);
    footer(s, "CIRCLE  BACK", "STREMIO  BY JAVASCRIPTX");
}

void progress(Scene2D& s, int stage, uint64_t downloaded,
    uint64_t expected, uint64_t elapsedUs, int errorCode) {
    s.FrameBufferFill(bg); brand(s);
    s.DrawText(82, 175, "PREPARING PLAYBACK", white, 5);
    const char* names[] = {"CONNECTING", "RESOLVING SOURCE", "BUFFERING MEDIA",
        "PREPARING DECODER", "STARTING PLAYBACK"};
    const int safeStage = std::max(0, std::min(4, stage));
    s.DrawText(82, 250, names[safeStage], violet, 3);
    s.DrawRoundedRectangle(82, 375, 1756, 300, 34, surface);
    const uint64_t rate = elapsedUs ? downloaded * 1000000ULL / elapsedUs : 0;
    char transfer[160];
    std::snprintf(transfer, sizeof(transfer), "%llu MB RECEIVED     %llu KB/S",
        static_cast<unsigned long long>(downloaded / 1048576ULL),
        static_cast<unsigned long long>(rate / 1024ULL));
    s.DrawText(145, 445, transfer, white, 3);
    s.DrawRoundedRectangle(145, 545, 1630, 18, 9, surface2);
    int width = 220;
    if (expected) width = static_cast<int>(std::min<uint64_t>(1630, downloaded * 1630 / expected));
    s.DrawRoundedRectangle(145, 545, width, 18, 9, violet);
    if (errorCode) { char error[96]; std::snprintf(error, sizeof(error), "ERROR 0x%08x", static_cast<unsigned int>(errorCode)); s.DrawText(145, 615, error, Color{238, 112, 128}, 2); }
    footer(s, "PLEASE WAIT", "CIRCLE  CANCEL");
}

void player(Scene2D& s, const std::vector<uint32_t>& pixels,
    uint32_t width, uint32_t height, const std::string& title,
    bool hud, bool fill, bool audio, bool paused, uint64_t current,
    uint64_t duration, uint64_t frames, uint64_t fpsTimesTen,
    int audioOffsetMs) {
    s.FrameBufferFill(Color{0, 0, 0});
    if (!pixels.empty() && width && height) {
        int x = 0, y = 0, w = W, h = H;
        if (!fill) {
            h = static_cast<int>(static_cast<uint64_t>(W) * height / width);
            if (h > H) { h = H; w = static_cast<int>(static_cast<uint64_t>(H) * width / height); }
            x = (W - w) / 2; y = (H - h) / 2;
        }
        s.BlitRgbScaled(x, y, w, h, pixels.data(), width, height);
    }
    if (!hud && !pixels.empty()) return;
    s.DrawVerticalFade(0, 0, W, 230, bg, 235, 0);
    s.DrawVerticalFade(0, 820, W, 260, bg, 0, 245);
    s.DrawText(70, 45, "NOW PLAYING", violet, 2);
    s.DrawText(70, 90, trim(title.empty() ? "STREMIO STREAM" : title, 54).c_str(), white, 4);
    pill(s, 70, 165, 250, 50, "VIDEODEC2 H.264", false);
    pill(s, 338, 165, 220, 50, audio ? "AUDIO ACTIVE" : "AUDIO OFF", audio);
    pill(s, 576, 165, 190, 50, fill ? "ASPECT FILL" : "ASPECT FIT", false);
    const int progressWidth = duration ? static_cast<int>(current * 1640 / duration) : 0;
    s.DrawRoundedRectangle(140, 900, 1640, 12, 6, surface2);
    s.DrawRoundedRectangle(140, 900, progressWidth, 12, 6, white);
    char metrics[160]; std::snprintf(metrics, sizeof(metrics), "%s   %llu FRAMES   %llu.%llu FPS   AUDIO %+d MS",
        paused ? "PAUSED" : "PLAYING", static_cast<unsigned long long>(frames),
        static_cast<unsigned long long>(fpsTimesTen / 10),
        static_cast<unsigned long long>(fpsTimesTen % 10), audioOffsetMs);
    s.DrawText(140, 940, metrics, white, 2);
    footer(s, "X  PAUSE     TRIANGLE  RESTART     SQUARE  HIDE HUD", "CIRCLE  STOP");
}
}
