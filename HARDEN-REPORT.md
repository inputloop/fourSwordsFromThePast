# HARDEN report: HARDEN.md: fourSwordsFromThePast

stamp: 20261019-203057
ledger-hash: c8f060530e0827b1
source: HARDEN.md (the ledger block)
replaces: HARDEN/archive/HARDEN-REPORT-20261019-202842.md

## Top sheet

As of 2026-10-19 UTC: **1 open BLOCK**, **5 open HOLD**, 2 open opportunities, 10 fixed.
An open BLOCK breaks a contract the project states today; an open HOLD needs a reader before it is a risk or not.

### Actions

- **BLOCK H-001** (netplay): A joining client loads the host's snapshot as raw memory: g_mp.leader, resident and sprite_owner index g_mp.p[4] with no bound, so a hostile host writes 512 chosen bytes about 100 KB past the array  
  where: `src/mp.c:444;src/mp.c:179;src/instance.c:1110`; writers: pmo:323;epmo:B7
- **HOLD H-003** (netplay): One unauthenticated 34-byte Hello from a spoofed source makes the host stream about 0.5 MB (snapshot plus a Frames packet per tick for 6 s) to that address; three such Hellos per 6 s also hold the game full  
  where: `src/session.c:660;src/session.c:801`; writers: pmo:324
- **HOLD H-004** (netplay): Nothing after Hello is authenticated: packets match a client by ip:port; the 32-bit reconnect token travels in clear; a parked seat is taken by any sender with the same name (reproduced: ./h1 steal)  
  where: `src/session.c:722;src/session.c:744`; writers: pmo:324
- **HOLD H-010** (archipelago): A bare host:port on any server but archipelago.gg tries ws:// before wss://, so the slot password and all traffic go in clear with no downgrade needed (reproduced against a plaintext fake server)  
  where: `src/ap_client.c:1058`; writers: pmo:328
