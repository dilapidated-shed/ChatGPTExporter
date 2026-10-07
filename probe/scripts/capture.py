#!/usr/bin/env python3
"""Capture one authenticated chatgpt.com web-API response through CDP.

No third-party Python packages are required.

Examples:
  python capture.py GET '/api/auth/session'
  python capture.py GET '/backend-api/conversations?offset=0&limit=100&order=updated&hide_snorlax=false'
  python capture.py GET '/backend-api/conversation/CONVERSATION_ID'
  python capture.py --account-id ACCOUNT_ID GET '/backend-api/gizmos/snorlax/sidebar?conversations_per_gizmo=0&limit=20&owned_only=false'
"""

from __future__ import annotations

import argparse
import base64
import hashlib
import http.client
import json
import secrets
import socket
import struct
import sys
from datetime import datetime, timezone
from pathlib import Path
from urllib.parse import urlparse


def http_json(host: str, port: int, path: str):
    conn = http.client.HTTPConnection(host, port, timeout=10)
    conn.request("GET", path)
    response = conn.getresponse()
    body = response.read()
    if response.status != 200:
        raise RuntimeError(f"GET {path} -> HTTP {response.status}: {body[:300]!r}")
    return json.loads(body)


def choose_target(targets):
    pages = [target for target in targets if target.get("type") == "page"]
    chatgpt = [target for target in pages if "chatgpt.com" in (target.get("url") or "")]
    choices = chatgpt or pages
    if not choices:
        raise RuntimeError("No page target found. Is ChatGPT Web Probe open?")
    return choices[0]


class WebSocket:
    def __init__(self, url: str):
        parsed = urlparse(url)
        if parsed.scheme != "ws":
            raise RuntimeError(f"Only ws:// DevTools URLs are supported: {url}")
        self.host = parsed.hostname or "127.0.0.1"
        self.port = parsed.port or 80
        self.path = parsed.path or "/"
        if parsed.query:
            self.path += "?" + parsed.query

        self.sock = socket.create_connection((self.host, self.port), timeout=10)
        self.sock.settimeout(60)
        key = base64.b64encode(secrets.token_bytes(16)).decode("ascii")
        request = (
            f"GET {self.path} HTTP/1.1\r\n"
            f"Host: {self.host}:{self.port}\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            f"Sec-WebSocket-Key: {key}\r\n"
            "Sec-WebSocket-Version: 13\r\n\r\n"
        ).encode("ascii")
        self.sock.sendall(request)

        headers = bytearray()
        while b"\r\n\r\n" not in headers:
            chunk = self.sock.recv(4096)
            if not chunk:
                raise RuntimeError("Connection closed during WebSocket handshake")
            headers.extend(chunk)
            if len(headers) > 65536:
                raise RuntimeError("Oversized WebSocket handshake")
        status = bytes(headers).split(b"\r\n", 1)[0]
        if b" 101 " not in status:
            raise RuntimeError(f"WebSocket upgrade failed: {status.decode(errors='replace')}")

    def _exact(self, count: int) -> bytes:
        out = bytearray()
        while len(out) < count:
            chunk = self.sock.recv(count - len(out))
            if not chunk:
                raise RuntimeError("DevTools WebSocket closed unexpectedly")
            out.extend(chunk)
        return bytes(out)

    def send_text(self, text: str):
        payload = text.encode("utf-8")
        mask = secrets.token_bytes(4)
        length = len(payload)
        if length < 126:
            header = bytes([0x81, 0x80 | length])
        elif length < 65536:
            header = bytes([0x81, 0x80 | 126]) + struct.pack("!H", length)
        else:
            header = bytes([0x81, 0x80 | 127]) + struct.pack("!Q", length)
        masked = bytes(byte ^ mask[index % 4] for index, byte in enumerate(payload))
        self.sock.sendall(header + mask + masked)

    def _send_control(self, opcode: int, payload: bytes):
        mask = secrets.token_bytes(4)
        header = bytes([0x80 | opcode, 0x80 | len(payload)])
        masked = bytes(byte ^ mask[index % 4] for index, byte in enumerate(payload))
        self.sock.sendall(header + mask + masked)

    def recv_text(self) -> str:
        fragments = bytearray()
        text_message = False
        while True:
            b1, b2 = self._exact(2)
            fin = bool(b1 & 0x80)
            opcode = b1 & 0x0F
            masked = bool(b2 & 0x80)
            length = b2 & 0x7F
            if length == 126:
                length = struct.unpack("!H", self._exact(2))[0]
            elif length == 127:
                length = struct.unpack("!Q", self._exact(8))[0]
            mask = self._exact(4) if masked else None
            payload = self._exact(length)
            if mask:
                payload = bytes(byte ^ mask[index % 4] for index, byte in enumerate(payload))

            if opcode == 0x8:
                raise RuntimeError("DevTools WebSocket closed")
            if opcode == 0x9:
                self._send_control(0xA, payload)
                continue
            if opcode == 0xA:
                continue
            if opcode == 0x1:
                text_message = True
                fragments.extend(payload)
            elif opcode == 0x0:
                fragments.extend(payload)
            else:
                continue

            if fin:
                if not text_message:
                    raise RuntimeError("Expected a text DevTools frame")
                return fragments.decode("utf-8")

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass


def cdp(ws: WebSocket, method: str, params=None, command_id: int = 1):
    ws.send_text(json.dumps({
        "id": command_id,
        "method": method,
        "params": params or {},
    }, separators=(",", ":")))

    while True:
        message = json.loads(ws.recv_text())
        if message.get("id") != command_id:
            continue
        if "error" in message:
            raise RuntimeError(f"CDP {method} error: {message['error']}")
        return message.get("result", {})


