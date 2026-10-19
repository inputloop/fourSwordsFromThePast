# Archipelago client and apworld review (fourSwordsFromThePast, HEAD 048adbb)

Read-only review. No repo file was edited. Harnesses live in /home/claude/work/zelda/scratch/ (SDL shim in shim/, frames.c, fuzzmsg.c, amp.c, clientdrv.c, mitm.py, rawsrv.py, zfuzz.c, deep.c). Threat model used: (a) an on-path attacker on a ws:// connection, (b) a room operator who is hostile or compromised. Items and DeathLink from a room are "authority" by design, so I looked at what goes beyond that: secrets, memory, crashes, code paths.

## Observed

TLS
- No hand-rolled TLS. src/ap_tls.c:3-5: Windows uses Schannel (secur32.dll, crypt32.dll), elsewhere the system OpenSSL, both loaded at run time. OpenSSL candidates are bare names `libssl.so.3`, `libssl.so.1.1`, `libssl.so` (ap_tls.c:695-697), macOS tries absolute Homebrew/MacPorts paths first (690-692). Windows loads from System32 only (ap_tls.c:232).
- OpenSSL path: min protocol TLS 1.2 (ap_tls.c:749), `SSL_VERIFY_PEER` (756), chain verified against system roots plus `ZELDA3_AP_CAFILE` (751-755), hostname via `SSL_set1_host` plus SNI (762-763), IP literals via `X509_VERIFY_PARAM_set1_ip_asc` (760). No cipher list set, so library defaults. No pinning. Return values of lines 760 and 763 are not checked.
- Windows path: TLS 1.2 only, `SCH_USE_STRONG_CRYPTO`, `MANUAL_CRED_VALIDATION` (ap_tls.c:504-505), then its own chain and host check in `CheckCertificate` (344-421) after the handshake and before any application data. Revocation flags are deliberately ignored (ap_tls.c:380).
- Ran (OpenSSL 3 on this box, real src/ap_tls.c, Python TLS server): self-signed cert for `localhost`, no CAFILE: client status "The server's certificate was rejected: self-signed certificate". Cert for `other.example` signed by a CA that is in `ZELDA3_AP_CAFILE`, connecting to `localhost`: "rejected: hostname mismatch". Both servers saw a fatal alert and received no application data.
- Secrets: password comes from the in-game field or `Password =` in zelda3.ini (config.c:555-556, main.c:635). `Ui_SaveSettings` writes only Server, Slot, DeathLink, World to zelda3.coop.ini (ui.c:2896-2901) and a comment says the password is not saved (ui.c:2893). The menu masks it as `****` (ui.c:864). The Windows crash handler writes only exception code and address (crash_handler.c:9-23). The password is sent in the Connect packet (ap_client.c:534-535), truncated silently to 63 bytes by `ApSettings.password[64]` (archipelago.h:146, main.c:635) and again to 127 in the client (ap_client.c:61, 1336). With env `ZELDA3_AP_DEBUG` set, the whole outgoing Connect JSON, password included, is printed to stderr (ap_client.c:331, up to 300 chars), and every incoming message raw (1011).
- Scheme choice (ap_client.c:1064-1075, 1226-1230): explicit `ws://`/`wss://` honoured. No scheme: `archipelago.gg` and `*.archipelago.gg` get wss only; any other host gets ws:// first, then wss://. Never wss to ws.
- Ran (clientdrv.c + mitm.py): `./clientdrv 127.0.0.1:39200` against a plaintext fake server. First bytes on the wire were `GET / HTTP/1.1`; the fake server read `{"cmd":"Connect","password":"hunter2-slot-password",...}` in clear, answered Connected, then injected two ReceivedItems and a Chat line. The client reported "Connected ... (ws)", `items=2`, and the chat text with ESC replaced by `?`.

WebSocket (src/ap_ws.c)
- Own RFC 6455 implementation. Length: 7/16/64-bit, then `len > kApWsMaxMessage` (16 MiB) rejected before any allocation (ap_ws.c:717; ap_ws.h). Masked server frames rejected (ap_ws.c:701). RSV2/3 and RSV1 on non-data frames rejected (727). Control frames must be FIN and at most 125 bytes (731-735). Fragment accumulation capped at 16 MiB (771). No UTF-8 validation of text frames (harmless here: cJSON tolerates it, Sanitize maps non-ASCII to `?`). Pings answered inline; close reason copied into an error string with `%.*s` (743).
- permessage-deflate: one-shot inflate through stb_image's `stbi_zlib_decode_noheader_buffer`, output grown by doubling up to 16 MiB (ap_ws.c:666-690). Handshake header capped at 16384 (547); `req[1024]` holds at most about 800 bytes given host[255]/path[255] limits (ap_ws.c:500-518, ApAddr in ap_client.c), so no overflow. Client masking key and Sec-WebSocket-Key come from a xorshift32 seeded by clock, time and a pointer (ap_ws.c:211, 604).
- Ran with ASan/UBSan, real ap_ws.c, raw Python server (rawsrv.py): 32-bit and 64-bit huge length: closed with "over 16 MiB", 17 ms. Fragmented text: reassembled. 126-byte ping: closed. Close frame with ESC and NUL in reason: error text keeps ESC, NUL truncates. Deflate stream inflating to 17 MiB: refused in 0.25 s. 2,000,000 one-byte continuation frames: one 2 MB message in 1.1 s. No ASan or UBSan reports.
- Ran `zfuzz.c`: 400,000 mutated raw-deflate streams through stb_image 2.27's zlib decoder under ASan/UBSan: no reports.

