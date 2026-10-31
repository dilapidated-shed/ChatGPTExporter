#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build
certificate="build/fixture-cert.pem"
private_key="build/fixture-key.pem"
rm -f "$certificate" "$private_key"
openssl req -x509 -newkey rsa:2048 -nodes -days 1 \
  -subj "/CN=chatgpt.com" \
  -addext "subjectAltName=DNS:chatgpt.com" \
  -keyout "$private_key" -out "$certificate" >/dev/null 2>&1
chmod 600 "$private_key"
python3 fixtures/https_fixture.py "$certificate" "$private_key" &
server=$!
cleanup() {
  kill "$server" 2>/dev/null || :
  wait "$server" 2>/dev/null || :
  rm -f "$certificate" "$private_key"
}
trap cleanup EXIT
ready=0
for _ in $(seq 1 60); do
  if (echo >/dev/tcp/127.0.0.1/18443) >/dev/null 2>&1; then
    ready=1
    break
  fi
  if ! kill -0 "$server" 2>/dev/null; then
    echo "local HTTPS fixture exited" >&2
    exit 1
  fi
  sleep 0.1
done
if [ "$ready" != 1 ]; then
  echo "local HTTPS fixture never bound its loopback listener" >&2
  exit 1
fi
./build/test_cgw_https
