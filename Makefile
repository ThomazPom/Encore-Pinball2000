# Encore — Pinball 2000 Emulator
# Build: make
# Run:   ./build/encore [--game swe1|rfm] [--roms /path/to/roms]

CC      = gcc
CFLAGS  = -O2 -g -Wall -Wextra -Wno-unused-parameter -Iinclude \
          $(shell pkg-config --cflags sdl2 SDL2_mixer)
LDFLAGS = $(shell pkg-config --libs sdl2 SDL2_mixer) -lunicorn -lm -lpthread

SRCDIR  = src
BLDDIR  = build
INCDIR  = include

SRCS    = $(SRCDIR)/main.c   \
          $(SRCDIR)/cpu.c    \
          $(SRCDIR)/memory.c \
          $(SRCDIR)/rom.c    \
          $(SRCDIR)/pci.c    \
          $(SRCDIR)/io.c     \
          $(SRCDIR)/bar.c    \
          $(SRCDIR)/display.c \
          $(SRCDIR)/sound.c  \
          $(SRCDIR)/netcon.c \
          $(SRCDIR)/stb_impl.c

OBJS    = $(patsubst $(SRCDIR)/%.c,$(BLDDIR)/%.o,$(SRCS))
TARGET  = $(BLDDIR)/encore

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS) | $(BLDDIR)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)
	@echo "=== Encore built: $@ ==="

$(BLDDIR)/%.o: $(SRCDIR)/%.c $(INCDIR)/encore.h | $(BLDDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BLDDIR):
	mkdir -p $(BLDDIR)

clean:
	rm -rf $(BLDDIR)