JSON (src/ap_json.c)
- ap_json.c is 6 lines: it `#include`s third_party/cJSON/cJSON.c (ap_json.c:5). `diff` against upstream v1.7.19 (fetched from raw.githubusercontent.com): no differences in cJSON.c or cJSON.h. `CJSON_NESTING_LIMIT` is 1000 (cJSON.h:137), not overridden in any build file I grepped.
- Ran `deep.c` in a 256 KiB-stack thread, -O2 and ASan: depth 998 parses, 1002 rejected, no overflow.
- Allocation hook returns NULL once the client is told to stop (ap_client.c:1006-1008).
- Ran `fuzzmsg.c` (includes ap_client.c itself, calls `HandleMessage` with mutated RoomInfo, DataPackage, Connected, ReceivedItems, PrintJSON, Bounced, ConnectionRefused seeds, random phases, 1 to 4 messages per Conn): 900,000 iterations under ASan+UBSan, clean. With `-fsanitize=float-cast-overflow` it stops at ap_client.c:800 ("1e+300 is outside the range of representable values of type 'int'").
- Ran `amp.c` (HandleMessage with one 16,777,212-byte ReceivedItems made of `{},`): 5,592,389 items accepted, peak RSS 700 MB, 0.85 s. A second such message with index equal to the current count: 11,184,778 items, peak RSS 956 MB, and g_ap.items copied them all (ap_client.c:889-902). Limits that exist: kMaxGames 2048, kMaxNames 262144 (ap_client.c:32-33). No limit on item count, slot_info entries or players (ap_client.c:763, 787, 890).

Message handling
- Server ids are never used as array indexes in ap_client.c: item, location and player names go through binary search or linear scan (`LookUp` 436-448, `FindSlot`, `PlayerName`) and fall back to "Item N" text. Slot number from the server is compared, not indexed (`atoi`, ap_client.c:791).
- Server text reaches the screen only through `Sanitize` (ap_client.c:185-206, called at 251 and in SetStatus) which keeps 0x20-0x7e and maps all else to `?`. Item numbers from the server pass `IsPoolItem` (27 codes, archipelago.c:238-248) or `AlttpGivesSomething` (256-261) before any effect, bounded to 0-255 (archipelago.c:880). Anything else becomes event byte 0xff and is skipped by `SetItemCount(idx+1)` (archipelago.c:387-391). No file path, command, or format string is built from server data; all `snprintf` formats are literals.
- DeathLink in: needs the local setting or `slot_data.death_link` (archipelago.c:781); queue of 8, names sanitized to 63 chars (ap_client.c:974-1003); the coalesced flag `death_to_give` (archipelago.c:789) yields one death event per frame.
- Item index is 16 bit in the save (archipelago.c:224-231).

