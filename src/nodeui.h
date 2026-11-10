// simulacro — policy of where the node parameter widgets are drawn
// Copyright (C) 2026 Flavio de Vasconcellos Correa
// SPDX-License-Identifier: GPL-3.0-or-later

// Configured once through SIMULACRO_NODE_UI and never re-read: `inner` (default)
// anchors the widget strip inside the node body, below its title, clipped to
// the node box; `below` keeps the fase-1 rail under the node; `off` disables
// the layer (in-canvas editing only via the wxPropertyGrid).
//
// Scope charts, map scopes and the wx widgets chrome are unrelated to this
// policy: ImGui never touches their rendering.
#pragma once

namespace nodeui {

enum class Mode { Inner, Below, Off };

Mode mode();  // reads SIMULACRO_NODE_UI once (defaults to Inner)

} // namespace nodeui
