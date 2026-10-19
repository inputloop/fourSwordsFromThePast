# Netplay review: fourSwordsFromThePast @ 048adbb

Scope: src/net.c, mp.c, session.c, instance.c, upnp.c, mptest.c, plus config.c, ui.c (input/draw), zelda_rtl.c (snapshot load), snes/dsp.c, snes/ppu.c. Nothing in the repo was edited. Harness: /home/claude/work/zelda/scratch/h1.c (includes the real session.c, stubs the rest; stub SDL.h in scratch/stub). Build: `cd scratch && gcc -g -fsanitize=address -I stub -I ../fsotp h1.c -o h1 -Wl,--unresolved-symbols=ignore-all`.

## Observed

Sockets and exposure
- Every socket binds 0.0.0.0: net.c:118. Host game socket on 47474 (session.c:630). Second socket on 47474 whenever hosting on another port (session.c:636-637). Client socket ephemeral (session.c:1094). LAN-scan socket ephemeral, opened on first "Find LAN games" and never closed: opened session.c:1765-1766, grep finds no Net_Close of lan_sock; it is polled every tick (session.c:1811).
- Host answers a 3-byte Discover from any source with a 30-byte Info (name, joined count, port): session.c:803-807. Not limited to the LAN: the socket is 0.0.0.0 and UPnP maps it to the internet.
- UseUpnp defaults true (config.c:706); StunServer defaults to stun.l.google.com:19302 (config.c:707), used only when UPnP fails (upnp.c:302). UPnP uses NewRemoteHost empty (any remote), external port equal to internal (upnp.c:190). Lease 7200 s, but if the router rejects a lease the code retries with lease 0, a permanent mapping (upnp.c:214-217).
- Packets carry magic "Z3", type byte, no checksum, no MAC, no encryption (session.c:68, 1790-1806). Post-Hello packets are matched to a client only by source ip:port (HostFindClient, session.c:709). The Input packet's id byte is read and discarded (session.c:899).
- Room code = 32-bit IP, 16-bit port, 12-bit FNV-derived check, no secret (session.c:1977-2019).
- Host-side limits: 3 clients (session.h:18), kicked[8], lan[8], notice[8]. 6 s silence parks a played-in client, drops a never-loaded one (session.c:985-990). Parked seat 120 s.

Joining a game (client side)
- The client replaces its whole state with bytes from the host: Inst_LoadState (instance.c:1110) reads g_ram, DMA, PPU (vram, cgram), APU RAM, DSP struct, SRAM, and `g_mp` raw (mp.c:443-445 `func(ctx,&g_mp,sizeof(g_mp))`). No field of g_mp is range-checked. Mp_OnStateLoaded (mp.c:409) sets `g_mp.resident = g_mp.leader` from the wire.
- g_mp.p has 4 entries of 516 bytes (mp.h:25-31, 97). Mp_SwapTo does `Mp_SaveCtx(g_mp.p[g_mp.resident].ctx)` (mp.c:179), a 512-byte memcpy of g_ram ranges (mp.c:87-92). Also `g_mp.p[g_mp.sprite_owner[k]].joined` (mp.c:948), `g_mp.p[g_mp.leader]` (mp.c:869, 1183, 1978).
- SnapInfo cap is 4 MB (session.c:1565). SnapData bound check is `off + n > g_s.snap_size` (session.c:1586) with off uint32, n uint16, so the sum is uint32.
- After SnapData, `snapshot_progress = snap_have * 100 / snap_size` (session.c:1592) with no zero check.
- RleDecompress has no output cap (session.c:206-226); worst case 65 output bytes per 1 input byte. Client proceeds to "connected" even when Inst_LoadState fails: it only sets a message (session.c:1452-1453).
- loadFunc uses `assert(st->pend - st->p >= data_size)` (zelda_rtl.c:384). Makefile builds `-O2 -Werror` with no -DNDEBUG (Makefile:4); zelda3.vcxproj defines NDEBUG (lines 148, 169, 210).
- Client accepts Frames, Roster, Reject, Bye, Welcome from any packet whose source equals the host ip:port (session.c:1521); no sequence or nonce.

