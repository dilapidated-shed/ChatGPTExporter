#!/data/data/com.termux/files/usr/bin/bash
set -euo pipefail

PORT="${1:-9222}"

line="$(rish -c 'grep -E "@.*webview.*devtools_remote|@webview_devtools_remote" /proc/net/unix' | tail -n 1 || true)"
if [[ -z "$line" ]]; then
    echo "No WebView DevTools socket is visible." >&2
    echo "Launch ChatGPT Web Probe first." >&2
    exit 1
fi

socket="${line##* @}"
if [[ "$socket" == "$line" || -z "$socket" ]]; then
    echo "Could not parse DevTools socket from: $line" >&2
    exit 1
fi

echo "DevTools socket: @$socket"
adb forward "tcp:$PORT" "localabstract:$socket" >/dev/null
echo "Forwarded: http://127.0.0.1:$PORT"

echo
echo "Targets:"
curl -fsS "http://127.0.0.1:$PORT/json/list"
echo
