"""A small stand-in for Archipelago's MultiServer, for testing the game's C
client (src/ap_client.c) through archipelago/tests/ap_client_test.c.

It speaks enough of the protocol for one room with three players and checks
what the client sends as strictly as the real server does (plus the things
our client promises: items_handling 7, version 0.6.9, tags, a stable uuid).
Problems are reported to the client as chat lines starting with
"MOCK ERROR", which make the test driver fail; "MOCK OK ..." lines mark the
steps of a scenario.

    python mock_server.py --scenario basic [--tls CERT KEY] [--no-compress]

Prints "READY <port>" once it listens. Needs websockets 13 (the version
Archipelago uses).
"""
import argparse
import asyncio
import json
import ssl
import sys

import websockets
from websockets.extensions.permessage_deflate import PerMessageDeflate, ServerPerMessageDeflateFactory

BASE = 0x46530000
GAME = "Four Swords of the Past"
OTHER = "Other Game"
BIG = "Big Game"
SEED = "MockSeed123"

FSOTP_ITEMS = {"Progressive Sword": BASE + 0x49, "Hookshot": BASE + 0x0A, "Lamp": BASE + 0x12,
               "Bow": BASE + 0x0B, "Hammer": BASE + 0x09, "Rupees (20)": BASE + 0x36}
FSOTP_LOCATIONS = {"Link's House": BASE + 0, "Sanctuary": BASE + 1, "Eastern Palace - Compass Chest": BASE + 2,
                   "Kakariko Well - Top": BASE + 3, "Link's Uncle": BASE + 0x100}
OTHER_ITEMS = {"Alice's Hat": 1000, "Rubber Duck": 1001}
OTHER_LOCATIONS = {"Alice's Chest": 2000, "Bob's Shed": 2001, "Haunted Well": 2002}
AP_ITEMS = {"Nothing": -1}
AP_LOCATIONS = {"Cheat Console": -1, "Server": -2}

PROG, USEFUL, TRAP = 1, 2, 4


def net_item(item, location, player, flags=0):
    return {"item": item, "location": location, "player": player, "flags": flags, "class": "NetworkItem"}


def big_package():
    # A few MB of names, like a large game's data package.
    items = {f"Big Item number {i:05d} with a rather long name to take up room": 500000 + i for i in range(30000)}
    locations = {f"Big Location number {i:05d} somewhere in a very large world map": 600000 + i for i in range(30000)}
    return {"item_name_to_id": items, "location_name_to_id": locations, "checksum": "big0001"}


class Room:
    def __init__(self, args):
        self.args = args
        self.scenario = args.scenario
        self.games = {GAME: {"item_name_to_id": FSOTP_ITEMS, "location_name_to_id": FSOTP_LOCATIONS, "checksum": "fsotp01"},
                      OTHER: {"item_name_to_id": OTHER_ITEMS, "location_name_to_id": OTHER_LOCATIONS, "checksum": "other01"},
                      "Archipelago": {"item_name_to_id": AP_ITEMS, "location_name_to_id": AP_LOCATIONS, "checksum": "ap01"}}
        alice_game = OTHER
        if self.scenario.startswith("bigdp"):
            self.games[BIG] = big_package()
            alice_game = BIG
        # slot: (name, game, type, members)
        self.slots = {1: ("Link", GAME, 1, []), 2: ("Alice", alice_game, 1, []), 3: ("Bob", GAME, 1, []),
                      4: ("Team Link", GAME, 2, [1, 3])}
        # What the server holds for Link: the start inventory and an item from Alice.
        self.items = [net_item(BASE + 0x49, -2, 0, PROG), net_item(BASE + 0x0A, 2000, 2, PROG)]
        # Link's locations: item, receiver, flags.
        self.placement = {BASE + 1: (BASE + 0x12, 1, PROG), BASE + 2: (BASE + 0x0B, 3, PROG),
                          BASE + 3: (1001, 2, TRAP), BASE + 0: (BASE + 0x36, 1, 0)}
        self.checked = set()
        self.connections = 0
        self.uuid = None
        self.datapackage_requests = 0
        self.goal_count = 0

    def room_info(self):
        games = sorted(set(g for (_, g, _, _) in self.slots.values()))
        return {"cmd": "RoomInfo", "password": bool(self.args.password), "games": games, "tags": ["AP"],
                "version": {"major": 0, "minor": 6, "build": 9, "class": "Version"},
                "generator_version": {"major": 0, "minor": 6, "build": 9, "class": "Version"},
                "permissions": {"release": 2, "remaining": 2, "collect": 2}, "hint_cost": 10,
                "location_check_points": 1, "seed_name": SEED,
                "datapackage_checksums": {g: d["checksum"] for g, d in self.games.items()}, "time": 1.0}


