# Supply-chain and build review: fourSwordsFromThePast @ 048adbb

Read-only. Clone: /home/claude/work/zelda/fsotp. Not checked: the GitHub repo settings (default workflow token permission, private vulnerability reporting, branch protection; the `gh api` calls were refused by the proxy), the UPnP/SSDP/XML parser in src/upnp.c, the UDP packet parsers in src/net.c and src/session.c, the TLS code beyond its documented policy, the vcxproj, the Archipelago world's Python beyond its packer, the tests/ scripts.

## Observed

CI (.github/workflows/build.yaml)
- Triggers: `push:` (all branches), `pull_request:`, `workflow_dispatch` (lines 3-6). No `pull_request_target`, no `workflow_run`. `grep -rn "secrets\.\|GITHUB_TOKEN\|token" .github` finds nothing. No `permissions:` block anywhere (grep "permissions" returns nothing), so the token gets whatever the repo default is.
- Actions: `actions/checkout@v4` (lines 23, 119, 153, 161), `actions/setup-python@v5` (24, 162), `actions/upload-artifact@v4` (109, 140). All by tag, none by SHA. All first-party (actions/*). No third-party actions. No `cache:` or actions/cache anywhere.
- checkout is the default (the PR merge ref on pull_request); no `ref: github.event.pull_request.head.sha`. `persist-credentials` is left at its default (true).
- Line 151: `container: devkitpro/devkita64`, a floating tag, no digest. The job runs `make switch` and uploads nothing.
- Line 42: `curl -sSL ... SDL2-2.32.10.tar.gz` from github.com/libsdl-org, then `tar xzf`, no hash check. The result is statically linked into the shipped Mac binary.
- tools/build_windows_mingw.sh:14: `curl -sSL` of SDL2-devel-2.28.2-mingw.tar.gz, no hash; line 27 copies its `SDL2.dll` into the Windows artifact players are told to download (README.md:640).
- Line 169: `pip install websockets==13.1` (version pinned, no hash). apt and brew installs are unpinned (lines 31, 35, 91, 123, 126, 168).
- No `curl | sh` anywhere in the repo (`grep -rn "| *sh\|| *bash" .github tools *.bat *.command Makefile` finds none).
- Artifacts: the tar of zelda3, zelda3.ini, README, MULTIPLAYER, AI_DISCLOSURE, requirements.txt, extract_assets.command, the YAML, `fsotp.apworld`, other/3x5_font.png, `git ls-files assets`, and help/ if built (lines 99-108); Windows: build-windows/dist/. No ROM, no zelda3_assets.dat, no ini from a user. `.gitignore` excludes /zelda3_assets.dat, *.sfc, *.smc, /token.txt.
- The CI job named "Package" runs without a ROM, so none of the ROM-needing tests run there. AI_DISCLOSURE.txt says this too ("they run on a computer that has the game, not on GitHub").

User-facing scripts
- run_with_tcc.bat:7: `curl -L -o third_party\tcc.zip https://github.com/FitzRoyX/tinycc/releases/download/tcc_20230519/tcc_20230519.zip`, `tar -xf`, then compiles with that tcc.exe and runs `zelda3.exe` (lines 67, 73). Line 23: SDL2-devel-2.28.2-VC.zip the same way, and its SDL2.dll is copied beside the exe (line 70). No hash on either. All https. The tcc comes from a personal fork's release, not from the tinycc project.
- get_python_libs.bat:3: `python -m pip install --upgrade pip pillow pyyaml`, unpinned, upgrades pip itself.
- extract_assets.bat:42 and extract_assets.command:174-180: `pip install --user pillow pyyaml` if `import PIL, yaml` fails; the .command then falls through `--break-system-packages` and a system-wide `pip install` (lines 176-178). requirements.txt is `Pillow` / `PyYAML`, unpinned. Both run with the user's privileges; neither asks for admin. No plain http URL anywhere in these scripts (all https).
- packages.config: nuget sdl2 2.0.5 (old) for the Visual Studio project; not used by CI.
- src/main.c:1287: after a dialog, `system("cmd /c extract_assets.bat")` or `system("/bin/sh ./extract_assets.command")`, resolved against the current directory.

Python tools
- No pickle, marshal, eval, exec, `shell=True`, os.system, urllib or requests anywhere outside third_party (grep over *.py *.sh *.awk). The only subprocess calls are tools/books.py:102 (`pandoc`, list args) and :144 (`chmcmd`, list args).
- No zip or tar extraction in any Python tool. archipelago/build_apworld.py only writes a zip (lines 54-62), with fixed arcnames from `os.walk` of its own tree.
- YAML is read with `yaml.safe_load` (assets/compile_resources.py:189, 458, 689, 698); `yaml.dump` only writes.
- assets/restool.py runs `os.chdir(dirname(__file__))` and loads the ROM first (util.py:61-87): the ROM's SHA1 is checked against a table and anything else is refused before any extraction. Output paths are fixed names (`overworld/overworld-%d.yaml` with an integer, assets/extract_resources.py:233), not taken from the ROM.
- other/replace_in_files.py globs *.c *.h in cwd and rewrites them; developer-only, takes a replacement file.

Crash and screenshots
- src/crash_handler.c is Windows-only (line 3; the other branch is an empty function, line 34). It appends to `zelda3_crash.txt` in the cwd: time, exception code, faulting address, and the full path from `GetModuleFileNameA` of the module (lines 15-22). No memory dump, no player name, room code, IP or password. Nothing is uploaded (no network code in the file). The message box asks the user to send the file (line 25).
- src/screenshot.c: writes `screenshots/fsotp-<timestamp>.png` in the cwd (lines 76-90, 106-108) with stbi_write_png, no metadata, no network.

Repo hygiene
- Secret greps over all history (`git log --all -p -G<pattern>`): no `BEGIN ... PRIVATE`, no `ghp_`, `sk-`, `AKIA`, `xox[bp]-`. Hits for `password=` and `token=` are the game's own netplay code, test strings (`secretpw`, `secret`) and mock-server tests, not credentials. `/token.txt` is in .gitignore (line 33).
- Deleted files in history (`git log --all --diff-filter=D --name-only`): CONTEXT-MODELS/data/matrix1-builds.tsv, src/platform/switch/{.gitignore,Makefile,icon.jpg,zelda3.ini} (later relanded), other/glitches_faq.txt, src/instance.c.orig, src/mptest.c.orig. None sensitive by name; contents of matrix1-builds.tsv and the .orig files not read.
- Authors across all branches: 169 commits Claude, 12 Corwin Brust (corwin@bru.st), 2 merge commits and 1 web delete by Bryant/Corwin via GitHub. AI_DISCLOSURE.txt says "Every commit ... was made by an AI assistant" and "each carries a Co-Authored-By line": 1 non-merge commit has no trailer (the web deletion 0831a3d), and the two merges (048adbb, 8711960) are Bryant's.
- LICENSE.txt: MIT, "Copyright (c) 2022 snesrev, (c) 2021 elzo_d", then the Opus BSD notice and the cJSON MIT notice. README.md:885 says MIT. No SPDX tags in source (the SPDX grep hits were the strings "Place" inside identifiers, not SPDX lines). No GPL text in the tree (grep hits were identifiers such as `LangPL`, `PlayerName`). stb_image(_write) and gl_core carry their own headers; AI_DISCLOSURE.txt:50-54 says stb licences are "in their headers", LICENSE.txt does not list them. The Archipelago apworld is a separate Python package inside an MIT repo; the code it imports from Archipelago (GPL-licensed upstream, not checked here) is not vendored.
- saves/ref/*.sav: 13 files, 278 KB to 562 KB (6.5 MB), committed in the import commit 715f591. .gitignore `/saves/*.sav` (line 22) does not match the subfolder. They are StateRecorder files (src/zelda_rtl.c:854-870, header words 1, 42998, ...) holding a full emulator state, which includes video and work memory the game built from the ROM. `strings -n 4` shows only noise; no player name or path found (a name would be the in-game "Link" default; not decoded). They load at runtime for the `load <n>` test command (docs/MODDING.md:822).
- Default branch: `origin/HEAD -> origin/claude/great-maxwell-00u7q3`. Other branches: `claude/great-curie-ln6uck`, `recipe`. A fresh `git clone` checks out the AI-session-named branch.
- No SECURITY.md, PRIVACY.md, CONTRIBUTING or CODE_OF_CONDUCT (`ls`). .github/ISSUE_TEMPLATE/config.yml has `blank_issues_enabled: false` and `contact_links: []`. The bug template (bugs.yml) asks for a description and a build target; it does not warn against pasting room codes, IPs, slot names or logs. The only contact route is the issue tracker and a Discord link (README.md:29).

Network disclosures
- Hosting: UPnP port map, and a STUN request to `stun.l.google.com:19302` by default (src/config.c:707, zelda3.ini:283). MULTIPLAYER.md:31-36 tells the user about UPnP, the room code ("just your public address and port written short ... no server is involved") and the STUN server by name of the ini key; it does not say that Google's server is the default or that the request is sent when hosting starts.
- The room code is the host's public IPv4 and port plus a 12-bit check (MULTIPLAYER.md:432). The menu says to copy and send it; nothing warns that it is the address itself.
- Archipelago: README.md:402-415 states the scheme policy. With no scheme given, the client tries ws:// first and then wss:// unless the host is archipelago.gg or wss has worked before (src/ap_client.c:1058-1081). The password is read from zelda3.ini `[Archipelago] Password` in plain text and sent in the Connect packet (src/config.c:555; README.md:423 says the menu never saves it).
- The game's netplay is UDP with a per-player token (src/session.c:89, 164); I did not check for encryption or authentication of datagrams (not checked).

Docs claims against the build
- BUILDING.md:9-10 tells the reader to `git clone https://github.com/snesrev/zelda3`, the upstream repo, not this one; it then says to run `pip install -r requirements.txt`, which exists in this repo only. BUILDING.md:52-53 gives a SHA256 for the ROM; README.md:635 and assets/util.py:15 use SHA1. The code checks only SHA1.
- README.md:651-653 says the Mac artifact has "nothing to install: SDL2 is built in". The workflow runs `otool -L zelda3` (line 65) but asserts nothing from it, so a regression to a dynamic link would pass CI.
- README.md:647 / AI_DISCLOSURE.txt: "Every change is built for Linux, macOS and Windows". Windows is only the MinGW cross build (build-windows job); run_with_tcc.bat, Zelda3.sln and the packages.config path are not built in CI.
- README.md:628-629: artifacts are for "log in to GitHub to download"; artifact retention is the repo default (not checked).

## Judged

### 1. HOLD: unverified binaries fetched and run or shipped
Claim: every fetched toolchain or runtime (tcc.exe, SDL2.dll, SDL2 source) is accepted on the strength of an https URL alone, and two of them reach players' machines.
Lines: run_with_tcc.bat:7,23,67,70; tools/build_windows_mingw.sh:14,27; build.yaml:42.
Failure: a compromised or retagged release asset on the fork (FitzRoyX/tinycc) or a swapped SDL2.dll in a CI run yields a trojaned compiler run by double-click, or a trojaned DLL inside the `zelda3-windows-<sha>` artifact that README.md:640 tells players to run. The tcc case is the worse one: a personal fork, no hash, runs immediately.
Falsifier: a `sha256sum -c` or `certutil -hashfile` comparison against a literal in the script exists before extraction; `grep -n "sha256\|certutil\|Get-FileHash" run_with_tcc.bat tools/build_windows_mingw.sh .github/workflows/build.yaml` currently finds nothing.
Fix shape: store the expected SHA-256 of each archive next to its URL and fail on mismatch (`certutil -hashfile` on Windows, `sha256sum -c` in sh); for tcc, either pin the hash or drop the script and point at the MinGW build.

### 2. HOLD: the committed reference saves are emulator state from the commercial game
Claim: saves/ref/*.sav (6.5 MB, 13 files) are full game-state captures; the README says no Nintendo data is in the repo, and .gitignore line 38 says "Never commit Nintendo-copyrighted data".
Lines: saves/ref/, src/zelda_rtl.c:854-870, .gitignore:22,38.
Failure: a rights holder or a mirror operator reads those files as containing game graphics and memory; the repo is public and the files have been there since the first import (715f591, inherited from upstream snesrev/zelda3 which ships them too).
Falsifier: decode one file with StateRecorder_Load and show that its video and work memory sections are empty or zero, or that the format stores only a delta from a fresh ROM boot. I did not do this.
Fix shape: have the maintainers decide: keep with a note in README/AI_DISCLOSURE that they come from upstream, or generate them on the player's machine from the ROM and ignore `/saves/ref/`.

### 3. WARN: no workflow `permissions:` and floating pins
Claim: the workflow depends on repo defaults for the token and on moving tags for three actions and the devkitPro image.
Lines: build.yaml (top-level, no `permissions`), 23-24, 109, 119, 140, 151-153, 161-162.
Failure: if the repo default is read/write, a push-event job (which runs `make` and a Python script from the pushed tree, with a persisted token in .git/config) can write to the repo; a retagged action or devkitpro/devkita64 image runs arbitrary code in the job. fork PRs are safe from this (read-only token, no secrets), so the exposure is branches inside the repo.
Falsifier: the repo setting "Workflow permissions" is already "Read repository contents" and pins are not wanted; if so only the pins remain. Not checked.
Fix shape: add `permissions: contents: read` at the top, `persist-credentials: false` on checkout, pin actions and the container by SHA/digest with a comment of the tag.

### 4. WARN: Windows crash file records the user's folder path
Claim: zelda3_crash.txt contains the faulting module's full path, usually `C:\Users\<name>\...\zelda3.exe`, and the dialog asks the player to send it.
Lines: src/crash_handler.c:14-22,25.
Failure: a player posts the file on the public issue tracker and publishes their Windows account name. No other personal data is in it (no memory, names or addresses).
Falsifier: `GetModuleFileNameA` on the main module returns a relative or trimmed name in a test run; a crash test on Windows would show this. Not run here.
Fix shape: write only the basename plus offset, or tell the player in the message box that the file contains a folder path.

### 5. WARN: the network disclosures a player needs are not where the player looks
Claim: the user is not told in the game or the README that hosting contacts Google's STUN server by default, that the room code is the host's raw IPv4 address, and that Archipelago passwords may go over ws:// in the clear when no scheme is typed.
Lines: src/config.c:707; MULTIPLAYER.md:31-36; README.md:402-415; src/ap_client.c:1058-1081.
Failure: a streamer shows the room code believing it is an opaque token and exposes the home IP; a player on a shared network types a bare hostname with a room password and the first attempt is plaintext.
Falsifier: the in-game Multiplayer screen already shows a line saying so (the menu text lives in src/ui.c; not read). 
Fix shape: one PRIVACY section in MULTIPLAYER.md ("what leaves your computer": UPnP to your router, STUN to Google by default and how to turn it off, room code is your address, AP password and scheme), and a line in the menu next to the room code.

### 6. WARN: no vulnerability-report route, and the issue form invites pasting addresses
Claim: no SECURITY.md; blank issues are disabled and the only contact is the public tracker or Discord, and the bug form does not warn against posting room codes, IPs or the crash file's path.
Lines: .github/ISSUE_TEMPLATE/config.yml, bugs.yml.
Failure: a reporter of a netplay flaw has nowhere private to send it and posts it publicly.
Falsifier: private vulnerability reporting is enabled in the repo's Security tab (not checked).
Fix shape: a short SECURITY.md naming a private route, and a sentence in bugs.yml about redacting addresses and codes.

### 7. WARN: unpinned Python installs run at the player's privilege
Claim: `pip install pillow pyyaml` (and `--upgrade pip` in get_python_libs.bat) fetches whatever is current, and the .command falls back to a system-wide install.
Lines: get_python_libs.bat:3; extract_assets.bat:42; extract_assets.command:174-180; requirements.txt.
Failure: a malicious release of either package, or a dependency-confusion name, runs in the user's account the moment they double-click; the system-wide fallback widens it. This is the ordinary pip risk, no worse than the standard route.
Falsifier: a `--require-hashes` file exists; none does.
Fix shape: pin versions with hashes in requirements.txt and use `pip install --user --require-hashes -r requirements.txt`; drop the system-wide fallback or ask first.

### 8. NOTE: default branch is `claude/great-maxwell-00u7q3`
Claim: a stranger's `git clone` lands on an auto-named AI-session branch, not `main`; the `recipe` branch has been merged into it via PRs #1 and #2.
Failure: no security effect; it reads as unreviewed work, and `git pull` follows a branch whose name may be deleted or rotated, breaking clones and links that name it.
Falsifier: the maintainers have set it deliberately; AI_DISCLOSURE.txt does not mention it.
Fix shape: rename to `main` in repo settings (GitHub redirects the old name in the UI).

### 9. NOTE: AI_DISCLOSURE.txt is slightly overbroad
Claim: "Every commit ... was made by an AI assistant, and each carries a Co-Authored-By line". Observed: two merge commits by Bryant and one web-UI deletion by Corwin have no such trailer (git log above).
Falsifier: the sentence is meant as "every content commit"; the file then needs to say so.
Fix shape: say "every commit except the merge commits and one deletion made on GitHub".

### 10. NOTE: BUILDING.md is stale
Claim: it clones upstream snesrev/zelda3 and gives a SHA256 where the build checks SHA1. Lines: BUILDING.md:9-10, 52-53. Failure: a builder gets the upstream tree, which lacks the mods, then wonders why the guide does not match. Falsifier: BUILDING.md is intentionally the upstream text with "additions" (AI_DISCLOSURE says so). Fix shape: clone this repo's URL and give the SHA1 or both hashes.

### 11. NOTE: CI does not guard the claims it relies on
Claim: `otool -L` output on the Mac build is printed, not checked; the TCC and Visual Studio paths are never built. Lines: build.yaml:65. Failure: README's "nothing to install: SDL2 is built in" breaks silently. Falsifier: add `otool -L zelda3 | grep -q SDL2 && exit 1` and it still passes. Fix shape: add that assert; add a TCC build to the Windows job if the .bat is kept.

### 12. NOTE: licence file does not list the stb libraries
Claim: AI_DISCLOSURE.txt:50-54 and LICENSE.txt: stb is covered "in its headers" only, no SPDX tags in sources. Failure: a redistributor copying LICENSE.txt alone misses a notice (stb is public domain or MIT, so the cost is low). Falsifier: stb headers' licence text is intact in the tree (they are present). Fix shape: add an SPDX-License-Identifier line to LICENSE.txt's list and name stb and gl_core.

## What holds

- fork PRs cannot reach secrets or a write token: `pull_request` only, no `pull_request_target`, no secrets in the workflow, no PR-head checkout override.
- No third-party actions, no cache, no `curl | sh`, no self-hosted runners, no plain http in any shipped script.
- Artifacts carry no ROM, no assets dat, no ini from a user, and the .gitignore covers ROMs, saves (top level), zelda3_assets.dat, token.txt, build output.
- The Python tools have no pickle/eval/exec/shell=True, no archive extraction (so no zip slip), use `yaml.safe_load`, write to fixed names, and the ROM is hash-checked before any parse.
- The crash handler is local, Windows-only, small, and uploads nothing; screenshots write PNGs locally with no metadata.
- History has no keys, tokens or passwords in any pattern searched; the deleted files are build leftovers.
- Licensing is plain MIT with Opus and cJSON notices included; no GPL text in the tree; AI_DISCLOSURE.txt is specific about what the AI wrote and what came from upstream.
- The Archipelago client refuses to fall back from wss to ws and says why (README.md:405-407).
