// nodesim — node graph editor and simulator
// Copyright (C) 2026 Flavio de Vasconcellos Correa
// SPDX-License-Identifier: GPL-3.0-or-later

// Runtime language selection for the graphical interface.
//
// English is the source language: all UI strings in the code are written in
// English. This module translates them to Brazilian Portuguese when the user
// selects Portuguese (NODESIM_LANG=pt-BR or View > Language). Stable
// identifiers, serialized keys and plugin ABI strings are never translated.
#pragma once
#include <string>
#include <utility>

namespace i18n {

enum class Lang { EN, PT_BR };

// Explicit translation marker. UI labels are wrapped with tr(...) at the
// drawing site; dynamic values (node titles, numbers, plugin output) stay as
// plain strings and are never translated by accident.
struct Key {
    std::string en;
    explicit Key(const char* s) : en(s ? s : "") {}
    explicit Key(std::string s) : en(std::move(s)) {}
};
inline Key tr(const char* s) { return Key(s); }
inline Key tr(const std::string& s) { return Key(s); }
inline Key tr(std::string&& s) { return Key(std::move(s)); }

Lang lang();
void setLang(Lang l);
// Reads NODESIM_LANG (en/pt-BR/pt/auto). Defaults to English.
void initFromEnv();
// Accepts "en", "pt", "pt-BR", "pt_BR" (case-insensitive). Returns false for
// an unknown code (the current language is kept).
bool setFromCode(const std::string& code);
std::string code();  // "en" or "pt-BR"
std::string name();  // "English" or "Português (Brasil)"

// Translates one UI string (English -> current language). Exact matches first,
// then the dynamic prefixes registered below. Unknown strings are unchanged.
std::string text(const std::string& en);

// Display name for a serialized plugin/scope parameter key. The key itself is
// kept stable for compatibility; only the label shown in the UI changes.
std::string paramName(const std::string& key);

} // namespace i18n
