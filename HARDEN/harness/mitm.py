import socket, hashlib, base64, struct, json, sys, time
port=int(sys.argv[1])
s=socket.socket(); s.setsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR,1); s.bind(("127.0.0.1",port)); s.listen(5)
def fr(t): d=t.encode(); n=len(d); return (bytes([0x81,n]) if n<126 else bytes([0x81,126])+struct.pack(">H",n))+d
def rd(c):
    h=c.recv(2); l=h[1]&127
    if l==126: l=struct.unpack(">H",c.recv(2))[0]
    m=c.recv(4); d=b""
    while len(d)<l: d+=c.recv(l-len(d))
    return bytes(b^m[i%4] for i,b in enumerate(d)).decode()
c,_=s.accept(); c.settimeout(5)
req=b""
while b"\r\n\r\n" not in req: req+=c.recv(4096)
print("FIRST BYTES ON WIRE (plaintext HTTP):", req.split(b"\r\n")[0].decode())
key=[l.split(b":",1)[1].strip() for l in req.split(b"\r\n") if l.lower().startswith(b"sec-websocket-key")][0]
acc=base64.b64encode(hashlib.sha1(key+b"258EAFA5-E914-47DA-95CA-C5AB0DC85B11").digest())
c.sendall(b"HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: "+acc+b"\r\n\r\n")
c.sendall(fr(json.dumps([{"cmd":"RoomInfo","seed_name":"s","games":["A Link to the Past"],"datapackage_checksums":{}}])))
print("CLIENT SENT:", rd(c))
c.sendall(fr(json.dumps([{"cmd":"Connected","team":0,"slot":1,"players":[],"slot_info":{"1":{"name":"Link","game":"A Link to the Past"}},"slot_data":{}},
  {"cmd":"ReceivedItems","index":0,"items":[{"item":59,"location":1,"player":0},{"item":18,"location":2,"player":0}]},
  {"cmd":"PrintJSON","type":"Chat","data":[{"text":"injected \x1b[2J chat line"}]}])))
time.sleep(2)
