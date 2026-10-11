#include "playstation_shell.h"

#include <algorithm>

namespace rebuild {
namespace {
constexpr int width = 1920;
const Color night{3, 12, 28};
const Color panel{18, 38, 67};
const Color panel2{27, 54, 90};
const Color blue{31, 112, 219};
const Color cyan{77, 190, 255};
const Color white{247, 251, 255};
const Color muted{157, 181, 210};

std::string shortText(const std::string& text, size_t limit) {
    return text.size() <= limit ? text : text.substr(0, limit - 3) + "...";
}
void centered(Scene2D& s, int x, int y, int w, const char* text, Color c, int scale) {
    s.DrawText(x + (w - s.MeasureText(text, scale)) / 2, y, text, c, scale);
}
void focusCard(Scene2D& s, int x, int y, int w, int h, bool active) {
    if (active) s.DrawRoundedRectangle(x - 4, y - 4, w + 8, h + 8, 26, white);
    s.DrawRoundedRectangle(x, y, w, h, 22, active ? blue : panel);
}
}

void PlayStationShell::header(Scene2D& s, const char* section) {
    s.DrawText(148, 42, "STREMIO", theme_.textPrimary, 3);
    s.DrawText(345, 49, section, theme_.accent, 2);
    s.DrawText(1680, 49, "PS4 NATIVE", theme_.textMuted, 2);
    s.DrawRectangle(148, 94, 1624, 1, theme_.border);
}

void PlayStationShell::footer(Scene2D& s, const char* controls) {
    s.DrawVerticalFade(0, 922, width, 158, theme_.backgroundBottom, 0, 245);
    const int w = s.MeasureText(controls, 2) + 100;
    s.DrawRoundedRectangle(width - w - 72, 987, w, 62, 31, theme_.surface);
    centered(s, width - w - 72, 1008, w, controls, theme_.textPrimary, 2);
}

void PlayStationShell::rail(Scene2D& s, int selected, bool expanded) {
    const int railWidth = expanded ? 390 : 112;
    s.DrawRectangle(0, 0, railWidth, 1080, Color{3, 14, 31});
    s.DrawRectangle(railWidth - 1, 0, 1, 1080, theme_.border);
    s.DrawRoundedRectangle(28, 28, 56, 56, 15, theme_.accent);
    centered(s, 28, 43, 56, ">", theme_.textPrimary, 3);
    if (expanded) s.DrawText(112, 43, "STREMIO", theme_.textPrimary, 3);
    const char* labels[] = {"Home", "Search", "Library", "Settings"};
    const char* icons[] = {"H", "Q", "L", "*"};
    for (int i = 0; i < 4; ++i) {
        const int y = 210 + i * 120;
        if (expanded && selected == i)
            s.DrawRoundedRectangle(22, y, 344, 76, 38, theme_.surfaceSelected);
        s.DrawRoundedRectangle(30, y + 8, 60, 60, 30,
            selected == i ? theme_.accent : theme_.surface);
        centered(s, 30, y + 27, 60, icons[i], theme_.textPrimary, 2);
        if (expanded) s.DrawText(126, y + 25, labels[i],
            selected == i ? theme_.textPrimary : theme_.textSecondary, 2);
    }
    if (expanded) {
        s.DrawText(40, 900, "NATIVE PS4 APPLICATION", theme_.accent, 2);
        s.DrawText(40, 940, "BY JAVASCRIPTX", theme_.textMuted, 2);
    }
}

void PlayStationShell::home(Scene2D& s, const ViewModel& m) {
    header(s, m.type == "series" ? "TV SHOWS" : "MOVIES");
    if (m.items.empty()) return;
    const CatalogItem& hero = m.items[std::min<int>(m.selected, m.items.size() - 1)];
    s.DrawText(148, 155, "FEATURED", theme_.accent, 2);
    s.DrawText(148, 195, shortText(hero.name, 38).c_str(), white, 5);
    const std::string facts = hero.releaseInfo + "    " + hero.runtime + "    IMDB " + hero.imdbRating;
    s.DrawText(152, 275, facts.c_str(), cyan, 2);
    s.DrawText(152, 325, shortText(hero.genres, 62).c_str(), muted, 2);
    s.DrawText(152, 375, shortText(hero.description, 92).c_str(), white, 2);
    s.DrawText(148, 520, m.type == "series" ? "TRENDING SERIES" : "TRENDING MOVIES", white, 3);
    const int first = std::max(0, m.selected - 2);
    for (int slot = 0; slot < 7; ++slot) {
        const int index = first + slot;
        if (index >= static_cast<int>(m.items.size())) break;
        const int x = 148 + slot * 244;
        focusCard(s, x, 585, 210, 280, index == m.selected);
        s.DrawText(x + 22, 620, m.type == "series" ? "SERIES" : "MOVIE", cyan, 2);
        s.DrawText(x + 22, 690, shortText(m.items[index].name, 17).c_str(), white, 3);
        s.DrawText(x + 22, 800, shortText(m.items[index].releaseInfo, 17).c_str(), muted, 2);
    }
}

void PlayStationShell::search(Scene2D& s, const ViewModel& m) {
    header(s, "SEARCH");
    s.DrawText(74, 170, "SEARCH STREMIO", white, 5);
    s.DrawText(76, 245, "USE THE PLAYSTATION SYSTEM KEYBOARD", muted, 2);
    focusCard(s, 74, 320, 1770, 130, true);
    s.DrawText(125, 355, "TITLE", cyan, 2);
    s.DrawText(125, 395, m.query.empty() ? "PRESS X TO TYPE" : m.query.c_str(), white, 3);
    s.DrawText(74, 535, "RESULTS", white, 3);
    for (int i = 0; i < static_cast<int>(m.items.size()) && i < 5; ++i) {
        focusCard(s, 74, 600 + i * 68, 1770, 54, i == m.selected);
        s.DrawText(110, 617 + i * 68, shortText(m.items[i].name, 70).c_str(), white, 2);
    }
}

void PlayStationShell::settings(Scene2D& s, const ViewModel& m) {
    header(s, "SETTINGS");
    s.DrawText(74, 170, "SYSTEM SETTINGS", white, 5);
    const char* title[] = {"STREMIO ACCOUNT", "ADDONS", "NETWORK", "PLAYBACK", "ACCESSIBILITY", "ABOUT"};
    const char* sub[] = {"Device link and sync", "Installed transports", "Sony HTTP and companion", "VideoDec2 and AudioOut", "Text and motion", "Project information"};
    for (int i = 0; i < 6; ++i) {
        const int x = 74 + (i % 3) * 590, y = 285 + (i / 3) * 285;
        focusCard(s, x, y, 550, 240, i == m.selected);
        s.DrawText(x + 34, y + 40, title[i], white, 3);
        s.DrawText(x + 34, y + 105, sub[i], muted, 2);
        s.DrawText(x + 475, y + 170, ">", cyan, 3);
    }
}

void PlayStationShell::details(Scene2D& s, const ViewModel& m) {
    header(s, "DETAILS");
    s.DrawText(90, 190, shortText(m.details.name, 44).c_str(), white, 5);
    const std::string facts = m.details.releaseInfo + "    " + m.details.runtime + "    IMDB " + m.details.imdbRating;
    s.DrawText(94, 275, facts.c_str(), cyan, 2);
    s.DrawText(94, 335, m.details.genres.c_str(), muted, 2);
    s.DrawText(94, 430, "OVERVIEW", cyan, 2);
    s.DrawText(94, 485, shortText(m.details.description, 115).c_str(), white, 2);
    focusCard(s, 94, 680, 330, 78, true);
    centered(s, 94, 705, 330, "FIND STREAMS", white, 2);
}

void PlayStationShell::draw(Scene2D& s, const ViewModel& m, int pulse) {
    s.FrameBufferFill(theme_.backgroundBottom);
    s.DrawVerticalFade(0, 0, 1920, 800, theme_.backgroundTop, 235, 0);
    if (m.busy) {
        header(s, "STARTING");
        centered(s, 0, 390, 1920, "STREMIO", white, 6);
        centered(s, 0, 475, 1920, "BY JAVASCRIPTX", cyan, 2);
        s.DrawRoundedRectangle(650, 580, 620, 12, 6, panel2);
        s.DrawRoundedRectangle(650, 580, 110 + (pulse % 6) * 100, 12, 6, cyan);
        centered(s, 0, 635, 1920, m.status.c_str(), muted, 2);
        return;
    }
    if (m.page == Page::Home || m.page == Page::Library) home(s, m);
    else if (m.page == Page::Search) search(s, m);
    else if (m.page == Page::Settings) settings(s, m);
    else details(s, m);
    footer(s, m.page == Page::Details ? "O  Back       X  Find streams" : "L3  Menu       O  Back       X  Select");
    rail(s, m.navigation, m.rail);
}
}