Harness runs (all against the real session.c)
- `./h1 zero` (SnapInfo size 0, then SnapData off 0 n 0): `ERROR: AddressSanitizer: FPE ... in ClientHandlePacket session.c:1592`.
- `./h1 wrap` (SnapInfo size 4 MB, then SnapData id 1, off 0xFFFFFC00, n 1024): bound check passes, `SEGV ... WRITE memory access ... __memmove_evex_unaligned_erms ... ClientHandlePacket session.c:1588`. The fault address is the malloc'd snap pointer + 4294966272.
- `./h1 chunkidx`: with size 4 MB, snap_chunk_have is calloc'd 4097 bytes (session.c:1572-1573); the wrap case indexes it at 0x3FFFFF.
- `./h1 amp`: one 34-byte Hello from address 8.8.8.8:9999, no later packet from it, 360 host ticks (5.76 s). Output: `Welcome 1 pkt 32 bytes; Frames 360 pkts 436086 bytes; SnapInfo 360 pkts 2880 bytes; SnapData 131 pkts 135435 bytes; Roster 12 pkts 1092; total 575525 bytes for a 34-byte request`. The snapshot is a stub (RAWKB=256 gives 134125 bytes compressed; RAWKB=64 gives 33542 and total 473962). Real snapshot size not measured. By the size list at zelda_rtl.c:388-398 a world is roughly 270 KB raw.
- `./h1 kick`: after Session_Kick(1): same name and address gets Reject; same address, name "Alice2", token 0: `Welcome=1` (accepted).
- `./h1 steal`: client parked after 7 s of silence; Hello from a different IP, same name, token 0: `Welcome=1, seat now at ip=0a000002 (was 0a000001)`.

## Judged

### 1. BLOCK: the snapshot a host sends is trusted as raw memory; a hostile host corrupts client memory
- Claim: joining a stranger's room code loads attacker-chosen `g_mp.resident`/`leader`/`sprite_owner` etc., which index `g_mp.p[]` with no bound.
- Lines: instance.c:1110-1150, mp.c:409-413, 179, 948, 443-445; MpPlayer size mp.h:25-31.
- Scenario: host (or anyone who answers at the room-code address first, see finding 7) sends a valid-length snapshot with g_mp.leader = 200. Next Mp_SwapTo writes 512 bytes of game RAM (also host-supplied) to g_mp + 200*516 bytes, up to about 131 KB past the array, over whatever globals follow. Data is attacker controlled, offset is a multiple of 516. Outcome: crash at minimum, bss corruption with chosen bytes at worst. Other raw-loaded structures also feed indices: Dsp.firBufferIndex is a uint8 used on int16[8] (dsp.c:175, struct in dsp.h; spills inside the Dsp struct only), channel decode offsets and echo indices. Not checked: the recompiled game code's tables indexed by RAM bytes (an arbitrary RAM image is attacker data for all of it).
- Not run, reasoned from the lines above. FALSIFIER: build with ASan, feed Inst_LoadState a snapshot whose g_mp.leader byte is 200 and run 2 ticks with two joined players: no ASan report in mp.c:179 would show this wrong.
- Fix shape: after load, validate g_mp (leader, resident, sprite_owner, ancilla_owner, carried_by, split_request all < kMpMaxPlayers or the documented sentinels) and reject the snapshot otherwise; cap decompressed size at the exact expected size.

### 2. BLOCK: SnapData bound check wraps, SnapInfo size 0 divides by zero (reproduced)
- Lines: session.c:1565, 1586-1592.
- Scenario A: SnapInfo size 4 MB, then SnapData off=0xFFFFFC00 n=1024: sum wraps to 0, check passes, memcpy to snap+4 GB (and `snap_chunk_have[0x3FFFFF]=1` is a 4 MB overrun of a 4 KB buffer). Reproduced as SEGV above. Scenario B: SnapInfo size 0, SnapData off 0 n 0: SIGFPE. Anyone who can send from the host's ip:port can trigger it (a spoofer needs the client's source port, 64k tries).
- Also: malloc/calloc results unchecked (session.c:1571-1573); snap_size 1..3 makes `memcpy(&snap_frame, snap, 4)` over-read (session.c:1594).
- FALSIFIER: rerun `./h1 wrap` and `./h1 zero` after the fix; both must return "survived".
- Fix shape: `(uint64)off + n > snap_size`, reject size < 12 at SnapInfo, check allocations.

### 3. HOLD: one unauthenticated Hello makes the host stream about 0.5 MB at any address
- Lines: session.c:858-889 (new client from any source, HostStartSnapshot), 988-1071 (HostSend).
- Scenario: Hello (34 bytes) with spoofed source V. Host allocates a client slot, builds a snapshot, sends it to V, then a Frames packet every tick (client never acks, need_frame is fixed) until the 6 s timeout. Measured 474 to 576 KB with stub snapshots, about 14,000 to 17,000 times the request. Three slots refilled every 6 s keeps it going. Same three packets per 6 s also block every real join: pending joins count toward "Game is full" (session.c:855-862). Needs a network that permits source spoofing and a reachable host port.
- FALSIFIER: run `./h1 amp`; if total bytes to the Hello source are under 3x the request, wrong. In the live game: Hello to a hosted port from a throwaway socket, then count bytes received for 6 s.
- Fix shape: do not send snapshot or frames until the peer has echoed a host-chosen nonce (stateless cookie in Welcome, required in the first Input).

