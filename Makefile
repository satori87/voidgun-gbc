GBDK_HOME = C:/gbdk
LCC = $(GBDK_HOME)/bin/lcc

# -Wa-l    = assembler listing
# -Wl-m    = linker map file
# -Wl-j    = linker debug output
# -Wm-yC   = CGB-only ROM header
CFLAGS = -Wa-l -Wl-m -Wl-j -Wm-yC -Wm-yo4 -Wm-yt1 -Ihugedriver/include
# 64KB ROM (4 banks, MBC1): banks 0-1 = code+music+SFX, bank 2 = asset tiles
LIBS = -Wl-lhugedriver/gbdk/hUGEDriver.lib

TARGET = voidgun.gbc

all: $(TARGET)

SOURCES = src/main.c src/ship.c src/ship2.c src/flag_spr.c src/font.c \
          src/music.c \
          src/sfx_yougrabbed.c src/sfx_gammacapture.c src/sfx_yourshiphit.c \
          src/sfx_ui.c src/sfx_shiphit.c src/sfx_deltacapture.c src/sfx_deltagrabbed.c

$(TARGET): $(SOURCES)
	$(LCC) $(CFLAGS) $(LIBS) -o $@ $(SOURCES)

clean:
	rm -f $(TARGET) *.map *.noi
	rm -f src/*.asm src/*.lst src/*.sym src/*.o src/*.ihx

.PHONY: all clean