class Client:
    def __init__(self, room, ws):
        self.room = room
        self.ws = ws
        self.tags = []
        self.slot = None
        self.number = room.connections
        self.after_resend = set()
        self.goal_again = False

    async def send(self, *msgs):
        await self.ws.send(json.dumps(list(msgs)))

    async def error(self, text):
        print(f"MOCK ERROR: {text}", file=sys.stderr, flush=True)
        await self.chat(f"MOCK ERROR: {text}")

    async def chat(self, text):
        await self.send({"cmd": "PrintJSON", "type": "ServerChat", "message": text, "data": [{"text": text}]})

    async def run(self):
        room = self.room
        await self.send(room.room_info())
        async for data in self.ws:
            try:
                msgs = json.loads(data)
            except ValueError:
                await self.error("not JSON")
                continue
            if not isinstance(msgs, list):
                await self.error("not a list of commands")
                continue
            for msg in msgs:
                await self.handle(msg)

    async def handle(self, msg):
        room = self.room
        cmd = msg.get("cmd")
        if cmd == "GetDataPackage":
            room.datapackage_requests += 1
            wanted = msg.get("games")
            allowed = set(room.room_info()["games"]) | {"Archipelago"}
            if not isinstance(wanted, list) or not set(wanted) <= allowed:
                await self.error(f"GetDataPackage for games not in the room: {wanted}")
                return
            if room.scenario == "reconnect" and self.number > 1:
                await self.error("data package fetched again although the checksums didn't change")
            await self.send({"cmd": "DataPackage", "data": {"games": {g: room.games[g] for g in wanted}}})
        elif cmd == "Connect":
            await self.connect(msg)
        elif self.slot is None:
            await self.error(f"{cmd} before Connect")
        elif cmd == "LocationChecks":
            await self.location_checks(msg)
        elif cmd == "StatusUpdate":
            if msg.get("status") != 30:
                await self.error(f"unexpected status {msg.get('status')}")
                return
            room.goal_count += 1
            await self.send({"cmd": "PrintJSON", "type": "Goal", "team": 0, "slot": 1,
                             "data": [{"text": "Link (Team #1) has completed their goal."}]})
            if room.scenario == "reconnect" and self.number > 1:
                self.goal_again = True
                await self.check_resend()
            await self.maybe_drop()
        elif cmd == "Sync":
            if room.scenario == "gap" and len(room.items) == 2:
                room.items.append(net_item(BASE + 0x12, 2001, 3, PROG))  # what the gap hid
            await self.send({"cmd": "ReceivedItems", "index": 0, "items": room.items})
            if room.scenario == "gap":
                await self.chat("MOCK OK sync")
        elif cmd == "ConnectUpdate":
            self.tags = msg.get("tags", self.tags)
            if not isinstance(self.tags, list) or "AP" not in self.tags:
                await self.error(f"bad tags in ConnectUpdate: {self.tags}")
            await self.chat("MOCK OK tags " + ",".join(self.tags))
            if room.scenario == "deathlink" and "DeathLink" not in self.tags:
                # The real server wouldn't send this any more; the client must ignore it anyway.
                await self.send({"cmd": "Bounced", "tags": ["DeathLink"],
                                 "data": {"time": 1700000500.25, "source": "Bob", "cause": "too late"}})
                await self.chat("MOCK OK after")
        elif cmd == "Bounce":
            await self.bounce(msg)
        else:
            await self.error(f"unexpected command {cmd}")

    async def connect(self, msg):
        room = self.room
        errors = []
        for key in ("password", "game", "name", "uuid", "version", "items_handling", "tags", "slot_data"):
            if key not in msg:
                await self.error(f"Connect without {key}")
                return
        if msg["password"] is not None and not isinstance(msg["password"], str):
            await self.error("password must be a string or null")
        if msg["version"] != {"major": 0, "minor": 6, "build": 9, "class": "Version"}:
            await self.error(f"bad version {msg['version']}")
        if msg["items_handling"] != 7:
            await self.error(f"items_handling {msg['items_handling']}")
        if not isinstance(msg["tags"], list) or "AP" not in msg["tags"]:
            await self.error(f"tags {msg['tags']}")
        if msg["slot_data"] is not True:
            await self.error("slot_data not requested")
        if not isinstance(msg["uuid"], str) or not msg["uuid"]:
            await self.error("no uuid")
        elif room.uuid and room.uuid != msg["uuid"]:
            await self.error("the uuid changed between connections")
        room.uuid = msg["uuid"]
        deflate = any(isinstance(e, PerMessageDeflate) for e in self.ws.extensions)
        if deflate == self.room.args.no_compress:
            await self.error(f"compression is {'on' if deflate else 'off'}")
        if room.args.password and msg["password"] != room.args.password:
            errors.append("InvalidPassword")
        special = {"VersionTest": "IncompatibleVersion", "HandlingTest": "InvalidItemsHandling",
                   "UnknownTest": "SomethingNew"}
        names = {name: slot for slot, (name, _, _, _) in room.slots.items()}
        if msg["name"] in special:
            errors.append(special[msg["name"]])
        elif msg["name"] not in names:
            errors.append("InvalidSlot")
        elif room.slots[names[msg["name"]]][1] != msg["game"]:
            errors.append("InvalidGame")
        if errors:
            await self.send({"cmd": "ConnectionRefused", "errors": errors})
            return
        self.slot = names[msg["name"]]
        self.tags = msg["tags"]
        if self.slot != 1:
            await self.error("only Link can play here")
            return
        players = [{"team": 0, "slot": s, "alias": n, "name": n, "class": "NetworkPlayer"}
                   for s, (n, _, t, _) in room.slots.items() if t == 1]
        slot_info = {str(s): {"name": n, "game": g, "type": t, "group_members": m, "class": "NetworkSlot"}
                     for s, (n, g, t, m) in room.slots.items()}
        connected = {"cmd": "Connected", "team": 0, "slot": 1, "players": players,
                     "missing_locations": sorted(set(room.placement) - room.checked),
                     "checked_locations": sorted(room.checked), "slot_info": slot_info, "hint_points": 0,
                     "slot_data": {"version": 1, "death_link": 1 if room.scenario == "deathlink" else 0, "goal": 0}}
        if room.scenario == "reconnect" and self.number > 1:
            room.items.append(net_item(BASE + 0x09, 2001, 3, USEFUL))  # Bob found Link's Hammer meanwhile
        reply = [connected]
        if room.items:
            reply.append({"cmd": "ReceivedItems", "index": 0, "items": room.items})
        await self.send(*reply)
        await self.after_connect()

    async def after_connect(self):
        room = self.room
        sc = room.scenario
        if sc in ("basic", "tls", "nocompress"):
            # A message in three fragments.
            text = json.dumps([{"cmd": "PrintJSON", "type": "ItemSend", "receiving": 1,
                                "item": net_item(BASE + 0x0A, 2000, 2, PROG),
                                "data": [{"text": "2", "type": "player_id"}, {"text": " sent "},
                                         {"text": str(BASE + 0x0A), "type": "item_id", "player": 1, "flags": 1},
                                         {"text": " to "}, {"text": "1", "type": "player_id"}]}])
            third = len(text) // 3
            await self.ws.send([text[:third], text[third:2 * third], text[2 * third:]])
            await self.send(
                # Not about us: hidden.
                {"cmd": "PrintJSON", "type": "ItemSend", "receiving": 3, "item": net_item(BASE + 0x12, 2001, 2, PROG),
                 "data": [{"text": "Alice sent Lamp to Bob"}]},
                # To our item link group.
                {"cmd": "PrintJSON", "type": "ItemSend", "receiving": 4, "item": net_item(BASE + 0x36, 2002, 2, 0),
                 "data": [{"text": "Alice sent Rupees (20) to Team Link"}]},
                {"cmd": "PrintJSON", "type": "Hint", "receiving": 1, "found": False,
                 "item": net_item(BASE + 0x09, 2002, 2, USEFUL),
                 "data": [{"text": "[Hint]: "}, {"text": "1", "type": "player_id"}, {"text": "'s "},
                          {"text": str(BASE + 0x09), "type": "item_id", "player": 1, "flags": USEFUL},
                          {"text": " is at "}, {"text": "2002", "type": "location_id", "player": 2},
                          {"text": " in "}, {"text": "2", "type": "player_id"}, {"text": "'s World"},
                          {"text": ". "}, {"text": "(unspecified)", "type": "hint_status", "hint_status": 0}]},
                {"cmd": "PrintJSON", "type": "ServerChat", "message": "Grüße ✓",
                 "data": [{"text": "[Server]: Grüße ✓"}]},
                {"cmd": "PrintJSON", "type": "Chat", "team": 0, "slot": 2, "message": "hello",
                 "data": [{"text": "Alice: hello"}]},
                {"cmd": "PrintJSON", "type": "Join", "data": [{"text": "Link (Team #1) playing ... has joined."}]},
                {"cmd": "PrintJSON", "type": "Tutorial", "data": [{"text": "Now that you are connected ..."}]})
            await self.chat("MOCK OK texts")
        elif sc == "gap":
            await self.send({"cmd": "ReceivedItems", "index": 5, "items": [net_item(BASE + 0x0B, 2001, 2, PROG)]})
        elif sc.startswith("bigdp"):
            await self.send(
                {"cmd": "PrintJSON", "type": "ItemSend", "receiving": 1, "item": net_item(BASE + 0x12, 612345, 2, PROG),
                 "data": [{"text": "Alice sent Lamp to Link"}]},
                {"cmd": "PrintJSON", "type": "ItemSend", "receiving": 2, "item": net_item(529999, BASE + 3, 1, 0),
                 "data": [{"text": "Link sent a big item to Alice"}]})
            await self.chat("MOCK OK big")
        elif sc == "flood":
            await self.send(*[{"cmd": "PrintJSON", "type": "ServerChat", "message": f"line {i}",
                               "data": [{"text": f"line {i}"}]} for i in range(50)])
        elif sc == "ping":
            await asyncio.sleep(5)
            await self.chat("MOCK OK pings answered")

    async def location_checks(self, msg):
        room = self.room
        locations = msg.get("locations")
        if not isinstance(locations, list) or not all(isinstance(x, int) for x in locations):
            await self.error(f"bad LocationChecks {locations}")
            return
        new = [x for x in locations if x not in room.checked]
        texts = []
        for loc in sorted(new):
            room.checked.add(loc)
            if loc not in room.placement:
                await self.error(f"unknown location {loc}")
                continue
            item, receiver, flags = room.placement[loc]
            if receiver == 1:
                room.items.append(net_item(item, loc, 1, flags))
                texts.append({"cmd": "PrintJSON", "type": "ItemSend", "receiving": 1,
                              "item": net_item(item, loc, 1, flags), "data": [{"text": "Link found their item"}]})
            else:
                texts.append({"cmd": "PrintJSON", "type": "ItemSend", "receiving": receiver,
                              "item": net_item(item, loc, 1, flags), "data": [{"text": "Link sent an item"}]})
        if texts:
            await self.send(*texts)
        own = [x for x in sorted(new) if x in room.placement and room.placement[x][1] == 1]
        if own:
            start = len(room.items) - len(own)
            await self.send({"cmd": "ReceivedItems", "index": start, "items": room.items[start:]})
        if new:
            await self.send({"cmd": "RoomUpdate", "checked_locations": new, "hint_points": len(room.checked)})
        if room.scenario == "reconnect" and self.number > 1:
            self.after_resend |= set(locations)
            await self.check_resend()
        await self.maybe_drop()

    async def maybe_drop(self):
        room = self.room
        if room.scenario == "reconnect" and self.number == 1 and {BASE + 1, BASE + 2} <= room.checked \
                and room.goal_count:
            await asyncio.sleep(0.2)
            self.ws.transport.abort()  # the connection breaks, no goodbye

    async def check_resend(self):
        if {BASE + 1, BASE + 2} <= self.after_resend and self.goal_again:
            await self.chat("MOCK OK resend")

    async def bounce(self, msg):
        if self.room.scenario != "deathlink":
            await self.error("Bounce in a scenario without DeathLink")
            return
        data = msg.get("data", {})
        if msg.get("tags") != ["DeathLink"] or not isinstance(data.get("time"), float) \
                or data.get("source") != "Link" or not isinstance(data.get("cause"), str):
            await self.error(f"bad DeathLink bounce: {msg}")
            return
        if "DeathLink" not in self.tags:
            await self.error("DeathLink bounce without the tag")
        # Like the server: everyone with the tag gets it, the sender too.
        await self.send(dict(msg, cmd="Bounced"))
        await self.send({"cmd": "Bounced", "tags": ["DeathLink"],
                         "data": {"time": data["time"] + 1.5, "source": "Alice", "cause": "Alice was eaten"}})
        await self.chat("MOCK OK death " + data["cause"])


