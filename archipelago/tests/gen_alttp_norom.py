"""Generates a multiworld with Archipelago's Generate.py, with the "A Link to
the Past" world's ROM output switched off: the game plays that world without
a patched ROM, and the test machine has no Japanese ROM to patch. (The
multidata the server needs is complete without it.)

    python gen_alttp_norom.py <Archipelago source tree> [Generate.py arguments]
"""
import os
import sys

ap_root = sys.argv[1]
os.chdir(ap_root)
sys.path.insert(0, ap_root)
sys.argv = [sys.argv[0]] + sys.argv[2:]
import worlds.alttp as alttp  # noqa: E402


def no_rom_output(self, output_directory):
    self.rom_name = bytearray(b'AP000_0_00000000000\0')
    self.rom_name_available_event.set()


alttp.ALTTPWorld.generate_output = no_rom_output
alttp.ALTTPWorld.stage_assert_generate = classmethod(lambda cls, multiworld: None)  # (asks for the ROM)
import Generate  # noqa: E402
from Main import main as generate  # noqa: E402

erargs, seed = Generate.main()
generate(erargs, seed)
