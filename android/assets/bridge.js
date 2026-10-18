/* Thin page transport only. Native C chooses the operation and owns archive
 * authority. Credentials remain in this closure and never enter a port reply. */
(() => {
  'use strict';
  if (window !== window.top || location.origin !== 'https://chatgpt.com') return;
  const bootstrap = '__CGW_BOOTSTRAP__';
  const maximum = 5_000_000;
  const encoder = new TextEncoder();
  const secretKey = /^(access[_-]?token|refresh[_-]?token|id[_-]?token|authorization|x-authorization|cookie|set-cookie|password|session[_-]?token)$/i;
  const clean = (value, depth = 0) => {
    if (depth > 64) throw new Error('schema-depth');
    if (Array.isArray(value)) return value.map(item => clean(item, depth + 1));
    if (value && typeof value === 'object') {
      const result = Object.create(null);
      for (const [key, item] of Object.entries(value)) {
        if (!secretKey.test(key)) result[key] = clean(item, depth + 1);
      }
      return result;
    }
    // Signed file URLs stay in the page world; E1 does not retrieve assets.
    if (typeof value === 'string' && /^https?:\/\//i.test(value) &&
        /[?&](sig|signature|token|X-Amz-|se=|sp=|sv=)/i.test(value)) return null;
    return value;
  };
  const base64 = bytes => {
    let text = '';
    for (const byte of bytes) text += String.fromCharCode(byte);
    return btoa(text).replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/, '');
  };
  const boundedJson = async response => {
    if (!response.body || !/application\/json/i.test(response.headers.get('content-type') || ''))
      throw new Error('schema-content-type');
    const reader = response.body.getReader();
    const parts = [];
    let length = 0;
    try {
      for (;;) {
        const item = await reader.read();
        if (item.done) break;
        length += item.value.length;
        if (length > maximum) throw new Error('oversize');
        parts.push(item.value);
      }
    } finally { await reader.cancel(); }
    const bytes = new Uint8Array(length);
    let offset = 0;
    for (const part of parts) { bytes.set(part, offset); offset += part.length; }
    return JSON.parse(new TextDecoder('utf-8', { fatal: true }).decode(bytes));
  };
  const attach = event => {
    // Native postWebMessage may have a null source. Child-frame postMessage
    // has its child WindowProxy and still fails this check.
    if ((event.source !== window && event.source !== null) ||
        event.data !== bootstrap || event.ports.length !== 1) return;
    window.removeEventListener('message', attach);
    const port = event.ports[0];
    let busy = false;
    port.onmessage = async event => {
      if (busy || typeof event.data !== 'string' || event.data.length > 16384) return;
      const fields = event.data.split(';');
      if (fields.length !== 7 || fields[0] !== 'CGW1' || fields[1] !== 'request' ||
          !/^[0-9a-f]{32}$/.test(fields[2]) || !/^[1-9][0-9]{0,19}$/.test(fields[3]) ||
          !['GET', 'POST'].includes(fields[4]) ||
          !fields[5].startsWith('/backend-api/') || fields[5].includes('://') ||
          !/^[A-Za-z0-9_-]*$/.test(fields[6])) return;
      busy = true;
      const controller = new AbortController();
      const deadline = setTimeout(() => controller.abort(), 30000);
      const reply = (status, sequence, final, payload = '') =>
        port.postMessage(`CGW1;reply;${fields[2]};${fields[3]};${status};${sequence};${final};${payload}`);
      try {
        const sessionResponse = await fetch('/api/auth/session', { credentials: 'include', redirect: 'error', cache: 'no-store', signal: controller.signal });
        if (!sessionResponse.ok) { reply(sessionResponse.status, 0, 1); return; }
        const session = await boundedJson(sessionResponse);
        const token = session.accessToken;
        if (typeof token !== 'string' || !token.length || token.length > 16384) {
          reply(401, 0, 1); return;
        }
        const options = { method: fields[4], credentials: 'include', redirect: 'error', cache: 'no-store', signal: controller.signal,
          headers: { Authorization: `Bearer ${token}`, 'X-Authorization': `Bearer ${token}` } };
        if (fields[4] === 'POST') {
          options.headers['Content-Type'] = 'application/json';
          options.body = atob(fields[6].replace(/-/g, '+').replace(/_/g, '/'));
        }
        const response = await fetch(fields[5], options);
        if (!response.ok) { reply(response.status, 0, 1); return; }
        const body = encoder.encode(JSON.stringify(clean(await boundedJson(response))));
        if (body.length > maximum) throw new Error('oversize');
        // Refuse reflected credentials even in arbitrary string values.
        if (new TextDecoder().decode(body).includes(token)) throw new Error('credential-reflection');
        for (let offset = 0, sequence = 0; offset < body.length; offset += 8192, sequence++) {
          const chunk = body.subarray(offset, offset + 8192);
          reply(response.status, sequence, offset + chunk.length === body.length ? 1 : 0, base64(chunk));
        }
      } catch (_) { reply(502, 0, 1); }
      finally { clearTimeout(deadline); busy = false; }
    };
    port.start();
    port.postMessage(`CGW1;ready;${bootstrap}`);
  };
  window.addEventListener('message', attach);
})();
