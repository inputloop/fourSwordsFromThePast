## Installing libraries on Linux/MacOS
1. Open a terminal
2. Install pip if not already installed
```sh
python3 -m ensurepip
```
3. Clone the repo and `cd` into it
```sh
git clone https://github.com/snesrev/zelda3
cd zelda3
```
4. Install requirements using pip
```sh
python3 -m pip install -r requirements.txt
```
5. Install SDL2
* Ubuntu/Debian `sudo apt install libsdl2-dev`
* Fedora Linux `sudo dnf install SDL2-devel`
* Arch Linux `sudo pacman -S sdl2`
* macOS: `brew install sdl2` (you can get homebrew [here](https://brew.sh/))

## Compiling on Linux/MacOS
1. Place your US ROM file named `zelda3.sfc` in `zelda3`
2. Compile
```sh
make
```
<details>
<summary>
Advanced make usage ...
</summary>

```sh
make -j$(nproc) # run on all core
make clean all  # clear gen+obj and rebuild
CC=clang make   # specify compiler
```
</details>

## Archipelago
The Archipelago client needs no extra libraries to build. Encrypted
connections (wss://) use the system's TLS library, loaded when the game
first needs it: Schannel on Windows, OpenSSL (libssl.so.3 or .so.1.1) on
Linux, Homebrew's or MacPorts' OpenSSL on macOS. cJSON (third_party/cJSON,
MIT) is compiled in through src/ap_json.c. To make `fsotp.apworld`, the
Archipelago world: `python3 archipelago/build_apworld.py`.

## More Compilation Help

Look at the wiki at https://github.com/snesrev/zelda3/wiki for more help.

The ROM needs to be named `zelda3.sfc` and has to be from the US region with this exact SHA256 hash
`66871d66be19ad2c34c927d6b14cd8eb6fc3181965b6e517cb361f7316009cfb`

In case you're planning to move the executable to a different location, please include the file `zelda3_assets.dat`.
## Choosing the mods
Which mods a build carries is chosen before it compiles: `src/mods_build.h` is made by
`python3 tools/mods.py write` from `mods/default.cfg` and your own `mods.cfg` (if you have one).
`make`, Visual Studio, `tools/build_windows_mingw.sh` and `run_with_tcc.bat` do this for you. For any
other build (a packager's script, `cc src/*.c` by hand), run it first; Python 3 is then needed at build
time. Without Python, copy `src/mods_default.h` to `src/mods_build.h` for the shipped set. The
developer's guide (`docs/books/devguide.md`) has the rest.
