@echo off

set SDL2=third_party\SDL2-2.28.2

IF NOT EXIST "third_party\tcc\tcc.exe" (
  ECHO Downloading the TCC compiler...
  curl -L -o third_party\tcc.zip https://github.com/FitzRoyX/tinycc/releases/download/tcc_20230519/tcc_20230519.zip
  tar -xf third_party\tcc.zip -C third_party
  del third_party\tcc.zip
)
IF NOT EXIST "third_party\tcc\tcc.exe" (
  ECHO:
  ECHO ERROR: third_party\tcc\tcc.exe doesn't exist and could not be downloaded.
  ECHO   Download it from https://github.com/FitzRoyX/tinycc/releases/download/tcc_20230519/tcc_20230519.zip
  ECHO   and unzip it so that third_party\tcc\tcc.exe exists. It needs to be the 64-bit version.
  ECHO:
  PAUSE
  EXIT /B 1
)

IF NOT EXIST "%SDL2%\lib\x64\SDL2.dll" (
  ECHO Downloading SDL2...
  curl -L -o third_party\sdl.zip https://github.com/libsdl-org/SDL/releases/download/release-2.28.2/SDL2-devel-2.28.2-VC.zip
  tar -xf third_party\sdl.zip -C third_party
  del third_party\sdl.zip
)
IF NOT EXIST "%SDL2%\lib\x64\SDL2.dll" (
  ECHO:
  ECHO ERROR: SDL is not unzipped properly into %SDL2%
  ECHO   Download it from https://github.com/libsdl-org/SDL/releases/download/release-2.28.2/SDL2-devel-2.28.2-VC.zip
  ECHO:
  PAUSE
  EXIT /B 1
)

IF NOT EXIST "zelda3_assets.dat" (
  ECHO:
  ECHO ERROR: zelda3_assets.dat was not found.
  ECHO Put your ROM here as zelda3.sfc and run extract_assets.bat (needs Python),
  ECHO or copy zelda3_assets.dat from a build you extracted earlier. Please see README.md
  ECHO:
  PAUSE
  EXIT /B 1
) ELSE (
  REM
)


echo Choosing the mods (mods\default.cfg and your mods.cfg; see mods\SPEC.md)...
set MODS_PY=
where python >NUL 2>NUL && set MODS_PY=python
if not defined MODS_PY where py >NUL 2>NUL && set MODS_PY=py -3
if defined MODS_PY (
  %MODS_PY% tools\mods.py write
  IF ERRORLEVEL 1 (
    ECHO:
    ECHO The mods in mods.cfg were refused: the reason is above. Fix mods.cfg, or delete it
    ECHO to build the shipped set.
    PAUSE
    EXIT /B 1
  )
) ELSE (
  ECHO No Python found: building the shipped set of mods ^(src\mods_default.h^); a mods.cfg is not read.
  copy /Y src\mods_default.h src\mods_build.h >NUL
)
echo Building with TCC...
third_party\tcc\tcc.exe -ozelda3.exe -DCOMPILER_TCC=1 -DSTBI_NO_SIMD=1 -DHAVE_STDINT_H=1 -D_HAVE_STDINT_H=1 -DSYSTEM_VOLUME_MIXER_AVAILABLE=0 -I%SDL2%/include -L%SDL2%/lib/x64 -lSDL2 -lws2_32 -I. src/*.c snes/*.c third_party/gl_core/gl_core_3_1.c third_party/opus-1.3.1-stripped/opus_decoder_amalgam.c
IF ERRORLEVEL 1 goto GETOUT

copy %SDL2%\lib\x64\SDL2.dll .

echo Running...
zelda3.exe


:GETOUT
pause
