# Design: the room key (HARDEN H-026), an optional layer over the room code

Status: designed 2026-10-19 after Corwin and Bryant said yes to it as an optional layer; not built.
Companion layer, built the same day: the install id ban (H-019, `InstallId` and `BanByInstallId`).

## The problem it solves

The room code is the host's address and port with a typo check. It was never a secret, and the doc
says so. Everything a stranger needs to join, to draw a 0.5 MB snapshot per Hello (H-003), to take a
parked seat by name (H-004) or to come back after being removed (H-019 before the install id) follows
from knowing the address. A key makes the address public and the game private.

## The layers, and what each side may turn off

| layer | host setting | client setting | what it buys | what turning it off costs |
|---|---|---|---|---|
| address + name (today) | always | always | nothing against a stranger who has the code | nothing |
| install id ban | `BanByInstallId = 1` (default) | `InstallId =` (made on first run) or `off` | a removed install stays removed across restarts and names | host: a new name gets back in; client: cannot join hosts that ban by id |
| room key | `RequireRoomKey = 1` (default once built) | none: the key is typed with the code | nobody without the key gets a Welcome, a snapshot or a seat | host: back to the row above; the code alone admits anyone |

A host with every layer off is today's game. A client with every layer off joins hosts that ask for
nothing. Neither side's choice is hidden: the Reject text says which layer refused.

## Shape

- **Key.** 40 bits from `RandomId64()`, made when hosting starts, new every time the host presses
  *Host a game* (a rotation is "remove everyone who is not here").
- **Shown.** As eight more characters on the room code: `FW00-00DS-EB52` becomes
  `FW00-00DS-EB52-K7QM-2XRA`. The code's existing alphabet and typo check cover the key too (the check
  is recomputed over code plus key). The join field already accepts lower case and missing dashes; a code
  without the key half is a code for a host that does not require one.
- **Wire.** Hello carries the key (5 bytes, zero when none) after the install id; wire version 23. The
  host compares in constant time and answers a wrong key with a Reject that says "This game needs its
  room key (the longer code the host sees)". No snapshot, no client slot, no Frames: the reflection in
  H-003 ends with the check, since a Reject is one packet of under 64 bytes.
- **Reconnect.** The token stays; the key is sent again on every Hello, so a reconnecting client needs
  what it already typed. A seat by name (the H-004 path) needs the key as well, which closes the
  stranger-takes-a-parked-seat case without touching the name logic.
- **Discover.** Unchanged: the LAN list shows the game and its name; joining from the list asks for the
  key when the host requires one.
- **What it is not.** Not encryption: packets after Hello are still readable on the path, and a peer on
  the same path can still spoof a Bye. That is the next layer (a per-session MAC), and it is written as
  H-004's fix shape, not here.

## Falsifiers for the build

1. `h1 amp` with `RequireRoomKey` on and a Hello without the key: total bytes to the source under 3x
   the request.
2. `h1 steal` with the key required: a stranger with the name and no key gets `Welcome=0`.
3. A code with a wrong key half fails the typo check before any packet is sent (the decode test in
   `tests/` already covers the short code; add the long one).
4. A host with `RequireRoomKey = 0` shows the short code and admits a Hello with key 0 and with any key.

## What the doc will say

One paragraph under *Host*: the longer code is the key; give it to the people you want in. Under
"What leaves your computer": the key travels in your Hello, to the host only. The `zelda3.ini`
comment names the setting and what turning it off means.
