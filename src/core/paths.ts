// Adapted from GrokExporter commit 85922d6.
const WINDOWS_RESERVED = /^(con|prn|aux|nul|com[1-9]|lpt[1-9])(?:\.|$)/i;

export function safePathSegment(value: string, fallback = "untitled", maxLength = 120): string {
  const normalized = value.normalize("NFKC")
    .replace(/[\u0000-\u001f\u007f]/g, " ")
    .replace(/[\\/:*?"<>|]/g, "-")
    .replace(/\.{2,}/g, ".")
    .replace(/\s+/g, " ")
    .replace(/^[ .-]+|[ .]+$/g, "")
    .slice(0, maxLength)
    .replace(/[ .]+$/g, "");
  const candidate = normalized || fallback;
  return WINDOWS_RESERVED.test(candidate) ? `_${candidate}` : candidate;
}

export function assertSafeRelativePath(path: string): void {
  if (!path || path.startsWith("/") || path.startsWith("\\") || /^[a-z]:/i.test(path)) throw new Error(`Unsafe absolute path: ${path}`);
  const segments = path.replaceAll("\\", "/").split("/");
  if (segments.some((segment) => !segment || segment === "." || segment === "..")) throw new Error(`Unsafe relative path: ${path}`);
}

const CONVERSATION_ID = /^[A-Za-z0-9_-]{1,256}$/;

/** Historical loss-prone mapping, for conservative discovery only; never write new data here. */
export function legacyConversationBasePath(conversationId: string): string {
  const path = `conversations/${safePathSegment(conversationId, "missing-id", 160)}`;
  assertSafeRelativePath(path);
  return path;
}

/** Case-insensitive injective mapping across all accepted ASCII conversation IDs.
 * Existing ordinary lowercase ID directories retain their original paths.
 * Exceptional IDs use reversible lowercase hex split into bounded components.
 */
export function conversationBasePath(conversationId: string): string {
  if (!CONVERSATION_ID.test(conversationId)) throw new Error("Invalid conversation id for archive path.");
  const legacyCompatible = /^[a-z0-9_-]{1,160}$/.test(conversationId)
    && !conversationId.startsWith("-") && !conversationId.endsWith("-")
    && !WINDOWS_RESERVED.test(conversationId);
  const encoded = Array.from(conversationId, character => character.charCodeAt(0).toString(16).padStart(2, "0")).join("");
  const path = legacyCompatible ? `conversations/${conversationId}`
    : `conversations/.by-id-v2/${encoded.match(/.{1,96}/g)!.join("/")}`;
  assertSafeRelativePath(path);
  return path;
}

export function extensionFromMediaType(mediaType: string | null, bytes?: Uint8Array): string {
  const magic = bytes === undefined ? null : extensionFromMagic(bytes);
  if (magic) return magic;
  const normalized = mediaType?.toLowerCase().split(";", 1)[0]?.trim() ?? "";
  const known: Record<string, string> = {
    "application/json": "json", "application/pdf": "pdf", "application/zip": "zip",
    "audio/mpeg": "mp3", "audio/ogg": "ogg", "audio/wav": "wav", "image/gif": "gif",
    "image/jpeg": "jpg", "image/png": "png", "image/svg+xml": "svg", "image/webp": "webp",
    "text/csv": "csv", "text/html": "html", "text/markdown": "md", "text/plain": "txt",
    "video/mp4": "mp4", "video/webm": "webm",
  };
  return known[normalized] ?? safePathSegment(normalized.split("/").at(-1) ?? "bin", "bin", 12);
}

function extensionFromMagic(bytes: Uint8Array): string | null {
  if (starts(bytes, [0x89, 0x50, 0x4e, 0x47])) return "png";
  if (starts(bytes, [0xff, 0xd8, 0xff])) return "jpg";
  if (starts(bytes, [0x47, 0x49, 0x46, 0x38])) return "gif";
  if (starts(bytes, [0x25, 0x50, 0x44, 0x46])) return "pdf";
  if (starts(bytes, [0x50, 0x4b, 0x03, 0x04])) return "zip";
  if (bytes.length >= 12 && new TextDecoder().decode(bytes.slice(0, 4)) === "RIFF" && new TextDecoder().decode(bytes.slice(8, 12)) === "WEBP") return "webp";
  return null;
}

function starts(bytes: Uint8Array, expected: number[]): boolean {
  return expected.every((value, index) => bytes[index] === value);
}
