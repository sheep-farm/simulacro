// nodesim — node graph editor and simulator
// Copyright (C) 2026 Flavio de Vasconcellos Correa
// SPDX-License-Identifier: GPL-3.0-or-later

#include "png.h"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

uint32_t crc32tbl[256];
bool crcPronto = false;

void prepareCrc() {
    if (crcPronto) return;
    for (uint32_t n = 0; n < 256; ++n) {
        uint32_t c = n;
        for (int k = 0; k < 8; ++k) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        crc32tbl[n] = c;
    }
    crcPronto = true;
}

uint32_t crc32(const unsigned char* p, size_t n, uint32_t crc = 0) {
    prepareCrc();
    crc ^= 0xFFFFFFFFu;
    for (size_t i = 0; i < n; ++i) crc = crc32tbl[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

uint32_t adler32(const unsigned char* p, size_t n) {
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < n; ++i) {
        a = (a + p[i]) % 65521u;
        b = (b + a) % 65521u;
    }
    return (b << 16) | a;
}

void be32(std::string& s, uint32_t v) {
    s += (char)((v >> 24) & 0xFF);
    s += (char)((v >> 16) & 0xFF);
    s += (char)((v >> 8) & 0xFF);
    s += (char)(v & 0xFF);
}

void block(std::string& s, const char* kind, const std::string& data) {
    be32(s, (uint32_t)data.size());
    const size_t ini = s.size();
    s += kind;
    s += data;
    uint32_t c = crc32((const unsigned char*)s.data() + ini, s.size() - ini);
    be32(s, c);
}

// zlib with "stored" deflate (BTYPE=00): no dictionary, blocks up to 65535
void zlibStored(std::string& s, const std::string& raw) {
    s += (char)0x78; // CMF: deflate, 32K window
    s += (char)0x01; // FLG: no dictionary, valid check bits
    size_t off = 0;
    do {
        const size_t n = std::min<size_t>(65535, raw.size() - off);
        const bool end = (off + n) >= raw.size();
        s += (char)(end ? 1 : 0);
        s += (char)(n & 0xFF);
        s += (char)((n >> 8) & 0xFF);
        const uint32_t nl = (uint32_t)(~n) & 0xFFFFu;
        s += (char)(nl & 0xFF);
        s += (char)((nl >> 8) & 0xFF);
        s.append(raw, off, n);
        off += n;
    } while (off < raw.size());
    be32(s, adler32((const unsigned char*)raw.data(), raw.size()));
}

} // namespace

std::string pngRgba(int w, int h, const unsigned char* rgba) {
    if (w <= 0 || h <= 0 || !rgba) return std::string();
    const size_t line = (size_t)w * 4;
    std::string raw;
    raw.reserve(((size_t)h) * (line + 1));
    for (int y = 0; y < h; ++y) {
        raw += (char)0; // "none" filter
        raw.append((const char*)rgba + (size_t)y * line, line);
    }

    std::string s;
    s.reserve(raw.size() + 4096);
    const unsigned char sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    s.append((const char*)sig, 8);

    std::string ihdr;
    be32(ihdr, (uint32_t)w);
    be32(ihdr, (uint32_t)h);
    ihdr += (char)8; // depth
    ihdr += (char)6; // color type: RGBA
    ihdr += (char)0; // compression (deflate)
    ihdr += (char)0; // filter (default)
    ihdr += (char)0; // no interlacing
    block(s, "IHDR", ihdr);

    std::string z;
    zlibStored(z, raw);
    block(s, "IDAT", z);
    block(s, "IEND", std::string());
    return s;
}
