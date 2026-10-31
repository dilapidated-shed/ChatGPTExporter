/** Bounded-memory readers for supplied files; no provider or account access. */
import { createReadStream, createWriteStream } from "node:fs";
import { open, mkdir } from "node:fs/promises";
import { Transform } from "node:stream";
import { pipeline } from "node:stream/promises";
import { createInflateRaw } from "node:zlib";
import { createHash } from "node:crypto";
import path from "node:path";

export const DEFAULT_RECORD_BYTES = 64 * 1024 * 1024;
export const sha256 = bytes => createHash("sha256").update(bytes).digest("hex");
export function json(bytes) {
  try { return JSON.parse(new TextDecoder("utf-8", { fatal: true }).decode(bytes)); }
  catch { throw new Error("Supplied source contains invalid UTF-8 JSON."); }
}
export async function hashFile(filename) {
  const hash = createHash("sha256"); let size = 0;
  for await (const bytes of createReadStream(filename)) { hash.update(bytes); size += bytes.length; }
  return { sha256: hash.digest("hex"), byteSize: size };
}
export class ByteBudget extends Transform {
  constructor(limit, label = "Source") { super(); this.limit = limit; this.label = label; this.size = 0; }
  _transform(bytes, encoding, done) {
    this.size += bytes.length;
    done(this.size > this.limit ? new Error(this.label + " exceeds the configured byte limit.") : null, bytes);
  }
}

class Cursor {
  constructor(filename, limit) {
    this.iterator = createReadStream(filename, { highWaterMark: 65536 })[Symbol.asyncIterator]();
    this.bytes = Buffer.alloc(0); this.offset = 0; this.limit = limit;
  }
  async fill() {
    if (this.offset < this.bytes.length) return true;
    const next = await this.iterator.next();
    this.bytes = next.done ? Buffer.alloc(0) : next.value; this.offset = 0;
    return !next.done;
  }
  async space() {
    while (await this.fill()) {
      while (this.offset < this.bytes.length && [32, 9, 10, 13].includes(this.bytes[this.offset])) this.offset++;
      if (this.offset < this.bytes.length) break;
    }
  }
  async peek() { await this.space(); return await this.fill() ? this.bytes[this.offset] : -1; }
  async expect(byte) {
    if (await this.peek() !== byte) throw new Error("Malformed source JSON framing.");
    this.offset++;
  }
  async value() {
    const first = await this.peek();
    if (first < 0) throw new Error("Truncated source JSON.");
    const parts = []; let size = 0, depth = 0, quoted = false, escaped = false, started = false;
    const composite = first === 123 || first === 91, string = first === 34;
    while (await this.fill()) {
      const start = this.offset; let finished = false;
      while (this.offset < this.bytes.length) {
        const byte = this.bytes[this.offset];
        if (started && !composite && !string && [32, 9, 10, 13, 44, 93, 125].includes(byte)) { finished = true; break; }
        this.offset++; started = true;
        if (quoted) {
          if (escaped) escaped = false;
          else if (byte === 92) escaped = true;
          else if (byte === 34) { quoted = false; if (string) { finished = true; break; } }
        } else if (byte === 34) quoted = true;
        else if (composite) {
          if (byte === 123 || byte === 91) depth++;
          else if (byte === 125 || byte === 93) { depth--; if (!depth) { finished = true; break; } }
        }
      }
      const part = this.bytes.subarray(start, this.offset); size += part.length;
      if (size > this.limit) throw new Error("One source record exceeds --max-record-bytes.");
      parts.push(part);
      if (finished) return Buffer.concat(parts, size);
    }
    if (composite || quoted) throw new Error("Truncated source JSON value.");
    return Buffer.concat(parts, size);
  }
  async close() { await this.iterator.return?.(); }
}
async function* array(cursor) {
  await cursor.expect(91);
  if (await cursor.peek() === 93) { cursor.offset++; return; }
  for (;;) {
    const raw = await cursor.value(); yield { raw, value: json(raw) };
    const next = await cursor.peek(); cursor.offset++;
    if (next === 93) return;
    if (next !== 44 || await cursor.peek() === 93) throw new Error("Malformed conversation array.");
  }
}
export async function* conversationRecords(filename, limit = DEFAULT_RECORD_BYTES) {
  const cursor = new Cursor(filename, limit);
  try {
    const first = await cursor.peek();
    if (first === 91) yield* array(cursor);
    else if (first === 123) {
      cursor.offset++; const keys = new Set(), pairs = []; let wrapper = false, size = 2;
      if (await cursor.peek() !== 125) for (;;) {
        const keyBytes = await cursor.value(), key = json(keyBytes);
        if (typeof key !== "string" || keys.has(key)) throw new Error("Duplicate or invalid source object key.");
        keys.add(key); await cursor.expect(58);
        if (key === "conversations") { wrapper = true; pairs.length = 0; yield* array(cursor); }
        else {
          const raw = await cursor.value(); json(raw);
          if (!wrapper) {
            size += keyBytes.length + raw.length + 2;
            if (size > limit) throw new Error("One source record exceeds --max-record-bytes.");
            pairs.push(Buffer.concat([keyBytes, Buffer.from(":"), raw]));
          }
        }
        if (await cursor.peek() === 125) break;
        await cursor.expect(44);
        if (await cursor.peek() === 125) throw new Error("Trailing source object comma.");
      }
      await cursor.expect(125);
      if (!wrapper) {
        const raw = Buffer.concat([Buffer.from("{"), ...pairs.flatMap((pair, i) => i ? [Buffer.from(","), pair] : [pair]), Buffer.from("}")]);
        yield { raw, value: json(raw) };
      }
    } else throw new Error("Source must contain a conversation array or object.");
    if (await cursor.peek() !== -1) throw new Error("Trailing data after source JSON.");
  } finally { await cursor.close(); }
}

