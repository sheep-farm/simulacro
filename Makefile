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

.DEFAULT_GOAL := all

CXX      ?= g++
WX_CONFIG ?= wx-config

CXXFLAGS = -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter -MMD -MP \
           -ffile-prefix-map=$(CURDIR)=. \
           -Isrc -Isrc/core -Isrc/sdk \
           -I$(THIRDPARTY) -I$(THIRDPARTY)/backends \
           -DSIMULACRO_NODE_WINDOW=1 \
           $(EXTRA_CXXFLAGS) \
           $(shell $(WX_CONFIG) --cxxflags) $(shell sdl2-config --cflags)
LDLIBS   = $(shell $(WX_CONFIG) --libs std,aui,propgrid) \
           $(shell sdl2-config --libs) -lSDL2_ttf

SRC = src
OBJ = build
BIN = bin
PLUGDIR = plugins
THIRDPARTY = third_party/imgui
VERSION := 0.2.0

SRC_CPP := $(shell find $(SRC) -name '*.cpp' | sort)
OBJS = $(patsubst $(SRC)/%.cpp,$(OBJ)/%.o,$(SRC_CPP))

IMGUI_CPP := $(wildcard $(THIRDPARTY)/imgui.cpp $(THIRDPARTY)/imgui_draw.cpp \
              $(THIRDPARTY)/imgui_tables.cpp $(THIRDPARTY)/imgui_widgets.cpp)
IMGUI_BACKEND_CPP := $(THIRDPARTY)/backends/imgui_impl_sdlrenderer2.cpp
IMGUI_OBJS = $(OBJ)/imgui/imgui.o $(OBJ)/imgui/imgui_draw.o \
             $(OBJ)/imgui/imgui_tables.o $(OBJ)/imgui/imgui_widgets.o \
             $(OBJ)/imgui/imgui_backend.o

$(OBJ)/imgui/imgui_backend.o: $(THIRDPARTY)/backends/imgui_impl_sdlrenderer2.cpp | $(OBJ)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -I$(THIRDPARTY) -I$(THIRDPARTY)/backends -c -o $@ $<

$(OBJ)/imgui/%.o: $(THIRDPARTY)/%.cpp | $(OBJ)
	@mkdir -p $(OBJ)/imgui
	$(CXX) $(CXXFLAGS) -I$(THIRDPARTY) -I$(THIRDPARTY)/backends -c -o $@ $<

$(BIN)/simulacro: $(OBJS) $(IMGUI_OBJS) | $(BIN)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS) $(IMGUI_OBJS) $(LDLIBS)

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
	$(CXX) -std=c++17 -O2 -Wall -shared -fPIC -Isrc/core -Isrc/sdk -o $@ $<

test: $(BIN)/simulacro plugins
	tests/run.sh

run: $(BIN)/simulacro plugins
	./$(BIN)/simulacro

clean:
	rm -rf $(OBJ) $(BIN) $(PLUGINS)

distclean: clean
	rm -f $(PLUGDIR)/*.d

-include $(OBJS:.o=.d) $(IMGUI_OBJS:.o=.d)

.PHONY: all
all: $(BIN)/simulacro plugins

.PHONY: all plugins test run clean distclean