def make_expression(method: str, path: str, body: str | None, account_id: str | None) -> str:
    return f"""
(async () => {{
  const method = {json.dumps(method)};
  const path = {json.dumps(path)};
  const bodyText = {json.dumps(body)};
  const accountId = {json.dumps(account_id)};

  if (!location.hostname.endsWith('chatgpt.com')) {{
    throw new Error('Target page is not on chatgpt.com: ' + location.href);
  }}
  if (!path.startsWith('/')) throw new Error('Path must begin with /');

  const headers = new Headers();
  if (path !== '/api/auth/session') {{
    const sessionResponse = await fetch('/api/auth/session', {{
      credentials: 'include',
      cache: 'no-store',
      redirect: 'error'
    }});
    if (!sessionResponse.ok) {{
      throw new Error('Session request failed: HTTP ' + sessionResponse.status);
    }}
    const session = await sessionResponse.json();
    if (!session.accessToken) throw new Error('Session response had no accessToken');
    headers.set('Authorization', 'Bearer ' + session.accessToken);
    headers.set('X-Authorization', 'Bearer ' + session.accessToken);
    if (accountId) headers.set('ChatGPT-Account-Id', accountId);
  }}
  if (bodyText !== null) headers.set('Content-Type', 'application/json');

  const observedAt = new Date().toISOString();
  const response = await fetch(path, {{
    method,
    credentials: 'include',
    cache: 'no-store',
    redirect: 'error',
    headers,
    body: bodyText === null ? undefined : bodyText
  }});
  const text = await response.text();
  const responseHeaders = {{}};
  for (const [key, value] of response.headers.entries()) responseHeaders[key] = value;

  return JSON.stringify({{
    schemaVersion: 1,
    observedAt,
    pageUrl: location.href,
    userAgent: navigator.userAgent,
    method,
    path,
    accountIdUsed: accountId,
    status: response.status,
    statusText: response.statusText,
    headers: responseHeaders,
    body: text
  }});
}})()
"""


def safe_name(method: str, path: str) -> str:
    digest = hashlib.sha256(f"{method} {path}".encode()).hexdigest()[:10]
    stem = path.strip("/").replace("/", "_") or "root"
    stem = "".join(character if character.isalnum() or character in "._-" else "_"
                   for character in stem)
    return (stem[:80] or "request") + "-" + digest


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=9222)
    parser.add_argument("--out", default="captures")
    parser.add_argument("--account-id")
    parser.add_argument("method")
    parser.add_argument("path")
    parser.add_argument("body", nargs="?")
    args = parser.parse_args()

    method = args.method.upper()
    if not args.path.startswith("/"):
        parser.error("path must begin with /")

    target = choose_target(http_json(args.host, args.port, "/json/list"))
    ws_url = target.get("webSocketDebuggerUrl")
    if not ws_url:
        raise RuntimeError("Selected target has no webSocketDebuggerUrl")

    ws = WebSocket(ws_url)
    try:
        result = cdp(ws, "Runtime.evaluate", {
            "expression": make_expression(method, args.path, args.body, args.account_id),
            "awaitPromise": True,
            "returnByValue": True,
            "userGesture": True,
        }, 1)
    finally:
        ws.close()

    if "exceptionDetails" in result:
        raise RuntimeError(json.dumps(result["exceptionDetails"], indent=2))

    remote = result.get("result", {})
    if remote.get("subtype") == "error":
        raise RuntimeError(json.dumps(remote, indent=2))
    value = remote.get("value")
    if not isinstance(value, str):
        raise RuntimeError(f"Unexpected Runtime.evaluate value: {value!r}")

    record = json.loads(value)
    response_body = record.pop("body")
    if args.path == "/api/auth/session":
        try:
            session_for_disk = json.loads(response_body)
        except json.JSONDecodeError as error:
            raise RuntimeError("session response was not JSON; refusing to write it") from error
        if isinstance(session_for_disk, dict) and isinstance(session_for_disk.get("accessToken"), str):
            session_for_disk["accessToken"] = "<redacted>"
            record["redactions"] = ["accessToken"]
        response_body = json.dumps(session_for_disk, separators=(",", ":"))
    body_bytes = response_body.encode("utf-8")
    record["bodyUtf8Bytes"] = len(body_bytes)
    record["bodySha256"] = hashlib.sha256(body_bytes).hexdigest()
    record["devtoolsTarget"] = {
        "id": target.get("id"),
        "title": target.get("title"),
        "url": target.get("url"),
    }

    if args.body is not None:
        request_bytes = args.body.encode("utf-8")
        record["requestBodyUtf8Bytes"] = len(request_bytes)
        record["requestBodySha256"] = hashlib.sha256(request_bytes).hexdigest()

    timestamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    folder = Path(args.out) / f"{timestamp}-{safe_name(method, args.path)}"
    folder.mkdir(parents=True, exist_ok=False)
    (folder / "metadata.json").write_text(
        json.dumps(record, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    (folder / "body.txt").write_text(response_body, encoding="utf-8")
    if args.body is not None:
        (folder / "request-body.txt").write_text(args.body, encoding="utf-8")

    print(folder)
    print(f"HTTP {record['status']}  {record['bodyUtf8Bytes']} UTF-8 bytes")
    print(f"sha256 {record['bodySha256']}")


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        print(f"capture failed: {error}", file=sys.stderr)
        sys.exit(1)
