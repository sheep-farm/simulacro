// nodesim — node graph editor and simulator
// Copyright (C) 2026 Flavio de Vasconcellos Correa
// SPDX-License-Identifier: GPL-3.0-or-later

// Small helpers shared by the built-in plugins. Header-only and ABI-neutral:
// third-party plugins do not need to include this file.
#pragma once
#include <plugin.h>
#include <cstdlib>
#include <cstring>

namespace nsplug {

inline char* dupStr(const char* s) {
    const size_t n = std::strlen(s) + 1;
    char* o = static_cast<char*>(std::malloc(n));
    if (o) std::memcpy(o, s, n);
    return o;
}

inline double sockValue(const NsContext* c, int s) {
    if (!c || !c->in || !c->sockOff || s < 0 || s >= c->nsock) return 0.0;
    double v = 0.0;
    for (int k = c->sockOff[s]; k < c->sockOff[s + 1]; ++k) v += c->in[k];
    return v;
}

inline double sockSum(const NsContext* c, int s) { return sockValue(c, s); }

} // namespace nsplug
