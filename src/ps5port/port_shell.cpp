// SPDX-License-Identifier: GPL-3.0-or-later
// Design port derived from the public UI structure of:
// https://github.com/Sp9nky/unofficial-stremio-ps5-port

#include "port_shell.h"
#include <algorithm>

namespace ps5port {
namespace {
const Color ground{12, 11, 17};
const Color accent{123, 91, 245};
const Color accentSoft{165, 139, 255};
const Color accentDeep{58, 42, 143};
const Color ink{250, 250, 252};
const Color ink2{191, 188, 202};
const Color ink3{145, 140, 158};
const Color surface{31, 29, 39};
const Color surfaceHi{49, 45, 62};
const Color hairline{58, 54, 69};
constexpr int railW = 140;
constexpr int left = 190;
constexpr int right = 1860;

std::string fit(const std::string& s, size_t n) {
    return s.size() <= n ? s : s.substr(0, n - 3) + "...";
}
void center(Scene2D& s, int x, int y, int w, const char* text, Color color, int scale) {
    s.DrawText(x + (w - s.MeasureText(text, scale)) / 2, y, text, color, scale);
}
void card(Scene2D& s, int x, int y, int w, int h, bool focused) {
    if (focused) s.DrawRoundedRectangle(x - 4, y - 4, w + 8, h + 8, 22, ink);
    s.DrawRoundedRectangle(x, y, w, h, 18, focused ? surfaceHi : surface);
}
const CatalogItem* focused(const AppModel& m) {
    if (m.rows.empty()) return nullptr;
    const int r = std::max(0, std::min<int>(m.row, m.rows.size() - 1));
    if (m.rows[r].items.empty()) return nullptr;
    return &m.rows[r].items[std::max(0, std::min<int>(m.column, m.rows[r].items.size() - 1))];
}
}

void PortShell::backdrop(Scene2D& s, const AppModel& m) const {
    s.FrameBufferFill(ground);
    if (m.page == Page::Board && m.heroArtwork.valid()) {
        s.BlitRgbScaled(0, 0, 1920, 1080, m.heroArtwork.pixels.data(),
            m.heroArtwork.width, m.heroArtwork.height);
        s.DrawVerticalFade(0, 0, 1920, 1080, ground, 80, 205);
        s.DrawVerticalFade(0, 0, 1120, 1080, ground, 180, 180);
    }
    s.DrawVerticalFade(0, 0, 1920, 680, accentDeep, 115, 0);
}

void PortShell::rail(Scene2D& s, const AppModel& m) const {
    const int width = m.railFocused ? 390 : railW;
    s.DrawRectangle(0, 0, width, 1080, Color{9, 8, 14});
    s.DrawRectangle(width - 1, 0, 1, 1080, hairline);
    s.DrawRoundedRectangle(38, 30, 64, 64, 18, accent);
    if (m.shellLogo.valid())
        s.BlitRgbScaledRounded(38, 30, 64, 64, 18, m.shellLogo.pixels.data(),
            m.shellLogo.width, m.shellLogo.height);
    else center(s, 38, 48, 64, ">", ink, 3);
    if (m.railFocused) s.DrawText(126, 48, "STREMIO", ink, 3);
    const char* labels[] = {"Board", "Discover", "Library", "Addons", "Settings"};
    const char* glyphs[] = {"B", "D", "L", "+", "*"};
    for (int i = 0; i < 5; ++i) {
        const int y = 230 + i * 112;
        const bool active = (m.railFocused ? m.railCursor : static_cast<int>(m.page)) == i;
        if (m.railFocused && active)
            s.DrawRoundedRectangle(22, y, 345, 76, 38, surfaceHi);
        s.DrawRoundedRectangle(38, y + 8, 64, 64, 32, active ? accent : surface);
        if (i < static_cast<int>(m.navigationIcons.size()) && m.navigationIcons[i].valid())
            s.BlitRgbScaledRounded(47, y + 17, 46, 46, 12,
                m.navigationIcons[i].pixels.data(), m.navigationIcons[i].width,
                m.navigationIcons[i].height);
        else center(s, 38, y + 28, 64, glyphs[i], active ? ink : ink2, 2);
        if (m.railFocused) s.DrawText(132, y + 27, labels[i], active ? ink : ink2, 2);
    }
    if (m.railFocused) {
        s.DrawText(40, 930, "PS5 TO PS4 PORT", accentSoft, 2);
        s.DrawText(40, 966, "SP9NKY + CONTRIBUTORS", ink3, 2);
    }
}

void PortShell::hints(Scene2D& s, const char* value) const {
    const int w = s.MeasureText(value, 2) + 72;
    s.DrawRoundedRectangle(right - w, 996, w, 54, 27, Color{22, 20, 29});
    center(s, right - w, 1014, w, value, ink2, 2);
}

void PortShell::board(Scene2D& s, const AppModel& m) const {
    const CatalogItem* hero = focused(m);
    s.DrawText(left, 58, "BOARD", accentSoft, 2);
    if (!hero) {
        s.DrawText(left, 310, m.status.c_str(), ink2, 3);
        return;
    }
    s.DrawText(left, 135, fit(hero->name, 42).c_str(), ink, 5);
    const std::string meta = hero->releaseInfo + "   " + hero->runtime + "   IMDB " + hero->imdbRating;
    s.DrawText(left, 215, meta.c_str(), accentSoft, 2);
    s.DrawText(left, 265, fit(hero->genres, 60).c_str(), ink2, 2);
    s.DrawText(left, 320, fit(hero->description, 88).c_str(), ink, 2);
    int top = 475;
    for (int r = 0; r < static_cast<int>(m.rows.size()) && r < 2; ++r) {
        s.DrawText(left, top, m.rows[r].title.c_str(), r == m.row ? ink : ink2, 3);
        const int first = std::max(0, (r == m.row ? m.column : 0) - 2);
        for (int slot = 0; slot < 6; ++slot) {
            const int i = first + slot;
            if (i >= static_cast<int>(m.rows[r].items.size())) break;
            const int x = left + slot * 275;
            const bool active = r == m.row && i == m.column && !m.railFocused;
            card(s, x, top + 52, 238, 250, active);
            if (i < static_cast<int>(m.rows[r].artwork.size()) && m.rows[r].artwork[i].valid())
                s.BlitRgbScaledRounded(x, top + 52, 238, 250, 18,
                    m.rows[r].artwork[i].pixels.data(), m.rows[r].artwork[i].width,
                    m.rows[r].artwork[i].height);
            else {
                s.DrawText(x + 20, top + 80, "STREMIO", accentSoft, 2);
                s.DrawText(x + 20, top + 135, fit(m.rows[r].items[i].name, 18).c_str(), ink, 3);
            }
        }
        top += 365;
    }
}

void PortShell::discover(Scene2D& s, const AppModel& m) const {
    s.DrawText(left, 58, m.page == Page::Library ? "LIBRARY" : "DISCOVER", accentSoft, 2);
    s.DrawText(left, 122, m.page == Page::Library ? "YOUR TITLES" : "EXPLORE CATALOGS", ink, 5);
    const char* filters[] = {"TYPE  MOVIE", "CATALOG  POPULAR", "GENRE  ALL"};
    int chipX = left;
    for (int i = 0; i < 3; ++i) {
        const int w = i == 1 ? 300 : 230;
        s.DrawRoundedRectangle(chipX, 205, w, 54, 27, surfaceHi);
        center(s, chipX, 223, w, filters[i], i == 0 ? accentSoft : ink2, 2);
        chipX += w + 18;
    }
    const MediaRow* row = m.rows.empty() ? nullptr : &m.rows[0];
    if (!row) { s.DrawText(left, 350, m.status.c_str(), ink2, 3); return; }
    for (int i = 0; i < static_cast<int>(row->items.size()) && i < 12; ++i) {
        const int col = i % 6, line = i / 6;
        const int x = left + col * 275, y = 315 + line * 340;
        card(s, x, y, 232, 270, i == m.column && !m.railFocused);
        if (i < static_cast<int>(row->artwork.size()) && row->artwork[i].valid())
            s.BlitRgbScaledRounded(x, y, 232, 270, 18, row->artwork[i].pixels.data(),
                row->artwork[i].width, row->artwork[i].height);
        else {
            s.DrawText(x + 18, y + 34, "STREMIO", accentSoft, 2);
            s.DrawText(x + 18, y + 100, fit(row->items[i].name, 17).c_str(), ink, 3);
            s.DrawText(x + 18, y + 225, row->items[i].releaseInfo.c_str(), ink3, 2);
        }
    }
}

void PortShell::simplePage(Scene2D& s, const AppModel& m) const {
    const bool addons = m.page == Page::Addons;
    s.DrawText(left, 58, addons ? "ADDONS" : "SETTINGS", accentSoft, 2);
    s.DrawText(left, 122, addons ? "YOUR STREMIO ADDONS" : "PREFERENCES", ink, 5);
    const char* addonRows[] = {"Cinemeta", "OpenSubtitles", "Local companion", "Add addon URL"};
    const char* settingRows[] = {"Stremio account", "Streaming server", "Interface sounds", "Video decoder", "Subtitle language", "About this port"};
    const int count = addons ? 4 : 6;
    for (int i = 0; i < count; ++i) {
        const int y = 245 + i * 104;
        card(s, left, y, right - left, 82, i == m.column && !m.railFocused);
        s.DrawText(left + 30, y + 27, addons ? addonRows[i] : settingRows[i], ink, 2);
        s.DrawText(right - 70, y + 27, ">", accentSoft, 2);
    }
}

void PortShell::detail(Scene2D& s, const AppModel& m) const {
    s.DrawText(left, 58, "TITLE", accentSoft, 2);
    s.DrawText(left, 130, fit(m.details.name, 44).c_str(), ink, 5);
    const std::string meta = m.details.releaseInfo + "   " + m.details.runtime + "   IMDB " + m.details.imdbRating;
    s.DrawText(left, 210, meta.c_str(), accentSoft, 2);
    s.DrawText(left, 270, m.details.genres.c_str(), ink2, 2);
    s.DrawText(left, 350, "OVERVIEW", ink3, 2);
    s.DrawText(left, 405, fit(m.details.description, 112).c_str(), ink, 2);
    s.DrawText(left, 560, "STREAMS", ink, 3);
    card(s, left, 625, 560, 90, true);
    s.DrawText(left + 30, 655, "FIND AVAILABLE STREAMS", ink, 2);
    s.DrawText(left + 500, 655, ">", accentSoft, 2);
}

void PortShell::draw(Scene2D& s, const AppModel& m, int tick) const {
    backdrop(s, m);
    if (m.loading) {
        center(s, 0, 400, 1920, "STREMIO", ink, 6);
        center(s, 0, 485, 1920, "PS5 INTERFACE PORTED TO PS4", accentSoft, 2);
        s.DrawRoundedRectangle(660, 585, 600, 10, 5, surfaceHi);
        s.DrawRoundedRectangle(660, 585, 100 + (tick % 6) * 100, 10, 5, accent);
        center(s, 0, 635, 1920, m.status.c_str(), ink2, 2);
        return;
    }
    if (m.detailsOpen) detail(s, m);
    else if (m.page == Page::Board) board(s, m);
    else if (m.page == Page::Discover || m.page == Page::Library) discover(s, m);
    else simplePage(s, m);
    hints(s, m.detailsOpen ? "O  Back    X  Streams" : "O  Back    X  Select    Triangle  Search");
    if (!m.detailsOpen) rail(s, m);
}
}
