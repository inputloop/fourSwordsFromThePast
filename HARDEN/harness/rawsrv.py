import socket, sys, hashlib, base64, struct, zlib, time
scen = sys.argv[1]; port = int(sys.argv[2])
s = socket.socket(); s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1); s.bind(("127.0.0.1", port)); s.listen(1)
c, _ = s.accept()
req = b""
while b"\r\n\r\n" not in req: req += c.recv(4096)
key = [l.split(b":",1)[1].strip() for l in req.split(b"\r\n") if l.lower().startswith(b"sec-websocket-key")][0]
acc = base64.b64encode(hashlib.sha1(key + b"258EAFA5-E914-47DA-95CA-C5AB0DC85B11").digest())
ext = b"Sec-WebSocket-Extensions: permessage-deflate; server_no_context_takeover\r\n" if scen.startswith("deflate") else b""
c.sendall(b"HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: "+acc+b"\r\n"+ext+b"\r\n")
def fr(op, data, fin=1, rsv1=0):
    b0 = (fin<<7)|(rsv1<<6)|op; n=len(data)
    h = bytes([b0, n]) if n<126 else bytes([b0,126])+struct.pack(">H",n) if n<65536 else bytes([b0,127])+struct.pack(">Q",n)
    return h+data
if scen == "huge32": c.sendall(bytes([0x81,127])+struct.pack(">Q",0xFFFFFFFF))
elif scen == "huge64": c.sendall(bytes([0x81,127])+b"\xff"*8)
elif scen == "frag": c.sendall(fr(1,b'["a',0)+fr(0,b'b"]',1))
elif scen == "ctrl126": c.sendall(bytes([0x89,126])+struct.pack(">H",126)+b"x"*126)
elif scen == "closectl": c.sendall(fr(8,struct.pack(">H",1008)+b"\x1b[31mboom\x00hidden"))
elif scen == "deflate_bomb":
    co = zlib.compressobj(9, zlib.DEFLATED, -15); d = co.compress(b"[" + b"0,"*(8*1024*1024) + b"0]"); d += co.flush(zlib.Z_SYNC_FLUSH)
    assert d.endswith(b"\x00\x00\xff\xff"); d = d[:-4]
    print("compressed", len(d), "raw", 2*8*1024*1024+2, file=sys.stderr)
    c.sendall(fr(1,d,1,1))
elif scen == "deflate_over":
    co = zlib.compressobj(9, zlib.DEFLATED, -15); d = co.compress(b"A"*(17*1024*1024)); d += co.flush(zlib.Z_SYNC_FLUSH); d=d[:-4]
    c.sendall(fr(1,d,1,1))
elif scen == "tinyfrags":
    c.sendall(fr(1,b"[",0)); 
    blob = fr(0,b" ",0)*200000
    for i in range(10): c.sendall(blob)
    c.sendall(fr(0,b"]",1))
elif scen == "bigtext":
    c.sendall(fr(1,b"["+b"0,"*(8*1024*1024-1)+b"0]"))
time.sleep(2)