const crcTable = Array.from({ length: 256 }, (_, n) => {
  for (let i = 0; i < 8; i++) n = n & 1 ? 0xedb88320 ^ (n >>> 1) : n >>> 1;
  return n >>> 0;
});
class ZipIntegrity extends ByteBudget {
  constructor(entry, limit) { super(limit, "Unpacked ZIP member"); this.entry = entry; this.crc = 0xffffffff; }
  _transform(bytes, encoding, done) {
    for (const byte of bytes) this.crc = crcTable[(this.crc ^ byte) & 255] ^ (this.crc >>> 8);
    super._transform(bytes, encoding, done);
  }
  _flush(done) {
    done(this.size !== this.entry.size || ((this.crc ^ 0xffffffff) >>> 0) !== this.entry.crc
      ? new Error("ZIP member length or CRC mismatch.") : null);
  }
}
async function exact(file, offset, length) {
  const bytes = Buffer.alloc(length); let used = 0;
  while (used < length) {
    const { bytesRead } = await file.read(bytes, used, length - used, offset + used);
    if (!bytesRead) throw new Error("Truncated ZIP structure."); used += bytesRead;
  }
  return bytes;
}
function number64(bytes, offset) {
  const value = bytes.readBigUInt64LE(offset);
  if (value > BigInt(Number.MAX_SAFE_INTEGER)) throw new Error("ZIP64 offset exceeds exact filesystem range.");
  return Number(value);
}
function safeMember(name) {
  const parts = name.replace(/\/$/, "").split("/");
  if (!name || name.includes("\\") || /[\x00-\x1f\x7f]/.test(name) || parts.some(p => !p || p === "." || p === ".." || p.includes(":")))
    throw new Error("ZIP contains an unsafe member path.");
}
export async function sourceMembers(input, directory, maxBytes = Number.MAX_SAFE_INTEGER) {
  const file = await open(input, "r");
  try {
    const { size } = await file.stat(); const signature = await exact(file, 0, Math.min(size, 4));
    if (signature.length < 4 || signature.readUInt32LE(0) !== 0x04034b50) {
      if (signature.length === 4 && signature.readUInt32LE(0) === 0x06054b50) throw new Error("ZIP has no conversation JSON members.");
      return { zip: false, members: [{ name: "conversations.json", filename: input, ...(await hashFile(input)) }] };
    }
    const tailStart = Math.max(0, size - 65557), tail = await exact(file, tailStart, size - tailStart);
    let end = -1;
    for (let i = tail.length - 22; i >= 0; i--) if (tail.readUInt32LE(i) === 0x06054b50 && i + 22 + tail.readUInt16LE(i + 20) === tail.length) { end = i; break; }
    if (end < 0 || tail.readUInt16LE(end + 4) || tail.readUInt16LE(end + 6)) throw new Error("Malformed or multi-disk ZIP input.");
    let count = tail.readUInt16LE(end + 10), centralSize = tail.readUInt32LE(end + 12), offset = tail.readUInt32LE(end + 16);
    if (count === 65535 || centralSize === 0xffffffff || offset === 0xffffffff) {
      const locator = await exact(file, tailStart + end - 20, 20);
      if (locator.readUInt32LE(0) !== 0x07064b50 || locator.readUInt32LE(4) || locator.readUInt32LE(16) !== 1) throw new Error("Unsupported ZIP64 disk layout.");
      const zip64 = await exact(file, number64(locator, 8), 56);
      if (zip64.readUInt32LE(0) !== 0x06064b50 || zip64.readUInt32LE(16) || zip64.readUInt32LE(20)) throw new Error("Invalid ZIP64 directory.");
      count = number64(zip64, 32); centralSize = number64(zip64, 40); offset = number64(zip64, 48);
    }
    const centralEnd = offset + centralSize;
    if (centralEnd > tailStart + end || centralEnd > size || count > centralSize / 46) throw new Error("Invalid ZIP directory bounds.");
    const names = new Set(), entries = []; let unpacked = 0;
    for (let i = 0; i < count; i++) {
      const header = await exact(file, offset, 46);
      if (header.readUInt32LE(0) !== 0x02014b50) throw new Error("Invalid ZIP central entry.");
      const nameLength = header.readUInt16LE(28), extraLength = header.readUInt16LE(30), commentLength = header.readUInt16LE(32);
      const nameBytes = await exact(file, offset + 46, nameLength);
      let name; try { name = new TextDecoder("utf-8", { fatal: true }).decode(nameBytes); } catch { throw new Error("ZIP member name is not UTF-8."); }
      safeMember(name);
      if (names.has(name.toLowerCase())) throw new Error("Duplicate ZIP member identity."); names.add(name.toLowerCase());
      const entry = { name, crc: header.readUInt32LE(16), compressed: header.readUInt32LE(20), size: header.readUInt32LE(24), local: header.readUInt32LE(42), method: header.readUInt16LE(10), flags: header.readUInt16LE(8) };
      if (entry.size === 0xffffffff || entry.compressed === 0xffffffff || entry.local === 0xffffffff) {
        const extra = await exact(file, offset + 46 + nameLength, extraLength); let found = false;
        for (let at = 0; at + 4 <= extra.length;) {
          const type = extra.readUInt16LE(at), length = extra.readUInt16LE(at + 2), stop = at + 4 + length; at += 4;
          if (stop > extra.length) throw new Error("Malformed ZIP extra field.");
          if (type === 1) {
            for (const key of ["size", "compressed", "local"]) if (entry[key] === 0xffffffff) { if (at + 8 > stop) throw new Error("Truncated ZIP64 field."); entry[key] = number64(extra, at); at += 8; }
            found = true;
          }
          at = stop;
        }
        if (!found) throw new Error("Missing ZIP64 sizes.");
      }
      if (/^conversations(?:[-_][0-9]+)?\.json$/i.test(name.split("/").at(-1))) {
        if (entry.flags & 1 || ![0, 8].includes(entry.method)) throw new Error("Encrypted or unsupported conversation ZIP member.");
        unpacked += entry.size;
        if (!Number.isSafeInteger(unpacked) || unpacked > maxBytes) throw new Error("Unpacked source exceeds the configured byte limit.");
        entries.push(entry);
      }
      offset += 46 + nameLength + extraLength + commentLength;
      if (offset > centralEnd) throw new Error("ZIP directory entry exceeds its bounds.");
    }
    if (offset !== centralEnd || !entries.length) throw new Error("ZIP has no valid conversation JSON directory.");
    await mkdir(directory, { recursive: true, mode: 0o700 }); entries.sort((a, b) => a.name < b.name ? -1 : a.name > b.name ? 1 : 0);
    const members = [];
    for (const [i, entry] of entries.entries()) {
      const local = await exact(file, entry.local, 30);
      if (local.readUInt32LE(0) !== 0x04034b50 || local.readUInt16LE(8) !== entry.method || local.readUInt16LE(6) !== entry.flags) throw new Error("ZIP local/central mismatch.");
      const localName = await exact(file, entry.local + 30, local.readUInt16LE(26));
      if (localName.toString("utf8") !== entry.name) throw new Error("ZIP local member identity mismatch.");
      const start = entry.local + 30 + local.readUInt16LE(26) + local.readUInt16LE(28), stop = start + entry.compressed;
      if (!entry.compressed || stop > centralEnd - centralSize) throw new Error("Invalid ZIP member data bounds.");
      const filename = path.join(directory, String(i + 1).padStart(4, "0") + ".json");
      const stages = [createReadStream(input, { start, end: stop - 1 })];
      if (entry.method === 8) stages.push(createInflateRaw());
      stages.push(new ZipIntegrity(entry, maxBytes), createWriteStream(filename, { flags: "wx", mode: 0o600 }));
      await pipeline(stages);
      members.push({ name: entry.name, filename, ...(await hashFile(filename)) });
    }
    return { zip: true, members };
  } finally { await file.close(); }
}
