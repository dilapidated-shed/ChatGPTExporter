import { readFileSync } from 'node:fs';
import { runInNewContext } from 'node:vm';
import { describe, expect, it } from 'vitest';

const source = readFileSync(new URL('../../android/assets/bridge.js', import.meta.url), 'utf8');
const nonce = '0123456789abcdef0123456789abcdef';
const bootstrap = '0123456789abcdef0123456789abcdef';
const command = `CGW1;request;${nonce};1;GET;/backend-api/accounts/check/v4-2023-04-27;`;

function setup(options: { frame?: boolean; origin?: string; responses?: Response[] } = {}) {
  let listener: ((event: unknown) => void) | undefined;
  const messages: string[] = [];
  const requests: Array<{ path: string; options: RequestInit }> = [];
  const port = {
    onmessage: undefined as undefined | ((event: { data: string }) => Promise<void>),
    postMessage: (message: string) => messages.push(message),
    start: () => undefined,
  };
  const window = {
    top: undefined as unknown,
    addEventListener: (_: string, callback: typeof listener) => { listener = callback; },
    removeEventListener: () => { listener = undefined; },
  };
  window.top = options.frame ? {} : window;
  const responses = [...(options.responses ?? [])];
  runInNewContext(source.replace('__CGW_BOOTSTRAP__', bootstrap), {
    window, location: { origin: options.origin ?? 'https://chatgpt.com' },
    TextEncoder, TextDecoder, Uint8Array, atob, btoa, AbortController, setTimeout, clearTimeout,
    fetch: async (path: string, options: RequestInit) => {
      requests.push({ path, options });
      if (!responses.length) throw new Error('unexpected network');
      return responses.shift();
    },
  });
  const attach = (data = bootstrap, sameFrame: boolean | null = true) =>
    listener?.({ source: sameFrame === null ? null : sameFrame ? window : {}, data, ports: [port] });
  return { messages, requests, port, attach, hasListener: () => !!listener };
}

function json(value: unknown, status = 200) {
  return new Response(JSON.stringify(value), { status, headers: { 'content-type': 'application/json' } });
}

describe('Android page transport, synthetic host VM only', () => {
  it('does not install in an iframe or wrong origin', () => {
    expect(setup({ frame: true }).hasListener()).toBe(false);
    expect(setup({ origin: 'https://chatgpt.com.evil.example' }).hasListener()).toBe(false);
  });

  it('requires the exact bootstrap and same window', () => {
    const test = setup();
    test.attach('wrong');
    test.attach(bootstrap, false);
    expect(test.messages).toEqual([]);
    test.attach();
    expect(test.messages).toEqual([`CGW1;ready;${bootstrap}`]);
    expect(test.hasListener()).toBe(false);
  });

  it('accepts a native null-source bootstrap with the exact capability', () => {
    const test = setup();
    test.attach(bootstrap, null);
    expect(test.messages).toEqual([`CGW1;ready;${bootstrap}`]);
  });

  it('keeps signed-out and expired sessions data-free', async () => {
    for (const response of [json({}), json({}, 401), json({}, 403)]) {
      const test = setup({ responses: [response] });
      test.attach();
      await test.port.onmessage?.({ data: command });
      expect(test.requests.length).toBe(1);
      expect(test.messages[1]).toMatch(/;[14]0[13];0;1;$/);
    }
  });

  it('uses page-local credentials and strips secret fields', async () => {
    const test = setup({ responses: [
      json({ accessToken: 'synthetic-secret-long-enough' }),
      json({ account: 'synthetic', accessToken: 'must-not-cross',
        nested: { cookie: 'must-not-cross', value: 1 } }),
    ] });
    test.attach();
    await test.port.onmessage?.({ data: command });
    expect(test.requests.length).toBe(2);
    expect(test.requests[1]!.options.redirect).toBe('error');
    const encoded = test.messages[1]!.split(';')[7]!;
    expect(JSON.parse(atob(encoded.replace(/-/g, '+').replace(/_/g, '/'))))
      .toEqual({ account: 'synthetic', nested: { value: 1 } });
    expect(test.messages.join('')).not.toContain('synthetic-secret');
  });

  it('rejects credential reflection and schema drift', async () => {
    for (const response of [json({ value: 'synthetic-secret-long-enough' }),
      new Response('not-json', { headers: { 'content-type': 'text/html' } })]) {
      const test = setup({ responses: [json({ accessToken: 'synthetic-secret-long-enough' }), response] });
      test.attach();
      await test.port.onmessage?.({ data: command });
      expect(test.messages[1]).toBe(`CGW1;reply;${nonce};1;502;0;1;`);
    }
  });

  it('returns 429 without its provider body', async () => {
    const test = setup({ responses: [json({ accessToken: 'synthetic-token' }), json({ secret: 'not-forwarded' }, 429)] });
    test.attach();
    await test.port.onmessage?.({ data: command });
    expect(test.messages[1]).toBe(`CGW1;reply;${nonce};1;429;0;1;`);
  });

  it('refuses arbitrary URLs and oversized messages before network', async () => {
    const test = setup();
    test.attach();
    await test.port.onmessage?.({ data: command.replace('/backend-api/', 'https://evil.example/') });
    await test.port.onmessage?.({ data: 'x'.repeat(16385) });
    expect(test.requests).toEqual([]);
    expect(test.messages.length).toBe(1);
  });

  it('rejects oversized decoded responses without publishing a chunk', async () => {
    const test = setup({ responses: [json({ accessToken: 'synthetic-token' }),
      new Response('x'.repeat(5_000_001), { headers: { 'content-type': 'application/json' } })] });
    test.attach();
    await test.port.onmessage?.({ data: command });
    expect(test.messages).toEqual([`CGW1;ready;${bootstrap}`, `CGW1;reply;${nonce};1;502;0;1;`]);
  });
});
