// nodesim — node graph editor and simulator
// Copyright (C) 2026 Flavio de Vasconcellos Correa
// SPDX-License-Identifier: GPL-3.0-or-later
// Theme: discovery and reading of the colors.toml of the Omarchy in use.

#include "tema.h"
#include "compat.h"
#include "draw.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <sys/stat.h>

namespace {
ThemeColors gTheme;
int gExportMode = THEME_EXPORT_CURRENT;
bool gLoaded = false;
std::string gDir;          // theme directory (…/current/theme)
std::string gFileArg;          // colors.toml actually read
time_t gMtime = 0;
long long gTam = -1;
std::string gNameRead;

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) ++a;
    while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
    return s.substr(a, b - a);
}

bool fileExists(const std::string& p) {
    struct stat st;
    return !p.empty() && ::stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}
bool existsDir(const std::string& p) {
    struct stat st;
    return !p.empty() && ::stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}
std::string homeDir() {
    const char* h = std::getenv("HOME");
    return h && *h ? h : ".";
}

int hexDig(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
bool parseColor(const std::string& raw, SDL_Color& out) {
    std::string s = trim(raw);
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
        s = s.substr(1, s.size() - 2);
    if (s.size() >= 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s = "#" + s.substr(2);
    if (s.empty() || s[0] != '#') return false;
    s.erase(0, 1);
    if (s.size() == 3) {
        int r = hexDig(s[0]), g = hexDig(s[1]), b = hexDig(s[2]);
        if (r < 0 || g < 0 || b < 0) return false;
        out = {(Uint8)(r * 17), (Uint8)(g * 17), (Uint8)(b * 17), 255};
        return true;
    }
    if (s.size() != 6 && s.size() != 8) return false;
    int v[4] = {0, 0, 0, 255};
    for (size_t i = 0; i < s.size() / 2; ++i) {
        const int a = hexDig(s[i * 2]), b = hexDig(s[i * 2 + 1]);
        if (a < 0 || b < 0) return false;
        v[i] = a * 16 + b;
    }
    out = {(Uint8)v[0], (Uint8)v[1], (Uint8)v[2], (Uint8)v[3]};
    return true;
}
SDL_Color mix(SDL_Color a, SDL_Color b, float t) {
    auto m = [&](int x, int y) { return (int)std::lround(x + (y - x) * t); };
    return {(Uint8)m(a.r, b.r), (Uint8)m(a.g, b.g), (Uint8)m(a.b, b.b),
            (Uint8)m(a.a, b.a)};
}
std::string slugTitle(std::string s) {
    bool newValue = true;
    for (char& c : s) {
        if (c == '-' || c == '_' || c == ' ') { newValue = true; c = ' '; continue; }
        if (newValue) { c = (char)std::toupper((unsigned char)c); newValue = false; }
    }
    s = trim(s);
    return s.empty() ? std::string("Omarchy") : s;
}
std::string statePath() {
    const char* st = std::getenv("XDG_STATE_HOME");
    return (st && *st) ? std::string(st) : (homeDir() + "/.local/state");
}
std::string configPath() {
    const char* cfg = std::getenv("XDG_CONFIG_HOME");
    return (cfg && *cfg) ? std::string(cfg) : (homeDir() + "/.config");
}
// Theme directory: honors NODESIM_THEME (a path or "off"), otherwise the Omarchy
// state (…/omarchy/current/theme) and, finally, the old config.
std::string discoverDir() {
    const char* env = compat::getenvCompat("NODESIM_THEME");
    if (env) {
        std::string v = trim(env);
        if (v == "off" || v == "0" || v == "none" || v == "no") return {};
        if (!v.empty()) {
            if (existsDir(v) && fileExists(v + "/colors.toml")) return v;
            if (fileExists(v)) { // the colors.toml itself was passed
                const size_t bar = v.find_last_of('/');
                return bar == std::string::npos ? std::string(".") : v.substr(0, bar);
            }
            return v; // may appear later; poll tries again
        }
    }
    std::string d = statePath() + "/omarchy/current/theme";
    if (fileExists(d + "/colors.toml")) return d;
    std::string c = configPath() + "/omarchy/current/theme";
    if (fileExists(c + "/colors.toml")) return c;
    return d; // normal on Omarchy: may not exist yet
}
std::string themeName(const std::string& dir) {
    if (dir.empty()) return "default";
    // …/current/theme -> …/current/theme.name
    std::string base = dir;
    while (base.size() > 1 && base.back() == '/') base.pop_back();
    const size_t bar = base.find_last_of('/');
    const std::string parent = bar == std::string::npos ? std::string() : base.substr(0, bar);
    if (!parent.empty()) {
        std::ifstream f(parent + "/theme.name");
        std::string line;
        if (std::getline(f, line) && !trim(line).empty()) return slugTitle(trim(line));
    }
    const size_t b2 = base.find_last_of('/');
    return slugTitle(b2 == std::string::npos ? base : base.substr(b2 + 1));
}
bool readColors(const std::string& fileArg, ThemeColors& t) {
    std::ifstream f(fileArg);
    if (!f) return false;
    std::map<std::string, std::string> kv;
    std::string line;
    while (std::getline(f, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue; // line comment
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = trim(line.substr(0, eq));
        const std::string v = trim(line.substr(eq + 1));
        if (!k.empty() && !v.empty()) kv[k] = v;
    }
    if (kv.empty()) return false;
    auto get = [&](const char* k, SDL_Color fallback) {
        auto it = kv.find(k);
        SDL_Color c;
        return (it != kv.end() && parseColor(it->second, c)) ? c : fallback;
    };
    const ThemeColors fallback;
    const SDL_Color black{0, 0, 0, 255}, white{255, 255, 255, 255};
    const SDL_Color bg = get("background", fallback.panel);
    const SDL_Color dark = get("dark_background", mix(bg, black, 0.22f));
    const SDL_Color darker = get("darker_background", mix(bg, black, 0.40f));
    const SDL_Color light = get("lighter_background", mix(bg, white, 0.12f));
    const SDL_Color fg = get("foreground", fallback.text);
    const SDL_Color bfg = get("bright_foreground", mix(fg, white, 0.30f));
    const SDL_Color muted = get("muted", mix(fg, bg, 0.45f));
    const SDL_Color accent = get("accent", fallback.highlight);
    const SDL_Color sel = get("selection", mix(bg, accent, 0.18f));
    const SDL_Color red = get("bright_red", get("red", fallback.error));
    const SDL_Color yellow = get("bright_yellow", get("yellow", fallback.warning));
    const SDL_Color green = get("bright_green", get("green", fallback.ok));
    const SDL_Color orange = get("orange", get("yellow", fallback.textStr));
    const SDL_Color blue = get("blue", fallback.section);

    t.omarchy = true;
    t.background = darker;                    // canvas darker than the panels
    t.darkBackground = dark;
    t.panel = bg;
    t.box = mix(bg, light, 0.35f); // node body
    t.edge = mix(muted, bg, 0.30f);
    t.strongEdge = mix(accent, bg, 0.35f);
    t.grid = mix(light, bg, 0.55f);
    t.strongGrid = mix(light, bg, 0.28f);
    t.text = fg;
    t.weak = mix(muted, fg, 0.35f);
    t.strong = bfg;
    t.section = mix(blue, fg, 0.12f);
    t.label = mix(fg, muted, 0.45f);
    t.highlight = accent;
    t.hover = mix(bg, light, 0.42f);
    t.active = mix(sel, accent, 0.16f);
    t.error = red;
    t.warning = yellow;
    t.ok = green;
    t.textStr = orange;
    return true;
}
} // namespace

const ThemeColors& theme() { return gTheme; }

void themeApplyColors(const ThemeColors& t) {
    kUi.background = t.background;
    kUi.darkBackground = t.darkBackground;
    kUi.panel = t.panel;
    kUi.box = t.box;
    kUi.edge = t.edge;
    kUi.strongEdge = t.strongEdge;
    kUi.grid = t.grid;
    kUi.strongGrid = t.strongGrid;
    kUi.text = t.text;
    kUi.weak = t.weak;
    kUi.strong = t.strong;
    kUi.section = t.section;
    kUi.label = t.label;
    kUi.highlight = t.highlight;
    kUi.hover = t.hover;
    kUi.active = t.active;
    kUi.error = t.error;
    kUi.warning = t.warning;
    kUi.ok = t.ok;
    kUi.textStr = t.textStr;
    kTxt = t.text;
    kDim = t.weak;
    kErr = t.error;
    kSec = t.section;
    kLbl = t.label;
    kStr = t.textStr;
}
void themeApply() { themeApplyColors(gTheme); }

ThemeColors defaultTheme() {
    ThemeColors t;
    t.name = "nodesim (default)";
    return t;
}
int themeExportMode() { return gExportMode; }
void themeExportModeSet(int mode) {
    if (mode < THEME_EXPORT_CURRENT || mode > THEME_EXPORT_OMARCHY) mode = THEME_EXPORT_CURRENT;
    gExportMode = mode;
}
const char* themeExportName(int mode) {
    switch (mode) {
        case THEME_EXPORT_DEFAULT: return "default";
        case THEME_EXPORT_OMARCHY: return "Omarchy";
        default: return "current";
    }
}
ThemeColors themeExport() {
    if (gExportMode == THEME_EXPORT_DEFAULT) return defaultTheme();
    if (gExportMode == THEME_EXPORT_OMARCHY) return gTheme.omarchy ? gTheme : defaultTheme();
    return gTheme; // current: Omarchy when present, otherwise the default
}

bool themeInit() {
    const char* exp = compat::getenvCompat("NODESIM_THEME_EXPORT");
    if (exp) {
        std::string v = trim(exp);
        for (char& c : v) c = (char)std::tolower((unsigned char)c);
        if (v == "1" || v == "padrao" || v == "padrão" || v == "default")
            gExportMode = THEME_EXPORT_DEFAULT;
        else if (v == "2" || v == "omarchy" || v == "om")
            gExportMode = THEME_EXPORT_OMARCHY;
        else
            gExportMode = THEME_EXPORT_CURRENT;
    }
    gTheme = ThemeColors{};
    gDir = discoverDir();
    gFileArg = gDir.empty() ? std::string() : (gDir + "/colors.toml");
    gMtime = 0;
    gTam = -1;
    gNameRead.clear();
    gLoaded = false;
    if (!gFileArg.empty()) {
        struct stat st;
        if (::stat(gFileArg.c_str(), &st) == 0) {
            gMtime = st.st_mtime;
            gTam = (long long)st.st_size;
        }
        if (readColors(gFileArg, gTheme)) {
            gTheme.name = themeName(gDir);
            gTheme.path = gFileArg;
            gNameRead = gTheme.name;
            gLoaded = true;
        }
    }
    themeApply();
    return gLoaded;
}

bool themePoll(bool force) {
    const std::string dir = discoverDir();
    if (dir != gDir) { // the theme moved (or appeared)
        gDir = dir;
        gFileArg = gDir.empty() ? std::string() : (gDir + "/colors.toml");
        gMtime = 0;
        gTam = -1;
        force = true;
    }
    if (gFileArg.empty()) {
        if (!gLoaded && !force) return false;
        gTheme = ThemeColors{};
        gLoaded = false;
        themeApply();
        return true;
    }
    struct stat st;
    long long tam = -1;
    time_t mt = 0;
    if (::stat(gFileArg.c_str(), &st) == 0) {
        mt = st.st_mtime;
        tam = (long long)st.st_size;
    }
    const std::string name = themeName(gDir);
    if (!force && mt == gMtime && tam == gTam && name == gNameRead) return false;
    gMtime = mt;
    gTam = tam;
    ThemeColors newValue;
    if (!readColors(gFileArg, newValue)) {
        if (gLoaded) { // file disappeared/became invalid: back to the default
            gTheme = ThemeColors{};
            gLoaded = false;
            gNameRead.clear();
            themeApply();
            return true;
        }
        return false;
    }
    newValue.name = name;
    newValue.path = gFileArg;
    gTheme = newValue;
    gNameRead = name;
    gLoaded = true;
    themeApply();
    return true;
}

std::string themeDescription() {
    if (gTheme.omarchy) return gTheme.name + " (Omarchy)";
    return "nodesim (default)";
}
