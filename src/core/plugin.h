// nodesim — graph node editor and simulator
// Copyright (C) 2026 Flavio de Vasconcellos Correa
// SPDX-License-Identifier: GPL-3.0-or-later

// Host side of the plugin system. The public ABI (what a .so needs to see)
// lives in ../sdk/nodesim_plugin.h. This facade keeps the historical include
// path while the implementation is split into:
//   plugin/registry.h  — types/params/metadata
//   plugin/instance.h  — live instances and exception isolation
//   plugin/loader.h    — dlopen/dlsym, resources/exec/events
//   plugin/manager.h   — the PluginManager facade
//
// ABI 14: package identity/provenance metadata (author, license,
// homepage, description, and build) in addition to struct_size/capabilities (ABI 13).
// ABI 12: parameters with type/unit/help/options + input labels.
// A plugin exports: extern "C" const NsPlugin* nodesim_plugin();
#pragma once
#include "plugin/instance.h"
#include "plugin/registry.h"
#include "plugin/loader.h"
#include "plugin/manager.h"
