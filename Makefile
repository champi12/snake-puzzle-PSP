# Snake Puzzle para PSP - pspdev / PSPSDK
TARGET = snake
OBJS = src/main.o src/gfx.o src/gfx_psp.o src/level.o src/logic.o src/game.o src/play.o src/ui.o src/classic.o \
       src/save.o src/audio_psp.o src/devlog.o src/lang.o

INCDIR = src
CFLAGS = -O2 -G0 -g -Wall -ffast-math -fno-strict-aliasing -MMD -MP
CFLAGS += $(EXTRA)
# version publica por defecto (sin devlog); DEVLOG=1 para la de pruebas (devlog.txt y crash.txt)
DEVLOG ?= 0
CFLAGS += -DDEVLOG=$(DEVLOG)
# stick analogico: desactivado por ahora (fallaba en la consola de pruebas); make ANALOG=1 para usarlo
ANALOG ?= 0
CFLAGS += -DANALOG=$(ANALOG)
ASFLAGS = $(CFLAGS)
LIBS = -lpspgu -lpspmp3 -lpspaudio -lpsputility -lpsppower -lpsprtc -lm

EXTRA_TARGETS = EBOOT.PBP
PSP_EBOOT_TITLE = Snake Puzzle
# icono y fondo del menu de la PSP: los genera tools/extract.py en data/ a partir del APK
ifneq ($(wildcard data/ICON0.PNG),)
PSP_EBOOT_ICON = data/ICON0.PNG
endif
ifneq ($(wildcard data/PIC1.PNG),)
PSP_EBOOT_PIC1 = data/PIC1.PNG
endif

PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak

# dependencias de los .h: si cambia una cabecera se recompila lo que la usa
-include $(OBJS:.o=.d)
