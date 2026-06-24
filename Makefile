# av-x11: Standalone X11 image viewer (C99, no SDL/CMake dependency)
# Target: CentOS 6.x compatible (gcc + libX11-devel)

CC       ?= gcc
CFLAGS   ?= -std=c99 -O2 -Wall -Wextra
LDFLAGS  = -lX11 -lm
STB_DIR  = deps
STB_HDR  = $(STB_DIR)/stb_image.h
TARGET   = bin/av-x11

.PHONY: all clean x11

all: $(TARGET)

x11: $(TARGET)

$(STB_HDR):
	mkdir -p $(STB_DIR)
	curl -fSL -o $@ https://raw.githubusercontent.com/nothings/stb/master/stb_image.h

$(TARGET): src/av_x11.c $(STB_HDR)
	mkdir -p bin
	$(CC) $(CFLAGS) -I$(STB_DIR) -o $@ $< $(LDFLAGS)

clean:
	rm -f $(TARGET)