- **HOLD H-012** (supply): tcc.exe (a personal fork's release), SDL2.dll and the SDL2 source are fetched over https with no hash and run or shipped: the DLL goes into the Windows artifact players are told to download  
  where: `run_with_tcc.bat:7;tools/build_windows_mingw.sh:14;.github/workflows/build.yaml:46`; writers: pmo:330;epmo:B6
- **HOLD H-014** (supply): saves/ref/*.sav (13 files, 6.5 MB) are full emulator state built from the commercial ROM, inherited from upstream snesrev/zelda3, in a repo whose .gitignore says never to commit Nintendo data  
  where: `saves/ref/Chapter 1 - Zelda's Rescue.sav:1;.gitignore:38`; writers: pmo:331
- **OPP H-026** (netplay): The host already knows the trust model (unsigned UDP, address as credential); a per-game random key shown beside the room code would give players a secret to share without a server, and would carry H-003, H-004 and H-019 at once  
  where: `src/session.c:1986`; writers: pmo:324
- **OPP H-027** (supply): The reviewers' harnesses now live in HARDEN/harness (h1.c, bps_h.c, amp.c, fuzzmsg.c, clientdrv.c and the rest) with their build lines; wiring them into CI would let it go red on H-001, H-002, H-005 and H-011 for good  
  where: `HARDEN/harness/README.md:1`; writers: -

### Since the archived report

- no change in ids, severities or states since the archived report

## Counts

| sev | OPEN | FIXED | ACCEPTED | RETIRED | all |
|---|---|---|---|---|---|
| BLOCK | 1 | 1 | 0 | 0 | 2 |
| HOLD | 5 | 3 | 0 | 0 | 8 |
| WARN | 7 | 5 | 0 | 0 | 12 |
| NOTE | 2 | 1 | 0 | 0 | 3 |
| OPP | 2 | 0 | 0 | 0 | 2 |
| all | | | | | 27 |

## Every finding, one line

- H-001 BLOCK OPEN [netplay]: A joining client loads the host's snapshot as raw memory: g_mp.leader, resident and sprite_owner index g_mp.p[4] with no bound, so a hostile host writes 512 chosen bytes about 100 KB past the array (`src/mp.c:444;src/mp.c:179;src/instance.c:1110`)
- H-002 BLOCK FIXED [netplay]: SnapData bound check wrapped (off near 2^32 plus n passed, memcpy 4 GB past the buffer) and SnapInfo size 0 divided by zero; both reproduced under ASan (`src/session.c:1595;src/session.c:1566`)
- H-003 HOLD OPEN [netplay]: One unauthenticated 34-byte Hello from a spoofed source makes the host stream about 0.5 MB (snapshot plus a Frames packet per tick for 6 s) to that address; three such Hellos per 6 s also hold the game full (`src/session.c:660;src/session.c:801`)
- H-004 HOLD OPEN [netplay]: Nothing after Hello is authenticated: packets match a client by ip:port; the 32-bit reconnect token travels in clear; a parked seat is taken by any sender with the same name (reproduced: ./h1 steal) (`src/session.c:722;src/session.c:744`)
- H-005 HOLD FIXED [local]: ApplyBps trusted the patch past its CRCs: outputOffset was never bounded against dst_size (ASan heap-buffer-overflow WRITE at util.c:259 with a CRC-valid lying patch) (`src/util.c:256`)
- H-006 WARN OPEN [netplay]: A joined client can make the host rebuild and resend a snapshot per packet (rejoin=1 Hello, resync Input), pinning CPU and uplink; no rate limit on any packet type (`src/session.c:660`)
- H-007 WARN OPEN [netplay]: UPnP/SSDP takes LOCATION, control URL and external IP from whichever device answers first, on any host; a rejected lease retries as permanent (lease 0); STUN replies are taken from any source (`src/upnp.c:102;src/upnp.c:206`)
- H-008 HOLD FIXED [local]: A host-planted save (Keep a copy writes the host's RAM with a good checksum) with a max-health byte under 8 made the file-select draw loop never end, writing past vram_upload_data (`src/select_file.c:108`)
- H-009 WARN OPEN [local]: Snapshot, save and replay parsers rely on assert (loadFunc bound, StateRecorder version); the vcxproj Release build defines NDEBUG and loses them; RLE output is uncapped (about 270 MB from a 4 MB snapshot) (`src/zelda_rtl.c:384;src/session.c:206`)
- H-010 HOLD OPEN [archipelago]: A bare host:port on any server but archipelago.gg tries ws:// before wss://, so the slot password and all traffic go in clear with no downgrade needed (reproduced against a plaintext fake server) (`src/ap_client.c:1058`)
- H-011 WARN OPEN [archipelago]: No cap on ReceivedItems, players or slot_info from the server: one 16 MiB message (about 20 KB compressed) cost 700 MB RSS, a second 956 MB (`src/ap_client.c:879;src/ap_ws.h:28`)
- H-012 HOLD OPEN [supply]: tcc.exe (a personal fork's release), SDL2.dll and the SDL2 source are fetched over https with no hash and run or shipped: the DLL goes into the Windows artifact players are told to download (`run_with_tcc.bat:7;tools/build_windows_mingw.sh:14;.github/workflows/build.yaml:46`)
- H-013 WARN OPEN [supply]: pip installs are unpinned (pip itself upgraded in one script) and run at the player's privilege, with a system-wide fallback (`get_python_libs.bat:3;extract_assets.command:40`)
- H-014 HOLD OPEN [supply]: saves/ref/*.sav (13 files, 6.5 MB) are full emulator state built from the commercial ROM, inherited from upstream snesrev/zelda3, in a repo whose .gitignore says never to commit Nintendo data (`saves/ref/Chapter 1 - Zelda's Rescue.sav:1;.gitignore:38`)
- H-015 WARN OPEN [privacy]: Every joiner receives the host's whole SRAM, all three save slots and backups, in the join snapshot; the doc said only that joining downloads the host's game (`src/session.c:660;src/zelda_rtl.c:398`)
- H-016 WARN FIXED [docs]: No SECURITY.md or private report route; blank issues disabled and the bug form invited pasting addresses (`SECURITY.md:1`)
- H-017 HOLD FIXED [privacy]: The Windows crash file recorded the faulting module's full path (the player's account folder) and the dialog asked the player to send it (`src/crash_handler.c:25`)
- H-018 WARN FIXED [docs]: What leaves the machine was not in one place: STUN to Google by default, the room code is the raw address, netplay unencrypted, ws:// first for Archipelago, coop.ini keeps the last friend's address, F12 captures the overlay (`MULTIPLAYER.md:457`)
- H-019 WARN FIXED [docs]: "a removed player can't come back on their own" was false: kicks match token or (address, name), so a new name from the same machine is let in (reproduced: ./h1 kick) (`MULTIPLAYER.md:44;src/session.c:744`)
- H-020 NOTE OPEN [repo]: The default branch is claude/great-maxwell-00u7q3, so a stranger's clone lands on an AI-session-named branch; README line 13 "room codes instead of IP addresses" reads as hiding the address; BUILDING.md clones upstream and gives SHA256 where the code checks SHA1; AI_DISCLOSURE says every commit has a trailer (two merges and one web deletion do not); CI prints otool -L and asserts nothing (`README.md:13;BUILDING.md:9;AI_DISCLOSURE.txt:13;.github/workflows/build.yaml:69`)
- H-021 WARN OPEN [local]: After a joined game, Reset restores the player's SRAM only when saves/sram.dat opens at 8192 bytes; otherwise the host's SRAM stays in memory and the foreign flag clears, so the next save writes the host's slots to the player's file (read, not run) (`src/zelda_rtl.c:933`)
- H-022 NOTE OPEN [local]: Small items: ReadWholeFile on a FIFO writes one byte before its buffer; ini !include fan-out and shader #include depth unbounded; remote names reach stderr and toasts unfiltered; DSP firBufferIndex unmasked; AP password silently cut at 63 bytes; SSL_set1_host return unchecked; out-of-range double to int casts; build_apworld.py follows symlinks; a test script binds 0.0.0.0; ZELDA3_AP_DEBUG prints the password (`src/util.c:46;src/config.c:683;src/ap_client.c:800`)
- H-023 WARN FIXED [repo]: .gitignore missed files the game writes: saves/sram.dat.bak (a save), zelda3_crash.txt, spoiler_*.txt (`.gitignore:34`)
- H-024 WARN FIXED [supply]: The workflow had no permissions block, so the token took the repository default; actions and the devkitPro image are pinned by tag only (still open) (`.github/workflows/build.yaml:8`)
- H-025 NOTE FIXED [repo]: get_python_libs.bat was stored with CRLF while .gitattributes says *.bat text eol=crlf, so every fresh clone showed it modified (`get_python_libs.bat:1`)
- H-026 OPP OPEN [netplay]: The host already knows the trust model (unsigned UDP, address as credential); a per-game random key shown beside the room code would give players a secret to share without a server, and would carry H-003, H-004 and H-019 at once (`src/session.c:1986`)
- H-027 OPP OPEN [supply]: The reviewers' harnesses now live in HARDEN/harness (h1.c, bps_h.c, amp.c, fuzzmsg.c, clientdrv.c and the rest) with their build lines; wiring them into CI would let it go red on H-001, H-002, H-005 and H-011 for good (`HARDEN/harness/README.md:1`)

## How to read this, and how to check it

- Three layers: this top sheet (judged, for a decision), the ledger below and in HARDEN.md (judged, one row a finding),
  and the sources each row names in its `back` column (observed: reports, quotes, commands and their output).
- `hd1rp gauge TREE` re-reads the ledger and says FAULT or STALE; `t/hd1rp-spec.pl TREE` re-derives these counts in another language.
- `fwd` names the second writers: `pmo:ID` (one target), `epmo:ID` (two or more projects), `commit:SHA`, `pr:N`.

## The ledger this report was drawn from

```harden
id	sev	state	area	claim	where	falsifier	back	fwd
H-001	BLOCK	OPEN	netplay	A joining client loads the host's snapshot as raw memory: g_mp.leader, resident and sprite_owner index g_mp.p[4] with no bound, so a hostile host writes 512 chosen bytes about 100 KB past the array	src/mp.c:444;src/mp.c:179;src/instance.c:1110	ASan build; feed Inst_LoadState a snapshot with g_mp.leader=200 and run two ticks with two joined players: no ASan report at mp.c:179 shows this wrong	HARDEN/netplay.md#1	pmo:323;epmo:B7
H-002	BLOCK	FIXED	netplay	SnapData bound check wrapped (off near 2^32 plus n passed, memcpy 4 GB past the buffer) and SnapInfo size 0 divided by zero; both reproduced under ASan	src/session.c:1595;src/session.c:1566	scratch/h1.c: ./h1 wrap and ./h1 zero both print survived (they do, after the fix)	HARDEN/netplay.md#2;HARDEN/netplay.md harness runs	commit:HEAD
H-003	HOLD	OPEN	netplay	One unauthenticated 34-byte Hello from a spoofed source makes the host stream about 0.5 MB (snapshot plus a Frames packet per tick for 6 s) to that address; three such Hellos per 6 s also hold the game full	src/session.c:660;src/session.c:801	./h1 amp total bytes to the Hello source under 3x the request	HARDEN/netplay.md#3	pmo:324
H-004	HOLD	OPEN	netplay	Nothing after Hello is authenticated: packets match a client by ip:port; the 32-bit reconnect token travels in clear; a parked seat is taken by any sender with the same name (reproduced: ./h1 steal)	src/session.c:722;src/session.c:744	./h1 steal prints Welcome=0 for the stranger	HARDEN/netplay.md#4	pmo:324
H-005	HOLD	FIXED	local	ApplyBps trusted the patch past its CRCs: outputOffset was never bounded against dst_size (ASan heap-buffer-overflow WRITE at util.c:259 with a CRC-valid lying patch)	src/util.c:256	scratch/bps_h.c returns NULL with no ASan report (it does, after the fix)	HARDEN/local.md HOLD 2	commit:HEAD
H-006	WARN	OPEN	netplay	A joined client can make the host rebuild and resend a snapshot per packet (rejoin=1 Hello, resync Input), pinning CPU and uplink; no rate limit on any packet type	src/session.c:660	host frame time stays normal with a client sending 1000 resync Inputs per second	HARDEN/netplay.md#6	pmo:325
H-007	WARN	OPEN	netplay	UPnP/SSDP takes LOCATION, control URL and external IP from whichever device answers first, on any host; a rejected lease retries as permanent (lease 0); STUN replies are taken from any source	src/upnp.c:102;src/upnp.c:206	a fake SSDP responder with a LOCATION on another subnet draws no HTTP request	HARDEN/netplay.md#7;HARDEN/local.md WARN 6	pmo:326
H-008	HOLD	FIXED	local	A host-planted save (Keep a copy writes the host's RAM with a good checksum) with a max-health byte under 8 made the file-select draw loop never end, writing past vram_upload_data	src/select_file.c:108	saves/sram.dat with byte 0x36c = 0 and a valid checksum draws the file select normally	HARDEN/local.md HOLD 1	commit:HEAD
H-009	WARN	OPEN	local	Snapshot, save and replay parsers rely on assert (loadFunc bound, StateRecorder version); the vcxproj Release build defines NDEBUG and loses them; RLE output is uncapped (about 270 MB from a 4 MB snapshot)	src/zelda_rtl.c:384;src/session.c:206	a truncated saves/ref file loaded in an NDEBUG ASan build is refused, and a snapshot expanding to 20 MB is refused before allocation	HARDEN/local.md WARN 5;HARDEN/netplay.md#9	pmo:327
H-010	HOLD	OPEN	archipelago	A bare host:port on any server but archipelago.gg tries ws:// before wss://, so the slot password and all traffic go in clear with no downgrade needed (reproduced against a plaintext fake server)	src/ap_client.c:1058	clientdrv against a port where only a plaintext listener answers never sends Connect	HARDEN/archipelago.md#1	pmo:328
H-011	WARN	OPEN	archipelago	No cap on ReceivedItems, players or slot_info from the server: one 16 MiB message (about 20 KB compressed) cost 700 MB RSS, a second 956 MB	src/ap_client.c:879;src/ap_ws.h:28	scratch/amp.c peak RSS under 100 MB for the first message	HARDEN/archipelago.md#2	pmo:329
H-012	HOLD	OPEN	supply	tcc.exe (a personal fork's release), SDL2.dll and the SDL2 source are fetched over https with no hash and run or shipped: the DLL goes into the Windows artifact players are told to download	run_with_tcc.bat:7;tools/build_windows_mingw.sh:14;.github/workflows/build.yaml:46	grep for sha256, certutil or Get-FileHash in those three files finds a check before extraction	HARDEN/supply.md#1	pmo:330;epmo:B6
H-013	WARN	OPEN	supply	pip installs are unpinned (pip itself upgraded in one script) and run at the player's privilege, with a system-wide fallback	get_python_libs.bat:3;extract_assets.command:40	a requirements file with --require-hashes exists and the scripts use it	HARDEN/supply.md#7;HARDEN/local.md WARN 7	pmo:330
H-014	HOLD	OPEN	supply	saves/ref/*.sav (13 files, 6.5 MB) are full emulator state built from the commercial ROM, inherited from upstream snesrev/zelda3, in a repo whose .gitignore says never to commit Nintendo data	saves/ref/Chapter 1 - Zelda's Rescue.sav:1;.gitignore:38	decode one file with StateRecorder_Load and show its video and work memory are empty or a delta from a fresh boot	HARDEN/supply.md#2;HARDEN/local.md NOTE 11	pmo:331
H-015	WARN	OPEN	privacy	Every joiner receives the host's whole SRAM, all three save slots and backups, in the join snapshot; the doc said only that joining downloads the host's game	src/session.c:660;src/zelda_rtl.c:398	decompress a snapshot and find no bytes of the two inactive slots	HARDEN/local.md WARN 4	pmo:332
H-016	WARN	FIXED	docs	No SECURITY.md or private report route; blank issues disabled and the bug form invited pasting addresses	SECURITY.md:1	the file names a private route and asks reporters to leave addresses out	HARDEN/supply.md#6	commit:HEAD;epmo:B5
H-017	HOLD	FIXED	privacy	The Windows crash file recorded the faulting module's full path (the player's account folder) and the dialog asked the player to send it	src/crash_handler.c:25	a crash on Windows writes only the module's base name and offset	HARDEN/supply.md#4;HARDEN/local.md HOLD 3	commit:HEAD
H-018	WARN	FIXED	docs	What leaves the machine was not in one place: STUN to Google by default, the room code is the raw address, netplay unencrypted, ws:// first for Archipelago, coop.ini keeps the last friend's address, F12 captures the overlay	MULTIPLAYER.md:457	the section "What leaves your computer, and what arrives" names each of those	HARDEN/supply.md#5;HARDEN/local.md Privacy surface	commit:HEAD;epmo:B5
H-019	WARN	FIXED	docs	"a removed player can't come back on their own" was false: kicks match token or (address, name), so a new name from the same machine is let in (reproduced: ./h1 kick)	MULTIPLAYER.md:44;src/session.c:744	the sentence now says removing is a nudge, not a lock; a code fix would make ./h1 kick print Welcome=0	HARDEN/netplay.md#5	commit:HEAD
H-020	NOTE	OPEN	repo	The default branch is claude/great-maxwell-00u7q3, so a stranger's clone lands on an AI-session-named branch; README line 13 "room codes instead of IP addresses" reads as hiding the address; BUILDING.md clones upstream and gives SHA256 where the code checks SHA1; AI_DISCLOSURE says every commit has a trailer (two merges and one web deletion do not); CI prints otool -L and asserts nothing	README.md:13;BUILDING.md:9;AI_DISCLOSURE.txt:13;.github/workflows/build.yaml:69	each sentence corrected, or the default branch renamed	HARDEN/supply.md#8;HARDEN/supply.md#9;HARDEN/supply.md#10;HARDEN/supply.md#11	pmo:333
H-021	WARN	OPEN	local	After a joined game, Reset restores the player's SRAM only when saves/sram.dat opens at 8192 bytes; otherwise the host's SRAM stays in memory and the foreign flag clears, so the next save writes the host's slots to the player's file (read, not run)	src/zelda_rtl.c:933	delete saves/sram.dat, join, leave, Reset, save: the file does not equal the host's SRAM	HARDEN/netplay.md#8	pmo:334
H-022	NOTE	OPEN	local	Small items: ReadWholeFile on a FIFO writes one byte before its buffer; ini !include fan-out and shader #include depth unbounded; remote names reach stderr and toasts unfiltered; DSP firBufferIndex unmasked; AP password silently cut at 63 bytes; SSL_set1_host return unchecked; out-of-range double to int casts; build_apworld.py follows symlinks; a test script binds 0.0.0.0; ZELDA3_AP_DEBUG prints the password	src/util.c:46;src/config.c:683;src/ap_client.c:800	each item has its own falsifier in the reports	HARDEN/local.md WARN 9;HARDEN/local.md WARN 10;HARDEN/archipelago.md#3	pmo:335
H-023	WARN	FIXED	repo	.gitignore missed files the game writes: saves/sram.dat.bak (a save), zelda3_crash.txt, spoiler_*.txt	.gitignore:34	git check-ignore names all three	HARDEN/local.md WARN 8	commit:HEAD
H-024	WARN	FIXED	supply	The workflow had no permissions block, so the token took the repository default; actions and the devkitPro image are pinned by tag only (still open)	.github/workflows/build.yaml:8	the top-level permissions block reads contents: read; pins by SHA remain for a later round	HARDEN/supply.md#3	commit:HEAD
H-025	NOTE	FIXED	repo	get_python_libs.bat was stored with CRLF while .gitattributes says *.bat text eol=crlf, so every fresh clone showed it modified	get_python_libs.bat:1	git status after a fresh clone is clean	git ls-files --eol get_python_libs.bat (i/crlf before, i/lf after)	commit:HEAD
H-026	OPP	OPEN	netplay	The host already knows the trust model (unsigned UDP, address as credential); a per-game random key shown beside the room code would give players a secret to share without a server, and would carry H-003, H-004 and H-019 at once	src/session.c:1986	a design note or branch exists that puts a key in the room code	HARDEN/netplay.md#4 fix shape	pmo:324
H-027	OPP	OPEN	supply	The reviewers' harnesses now live in HARDEN/harness (h1.c, bps_h.c, amp.c, fuzzmsg.c, clientdrv.c and the rest) with their build lines; wiring them into CI would let it go red on H-001, H-002, H-005 and H-011 for good	HARDEN/harness/README.md:1	a CI job builds and runs them and fails on an ASan report	HARDEN/netplay.md;HARDEN/archipelago.md;HARDEN/local.md	-
```
