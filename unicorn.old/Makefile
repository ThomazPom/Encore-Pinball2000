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
          $(SRCDIR)/splash.c \
          $(SRCDIR)/sound.c  \
          $(SRCDIR)/netcon.c \
          $(SRCDIR)/lpt_pass.c \
          $(SRCDIR)/symbols.c \
          $(SRCDIR)/stb_impl.c

OBJS    = $(patsubst $(SRCDIR)/%.c,$(BLDDIR)/%.o,$(SRCS))

# Default splash screen — embedded verbatim into the binary by `ld -r -b
# binary`. Drop your own JPEG over assets/splash-screen.jpg before `make`
# to ship a custom default; users can also override at runtime with
# `--splash-screen PATH`. The symbol names below come from the file path
# (slashes become underscores).
SPLASH_SRC = assets/splash-screen.jpg
SPLASH_OBJ = $(BLDDIR)/splash_data.o

TARGET  = $(BLDDIR)/encore

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS) $(SPLASH_OBJ) | $(BLDDIR)
	$(CC) $(OBJS) $(SPLASH_OBJ) -o $@ $(LDFLAGS)
	@echo "=== Encore built: $@ ==="

$(SPLASH_OBJ): $(SPLASH_SRC) | $(BLDDIR)
	ld -r -b binary -o $(SPLASH_OBJ) $(SPLASH_SRC)

$(BLDDIR)/%.o: $(SRCDIR)/%.c $(INCDIR)/encore.h | $(BLDDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BLDDIR):
	mkdir -p $(BLDDIR)

clean:
	rm -rf $(BLDDIR)
