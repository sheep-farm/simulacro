// nodesim — node graph editor and simulator
// Copyright (C) 2026 Flavio de Vasconcellos Correa
// SPDX-License-Identifier: GPL-3.0-or-later

#include "scopesvg.h"
#include "svg.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {

const int kPal[8][3] = {
    {110, 230, 140}, {90, 200, 255}, {255, 170, 90}, {255, 120, 120},
    {205, 150, 255}, {255, 220, 90}, {120, 235, 220}, {255, 150, 210},
};

std::string num(double v, const char* fmt) {
    char b[64];
    std::snprintf(b, sizeof b, fmt, v);
    return b;
}

std::string rgb(int r, int g, int b) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "rgb(%d,%d,%d)", r, g, b);
    return buf;
}



std::string text(const char* x, const char* y, const std::string& extra, const std::string& s) {
    return std::string("<text x=\"") + x + "\" y=\"" + y + "\"" + extra + ">" + svg::escape(s) + "</text>";
}

} // namespace

void scopeSvgColor(int k, int& r, int& g, int& b) {
    const int i = (k % 8 + 8) % 8;
    r = kPal[i][0];
    g = kPal[i][1];
    b = kPal[i][2];
}

std::string scopeSvg(const std::vector<ScopeSvgChannel>& channels, const std::string& title,
                     const ScopeSvgCfg& cfg) {
    const int W = std::max(200, std::min(4000, cfg.W));
    const int H = std::max(120, std::min(4000, cfg.H));
    const double window = std::max(0.1, std::min(600.0, cfg.window));
    double dt = cfg.dt > 0 ? cfg.dt : 1.0 / 60.0;
    const double scale = cfg.scale == 0 ? 1.0 : cfg.scale;
    const int nc = (int)channels.size();

    const int legLin = cfg.legend && nc > 0 ? ((nc + 3) / 4) * 18 + 6 : 0;
    const int x0 = 68, xr = 18, y0 = 52, yb = 26 + legLin;
    int pw = W - x0 - xr, ph = H - y0 - yb;
    if (pw < 40) pw = W - 40;
    if (ph < 40) ph = H - 60;

    const double m = std::max(2.0, std::ceil(window / dt)); // samples in the window
    const size_t vis = (size_t)m;

    // visible range of each channel (the last `vis` samples) and Y scale
    double mn = 0, mx = 0;
    bool some = false;
    for (const auto& c : channels) {
        const size_t ini = c.values.size() > vis ? c.values.size() - vis : 0;
        for (size_t i = ini; i < c.values.size(); ++i) {
            const double v = c.values[i] * scale;
            if (!some) { mn = mx = v; some = true; }
            else { mn = std::min(mn, v); mx = std::max(mx, v); }
        }
    }
    if (!cfg.yauto) { mn = cfg.ymin; mx = cfg.ymax; }
    if (!some && cfg.yauto) { mn = -1; mx = 1; }
    if (mx <= mn) mx = mn + 1;
    if (cfg.yauto) {
        const double slack = (mx - mn) * 0.1;
        mn -= slack;
        mx += slack;
    }

    auto px = [&](double gi) { return (double)x0 + (double)pw + gi * ((double)pw / (m - 1.0)); };
    auto py = [&](double v) { return (double)y0 + (mx - v * scale) / (mx - mn) * (double)ph; };

    const std::string backgroundColor = rgb(cfg.theme.background[0], cfg.theme.background[1], cfg.theme.background[2]);
    const std::string panelColor = rgb(cfg.theme.panel[0], cfg.theme.panel[1], cfg.theme.panel[2]);
    const std::string gridColor = rgb(cfg.theme.grid[0], cfg.theme.grid[1], cfg.theme.grid[2]);
    const std::string textColor = rgb(cfg.theme.text[0], cfg.theme.text[1], cfg.theme.text[2]);
    const std::string weakColor = rgb(cfg.theme.weak[0], cfg.theme.weak[1], cfg.theme.weak[2]);

    std::string s;
    s.reserve(4096 + 256 * (size_t)nc);
    s += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    s += "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" + std::to_string(W) + "\" height=\"" +
         std::to_string(H) + "\" viewBox=\"0 0 " + std::to_string(W) + " " + std::to_string(H) + "\">\n";
    s += "<rect width=\"" + std::to_string(W) + "\" height=\"" + std::to_string(H) +
         "\" fill=\"" + backgroundColor + "\"/>\n";
    s += "<g font-family=\"DejaVu Sans Mono, monospace\" font-size=\"12\" fill=\"" +
         textColor + "\">\n";

    // chart area + grid
    s += "<rect x=\"" + std::to_string(x0) + "\" y=\"" + std::to_string(y0) + "\" width=\"" +
         std::to_string(pw) + "\" height=\"" + std::to_string(ph) + "\" fill=\"" + panelColor +
         "\" stroke=\"" + weakColor + "\"/>\n";
    if (cfg.grid) {
        s += "<g stroke=\"" + gridColor + "\" stroke-width=\"1\">\n";
        for (int i = 1; i < 4; ++i) {
            const int y = y0 + ph * i / 4;
            s += "<line x1=\"" + std::to_string(x0) + "\" y1=\"" + std::to_string(y) + "\" x2=\"" +
                 std::to_string(x0 + pw) + "\" y2=\"" + std::to_string(y) + "\"/>\n";
        }
        for (int i = 1; i < 6; ++i) {
            const int x = x0 + pw * i / 6;
            s += "<line x1=\"" + std::to_string(x) + "\" y1=\"" + std::to_string(y0) + "\" x2=\"" +
                 std::to_string(x) + "\" y2=\"" + std::to_string(y0 + ph) + "\"/>\n";
        }
        s += "</g>\n";
    }
    if (cfg.zero && mn <= 0 && 0 <= mx) {
        const int y = (int)py(0.0);
        s += "<line x1=\"" + std::to_string(x0) + "\" y1=\"" + std::to_string(y) + "\" x2=\"" +
             std::to_string(x0 + pw) + "\" y2=\"" + std::to_string(y) +
             "\" stroke=\"" + weakColor + "\" stroke-dasharray=\"4 3\"/>\n";
    }

    // curves
    for (int k = 0; k < nc; ++k) {
        const ScopeSvgChannel& c = channels[(size_t)k];
        if (c.values.empty()) continue;
        const size_t ini = c.values.size() > vis ? c.values.size() - vis : 0;
        const double off = (double)c.values.size() - 1.0; // gi = i - (n-1): 0 = newest
        const std::string color = rgb(c.r, c.g, c.b);
        std::string pts;
        for (size_t i = ini; i < c.values.size(); ++i) {
            const double v = c.values[i] * scale;
            if (!std::isfinite(v)) continue; // non-finite samples do not enter the figure
            if (!pts.empty()) pts += ' ';
            pts += num(px((double)i - off), "%.2f") + "," + num(py(v), "%.2f");
        }
        if (pts.empty()) continue;
        if (c.values.size() - ini < 2) { // single sample: point
            s += "<circle cx=\"" + num(px((double)(c.values.size() - 1) - off), "%.2f") + "\" cy=\"" +
                 num(py(c.values.back() * scale), "%.2f") + "\" r=\"2\" fill=\"" + color + "\"/>\n";
            continue;
        }
        s += "<polyline fill=\"none\" stroke=\"" + color + "\" stroke-width=\"1.6\" " +
             "stroke-linejoin=\"round\" points=\"" + pts + "\"/>\n";
    }

    // axis labels
    s += text(std::to_string(x0 - 6).c_str(), std::to_string(y0 + 5).c_str(),
               " text-anchor=\"end\" fill=\"" + weakColor + "\"", num(mx, "%.6g"));
    s += text(std::to_string(x0 - 6).c_str(), std::to_string(y0 + ph / 2 + 4).c_str(),
               " text-anchor=\"end\" fill=\"" + weakColor + "\"", num((mx + mn) / 2, "%.6g"));
    s += text(std::to_string(x0 - 6).c_str(), std::to_string(y0 + ph + 4).c_str(),
               " text-anchor=\"end\" fill=\"" + weakColor + "\"", num(mn, "%.6g"));
    s += text(std::to_string(x0).c_str(), std::to_string(y0 + ph + 18).c_str(),
               " text-anchor=\"start\" fill=\"" + weakColor + "\"", num(-window, "%.6g") + " s");
    s += text(std::to_string(x0 + pw / 2).c_str(), std::to_string(y0 + ph + 18).c_str(),
               " text-anchor=\"middle\" fill=\"" + weakColor + "\"", num(-window / 2, "%.6g") + " s");
    s += text(std::to_string(x0 + pw).c_str(), std::to_string(y0 + ph + 18).c_str(),
               " text-anchor=\"end\" fill=\"" + weakColor + "\"", "0 s");

    // title
    s += text("12", "20", " font-size=\"13\" font-weight=\"bold\" fill=\"" + textColor + "\"", title);
    s += text("12", "34", " fill=\"" + weakColor + "\"",
               num(window, "%.6g") + " s window  ·  " + std::to_string(nc) + " channel(s)");

    // legend (name = last value)
    if (cfg.legend && nc > 0) {
        const int cols = pw >= 380 ? 4 : (pw >= 200 ? 2 : 1);
        const int colW = pw / cols;
        int ly = y0 + ph + 30;
        for (int k = 0; k < nc; ++k) {
            const ScopeSvgChannel& c = channels[(size_t)k];
            if (ly > H - 8) break;
            const int cx = x0 + (k % cols) * colW;
            const std::string color = rgb(c.r, c.g, c.b);
            s += "<rect x=\"" + std::to_string(cx) + "\" y=\"" + std::to_string(ly - 9) +
                 "\" width=\"10\" height=\"10\" fill=\"" + color + "\"/>\n";
            std::string ult = "—";
            if (!c.values.empty()) ult = num(c.values.back(), "%.4g");
            std::string name = c.name.empty() ? ("ch" + std::to_string(k)) : c.name;
            s += text(std::to_string(cx + 14).c_str(), std::to_string(ly).c_str(),
                       " fill=\"" + textColor + "\"",
                       name + " = " + ult);
            if (k % cols == cols - 1) ly += 17;
        }
    }
    s += "</g>\n</svg>\n";
    return s;
}
