CXX ?= g++
PKGS = sdl2 SDL2_ttf SDL2_image SDL2_mixer
CXXFLAGS ?= -O2 -std=gnu++17 -Wall -Wno-unused-parameter
# shared on-screen keyboard engine (s40kb.h, also used by s40web); kept out of CXXFLAGS so
# builds that override CXXFLAGS still find it
COMMON ?= ../common
SRC = $(wildcard src/*.cpp)

s40: $(SRC) src/s40.h $(COMMON)/s40kb.h
	$(CXX) $(CXXFLAGS) -I$(COMMON) $(shell pkg-config --cflags $(PKGS)) $(SRC) -o $@ $(shell pkg-config --libs $(PKGS)) -lpthread -lz -lvterm -lutil

assets: tools/make_icons.py
	python3 tools/make_icons.py ../nokia_s40_ui/index.html assets/icons

clean:
	rm -f s40

.PHONY: assets clean
