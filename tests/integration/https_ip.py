#!/usr/bin/env python3
"""Test-only HTTPS 'what is my IP' service: answers every GET with a fixed JSON.
Usage: https_ip.py <port> <cert.pem> <key.pem>"""
import http.server
import ssl
import sys


class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        body = b'{"ip":"203.0.113.7","country":"UA","city":"Test","org":"TEST-NET-3"}'
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *args):
        pass


srv = http.server.ThreadingHTTPServer(("127.0.0.1", int(sys.argv[1])), Handler)
ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
ctx.load_cert_chain(sys.argv[2], sys.argv[3])
srv.socket = ctx.wrap_socket(srv.socket, server_side=True)
srv.serve_forever()
