// nodesim — node graph editor and simulator
// Copyright (C) 2026 Flavio de Vasconcellos Correa
// SPDX-License-Identifier: GPL-3.0-or-later

// Compatibility and migration tables. Everything that maps a legacy Portuguese
// name to the canonical English name lives here, so the host, tools and tests
// cannot drift apart.
#pragma once
#include <string>

namespace compat {

// Node type id: legacy -> canonical. Unknown ids are returned unchanged.
std::string canonicalTypeId(const std::string& id);

// True for the built-in type ids. State-key migration is intentionally limited
// to built-in plugins; third-party blobs are opaque and must not be rewritten.
bool isBuiltinTypeId(const std::string& id);

// Migrates a plugin state blob (key=value tokens) from legacy keys to canonical
// English keys. Returns the blob unchanged for third-party plugins.
std::string canonicalStateBlob(const std::string& typeId, const std::string& blob);

// Scope node params (`k=v, k=v`) legacy -> canonical.
std::string canonicalScopeParams(const std::string& params);

// Command-line option, environment variable and config key aliases.
std::string canonicalCliOption(const std::string& option);
const char* getenvCompat(const char* name);
std::string canonicalConfigKey(const std::string& key);

} // namespace compat
