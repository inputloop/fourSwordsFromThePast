import ssl, socket, sys
cert,key,port=sys.argv[1],sys.argv[2],int(sys.argv[3])
ctx=ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER); ctx.load_cert_chain(cert,key)
s=socket.socket(); s.setsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR,1); s.bind(("127.0.0.1",port)); s.listen(1)
c,_=s.accept()
try:
    t=ctx.wrap_socket(c,server_side=True); print("TLS established; got", t.recv(100)[:20])
except Exception as e: print("server saw handshake fail:", e)
