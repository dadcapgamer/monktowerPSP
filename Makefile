.DEFAULT_GOAL := all

TARGET = monk_tower_psp
OBJS = src/main.o

PSPPREFIX := $(shell psp-config --psp-prefix)

INCDIR = $(PSPPREFIX)/include/SDL2
CFLAGS = -O2 -G0 -Wall -Wextra -Werror -std=c11
ASFLAGS = $(CFLAGS)

ifeq ($(QA_AUTOSTART),1)
CFLAGS += -DQA_AUTOSTART
endif

ifeq ($(QA_SETTINGS),1)
CFLAGS += -DQA_SETTINGS
endif

ifeq ($(QA_CONTENT),1)
CFLAGS += -DQA_CONTENT
endif

ifeq ($(QA_SAVE),1)
CFLAGS += -DQA_SAVE
endif

ifeq ($(QA_WIN),1)
CFLAGS += -DQA_WIN
endif

LIBDIR = $(PSPPREFIX)/lib
LIBS = -lSDL2_image -lSDL2 -lpng -lz -ljpeg -lm \
	-lGL -lpspvram -lpspaudio -lpspvfpu -lpspdisplay -lpspgu -lpspge \
	-lpsphprm -lpspctrl -lpsppower
LDFLAGS =

EXTRA_TARGETS = EBOOT.PBP
PSP_EBOOT_TITLE = Monk Tower PSP - Release Candidate 2

# XMB art, exported from the "monktower" page of the Art 4 Ports Figma file.
# ICON0 is the 144x80 game-list thumbnail, PIC1 the 480x272 full-screen
# background shown once the title is highlighted. Both are opaque 8-bit RGB.
PSP_EBOOT_ICON = psp-xmb/ICON0.PNG
PSP_EBOOT_PIC1 = psp-xmb/PIC1.PNG

# build.mak's EBOOT.PBP rule lists the art only on the pack-pbp command line,
# never as a prerequisite, so without this an art-only change never repacks.
EBOOT.PBP: $(PSP_EBOOT_ICON) $(PSP_EBOOT_PIC1)

PSPSDK := $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak
