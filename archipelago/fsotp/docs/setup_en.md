# Four Swords of the Past Setup Guide

## What you need

- Four Swords of the Past, a build with Archipelago support, set up with
  your own ROM of *A Link to the Past* (USA) as its README describes.
- [Archipelago](https://github.com/ArchipelagoMW/Archipelago/releases) 0.6.4
  or later, to make the multiworld.
- `fsotp.apworld`, the Archipelago world for this game. If your download of
  the game has none, build it from the game's sources with
  `python3 archipelago/build_apworld.py` (it only needs Python 3).

## Installing the world

Open `fsotp.apworld` with the Archipelago Launcher (double-click it, or use
*Install APWorld* in the Launcher), or copy it into the `custom_worlds` folder
of your Archipelago install. Restart the Launcher afterwards.

## Making your YAML

Your options live in a YAML file. Either start from
`Four Swords of the Past.yaml`, which comes with the game's sources (in the
`archipelago` folder), or use *Generate Template Options* in the Launcher and
take `Four Swords of the Past.yaml` from the `Players/Templates` folder.

Set `name` to your slot name: you type it into the game to connect, so keep
it short (at most 16 characters). The options are:

- `goal`: `ganon`, the only goal for now.
- `death_link`: `true` or `false`.
- The options every Archipelago game has, like `progression_balancing`,
  `accessibility`, `local_items` and `start_inventory`.

A co-op team is one player: the team makes one YAML and plays one slot.

## Making the multiworld

This world isn't part of Archipelago itself, so the multiworld has to be
generated on a computer with the world installed:

1. Put everyone's YAML files into the `Players` folder of your Archipelago
   install.
2. Run *Generate* from the Launcher. The multiworld lands in the `output`
   folder as `AP_<number>.zip`.
3. Host it: upload the zip at
   [archipelago.gg/uploads](https://archipelago.gg/uploads) and create a room,
   or run *Host* from the Launcher and pick the zip.

## Connecting the game

1. Start Four Swords of the Past and open the menu (`Esc`, the controller's
   Guide button, or Back+Start).
2. Go to *Multiplayer > Archipelago*.
3. Enter the server address (for example `archipelago.gg:38281`, the address
   the room page shows), your slot name exactly as in your YAML (capitals
   count, even though the menu shows everything in capitals) and the
   password if the room has one, and connect. The first line of the screen
   says when you are connected.
4. Now make a **new** save file: while the game is connected, new files are
   files of that room (the file select screen says "New files: Archipelago
   (this room)").

Once connected, the items found in the multiworld for you start arriving,
and every location you check is sent to the server. The save file you play
belongs to that slot: to continue another day, connect to the same room
again first and then load the same file. Without a connection the file can
still be played; what you check is sent when you connect again.

If the game says the room is for another version, the world that generated
the multiworld and the game don't match: update both to the same release.

## Playing in co-op

Play co-op as usual (see the game's MULTIPLAYER.md): couch players and
online players together are one team, with one shared inventory, playing the
one slot of the team's YAML. Online, only the host connects to the
Archipelago room; the players who join the host's game get the items at the
same moment and their checks count like the host's.
