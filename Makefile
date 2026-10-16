TARGET_EXEC:=zelda3
SRCS:=$(wildcard src/*.c snes/*.c) third_party/gl_core/gl_core_3_1.c third_party/opus-1.3.1-stripped/opus_decoder_amalgam.c
OBJS:=$(SRCS:%.c=%.o)
PYTHON:=/usr/bin/env python3
CFLAGS:=$(if $(CFLAGS),$(CFLAGS),-O2 -Werror) -I .
CFLAGS:=${CFLAGS} $(shell sdl2-config --cflags) -DSYSTEM_VOLUME_MIXER_AVAILABLE=0

ifeq (${OS},Windows_NT)
    WINDRES:=windres
    RES:=zelda3.res
    SDLFLAGS:=-Wl,-Bstatic $(shell sdl2-config --static-libs) -lws2_32
else
    SDLFLAGS:=$(shell sdl2-config --libs) -lm
endif

.PHONY: all clean clean_obj clean_gen books

all: $(TARGET_EXEC) zelda3_assets.dat

# Which mods this build carries (mods/SPEC.md): src/mods_build.h comes from
# mods/default.cfg and your own mods.cfg. It is remade on every make, but only
# rewritten (and so only recompiles anything) when the composition changed.
src/mods_build.h: FORCE
	@$(PYTHON) tools/mods.py write
FORCE:
$(OBJS): | src/mods_build.h
$(TARGET_EXEC): $(OBJS) $(RES)
	$(CC) $^ -o $@ $(LDFLAGS) $(SDLFLAGS)
%.o : %.c
	$(CC) -c $(CFLAGS) -MMD -MP $< -o $@

# Rebuild objects when headers change.
-include $(OBJS:.o=.d)

$(RES): src/platform/win32/zelda3.rc
	@echo "Generating Windows resources"
	@$(WINDRES) $< -O coff -o $@

# The player's book and the developer's guide (docs/books): help/ with Windows help (.chm, needs
# chmcmd from Free Pascal), the pages as HTML (needs pandoc), and plain text.
books:
	$(PYTHON) tools/books.py build

# The Nintendo Switch build (src/platform/switch), with its own set of mods (mods/switch.cfg).
# Needs devkitPro (libnx, switch-sdl2); its container image works: devkitpro/devkita64.
.PHONY: switch
switch:
	$(MAKE) -C src/platform/switch

zelda3_assets.dat:
	@echo "Extracting game resources"
	$(PYTHON) assets/restool.py --extract-from-rom

clean: clean_obj clean_gen
clean_obj:
	@$(RM) $(OBJS) $(OBJS:.o=.d) $(TARGET_EXEC)
clean_gen:
	@$(RM) src/mods_build.h $(RES) zelda3_assets.dat assets/zelda3_assets.dat assets/*.txt tables/*.png assets/sprites/*.png assets/*.yaml
	@rm -rf assets/__pycache__ assets/dungeon assets/img assets/overworld assets/sound