### 4. HOLD: no authentication after Hello; the 32-bit token is cleartext, a seat can be taken by name
- Lines: session.c:722-742, 758-763, 835-852, 899, 1521, 1534-1560.
- Scenarios: (a) a Bye from a client's ip:port drops that client's players (session.c:962-963); a Reject or Bye from the host's ip:port disconnects a client (session.c:1557-1560, 1621-1623). Needs the 5-tuple, trivial on a shared LAN or NAT. (b) Token travels in Welcome and in every reconnect Hello in clear; any matching token moves the seat to the sender even if the seat is connected (HostFindReturning with `token`, session.c:724-728, then session.c:844). NewToken is a multiply-xor of ticks and perf counter into a static (session.c:758-763); no rate limit, so 2^32 guesses is the bound (about an hour at 1.5 Mpps, my estimate, not measured). Failed guesses on a non-full host also create new clients. (c) Parked seat plus default name: reproduced by `./h1 steal`. The condition is `hc->parked ||` with no IP check (session.c:736), so any internet host that knows or guesses the name (roster names are sent to all clients, session.c:1005; default is "Link") takes the Link, hearts and position. The comment at session.c:717-719 says a stranger must never get someone else's seat; for a parked seat the code does not keep that.
- FALSIFIER for (c): `./h1 steal` printing Welcome=0 for the stranger.
- Fix shape: per-session random 64-bit key from the OS, required on every packet and verified with a MAC over the packet; reconnect by key only, never by name.

### 5. WARN: "a removed player can't come back on their own" is false (reproduced)
- Lines: session.c:744-749, 1949-1968, README claim MULTIPLAYER.md:43-44.
- Scenario: kicked client restarts (token 0) and sets any other PlayerName; HostWasKicked matches only on token or (same IP and same name). `./h1 kick`: `new name, same addr, no token: Welcome=1`. Different IP also passes. After 8 kicks the last slot is overwritten (session.c:1962).
- FALSIFIER: kick then rejoin with a different name from the same machine is refused.
- Fix shape: either drop the claim, or ban by a per-install random id kept in the ini (still bypassable), and say so in the doc.

### 6. WARN: any joined client can pin the host's CPU and uplink
- Lines: session.c:835-852 (Hello with rejoin=1 restarts a snapshot each time), 953-960 (resync flag, or a crafted `need`, restarts one), 658-690 (HostStartSnapshot saves state, RLE compresses, swaps rollback state).
- Scenario: a client sends Input packets with resync=1 at line rate and acks the snapshot; the host rebuilds and resends about 100 KB per packet, 60 fps loop stalls. No rate limit on any packet type; PollSocket drains the whole socket each tick (session.c:1787-1806).
- FALSIFIER: host frame time with a client sending 1000 resync Inputs per second stays within normal range.
- Fix shape: at most one snapshot per client per N seconds; ignore resync inside that window.

### 7. WARN: UPnP/SSDP trusts whichever LAN device answers first
- Lines: upnp.c:95-113 (reply source ignored, first LOCATION wins), 37-50 and 154-159 (any http host, public IPs included, control URL may name another host), 223-224 (external IP taken as text from the reply).
- Scenario: a device on the LAN answers the M-SEARCH, supplies its own control URL and an arbitrary "external IP". The room code (session.c:2031-2043) then advertises the attacker's address; friends who join it run finding 1 and 2. The game also issues HTTP GET and POST to an address from an unauthenticated reply. Permanent mapping fallback (upnp.c:216) survives a crash; mapping description "Zelda3 co-op" (upnp.c:192).
- FALSIFIER: a fake SSDP responder returning a LOCATION on another subnet gets no HTTP request from the game.
- Fix shape: accept LOCATION only when its host equals the SSDP reply's source IP and is a private address; refuse lease 0.

