CC ?= gcc

HAS_FLAC ?= 1
DEFINES ?=

CFLAGS ?= \
	-O3 -march=native -mtune=native \
	-Wall -Wextra \
	-Wshadow -Winit-self -Wunused -Wunreachable-code \
	-Wno-missing-field-initializers -Wno-unused-result \
	-Wno-strict-aliasing -Wno-stringop-overflow

LDFLAGS ?= -lSDL2 -lm

SRC = $(wildcard \
	src/gfx/*.c \
	src/modloaders/*.c \
	src/smploaders/*.c \
)

ifeq ($(HAS_FLAC), 1)
	DEFINES += -DHAS_LIBFLAC
	SRC += $(wildcard src/libflac/*.c)
endif

SRC += $(wildcard src/*.c)

ifndef DEBUG
	DEFINES += -DNDEBUG
endif

OUT_DIR = release/other
OUT_FILE = $(OUT_DIR)/pt2-clone

.PHONY: all clean

all: $(OUT_FILE)

OBJ = $(SRC:%.c=%.o)

$(OUT_FILE): $(OBJ)
	@mkdir -p $(OUT_DIR)
	$(CC) $(OBJ) $(LDFLAGS) -o $(OUT_FILE)
	@echo "Done. The executable can be found in '$(OUT_DIR)' if everything went well."

%.o: %.c
	@echo "CC $<"
	@$(CC) $(DEFINES) $(CFLAGS) -c $< -o $@

clean:
	-rm -f $(OUT_FILE)
	-rm -f $(SRC:%.c=%.o)
