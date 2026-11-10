// nodesim — node graph editor and simulator
// Copyright (C) 2026 Flavio de Vasconcellos Correa
// SPDX-License-Identifier: GPL-3.0-or-later

// Minimal SHA-256 (FIPS 180-4), no dependencies — used in the reproducibility
// manifest (hash of the .nodesim, the trace and the binary).
#pragma once
#include <cstddef>
#include <string>

std::string sha256Hex(const void* data, std::size_t n);
std::string sha256File(const std::string& path); // "" if it can't be opened
