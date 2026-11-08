# Study Planner
#   make          build the GTK app            (build/studyplanner)
#   make test     build and run core unit tests
#   make run      build and launch
#   make clean
#
# On Windows use the MSYS2 UCRT64 shell (or put C:\msys64\ucrt64\bin on PATH)
# and run `mingw32-make`.

CC      ?= gcc
CFLAGS  ?= -O2 -g
WARN    := -std=c11 -Wall -Wextra -Wpedantic
BUILD   := build

GTK_CFLAGS := $(shell pkg-config --cflags gtk4 2>/dev/null)
GTK_LIBS   := $(shell pkg-config --libs gtk4 2>/dev/null)

ifeq ($(OS),Windows_NT)
  EXE := .exe
  GUI_LDFLAGS := -mwindows
endif

CORE_SRC := core/planner.c
UI_SRC   := ui/main.c

.PHONY: all test run clean

all: $(BUILD)/studyplanner$(EXE)

$(BUILD):
	@mkdir -p $(BUILD)

$(BUILD)/studyplanner$(EXE): $(CORE_SRC) $(UI_SRC) core/planner.h ui/style.h | $(BUILD)
	@test -n "$(GTK_LIBS)" || { echo "GTK 4 not found (pkg-config gtk4). See README."; exit 1; }
	$(CC) $(WARN) $(CFLAGS) $(GTK_CFLAGS) $(CORE_SRC) $(UI_SRC) -o $@ $(GTK_LIBS) -lm $(GUI_LDFLAGS)

$(BUILD)/test_core$(EXE): $(CORE_SRC) tests/test_core.c core/planner.h | $(BUILD)
	$(CC) $(WARN) $(CFLAGS) $(CORE_SRC) tests/test_core.c -o $@

test: $(BUILD)/test_core$(EXE)
	./$(BUILD)/test_core$(EXE)

run: all
	./$(BUILD)/studyplanner$(EXE)

clean:
	rm -rf $(BUILD)
