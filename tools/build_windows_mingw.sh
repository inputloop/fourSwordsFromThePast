#!/bin/sh
# Cross-compiles zelda3.exe for 64-bit Windows from Linux using MinGW-w64.
#   sudo apt-get install gcc-mingw-w64-x86-64
#   tools/build_windows_mingw.sh
# The result is placed in build-windows/ together with SDL2.dll.
set -e
SDL_VERSION=2.28.2
CC=x86_64-w64-mingw32-gcc
OUT=build-windows
SDL_DIR=$OUT/SDL2-$SDL_VERSION/x86_64-w64-mingw32

mkdir -p $OUT/obj
if [ ! -d "$SDL_DIR" ]; then
  curl -sSL -o $OUT/sdl.tar.gz https://github.com/libsdl-org/SDL/releases/download/release-$SDL_VERSION/SDL2-devel-$SDL_VERSION-mingw.tar.gz
  tar xzf $OUT/sdl.tar.gz -C $OUT
fi

CFLAGS="-O2 -Werror -I. -I$SDL_DIR/include/SDL2 -Dmain=SDL_main -DSYSTEM_VOLUME_MIXER_AVAILABLE=0"
OBJS=""
for f in src/*.c snes/*.c third_party/gl_core/gl_core_3_1.c third_party/opus-1.3.1-stripped/opus_decoder_amalgam.c; do
  o=$OUT/obj/$(echo $f | tr / _).o
  $CC -c $CFLAGS $f -o $o
  OBJS="$OBJS $o"
done
$CC $OBJS -o $OUT/zelda3.exe -L$SDL_DIR/lib -lmingw32 -lSDL2main -lSDL2 -lws2_32 -mwindows -static-libgcc
cp $SDL_DIR/bin/SDL2.dll $OUT/
cp zelda3.ini $OUT/
echo "Built $OUT/zelda3.exe"