Python and scripts
- archipelago/fsotp/*.py import only Archipelago base classes, `functools`, `typing`; no `open`, `os`, `subprocess`, `socket`, `eval`, `exec` (grep over fsotp/*.py). Manifest archipelago.json is static.
- build_apworld.py opens files with plain `open(path,'rb')` after `os.walk` (build_apworld.py:46-52). Ran in scratch: a symlink `archipelago/fsotp/leak.txt -> /etc/hostname` was packed into fsotp.apworld (`unzip -l` shows `fsotp/leak.txt`).
- mock_server.py binds `--host` default 127.0.0.1 (mock_server.py:446) and its internal servers to 127.0.0.1 (487-492). run_real_server_test.sh:48 starts Archipelago's MultiServer with `--host 0.0.0.0`. e2e.sh starts MultiServer with only `--port` (host default not checked). e2e.sh copies the built apworld into `$AP_ROOT/custom_worlds` (outside the repo, by design). tools/gen_*.py write `src/ap_alttp_data.h` and `archipelago/fsotp/location_data.py` relative to cwd (gen_alttp_table.py:202, gen_locations.py:60).

## Judged

**1. HOLD: the default for a bare host sends the room password and all traffic in plaintext, and an on-path attacker needs no downgrade to get it.**
Lines: ap_client.c:1064-1075 (ws:// first unless archipelago.gg), 1226 (scheme defaults to Either), 534-535 (password in Connect); README "Secure connections" paragraph (README.md:401-412).
Scenario: player types `myserver.example:38281` (or pastes `/connect ...`), someone on the path answers on that port with a ws:// server. Observed above: password captured, items, chat and (if enabled) DeathLink injected. The README sentence "The game never falls back from encrypted to plain ... (that is what someone listening in between would cause)" is literally true for wss to ws, but the plain attempt comes first, so the attacker never has to cause a fallback. Self-hosted AP rooms are plain ws:// by default, so this is partly inherent; the finding is that the UI and README do not say "password travels in clear unless you type wss://".
Falsifier: run clientdrv with a bare `host:port` where only a wss server exists and a second listener on the same port accepts ws://; if the client never sends Connect on the plain listener, the finding is wrong. (I ran the equivalent against a plaintext-only listener and it did send it.)
Fix shape: when the scheme is omitted and the host is not archipelago.gg, either try wss first, or keep the order but show "connected without encryption" prominently and say in the README that the password is sent as typed over ws://.

**2. WARN: one 16 MiB message from the server costs about 700 MB of RAM, and the item list has no cap.**
Lines: ap_client.c:879-902 (OnReceivedItems, `Grow` per item, copy into g_ap.items), 763 and 787 (players and slot_info sized from the array), ws limit ap_ws.h kApWsMaxMessage.
Scenario: a hostile room operator, or a MITM on ws://, sends `[{"cmd":"ReceivedItems","index":0,"items":[{},{},...]}]` (about 5.6 M items in 16 MiB). Measured 700 MB peak, 956 MB after a second message; the game process can be killed by the OS, losing unsaved play. Also the same list is kept in g_ap.items until Stop, and a deflate-compressed 16 MiB message costs only about 20 KB on the wire.
Falsifier: build amp.c as given; if peak RSS for the first message is below about 100 MB, wrong.
Fix shape: cap items per slot at 65535 (the save counter is 16 bit anyway), and cap players and slot_info entries at a few thousand; reject the message and set Failed when exceeded.

**3. WARN: README claim "Password: ... never saved" is kept, but the password is silently truncated at 63 characters.**
Lines: archipelago.h:146, main.c:635, ui.c edit field uses sizeof(password) (ui.c:826-830).
Scenario: a room password longer than 63 bytes is cut; the server answers InvalidPassword and the UI says "Wrong password" with no hint. Not a leak; a usability and diagnosability defect that pushes users to weaker passwords.
Falsifier: set a 100-character `Password =` in zelda3.ini and capture Connect with mitm.py; if the full 100 characters are sent, wrong.
Fix shape: size the settings buffer like the client's (128) and say the limit in the README, or report "password too long" at Connect.

**4. NOTE: hostname check silently skipped if `SSL_set1_host` / `X509_VERIFY_PARAM_set1_ip_asc` fail.**
Lines: ap_tls.c:760, 763. Failure returns 0 (allocation failure or invalid argument); the code ignores it, leaving chain-only verification, which accepts any certificate a trusted CA issued to anyone. I did not make it fail; the mock tests do not cover it.
Falsifier: LD_PRELOAD a stub `SSL_set1_host` returning 0 and connect to a valid cert for another host; if the connection is refused anyway, wrong.
Fix shape: treat a 0 return as kApTls_Failed.

**5. NOTE: out-of-range double to integer casts in server-controlled number handling (undefined behaviour, no memory effect seen).**
Lines: ap_client.c:349 (`GetNum`), 408 (name ids), 800 (`group_members`). UBSan float-cast-overflow reproduces at 800 with `1e300`. On x86 and ARM this yields a wrong number (INT_MIN or saturation); I found no path where it indexes memory, because ids are only compared.
Falsifier: fuzzmsg2 (built with float-cast-overflow) exits clean when 800 is clamped; or find an index use of the result.
Fix shape: clamp with a helper (`d >= -9.2e18 && d <= 9.2e18`) before casting.

**6. NOTE: a server that lists more than 65,535 items for one slot makes the game hand out earlier items again.**
Lines: archipelago.c:224-231 (uint16 counter), 417 (`SetItemCount(idx + 1)`), 874-897 (`have` read back from two bytes, `ev[2..3]` carry only 16 bits). After index 65535 the counter wraps to 0 and `have < RoomItemCount()` holds again. Real ALTTP slots hold hundreds of items; only a hostile server gets there, and a hostile server can already grant items, so impact is duplicates of progressive items. Not run.
Falsifier: with Ap_TestGiveItem fill 65,537 entries and watch ItemCount; if it stops at 65,535, wrong.
Fix shape: stop at 65,535 (see finding 2).

**7. NOTE: debug output (`ZELDA3_AP_DEBUG`) prints the password and raw server bytes.**
Lines: ap_client.c:331, 1011. Opt-in via environment variable. Server text with terminal escape sequences reaches the user's terminal unsanitized (rawsrv.py closectl showed ESC survives into the error string; the Dbg path prints it raw). Not in logs by default.
Falsifier: run clientdrv with `ZELDA3_AP_DEBUG=1` and grep stderr for the password; absent means wrong.
Fix shape: mask the `password` value and pass `Dbg` output through `Sanitize`.

**8. NOTE: weak randomness for WebSocket masking and key.**
Lines: ap_ws.c:211-214, 604-606. xorshift32 seeded from clock, time and a pointer. RFC 6455 masking exists to defeat cache-poisoning proxies; with wss it is irrelevant, with ws:// a proxy-aware attacker could predict masks. The `uuid` sent to AP (ap_client.c:1299 MakeUuid) is not an authentication secret.
Falsifier: none practical beyond showing the seed entropy; low value.
Fix shape: read `/dev/urandom` or `SDL_GetPerformanceCounter` mixes plus `getrandom`/`BCryptGenRandom` once per connection.

**9. NOTE: apworld build follows file symlinks.**
Lines: build_apworld.py:46-52. A symlink dropped into archipelago/fsotp is packed into a file the user then double-clicks into Archipelago; needs repo write access first, so it is only a hygiene issue. Verified in scratch.
Fix shape: skip `os.path.islink(path)` and refuse paths whose realpath leaves SRC.

**10. NOTE: test scripts expose a real Archipelago server on all interfaces.**
Lines: run_real_server_test.sh:48 (`--host 0.0.0.0`, port 38511 for a minute or so, no password); e2e.sh (host not given, not checked). The test room holds only ChecksFinder test slots, so exposure is a nuisance, not a leak.
Fix shape: pass `--host 127.0.0.1`.

**11. NOTE: Windows TLS path only read, not run.**
Revocation is not checked (ap_tls.c:380) and TLS 1.3 is not offered (504). If `MultiByteToWideChar` fails the policy gets an empty server name (ap_tls.c:398); with flags 0 it should not fail for host up to 255 bytes, but I could not test what Windows does with an empty name. Falsifier: run the wine-based scenario in run_client_tests.sh with a host that has invalid UTF-8.

## What holds

- TLS: certificate chain and hostname are verified on both platforms before any application bytes (OpenSSL aborts the handshake; Windows checks in `CheckCertificate` before returning from `ApTls_Connect`). Tested rejection of self-signed and hostname-mismatch certificates. TLS 1.2 minimum. No fallback from wss to ws (ap_client.c:1064-1090, tracked by `preferred_tls`, reset on Start at ap_client.c:1345). `IsArchipelagoGg` matches only the exact host or a dotted suffix (ap_client.c:1053-1055); `archipelago.gg.` (trailing dot) would not match and would try ws:// first, a trivial typo case.
- Frame parser: every length is bounded before allocation, shifts and additions are done in 64-bit or size_t, no integer overflow found, continuation and control rules enforced, resource use bounded in time (0.25 to 1.1 s for bombs and floods). stb zlib decoder survived 400,000 mutated streams.
- JSON: unmodified cJSON 1.7.19 with a 1000-deep nesting limit; stack use is safe at 256 KiB; 900,000 fuzzed messages through the real handlers were clean under ASan/UBSan.
- Server data never indexes arrays, builds paths, runs commands or becomes a format string. On-screen text is printable ASCII only. Items are filtered by whitelist before they reach `Link_ReceiveItem`.
- ParseAddress (ap_client.c:1212-1306) strips whitespace and quotes, rejects `@` in the authority and unknown schemes, and caps host and path at 255 bytes; the HTTP request cannot be split with CR/LF and `req[1024]` cannot overflow.
- Password is not written to zelda3.coop.ini and not placed in crash dumps; the README's "never saved" statement is true. The zelda3.ini comment "The menu never writes this file" is true (only zelda3.coop.ini is opened for writing, ui.c:2816).
- Python apworld: pure data and logic, no I/O, no network, no dynamic execution. The mock server listens on 127.0.0.1 unless told otherwise.

## Not checked

Netplay carriage of Archipelago events and notices to other players (session.c, `Session_ShareNotice`, `SetRoomBytes` bounds); Schannel record handling under hostile input (read only); macOS library search behaviour for the bare `libssl.3.dylib` names; the C test driver archipelago/tests/ap_client_test.c; `MultiServer.py`'s default bind address; the full game running with an AP save.
