# Minimal SOCKS5 server for tests: optional user/pass auth, logs ATYP + target.
import asyncio, socket, struct, sys

PORT = int(sys.argv[1])
USER = sys.argv[2].encode() if len(sys.argv) > 2 else None
PASS = sys.argv[3].encode() if len(sys.argv) > 3 else None
LOG = sys.argv[4] if len(sys.argv) > 4 else "/dev/stdout"

def log(msg):
    with open(LOG, "a") as f:
        f.write(msg + "\n")

async def pipe(r, w):
    try:
        while True:
            d = await r.read(65536)
            if not d:
                break
            w.write(d)
            await w.drain()
    except Exception:
        pass
    finally:
        try: w.close()
        except Exception: pass

async def handle(r, w):
    try:
        ver, n = await r.readexactly(2)
        methods = await r.readexactly(n)
        if USER:
            if 2 not in methods:
                w.write(b"\x05\xff"); await w.drain(); w.close(); return
            w.write(b"\x05\x02"); await w.drain()
            _, ul = await r.readexactly(2); u = await r.readexactly(ul)
            (pl,) = await r.readexactly(1); p = await r.readexactly(pl)
            if u != USER or p != PASS:
                log("AUTH FAIL"); w.write(b"\x01\x01"); await w.drain(); w.close(); return
            w.write(b"\x01\x00"); await w.drain()
        else:
            w.write(b"\x05\x00"); await w.drain()
        _, cmd, _, atyp = await r.readexactly(4)
        if atyp == 1:
            host = socket.inet_ntoa(await r.readexactly(4))
        elif atyp == 3:
            (l,) = await r.readexactly(1); host = (await r.readexactly(l)).decode()
        else:
            host = socket.inet_ntop(socket.AF_INET6, await r.readexactly(16))
        (port,) = struct.unpack("!H", await r.readexactly(2))
        log(f"CONNECT atyp={atyp} host={host} port={port}")
        try:
            tr, tw = await asyncio.open_connection(host, port)
        except Exception:
            w.write(b"\x05\x05\x00\x01\x00\x00\x00\x00\x00\x00"); await w.drain(); w.close(); return
        w.write(b"\x05\x00\x00\x01\x7f\x00\x00\x01\x00\x00"); await w.drain()
        await asyncio.gather(pipe(r, tw), pipe(tr, w))
    except Exception as e:
        log(f"ERR {e}")

async def main():
    srv = await asyncio.start_server(handle, "127.0.0.1", PORT)
    async with srv:
        await srv.serve_forever()

asyncio.run(main())
