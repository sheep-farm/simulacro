# simulacro — editor de grafos de nós (wxWidgets/SDL) e simulador
# Copyright (C) 2026 Flavio de Vasconcellos Correa
# SPDX-License-Identifier: GPL-3.0-or-later
#
# wxWidgets owns the window, menus, docks, dialogs and property editors.
# SDL is kept only where it earns its keep: the immediate-mode raster views
# (the node canvas and the scope charts), rendered off-screen and blitted into
# wx panels. The engine lives under src/core and the built-in plugin sources
# under plugins/ — the tree is self-contained and does not require the former
# nodesim checkout.

CXX      ?= g++
WX_CONFIG ?= wx-config

CXXFLAGS = -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter -MMD -MP \
           -ffile-prefix-map=$(CURDIR)=. \
           -Isrc -Isrc/core -Isrc/sdk \
           $(EXTRA_CXXFLAGS) \
           $(shell $(WX_CONFIG) --cxxflags) $(shell sdl2-config --cflags)
LDLIBS   = $(shell $(WX_CONFIG) --libs std,aui,propgrid) \
           $(shell sdl2-config --libs) -lSDL2_ttf

SRC = src
OBJ = build
BIN = bin
PLUGDIR = plugins
VERSION := 0.1.0

SRC_CPP := $(shell find $(SRC) -name '*.cpp' | sort)
OBJS = $(patsubst $(SRC)/%.cpp,$(OBJ)/%.o,$(SRC_CPP))

$(BIN)/simulacro: $(OBJS) | $(BIN)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS) $(LDLIBS)

$(OBJ)/%.o: $(SRC)/%.cpp | $(OBJ)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(BIN):
	mkdir -p $(BIN)

$(OBJ):
	mkdir -p $(OBJ)

# Built-in plugins: ABI-stable .so files, compiled from the in-tree sources
# against the same SDK that lives in src/sdk. The same result holds if you
# build the sources any other way; only the ABI matters.
PLUGINS = $(patsubst $(PLUGDIR)/%.cpp,$(PLUGDIR)/%.so,$(wildcard $(PLUGDIR)/*.cpp))

plugins: $(PLUGINS)

$(PLUGDIR)/%.so: $(PLUGDIR)/%.cpp $(PLUGDIR)/common.h
	@mkdir -p $(PLUGDIR)
	$(CXX) -std=c++17 -O2 -Wall -shared -fPIC -Isrc/core -o $@ $<

test: $(BIN)/simulacro plugins
	tests/run.sh

run: $(BIN)/simulacro plugins
	./$(BIN)/simulacro

clean:
	rm -rf $(OBJ) $(BIN) $(PLUGINS)

distclean: clean
	rm -f $(PLUGDIR)/*.d

-include $(OBJS:.o=.d)

.PHONY: plugins test run clean distclean
