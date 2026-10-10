@echo off
REM Builds zelda3_assets.dat from your own ROM of
REM "Legend of Zelda, The - A Link to the Past (USA)".
REM Put the ROM (.sfc or .smc, any file name) in this folder and run this file.
cd /d "%~dp0"

REM Find Python 3
set PY=
py -3 -c "import sys; sys.exit(0 if sys.version_info[0] == 3 else 1)" >nul 2>&1
IF NOT ERRORLEVEL 1 set PY=py -3
IF "%PY%"=="" (
  python -c "import sys; sys.exit(0 if sys.version_info[0] == 3 else 1)" >nul 2>&1
  IF NOT ERRORLEVEL 1 set PY=python
)
IF "%PY%"=="" (
  ECHO:
  ECHO ERROR: Python 3 is needed once to extract the game assets.
  ECHO   Install it from https://www.python.org/downloads/
  ECHO   ^(tick "Add python.exe to PATH" in the installer^), then run this file again.
  ECHO:
  pause
  EXIT /B 1
)

REM Find the ROM
set ROM=
IF EXIST "zelda3.sfc" set ROM=zelda3.sfc
IF "%ROM%"=="" IF EXIST "zelda3.smc" set ROM=zelda3.smc
IF "%ROM%"=="" FOR %%F IN (*.sfc *.smc) DO IF "%ROM%"=="" set ROM=%%F
IF "%ROM%"=="" (
  ECHO:
  ECHO ERROR: No ROM found. Copy your "A Link to the Past (USA)" ROM ^(.sfc or .smc^)
  ECHO   into this folder: %CD%
  ECHO:
  pause
  EXIT /B 1
)
ECHO Using ROM: %ROM%

REM Install the two Python libraries the extractor needs, if missing
%PY% -c "import PIL, yaml" >nul 2>&1
IF ERRORLEVEL 1 %PY% -m pip install --user pillow pyyaml

2>nul (del zelda3_assets.dat)
%PY% assets/restool.py --extract-from-rom -r "%ROM%"

IF NOT EXIST "zelda3_assets.dat" (
  ECHO:
  ECHO ERROR: Asset extraction failed! See the messages above.
  pause
  EXIT /B 1
)

ECHO:
ECHO Complete! zelda3_assets.dat was created. You can now start zelda3.exe.
pause
