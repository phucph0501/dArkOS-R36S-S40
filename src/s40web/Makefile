# s40web - C++/SDL2 browser shell embedding WPE WebKit (via WPEBackend-fdo SHM export)
CXX ?= g++
CC ?= gcc
PKGS = sdl2 SDL2_ttf SDL2_image wpe-webkit-2.0 wpebackend-fdo-1.0 wpe-1.0 wayland-server xkbcommon libsoup-3.0
OPT ?= -O2
CXXFLAGS ?= $(OPT) -std=gnu++17 -Wall -Wno-missing-field-initializers
SRC = src/main.cpp src/WpeView.cpp src/Adblock.cpp
# shared on-screen keyboard engine (s40kb.h, also used by the S40 launcher)
COMMON ?= ../common

all: s40web ext/s40abext.so

# lib/libs40gbm.so is linked first (and exported to WebKit's child processes via LD_PRELOAD):
# it provides GBM functions the RK3326 libmali lacks, which the BIND_NOW-linked WebKit needs.
s40web: $(SRC) src/WpeView.h src/Adblock.h lib/libs40gbm.so $(COMMON)/s40kb.h
	$(CXX) $(CXXFLAGS) -I$(COMMON) $(shell pkg-config --cflags $(PKGS)) $(SRC) -o $@ -Wl,--no-as-needed -Llib -ls40gbm -Wl,-rpath,'$$ORIGIN/lib' $(shell pkg-config --libs $(PKGS))

lib/libs40gbm.so: src/gbmshim.c
	mkdir -p lib
	$(CC) $(OPT) -fPIC -shared $< -o $@ -ldl

# ad-block web-process extension, loaded by WPEWebProcess from <dir of s40web>/ext
ext/s40abext.so: src/abext.cpp
	mkdir -p ext
	$(CXX) $(CXXFLAGS) -fPIC -shared $(shell pkg-config --cflags wpe-web-process-extension-2.0) $< -o $@ $(shell pkg-config --libs wpe-web-process-extension-2.0)

clean:
	rm -rf s40web ext lib
.PHONY: all clean
