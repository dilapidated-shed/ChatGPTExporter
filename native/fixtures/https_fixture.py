#!/usr/bin/env python3
"""Local-only TLS fixture: synthetic IDs/tokens, never any ChatGPT traffic."""
import json
import ssl
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlparse

TOKEN = "synthetic-fixture-token-123456"
HOST = "127.0.0.1"
PORT = 18443


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, format, *args):
        # Never print request headers, URL IDs, payloads, or auth values.
        return

    def respond(self, code, data=b"", extra=None):
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("Connection", "close")
        for key, value in (extra or {}).items():
            self.send_header(key, value)
        self.end_headers()
        if self.command != "HEAD" and data:
            self.wfile.write(data)
            self.wfile.flush()
        self.close_connection = True

    def authorized(self):
        return (
            self.headers.get("Authorization") == "Bearer " + TOKEN
            and self.headers.get("X-Authorization") == "Bearer " + TOKEN
            and self.headers.get("ChatGPT-Account-Id") == "account_A"
        )

    def do_GET(self):
        if not self.authorized():
            self.respond(401, b'{"error":"unauthorized"}')
            return
        path = urlparse(self.path)
        if self.path == "/backend-api/conversations?offset=0&limit=2&order=updated":
            self.respond(200, b'{"items":[],"total":0}')
        elif path.path == "/backend-api/share/oversized":
            self.respond(200, b'{"data":"' + b"x" * 4096 + b'"}')
        elif path.path == "/backend-api/share/redirect":
            self.respond(302, b"", {"Location": "https://example.org/steal"})
        elif path.path == "/backend-api/share/auth":
            self.respond(401, b'{"error":"auth"}')
        elif path.path == "/backend-api/share/forbid":
            self.respond(403, b'{"error":"forbidden"}')
        elif path.path == "/backend-api/share/rate":
            self.respond(429, b'{"error":"rate"}', {"Retry-After": "2"})
        elif path.path == "/backend-api/share/server-error":
            self.respond(503, b'{"error":"upstream"}')
        elif path.path == "/backend-api/share/echo-token":
            self.respond(200, b'{"token":"' + TOKEN.encode() + b'"}')
        else:
            self.respond(404, b'{"error":"unrecognized-test-route"}')

    def do_POST(self):
        if not self.authorized():
            self.respond(401, b'{"error":"unauthorized"}')
            return
        if self.path != "/backend-api/conversations/batch":
            self.respond(404, b"{}")
            return
        try:
            length = int(self.headers.get("Content-Length", "-1"))
            if not 0 <= length <= 4096:
                raise ValueError("bad length")
            obj = json.loads(self.rfile.read(length))
            assert obj == {"conversation_ids": ["one", "two"]}
            assert self.headers.get("Content-Type") == "application/json"
        except (ValueError, AssertionError, json.JSONDecodeError):
            self.respond(400, b'{"error":"bad-batch"}')
            return
        self.respond(200, b'{"batch":true}')


def main():
    if len(sys.argv) != 3:
        raise SystemExit("fixture requires ephemeral certificate/key paths")
    certificate, key = (Path(item) for item in sys.argv[1:])
    tls = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    tls.load_cert_chain(str(certificate), str(key))
    server = ThreadingHTTPServer((HOST, PORT), Handler)
    server.socket = tls.wrap_socket(server.socket, server_side=True)
    server.serve_forever(poll_interval=0.1)


if __name__ == "__main__":
    main()
