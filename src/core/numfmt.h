// nodesim — node graph editor and simulator
// Copyright (C) 2026 Flavio de Vasconcellos Correa
// SPDX-License-Identifier: GPL-3.0-or-later

// Number formatting/parsing for the UI (no SDL, testable).
// Rule: display may be short, but the edit buffer uses the shortest
// representation that round-trips to the exact same double/float (std::to_chars).
#pragma once
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

// Shortest text that round-trips (e.g.: 0.1 -> "0.1", 1e-6 -> "1e-06").
inline std::string formatNumExact(double v) {
    char b[64];
    auto r = std::to_chars(b, b + sizeof b, v);
    if (r.ec == std::errc()) return std::string(b, r.ptr);
    std::snprintf(b, sizeof b, "%.17g", v);
    return b;
}

inline std::string fmtFloatExact(float v) {
    char b[64];
    auto r = std::to_chars(b, b + sizeof b, v);
    if (r.ec == std::errc()) return std::string(b, r.ptr);
    std::snprintf(b, sizeof b, "%.9g", (double)v);
    return b;
}

// Short display (7 significant digits), without losing the order of magnitude.
inline std::string formatNumShort(double v) {
    if (!std::isfinite(v)) return v != v ? "nan" : (v < 0 ? "-inf" : "inf");
    if (v == 0) return "0";
    char b[64];
    std::snprintf(b, sizeof b, "%.7g", v);
    return b;
}

inline std::string fmtFloatShort(float v) {
    if (!std::isfinite(v)) return v != v ? "nan" : (v < 0 ? "-inf" : "inf");
    if (v == 0) return "0";
    char b[64];
    std::snprintf(b, sizeof b, "%.7g", (double)v);
    return b;
}

// Accepts only the whole string (surrounding spaces), finite. strtod = double.
inline bool parseNumDouble(const std::string& s, double& out) {
    const char* p = s.c_str();
    while (*p == ' ' || *p == '\t') ++p;
    if (!*p) return false;
    char* e = nullptr;
    double v = std::strtod(p, &e);
    if (e == p) return false;
    while (*e == ' ' || *e == '\t') ++e;
    if (*e != '\0' || !std::isfinite(v)) return false;
    out = v;
    return true;
}

inline bool parseFloat(const std::string& s, float& out) {
    double v = 0;
    if (!parseNumDouble(s, v)) return false;
    float f = (float)v;
    if (!std::isfinite(f)) return false; // out of float range
    out = f;
    return true;
}

// Plugin parameter: parse as double + clamp to the declared range.
inline bool parseParam(const std::string& s, double lo, double hi, double& out) {
    double v = 0;
    if (!parseNumDouble(s, v)) return false;
    if (v < lo) v = lo;
    if (v > hi) v = hi;
    out = v;
    return true;
}