async def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--scenario", default="basic")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=0)
    parser.add_argument("--tls", nargs=2, metavar=("CERT", "KEY"))
    parser.add_argument("--no-compress", action="store_true")
    parser.add_argument("--password")
    args = parser.parse_args()
    room = Room(args)

    async def handler(ws, path=None):
        room.connections += 1
        client = Client(room, ws)
        try:
            await client.run()
        except websockets.ConnectionClosed:
            pass

    if args.scenario == "hang":
        # Takes the TCP connection and then says nothing at all.
        writers = []

        async def silent(reader, writer):
            writers.append(writer)
            await reader.read()

        server = await asyncio.start_server(silent, args.host, args.port)
    else:
        ssl_context = None
        if args.tls:
            ssl_context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
            ssl_context.load_cert_chain(args.tls[0], args.tls[1])
        kwargs = {"compression": None} if args.no_compress else {"extensions": [ServerPerMessageDeflateFactory(
            server_max_window_bits=11, client_max_window_bits=11, compress_settings={"memLevel": 4})]}
        ping = 1 if args.scenario == "ping" else 20
        server = await websockets.serve(handler, args.host, args.port, ssl=ssl_context, max_size=1 << 20,
                                        ping_interval=ping, ping_timeout=ping, **kwargs)
    port = server.sockets[0].getsockname()[1]
    print(f"READY {port}", flush=True)
    await asyncio.Future()


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        pass