### 8. WARN: "your own files are never touched otherwise" has a gap
- Lines: zelda_rtl.c:933-944 (ZeldaReadSram), main.c:983-986, session.c:1109-1111, 1749-1752.
- Scenario: on Reset after a joined game, ZeldaReadSram restores sram only if saves/sram.dat opens and is 8192 bytes. If the file is missing or short, the host's SRAM stays in memory, the foreign flag is cleared, and the next in-game save writes the host's save slots to the player's own sram.dat. Not run.
- FALSIFIER: delete saves/sram.dat, join, leave, press Reset, save in game: the file must not equal the host's SRAM.
- Fix shape: zero g_zenv.sram when no valid file is read.

### 9. WARN: other client-side resource and robustness points
- RLE expansion up to 65x of 4 MB, about 270 MB allocation from one SnapInfo (session.c:206-226, 1565).
- loadFunc assert: on Makefile builds a short snapshot aborts (crash); on the vcxproj build (NDEBUG) it over-reads the heap into game RAM (zelda_rtl.c:384).

### 10. NOTE
- lan_sock stays bound on 0.0.0.0 for the process lifetime after one LAN scan (session.c:1765); any host can inject up to 8 fake "LAN games" with its own IP into the list (session.c:1477-1500). Joining one is the user's choice, but it leads to finding 1.
- Discover answers expose the host's PlayerName and player count to anyone who probes the port (session.c:803-807), so hosts are enumerable by scanning UDP 47474.
- Remote names are printed raw to stdout (`[netplay] %s`, session.c:567); on-screen drawing skips bytes >= 128 and is safe (ui.c:146-150). Names are never written to files; only own PlayerName and LastJoinAddress go to zelda3.ini, and the join address text is filtered to alnum . : - (ui.c:1832-1836).
- STUN reply accepted from any source, 96-bit id from a weak mix of ticks and perf counter (upnp.c:248-250, 262); impact is a wrong public IP in the room code. Google sees the host's IP by default when UPnP fails.
- Net_ParseAddr resolves hostnames on the calling thread (net.c:229), so slow DNS freezes the window.
- mptest.c ships in the normal binary but is reachable only by `--mptest` (main.c:524); no network path found.
- No document states the trust model: README/MULTIPLAYER.md never say that joining executes the host's memory image, or that traffic is unencrypted.

## What holds

- Host-side packet parsing: Reader bounds every read and sets err (session.c:75-82); Hello name is terminated (session.c:812-814); Input count is capped at 8 before the loop (session.c:901-902); frame scheduling indices are modulo the table sizes and f-frame windows are signed-compared (session.c:910-924); HostLateInput checks owner and local index (session.c:786-791). No memory error found in HostHandlePacket.
- Host-authoritative inputs: a client's Input only fills slots whose owner equals its client id (session.c:420-429, 304-307); join and leave masks are resolved per owner (session.c:320-343). A client cannot press another player's buttons, leave another player, or change flags and features; those come from the host's BuildFrame (session.c:387-400).
- ReadFrame caps nevents at 4 before reading (session.c:474-477). ApNotice length is bounded against both the 96-byte buffer and the packet (session.c:1534-1538). Roster and Info reads are bounded and strings terminated (session.c:1498-1522).
- SnapData chunk offsets must be multiples of 1024 and within the packet (apart from the wrap in finding 2); the host's chunk loop clamps n to the remaining size (session.c:1017-1030).
- Fixed-size tables everywhere on the network paths (3 clients, 8 kicked, 8 LAN, 8 notices); per-peer memory is one snapshot plus a 64-slot input schedule. A rollback request from a client is limited to the window (conf, frame] (session.c:924), and the host's lead over conf is bounded by kMaxWait (session.c:1410) so the rollback ring index is always populated.
- SOAP request construction: only numbers and the local IPv4 go into the XML (upnp.c:190-194); buffers are sized for it; ExtractTag truncates to the output size (upnp.c:63-66); STUN attribute walk checks lengths against the clamped end (upnp.c:264-270). TCP response is NUL-terminated at maxlen-1 (net.c:298-304).
- Room code: typo-proof decode rejects wrong length and bad check, zero IP or port, and is tried before address parsing (session.c:1086-1092, 1998-2019). It was never claimed to be secret; MULTIPLAYER.md:33-34 describes it correctly as address plus check.
- Join-address input is filtered, so clipboard text cannot inject newlines into the ini (ui.c:1832-1836).
- Client SRAM: Session_Join disables SRAM writes at once and a disconnect from the host's world keeps them off (session.c:1111, 1749-1752), apart from finding 8. The host's public address is not sent to clients; clients' addresses are never forwarded to other clients (roster has names and pings only, session.c:1005).
- Not checked: archipelago.c beyond GiveItem/SetRoomBytes, ap_client TLS/WebSocket, hd3d/audio, and recompiled-game tables indexed by RAM bytes.
