// nodesim — graph node editor and simulator
// Copyright (C) 2026 Flavio de Vasconcellos Correa
// SPDX-License-Identifier: GPL-3.0-or-later

#include "geojson.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kDeg = kPi / 180.0;
constexpr double kRad = 180.0 / kPi;
constexpr double kMaxMercLat = 85.05112878;
constexpr size_t kMaxFile = 128u * 1024u * 1024u;
constexpr size_t kMaxVertices = 4000000u;
constexpr int kMaxDepth = 128;

struct J {
    enum Type { NUL, BOOL, NUM, STR, ARR, OBJ } type = NUL;
    bool b = false;
    double n = 0;
    std::string s;
    std::vector<J> a;
    std::vector<std::pair<std::string, J>> o;

    const J* find(const char* key) const {
        if (type != OBJ || !key) return nullptr;
        for (const auto& kv : o)
            if (kv.first == key) return &kv.second;
        return nullptr;
    }
};

void appendUtf8(std::string& out, unsigned cp) {
    if (cp <= 0x7F) out.push_back((char)cp);
    else if (cp <= 0x7FF) {
        out.push_back((char)(0xC0 | (cp >> 6)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        out.push_back((char)(0xE0 | (cp >> 12)));
        out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    } else {
        out.push_back((char)(0xF0 | (cp >> 18)));
        out.push_back((char)(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    }
}

struct Parser {
    const char* p = nullptr;
    const char* e = nullptr;
    int depth = 0;
    size_t vertices = 0;
    bool tooManyVertices = false;

    void ws() {
        while (p < e && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) ++p;
    }
    bool hex4(unsigned& v) {
        v = 0;
        for (int i = 0; i < 4; ++i) {
            if (p >= e) return false;
            const char c = *p++;
            v <<= 4;
            if (c >= '0' && c <= '9') v |= (unsigned)(c - '0');
            else if (c >= 'a' && c <= 'f') v |= (unsigned)(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= (unsigned)(c - 'A' + 10);
            else return false;
        }
        return true;
    }
    bool str(std::string& out) {
        if (p >= e || *p != '"') return false;
        ++p;
        while (p < e) {
            const unsigned char c = (unsigned char)*p++;
            if (c == '"') return true;
            if (c < 0x20) return false;
            if (c != '\\') { out.push_back((char)c); continue; }
            if (p >= e) return false;
            const char esc = *p++;
            switch (esc) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u': {
                    unsigned cp = 0;
                    if (!hex4(cp)) return false;
                    if (cp >= 0xD800 && cp <= 0xDBFF) {
                        if (p + 1 < e && p[0] == '\\' && p[1] == 'u') {
                            p += 2;
                            unsigned lo = 0;
                            if (!hex4(lo) || lo < 0xDC00 || lo > 0xDFFF) return false;
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        } else return false;
                    } else if (cp >= 0xDC00 && cp <= 0xDFFF) return false;
                    appendUtf8(out, cp);
                    break;
                }
                default: return false;
            }
        }
        return false;
    }
    bool value(J& v) {
        ws();
        if (p >= e) return false;
        const char c = *p;
        if (c == '{') {
            if (++depth > kMaxDepth) return false;
            ++p;
            v.type = J::OBJ;
            ws();
            if (p < e && *p == '}') { ++p; --depth; return true; }
            while (p < e) {
                ws();
                std::string key;
                if (!str(key)) return false;
                ws();
                if (p >= e || *p != ':') return false;
                ++p;
                J x;
                if (!value(x)) return false;
                v.o.emplace_back(std::move(key), std::move(x));
                ws();
                if (p < e && *p == ',') { ++p; continue; }
                if (p < e && *p == '}') { ++p; --depth; return true; }
                return false;
            }
            return false;
        }
        if (c == '[') {
            if (++depth > kMaxDepth) return false;
            ++p;
            v.type = J::ARR;
            ws();
            if (p < e && *p == ']') { ++p; --depth; return true; }
            while (p < e) {
                J x;
                if (!value(x)) return false;
                v.a.push_back(std::move(x));
                ws();
                if (p < e && *p == ',') { ++p; continue; }
                if (p < e && *p == ']') { ++p; --depth; return true; }
                return false;
            }
            return false;
        }
        if (c == '"') {
            if (!str(v.s)) return false;
            v.type = J::STR;
            return true;
        }
        if (p + 4 <= e && std::strncmp(p, "true", 4) == 0) {
            p += 4; v.type = J::BOOL; v.b = true; return true;
        }
        if (p + 5 <= e && std::strncmp(p, "false", 5) == 0) {
            p += 5; v.type = J::BOOL; v.b = false; return true;
        }
        if (p + 4 <= e && std::strncmp(p, "null", 4) == 0) {
            p += 4; v.type = J::NUL; return true;
        }
        char* end = nullptr;
        const double n = std::strtod(p, &end);
        if (end == p || end > e || !std::isfinite(n)) return false;
        p = end;
        v.type = J::NUM;
        v.n = n;
        return true;
    }
    bool root(J& v) {
        if (!value(v)) return false;
        ws();
        return p == e;
    }
};

std::string scalar(const J& v) {
    switch (v.type) {
        case J::STR: return v.s;
        case J::NUM: {
            char b[64];
            std::snprintf(b, sizeof b, "%.17g", v.n);
            return b;
        }
        case J::BOOL: return v.b ? "true" : "false";
        case J::NUL: return "";
        default: return {};
    }
}

void includePoint(GeoFeature& f, double x, double y) {
    if (!std::isfinite(x) || !std::isfinite(y)) return;
    if (!f.hasGeom) {
        f.minx = f.maxx = x;
        f.miny = f.maxy = y;
        f.hasGeom = true;
    } else {
        f.minx = std::min(f.minx, x);
        f.maxx = std::max(f.maxx, x);
        f.miny = std::min(f.miny, y);
        f.maxy = std::max(f.maxy, y);
    }
}

bool parsePosition(const J& v, GeoPoint& p) {
    if (v.type != J::ARR || v.a.size() < 2) return false;
    if (v.a[0].type != J::NUM || v.a[1].type != J::NUM) return false;
    if (!std::isfinite(v.a[0].n) || !std::isfinite(v.a[1].n)) return false;
    p = {v.a[0].n, v.a[1].n};
    return true;
}

bool parseRing(const J& v, GeoRing& ring, Parser& parser) {
    if (v.type != J::ARR) return false;
    for (const J& pos : v.a) {
        GeoPoint p;
        if (!parsePosition(pos, p)) continue;
        if (++parser.vertices > kMaxVertices) {
            parser.tooManyVertices = true;
            return false;
        }
        ring.pts.push_back(p);
    }
    return ring.pts.size() >= 3;
}

bool parsePolygon(const J& coords, std::vector<GeoRing>& out, Parser& parser) {
    if (coords.type != J::ARR) return false;
    for (const J& r : coords.a) {
        GeoRing ring;
        if (!parseRing(r, ring, parser)) {
            if (parser.tooManyVertices) return false;
            continue;
        }
        out.push_back(std::move(ring));
    }
    return !out.empty();
}

bool parseLine(const J& v, std::vector<GeoPoint>& line, Parser& parser) {
    if (v.type != J::ARR) return false;
    for (const J& pos : v.a) {
        GeoPoint p;
        if (!parsePosition(pos, p)) continue;
        if (++parser.vertices > kMaxVertices) {
            parser.tooManyVertices = true;
            return false;
        }
        line.push_back(p);
    }
    return line.size() >= 2;
}

void addFeaturePoint(GeoFeature& f, const GeoPoint& p) {
    f.points.push_back(p);
    includePoint(f, p.x, p.y);
}

bool parseGeometry(const J& g, GeoFeature& f, Parser& parser) {
    const J* type = g.find("type");
    const J* coords = g.find("coordinates");
    if (!type || type->type != J::STR || !coords) return false;
    const std::string& t = type->s;
    if (t == "Polygon") {
        std::vector<GeoRing> rings;
        if (!parsePolygon(*coords, rings, parser)) return false;
        for (const auto& r : rings)
            for (const auto& p : r.pts) includePoint(f, p.x, p.y);
        f.polygons.push_back(std::move(rings));
        return true;
    }
    if (t == "MultiPolygon") {
        if (coords->type != J::ARR) return false;
        bool any = false;
        for (const J& poly : coords->a) {
            std::vector<GeoRing> rings;
            if (!parsePolygon(poly, rings, parser)) {
                if (parser.tooManyVertices) return false;
                continue;
            }
            for (const auto& r : rings)
                for (const auto& p : r.pts) includePoint(f, p.x, p.y);
            f.polygons.push_back(std::move(rings));
            any = true;
        }
        return any;
    }
    if (t == "LineString") {
        std::vector<GeoPoint> line;
        if (!parseLine(*coords, line, parser)) return false;
        for (const auto& p : line) includePoint(f, p.x, p.y);
        f.lines.push_back(std::move(line));
        return true;
    }
    if (t == "MultiLineString") {
        if (coords->type != J::ARR) return false;
        bool any = false;
        for (const J& l : coords->a) {
            std::vector<GeoPoint> line;
            if (!parseLine(l, line, parser)) {
                if (parser.tooManyVertices) return false;
                continue;
            }
            for (const auto& p : line) includePoint(f, p.x, p.y);
            f.lines.push_back(std::move(line));
            any = true;
        }
        return any;
    }
    if (t == "Point") {
        GeoPoint p;
        if (!parsePosition(*coords, p)) return false;
        addFeaturePoint(f, p);
        return true;
    }
    if (t == "MultiPoint") {
        if (coords->type != J::ARR) return false;
        bool any = false;
        for (const J& x : coords->a) {
            GeoPoint p;
            if (!parsePosition(x, p)) continue;
            addFeaturePoint(f, p);
            any = true;
        }
        return any;
    }
    if (t == "GeometryCollection") {
        const J* gs = g.find("geometries");
        if (!gs || gs->type != J::ARR) return false;
        bool any = false;
        for (const J& sub : gs->a)
            if (sub.type == J::OBJ) any = parseGeometry(sub, f, parser) || any;
        return any;
    }
    return false;
}

void finishBounds(GeoFeature& f) {
    if (!f.hasGeom) {
        f.minx = f.miny = f.maxx = f.maxy = f.cx = f.cy = 0;
        return;
    }
    f.cx = (f.minx + f.maxx) * 0.5;
    f.cy = (f.miny + f.maxy) * 0.5;
}

bool parseFeature(const J& v, GeoFeature& f, Parser& parser) {
    if (v.type != J::OBJ) return false;
    const J* geom = v.find("geometry");
    if (geom && geom->type == J::OBJ) parseGeometry(*geom, f, parser);
    if (const J* props = v.find("properties"); props && props->type == J::OBJ) {
        for (const auto& kv : props->o) {
            const std::string s = scalar(kv.second);
            if (!s.empty()) f.props[kv.first] = s;
        }
    }
    if (const J* id = v.find("id")) {
        const std::string s = scalar(*id);
        if (!s.empty()) f.id = s;
    }
    finishBounds(f);
    return f.hasGeom;
}

void appendFeatures(const J& root, GeoMap& map, Parser& parser) {
    const J* feats = root.find("features");
    if (!feats || feats->type != J::ARR) {
        map.error = "FeatureCollection without 'features'";
        return;
    }
    for (const J& jf : feats->a) {
        GeoFeature f;
        if (!parseFeature(jf, f, parser)) continue;
        map.features.push_back(std::move(f));
        if (map.features.size() > 100000) {
            map.error = "GeoJSON with too many features";
            map.features.clear();
            return;
        }
        if (parser.tooManyVertices) {
            map.error = "GeoJSON with too many vertices";
            map.features.clear();
            return;
        }
    }
}

bool looksLonLat(const GeoMap& map) {
    return map.minx >= -180.0 && map.maxx <= 180.0 &&
           map.miny >= -90.0 && map.maxy <= 90.0 &&
           (map.maxx - map.minx) <= 360.0 && (map.maxy - map.miny) <= 180.0;
}

} // namespace

bool geoJSONParse(const std::string& text, GeoMap& out, std::string& err) {
    out = GeoMap{};
    err.clear();
    if (text.empty()) { err = "empty GeoJSON"; return false; }
    if (text.size() > kMaxFile) { err = "GeoJSON too large"; return false; }
    Parser p;
    p.p = text.data();
    p.e = text.data() + text.size();
    if (text.size() >= 3 && (unsigned char)text[0] == 0xEF &&
        (unsigned char)text[1] == 0xBB && (unsigned char)text[2] == 0xBF)
        p.p += 3; // BOM UTF-8
    J root;
    if (!p.root(root)) { err = "invalid JSON"; return false; }
    if (root.type != J::OBJ) { err = "JSON root is not an object"; return false; }
    const J* type = root.find("type");
    if (!type || type->type != J::STR || type->s != "FeatureCollection") {
        err = "expected FeatureCollection";
        return false;
    }
    appendFeatures(root, out, p);
    if (!out.error.empty()) { err = out.error; return false; }
    if (out.features.empty()) { err = "GeoJSON without supported geometry"; return false; }
    geoComputeBounds(out);
    return true;
}

bool geoJSONLoad(const std::string& path, GeoMap& out, std::string& err) {
    out = GeoMap{};
    err.clear();
    if (path.empty()) { err = "file not given"; return false; }
    std::ifstream f(path, std::ios::binary);
    if (!f) { err = "could not open the file"; return false; }
    f.seekg(0, std::ios::end);
    const std::streamoff sz = f.tellg();
    if (sz < 0) { err = "could not measure the file"; return false; }
    if ((unsigned long long)sz > kMaxFile) { err = "file too large"; return false; }
    f.seekg(0, std::ios::beg);
    std::string text((size_t)sz, '\0');
    if (sz > 0 && !f.read(&text[0], sz)) { err = "failed to read the file"; return false; }
    return geoJSONParse(text, out, err);
}

void geoAssignFields(GeoMap& map, const std::string& idField, const std::string& labelField) {
    for (size_t i = 0; i < map.features.size(); ++i) {
        GeoFeature& f = map.features[i];
        if (!idField.empty()) {
            auto it = f.props.find(idField);
            if (it != f.props.end() && !it->second.empty()) f.id = it->second;
        }
        if (f.id.empty()) {
            auto it = f.props.find("id");
            if (it != f.props.end() && !it->second.empty()) f.id = it->second;
            else f.id = std::to_string(i);
        }
        if (!labelField.empty()) {
            auto it = f.props.find(labelField);
            if (it != f.props.end()) f.label = it->second;
        }
        if (f.label.empty()) {
            for (const char* k : {"nome", "name", "label", "rotulo"}) {
                auto it = f.props.find(k);
                if (it != f.props.end() && !it->second.empty()) { f.label = it->second; break; }
            }
        }
        if (f.label.empty()) f.label = f.id;
    }
}

void geoComputeBounds(GeoMap& map) {
    bool any = false;
    for (auto& f : map.features) {
        f.hasGeom = false;
        double minx = 0, miny = 0, maxx = 0, maxy = 0;
        auto includes = [&](double x, double y) {
            if (!std::isfinite(x) || !std::isfinite(y)) return;
            if (!f.hasGeom) {
                minx = maxx = x;
                miny = maxy = y;
                f.hasGeom = true;
            } else {
                minx = std::min(minx, x);
                maxx = std::max(maxx, x);
                miny = std::min(miny, y);
                maxy = std::max(maxy, y);
            }
        };
        for (const auto& poly : f.polygons)
            for (const auto& ring : poly)
                for (const auto& p : ring.pts) includes(p.x, p.y);
        for (const auto& line : f.lines)
            for (const auto& p : line) includes(p.x, p.y);
        for (const auto& p : f.points) includes(p.x, p.y);
        if (!f.hasGeom) {
            f.minx = f.miny = f.maxx = f.maxy = f.cx = f.cy = 0;
            continue;
        }
        f.minx = minx; f.miny = miny; f.maxx = maxx; f.maxy = maxy;
        f.cx = (minx + maxx) * 0.5;
        f.cy = (miny + maxy) * 0.5;
        if (!any) {
            map.minx = minx; map.maxx = maxx;
            map.miny = miny; map.maxy = maxy;
            any = true;
        } else {
            map.minx = std::min(map.minx, minx);
            map.maxx = std::max(map.maxx, maxx);
            map.miny = std::min(map.miny, miny);
            map.maxy = std::max(map.maxy, maxy);
        }
    }
    if (!any) map.minx = map.miny = map.maxx = map.maxy = 0;
}

void geoProjectPoint(double& x, double& y, bool mercator) {
    (void)x; // x does not change; the signature makes its use explicit
    if (!mercator) return;
    const double lat = std::max(-kMaxMercLat, std::min(kMaxMercLat, y));
    y = std::log(std::tan(kPi * 0.25 + lat * kDeg * 0.5)) * kRad;
}

bool geoProject(GeoMap& map, const std::string& mode, std::string& err) {
    err.clear();
    if (map.features.empty()) { err = "GeoJSON without features"; return false; }
    if (mode != "auto" && mode != "mercator" && mode != "linear") {
        err = "unknown projection";
        return false;
    }
    geoComputeBounds(map);
    const bool mercator = (mode == "mercator") || (mode == "auto" && looksLonLat(map));
    map.mercator = mercator;
    if (!mercator) return true;
    for (auto& f : map.features) {
        for (auto& poly : f.polygons)
            for (auto& ring : poly)
                for (auto& p : ring.pts) geoProjectPoint(p.x, p.y, true);
        for (auto& line : f.lines)
            for (auto& p : line) geoProjectPoint(p.x, p.y, true);
        for (auto& p : f.points) geoProjectPoint(p.x, p.y, true);
    }
    geoComputeBounds(map);
    return true;
}

bool geoFeatureContains(const GeoFeature& f, double x, double y) {
    if (!f.hasGeom) return false;
    if (x < f.minx || x > f.maxx || y < f.miny || y > f.maxy) return false;
    for (const auto& poly : f.polygons) {
        if (poly.empty() || poly[0].pts.size() < 3) continue;
        const auto& ext = poly[0].pts;
        bool inside = false;
        for (size_t i = 0, j = ext.size() - 1; i < ext.size(); j = i++) {
            const GeoPoint& a = ext[i];
            const GeoPoint& b = ext[j];
            if (((a.y > y) != (b.y > y)) &&
                (x < (b.x - a.x) * (y - a.y) / (b.y - a.y) + a.x))
                inside = !inside;
        }
        if (!inside) continue;
        bool hole = false;
        for (size_t k = 1; k < poly.size() && !hole; ++k) {
            const auto& ring = poly[k].pts;
            if (ring.size() < 3) continue;
            bool h = false;
            for (size_t i = 0, j = ring.size() - 1; i < ring.size(); j = i++) {
                const GeoPoint& a = ring[i];
                const GeoPoint& b = ring[j];
                if (((a.y > y) != (b.y > y)) &&
                    (x < (b.x - a.x) * (y - a.y) / (b.y - a.y) + a.x))
                    h = !h;
            }
            hole = h;
        }
        if (!hole) return true;
    }
    for (const auto& p : f.points) {
        const double dx = p.x - x, dy = p.y - y;
        if (dx * dx + dy * dy <= 1e-18) return true;
    }
    return false;
}

double geoFeatureLineDistance(const GeoFeature& f, double x, double y) {
    if (f.lines.empty()) return std::numeric_limits<double>::infinity();
    double best = std::numeric_limits<double>::infinity();
    for (const auto& line : f.lines) {
        for (size_t i = 1; i < line.size(); ++i) {
            const double ax = line[i - 1].x, ay = line[i - 1].y;
            const double bx = line[i].x, by = line[i].y;
            const double dx = bx - ax, dy = by - ay;
            const double l2 = dx * dx + dy * dy;
            double u = 0;
            if (l2 > 1e-18) u = ((x - ax) * dx + (y - ay) * dy) / l2;
            u = std::max(0.0, std::min(1.0, u));
            const double px = ax + u * dx, py = ay + u * dy;
            const double d = std::hypot(x - px, y - py);
            if (d < best) best = d;
        }
    }
    return best;
}

int geoFeatureAt(const GeoMap& map, double x, double y, double tol) {
    for (int i = (int)map.features.size() - 1; i >= 0; --i) {
        const GeoFeature& f = map.features[(size_t)i];
        if (geoFeatureContains(f, x, y)) return i;
        if (tol > 0 && !f.lines.empty() &&
            x >= f.minx - tol && x <= f.maxx + tol &&
            y >= f.miny - tol && y <= f.maxy + tol &&
            geoFeatureLineDistance(f, x, y) <= tol)
            return i;
    }
    return -1;
}
