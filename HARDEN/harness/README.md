# HARDEN harnesses

The programs the round-1 reviewers built to reproduce their findings, kept so the falsifiers in
`../../HARDEN.md` can be run again. Each includes the real source it tests and stubs the rest; build from
the repository root with `-I .` so `src/...` and `third_party/...` resolve, and with the sanitizers on.
None of them needs SDL, a ROM, or the network beyond loopback.

    R=.                                  # repository root
    H=HARDEN/harness
    # netplay (H-002, H-003, H-004, H-019): includes src/session.c
    gcc -g -fsanitize=address -I $H/stub -I $R $H/h1.c -o h1 -Wl,--unresolved-symbols=ignore-all
    ./h1 zero; ./h1 wrap; ./h1 amp; ./h1 kick; ./h1 steal
    # BPS patch (H-005)
    gcc -g -fsanitize=address -iquote src -I $R $H/bps_h.c src/util.c -o bps_h && ./bps_h
    # file-select loop (H-008), whole-file read (H-022)
    gcc -g -fsanitize=address $H/health_h.c -o health_h && ./health_h
    gcc -g -fsanitize=address -iquote src -I $R $H/rwf_h.c src/util.c -o rwf_h && ./rwf_h
    # Archipelago (H-010, H-011, H-022): link the SDL shim
    gcc -g -fsanitize=address,undefined -I $H/shim -I $R $H/amp.c $H/shim/sdl_shim.c -o amp && ./amp
    gcc -g -fsanitize=address,undefined -I $H/shim -I $R $H/fuzzmsg.c $H/shim/sdl_shim.c -o fuzzmsg && ./fuzzmsg
    gcc -g -fsanitize=address,undefined -I $H/shim -I $R $H/clientdrv.c src/ap_client.c src/ap_ws.c src/ap_tls.c src/ap_json.c $H/shim/sdl_shim.c -o clientdrv -ldl
    python3 $H/mitm.py &  ./clientdrv 127.0.0.1:39200       # the plaintext room that reads the password
    python3 $H/rawsrv.py & ; gcc ... $H/frames.c ...        # raw WebSocket frames: lengths, control frames, bombs

`fmt.py` and `fmt3.py` list every printf-family call whose format is not a literal (the format-string
sweep). The exact flags each reviewer used are at the top of the matching report in `..`.

Expected after the round-1 fixes: `h1 zero` and `h1 wrap` print survived; `bps_h` prints `returned (nil)`;
`h1 amp`, `h1 kick` and `h1 steal` still show the open findings H-003, H-019 and H-004.
