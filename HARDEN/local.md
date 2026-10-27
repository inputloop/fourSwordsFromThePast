# Local trust boundaries and privacy surface: fsotp HEAD 048adbb

Wire protocols are another reviewer's. Harnesses: /home/claude/work/zelda/scratch/ (bps_h.c, rwf_h.c, health_h.c, fmt.py, fmt3.py). No repo file was edited.

## Observed

Config (src/config.c, src/util.c)
- No fixed buffers in the parser. ReadWholeFile slurps the file (util.c:41-57); lines are split in place (util.c:59-78); every string key (PlayerName config.c:599, LastJoinAddress :602, StunServer :605, Archipelago Server/Slot/Password :550-556, LinkGraphics :456, Shader :459, MSUPath :494, Language :543) stores a pointer into the file buffer. No length limit at parse time.
- The fixed-buffer copies happen later, all snprintf-bounded: player name into char[24] (ui.c:1456, session.c:487), join addr char[64] (ui.c:1455), AP server/slot/password into g_ap_settings (main.c:633-635; password is char[64], archipelago.h:146). Password is truncated silently at 63 bytes.
- Ints: atoi/strtol, clamped or cast to uint8/uint16 (config.c:366-368, 481); WindowSize is not range checked.
- Read order: zelda3.user.ini, else zelda3.ini, then zelda3.coop.ini, all relative to the working directory (config.c:728-735). `!include` follows relative paths, depth cap 10 but no fan-out cap (config.c:683).
- Written back: only zelda3.coop.ini (Ui_SaveSettings, ui.c:2813-2903), in the working directory. It contains PlayerName (:2891), LastJoinAddress (:2892, a friend's room code or IP), AP Server and Slot (:2897-2900). Password is never written (comment at ui.c:2893; README.md:423).
- Typed text is filtered (AP fields printable ASCII ui.c:1810-1822, name letter set ui.c:751, join address `[0-9A-Za-z.:-]` ui.c:1832-1835), so the UI cannot inject newlines into the ini.
- `#` ends a value on reload (util.c:68), so a slot or password containing `#` is cut short.
- `git check-ignore` (my run in /tmp/gi): ignored: zelda3.coop.ini, zelda3.user.ini, saves/sram.dat, saves/sram.bak, saves/*.sav, screenshots/. NOT ignored: saves/sram.dat.bak, zelda3_crash.txt, spoiler_*.txt, out.pcm. zelda3.ini is tracked and its comment invites `Password =` there (zelda3.ini:296-300).

Saves and snapshots
- SaveLoadSlot builds names into char[128] from a table or `%d` (zelda_rtl.c:857-863). Keep-a-copy uses the fixed path "saves/sram.dat" (ui.c:571).
- StateRecorder_Load (zelda_rtl.c:536-591): version is `assert(hdr[0] == 1)`; lengths hdr[2], hdr[6] go to ByteArray_Resize then fread, which Die()s on short read. loadFunc's bound is `assert(st->pend - st->p >= data_size)` (zelda_rtl.c:384). The Makefile has no -DNDEBUG; zelda3.vcxproj sets NDEBUG for Release (lines 148, 169, 210, 225). CI has no sanitizer run (.github/workflows/build.yaml).
- Replay log bytes are read with no bound in loops that continue on 255 (zelda_rtl.c:664-700); replay_pos starts at `hdr[5] >> 1` from the file (:579).
- DSP state is dumped raw from `ram` on (dsp.c:131-133); the apu_ram pointer sits before `ram`, so no pointers come from the file (dsp.h). But `firBufferIndex` is saved/loaded and used unmasked at dsp.c:175-182, so a loaded value of 8..255 writes inside the Dsp struct (sampleBuffer follows). Confined to the struct.
- SRAM: Intro_ValidateSram checks an additive 16-bit checksum and falls back to the backup copy (select_file.c:179-192). Forgeable.
- select_file.c:93-120 (SelectFile_Func17): `int health = sram[0x36c] >> 3; do { ... } while (--health);` For health 0 the loop does not end. health_h.c: 1,000,000 iterations, dst 1,000,011 uint16 past a 64-entry array.
- Session_KeepCopy copies 0x500 bytes of the host's live RAM at 0xF000 into one of our files and recomputes the checksum (session.c:2068-2076). Slot offsets stay in range (max 0x1e00 of 0x2000).
- saves/ref/*.sav: 13 files, 278-561 KB. My header parse: well-formed, one used slot with the default name bytes, no paths or e-mail in strings. They hold WRAM, VRAM and SPC RAM of the running game.

Assets and ROM
- zelda3_assets.dat: signature, count and per-asset size checked against file length, offsets chained, 4-byte aligned (main.c:1314-1329). Table contents are not validated (the kXxx accessors in assets.h index them freely). FindIndexInMemblk does bound its own offsets (util.c:175-195). ZeldaSetLanguage reads found.ptr[0..2] without a size check (zelda_rtl.c:834-836).
- The .bps path (main.c:1299-1307) calls ApplyBps (util.c:223-280). It checks CRCs of source and patch up front and of the output at the end, but the decode loop never bounds outputOffset against dst_size (util.c:252-272). bps_h.c, with CRC-valid patch: ASAN "heap-buffer-overflow WRITE of size 1 at util.c:259", 8-byte dst, 4096-byte TargetRead. `bps_size - 12` and the memcmp are unchecked for tiny patches (util.c:225-227).
- The C binary does not identify the ROM. SHA-1 is checked only in the Python extractor (assets/util.py:15, 70-87). A ROM argument to the binary (main.c:600-601) runs PatchRom before any size check (zelda_cpu_infra.c:578-579).
- If assets are missing and extract_assets.command/.bat exists in the working directory, a dialog then `system("/bin/sh ./extract_assets.command")` (main.c:1260-1288). The script runs `pip install --user pillow pyyaml`, falling back to `--break-system-packages` (extract_assets.command:40-42), unpinned. The dialog text mentions Python only (main.c:1275-1281). No ini value reaches system(), popen, ShellExecute (only helpbook_os.c:62 and :72, with fixed or table-built strings; Helpbook_Topic table not checked).
- ReadWholeFile on a non-seekable file (FIFO): rwf_h.c, ASAN "heap-buffer-overflow WRITE util.c:51" (ftell -1 gives malloc(0) then buffer[SIZE_MAX]).

Screenshots, crash
- screenshot.c: PNG via stb_image_write, no text chunks, name `screenshots/fsotp-<local time>.png` (screenshot.c:85), folder from a fixed default. F12 is the frame after the overlay (screenshot.h:5-8, main.c:270-296), so a menu on screen is captured: host IP and room code (ui.c:483-501, screenshots/menu-multiplayer.png), AP server and slot (ui.c:861-863; password shows `****`).
- crash_handler.c (Windows): appends to zelda3_crash.txt with time and exception address plus `GetModuleFileNameA` of the faulting module, i.e. the full path of the exe (crash_handler.c:14-22), then a box says "please send that file" (:25). docs/MODDING.md:979 repeats it.

UI strings
- My scripts (fmt.py, fmt3.py) listed every printf-family call whose format is not a literal, then every caller of the variadic wrappers (Dbg, AddText, SetStatus, BufPrintf, SetErr in ap_ws.c and ap_tls.c, Notice, StrFmt). All callers pass literals or ternaries of literals. No format-string bug found. Die(const char*) never takes a format.
- Peer and AP text: names from the wire are NUL-forced (session.c:811, 1509, 1532) and drawn by DrawText, which skips bytes >= 128 and has a 128-entry font (ui.c:146-150). AP text goes through Sanitize (ap_client.c:185-203).
- Remote names are not filtered before stderr or toasts (session.c:698, 768, 841, 880).

Platform
- Shaders: preset pass index bounded (glsl_shader.c:13-25), `#include` has no depth limit (glsl_shader.c:185-208), PNG textures capped at 4096 (glsl_shader.c:8-11).
- ap_tls.c: OpenSSL by soname (ap_tls.c:701); Windows loads from System32 with a plain LoadLibraryA fallback (ap_tls.c:231-234); ZELDA3_AP_CAFILE adds a CA (:753).
- bindings.c parsing clamps and tokenizes (bindings.c:390-440); volume_control.c is COM only. No issue seen.
- UPnP (upnp.c): SSDP reply LOCATION is accepted from any sender (upnp.c:100-120, Net_Recv with NULL source), the game then does an HTTP GET to that host:port (upnp.c:131-135, FetchControlUrl) and later POSTs SOAP to a controlURL that may be an absolute http:// URL on any host (upnp.c:154-155, 168-177). STUN replies are also taken from any source with a 96-bit transaction id from a weak PRNG (upnp.c:238-260).
- Join snapshot: HostStartSnapshot (session.c:660-695) calls Inst_SaveState, which includes the whole 8 KB SRAM (zelda_rtl.c:398, 439-442; instance.c:1098).

## Judged (most severe first; no BLOCK found)

**HOLD 1. A host can plant a file that crashes the client at every start.** Keep-a-copy (session.c:2068-2076) writes host-controlled RAM into our save with a good checksum; Func17 (select_file.c:105-120) then loops without end for a max-health byte below 8 and writes 0x520 over memory past vram_upload_data. Scenario: a modified host sets 0xF36C to 0, a guest presses Keep a copy, the guest's game dies on the file-select screen until they restore sram.dat.bak. FALSIFIER: put a slot with byte 0x36c = 0 and a valid checksum in saves/sram.dat and start the game; if the file select draws normally, this is wrong (I only ran the loop in isolation). Fix shape: `while (--health > 0)` with a floor of 1, and clamp the written count to the 10-cell row.

**HOLD 2. ApplyBps writes past its buffer.** util.c:252-272, proven with ASAN (bps_h.c). Scenario: a zelda3_assets.bps from a "mod pack" next to the game, with the public USA ROM CRC; CRCs are not authentication. FALSIFIER: a patch whose decode stays inside dst_size runs clean (it does), so the bug needs a lying patch, which bps_h.c shows is accepted. Fix shape: check outputOffset + length <= dst_size, and the three source offsets against src_size, before every copy; reject bps_size < 20.

**HOLD 3. The crash file leaks the Windows account name and the user is told to send it.** crash_handler.c:14-22, :25. Scenario: exe in C:\Users\<real name>\Downloads, crash, file pasted into a public issue. FALSIFIER: run the Windows build from a folder with a distinctive name and trigger a crash (not run here: Linux box); if the file shows only a relative or base name, wrong. Fix shape: write only the module base name and the offset, or strip the path to its last component.

**WARN 4. Every joiner gets all three of the host's save slots and backups.** session.c:660-695 via zelda_rtl.c:398. MULTIPLAYER.md says only that joining "downloads the host's game". Scenario: a stranger with the room code reads the host's other two files. FALSIFIER: decompress a snapshot (session.c RleCompress) and look for slot 2's name bytes; absent means scrubbed elsewhere. Fix shape: zero the two inactive slots and their backups in the outgoing copy, or say so in the doc.

**WARN 5. Save, replay and host-snapshot parsers rely on assert only.** zelda_rtl.c:384, 536-591, 664-700. In MSVC Release (NDEBUG) the version check and the loadFunc bound vanish: a short snapshot or .sav gives a heap over-read in memcpy; a replay log ending in 0xFF bytes reads past the log. The same loadFunc parses a host's snapshot (instance.c:1133). Reads only; writes stay masked into g_ram. FALSIFIER: truncate a saves/ref file and F-key load it in an NDEBUG build under ASAN; clean means I am wrong. Fix shape: replace the assert with a real `if` that returns failure, bounds replay_pos against log.size at each byte read, and cap hdr[2]/hdr[6] at the largest sane size.

**WARN 6. UPnP and STUN take answers from anyone.** upnp.c:100-135, 154-177, 238-260. Scenario: a LAN peer answers SSDP with LOCATION http://127.0.0.1:PORT/path and the host's machine sends a GET and POST there; a spoofed STUN reply puts a wrong IP into the room code. FALSIFIER: a UDP listener sending that LOCATION while the game hosts; no GET at it means wrong. Fix shape: accept LOCATION and controlURL only when the host equals the SSDP sender and is a private address; check the STUN reply source.

**WARN 7. The first-run extractor is run from the working directory with a dialog that does not mention a network download.** main.c:1260-1288; extract_assets.command:40-42. Scenario: a hostile extract_assets.command sits beside a downloaded zelda3.ini and the user clicks "Extract now". Also unpinned pip with `--break-system-packages` fallback. FALSIFIER: the dialog text, the script path used. Fix shape: add "downloads Pillow and PyYAML from the internet" to the dialog; pin versions and hashes in requirements.txt.

**WARN 8. .gitignore misses files the game writes.** saves/sram.dat.bak (session.c:2078, from a Keep a copy, holds a save), zelda3_crash.txt (HOLD 3 content), spoiler_*.txt (ui.c:1250-1253). FALSIFIER: git check-ignore on those names (run; not ignored). Fix shape: add the three patterns.

**WARN 9. ReadWholeFile and include recursion can be turned on the user by an ini or shader.** util.c:46-51 (FIFO, /dev/stdin: one-byte write before the buffer); config.c:683 (self-include fans out 10^10); glsl_shader.c:185-208 (self-include recurses until the stack ends). Self-inflicted. FALSIFIER: rwf_h.c shows the first; a shader with `#include "self.glsl"` would show the third (not run). Fix shape: reject size_t-1 from ftell, count includes, depth-limit shader includes.

**WARN 10. Peer names reach stderr and toasts unfiltered.** session.c:698-880. FALSIFIER: join with an ESC in the name and watch the host terminal. Fix shape: filter names at receive time like AP text.

**NOTE 11.** DSP firBufferIndex (dsp.c:175): mask with &7. A ROM argument is patched before any size check (main.c:600, dev-only). Asset tables are trusted once the length check passes, so a swapped zelda3_assets.dat can read out of bounds (zelda_rtl.c:834). coop.ini is rewritten non-atomically (ui.c:2816) and `#` cuts a slot name or password short. The ref saves are ROM-derived memory images in a repo whose .gitignore says never to commit Nintendo data; I believe upstream ships them too (not checked).

## Privacy surface

| Datum | Goes to | When | Disclosed where |
|---|---|---|---|
| Public IP and port (as room code) | Whoever you send the code to; the code is the IP, reversible (session.c:1971-2019; example FW00-00DS-EB52 decodes to 127.0.0.1:47474) | Hosting | MULTIPLAYER.md:31-34 yes; README.md:13 says "room codes instead of IP addresses" which reads as hiding it |
| Public IP, request from your port | STUN server, default stun.l.google.com:19302 | Hosting when UPnP fails | zelda3.ini:280-283, MULTIPLAYER.md:34-36 (Google not named there; default-on not said) |
| LAN IP, "Zelda3 co-op" mapping, port | Router (UPnP SOAP); SSDP multicast on LAN | Hosting, UseUpnp=1 default | zelda3.ini:266, MULTIPLAYER.md:30-31 |
| Host's IP | Every client (they connect to it); each client's IP to the host (kick list, session.c:1963-1966) | Online play | Implicit only |
| Player name (24 bytes), token, inputs | Host, via host to clients (roster names 16 bytes, session.c:1005) | Online play | MULTIPLAYER.md:49-50 partly |
| Host's whole SRAM, all 3 files and backups, RAM, VRAM | Each joining client | Join | NOT (only "downloads the host's game") |
| Host name, player count, port | Any sender of a Discover packet, LAN or internet | While hosting | NOT |
| Slot name, password (if set), game, uuid, tags | Archipelago server; in cleartext when ws:// is used (ap_client.c:533-547, OpenConnection :1064-1100); uuid is per run, random | Connect | README.md:401-413 says ws:// and wss:// exist; NOT that the password and slot travel in cleartext over ws://. Status line shows "(WS)" |
| Item, location, death messages with slot name as source | Archipelago room | Play | README Archipelago section |
| pip downloads of Pillow, PyYAML; compiler and SDL zips (no hashes) | PyPI, github.com | extract script, build scripts | README.md:644, 670; the in-game dialog does not say |
| Nothing | GitHub, telemetry, update checks | Checked: no http client or URL in src/ except UPnP's local HTTP | n/a |

Files written outside the game folder: none found. Working-directory files: saves/sram.dat, sram.bak, sram.dat.bak, save*.sav (F-keys), zelda3.coop.ini (name, last join code, AP server and slot), screenshots/*.png (overlay, IP, room code, AP server), spoiler_XXXXXXXX.txt, zelda3_crash.txt (exe path), `--mptest` outputs. README.md:784 and :745 disclose coop.ini and screenshots in general; NOT that coop.ini keeps the last friend's address or that F12 captures the overlay.

## What holds

- No format-string vulnerability: every variadic wrapper is called with literal formats; Die takes a plain string.
- No ini value reaches a shell, exec or ShellExecute; helpbook paths are built from fixed text.
- The AP password is never written by the game (ui.c:2893), and the no-fallback-from-wss rule is in the code (ap_client.c:1056-1062).
- UI typing filters make the ini written back injection-free; remote and AP text is NUL-forced and drawn through a bounded 128-glyph font.
- Asset file offsets are checked against the file length; `FindIndexInMemblk` bounds itself.
- State files carry no pointers (apu_ram sits before the saved region); the ref saves carry the default name and no host paths.
- Screenshots have no metadata; keep-a-copy path is fixed; MSU resume fields are bounded by file reads (audio.c:195-375).

Not checked: ppu/apu/spc state fields beyond the DSP ones; opengl.c internals; stb versions; the Helpbook_Topic table; Mp_SaveLoad field uses (raw g_mp, instance.c:1075); a real Windows crash; end-to-end run of HOLD 1; Switch port; tests/ and mods/.
