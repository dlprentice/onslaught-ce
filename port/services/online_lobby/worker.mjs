// SPDX-License-Identifier: GPL-3.0-only
// Onslaught CE Online Lobby Worker, SQLite Durable Objects (LobbyDirectory, GameRoom), and PermitAuthority.
import { DurableObject, WorkerEntrypoint } from "cloudflare:workers";

const PROTO_VERSION = 1;
const MAX_MATCH_SLOTS = 4;
const WIRE_MAX_SLOTS = 64;

const PREFIX = "/ce-lobby/v1/";
const MAX_BODY = 65536;
const AUTH_WINDOW_SECONDS = 120;
const DEFAULT_SESSION_SECONDS = 14400;
const MAX_SESSION_SECONDS = 86400;
const DEFAULT_TURN_TTL_SECONDS = 3600;
const MAX_TURN_TTL_SECONDS = 14400;
const DEFAULT_DAILY_TURN_LIMIT = 200;
const DEFAULT_MONTHLY_TURN_LIMIT = 2000;
const DEFAULT_PER_IP_DAILY_TURN_LIMIT = 40;
const DEFAULT_HEARTBEAT_TIMEOUT_MS = 30000;
const DEFAULT_MAX_ACTIVE_ROOMS = 128;
const DEFAULT_AUTH_IP_LIMIT = 20;
const DEFAULT_AUTH_PUBKEY_LIMIT = 10;
const AUTH_RATE_WINDOW_SECONDS = 60;
const ROOM_CREATE_LIMIT_PER_MIN = 10;
const CHAT_RING_LIMIT = 32;
const CHAT_MAX_BYTES = 160;
const CHAT_RATE_MAX = 5;
const CHAT_RATE_WINDOW_MS = 10000;
const SIGNAL_MAX_SDP_BYTES = 4096;
const SIGNAL_RING_LIMIT = 256;
const MAX_TURN_PER_PAIR_PER_MATCH = 4;

const VALID_MODES = new Set(["coop_defence", "coop_skirmish", "versus_2v2", "tutorial"]);
const VALID_MAPS = new Set([
  100, 850, 851, 852, 853, 854, 855, 856, 857, 858,
  859, 860, 861, 862, 863, 864, 865, 866,
]);
const VALID_VISIBILITIES = new Set(["public", "unlisted"]);
const VALID_ROUTES = new Set(["unknown", "direct", "relay"]);
const VALID_SEAT_SET_KINDS = new Set(["open", "ai", "closed"]);
const ALLOWED_STATUS_CODES = new Set([200, 400, 401, 403, 404, 409, 429, 503]);

const CROCKFORD_LOWER = "0123456789abcdefghjkmnpqrstvwxyz";
const ROOM_CODE_ALPHABET = "23456789ABCDEFGHJKLMNPQRSTUVWXYZ";

const encoder = new TextEncoder();
const decoder = new TextDecoder("utf-8", { fatal: true });

export class ApiError extends Error {
  constructor(httpStatus, errorCode, message) {
    super(message);
    this.httpStatus = ALLOWED_STATUS_CODES.has(httpStatus) ? httpStatus : 503;
    this.errorCode = errorCode;
    this.safeMessage = message;
  }
}

export function requireCond(value, httpStatus = 400, errorCode = "invalid_request", message = "Invalid request.") {
  if (!value) throw new ApiError(httpStatus, errorCode, message);
}

export function integer(value) {
  return Number.isSafeInteger(value) && value >= 0;
}

export function nowSeconds() {
  return Math.floor(Date.now() / 1000);
}

export function hex(bytes) {
  let out = "";
  for (let i = 0; i < bytes.length; i++) {
    out += bytes[i].toString(16).padStart(2, "0");
  }
  return out;
}

export function unhex(text, byteLength) {
  requireCond(typeof text === "string" && text.length === byteLength * 2 && /^[0-9a-f]+$/.test(text),
    400, "invalid_request", "Expected lowercase hexadecimal value.");
  const out = new Uint8Array(byteLength);
  for (let i = 0; i < byteLength; i++) {
    out[i] = Number.parseInt(text.slice(i * 2, i * 2 + 2), 16);
  }
  return out;
}

export function randomHex(byteLength) {
  return hex(crypto.getRandomValues(new Uint8Array(byteLength)));
}

export function crockfordBase32(bytes) {
  let out = "";
  let buffer = 0;
  let bits = 0;
  for (let i = 0; i < bytes.length; i++) {
    buffer = (buffer << 8) | bytes[i];
    bits += 8;
    while (bits >= 5) {
      bits -= 5;
      out += CROCKFORD_LOWER[(buffer >>> bits) & 31];
    }
  }
  if (bits > 0) {
    out += CROCKFORD_LOWER[(buffer << (5 - bits)) & 31];
  }
  return out;
}

export function canonical(value) {
  if (Array.isArray(value)) return `[${value.map(canonical).join(",")}]`;
  if (value && typeof value === "object") {
    return `{${Object.keys(value).sort().map(k => `${JSON.stringify(k)}:${canonical(value[k])}`).join(",")}}`;
  }
  return JSON.stringify(value);
}

export function base64UrlEncode(bytes) {
  let binary = "";
  for (let i = 0; i < bytes.length; i++) binary += String.fromCharCode(bytes[i]);
  return btoa(binary).replaceAll("+", "-").replaceAll("/", "_").replace(/=+$/, "");
}

export function base64UrlDecode(text, maxBytes) {
  requireCond(typeof text === "string" && text.length > 0 && text.length <= Math.ceil(maxBytes / 3) * 4,
    401, "unauthorized", "Malformed token.");
  requireCond(/^[A-Za-z0-9_-]+$/.test(text), 401, "unauthorized", "Malformed token.");
  let padded = text.replaceAll("-", "+").replaceAll("_", "/");
  while (padded.length % 4 !== 0) padded += "=";
  let bytes;
  try {
    bytes = Uint8Array.from(atob(padded), ch => ch.charCodeAt(0));
  } catch {
    throw new ApiError(401, "unauthorized", "Malformed token.");
  }
  requireCond(bytes.length <= maxBytes && base64UrlEncode(bytes) === text,
    401, "unauthorized", "Malformed token.");
  return bytes;
}

// Bounded JSON parser rejecting duplicate keys at every nesting level.
export function strictJson(bytes, maximum = MAX_BODY) {
  requireCond(bytes.byteLength > 0 && bytes.byteLength <= maximum,
    400, "invalid_json", "Request body must be valid bounded JSON.");
  let text;
  try {
    text = decoder.decode(bytes);
  } catch {
    throw new ApiError(400, "invalid_json", "Request body must be valid UTF-8.");
  }
  let offset = 0;
  let tokens = 0;
  const skipWs = () => {
    while (offset < text.length && /[ \t\r\n]/.test(text[offset])) offset++;
  };
  function parseString() {
    const start = offset++;
    while (offset < text.length) {
      const ch = text[offset++];
      if (ch === "\\") {
        requireCond(offset < text.length, 400, "invalid_json", "Unterminated escape in JSON string.");
        offset++;
      } else if (ch === '"') {
        try {
          return JSON.parse(text.slice(start, offset));
        } catch {
          throw new ApiError(400, "invalid_json", "Invalid JSON string literal.");
        }
      }
    }
    throw new ApiError(400, "invalid_json", "Unterminated JSON string.");
  }
  function parseValue(depth) {
    requireCond(depth <= 12 && ++tokens <= 2048, 400, "invalid_json", "JSON exceeds nesting or token limit.");
    skipWs();
    requireCond(offset < text.length, 400, "invalid_json", "Unexpected end of JSON.");
    const ch = text[offset];
    if (ch === '"') return parseString();
    if (ch === "{") {
      offset++;
      skipWs();
      const obj = Object.create(null);
      if (text[offset] === "}") {
        offset++;
        return obj;
      }
      while (true) {
        skipWs();
        requireCond(text[offset] === '"', 400, "invalid_json", "Expected string key in JSON object.");
        const key = parseString();
        requireCond(!Object.hasOwn(obj, key), 400, "duplicate_key", "Duplicate JSON key is not allowed.");
        skipWs();
        requireCond(text[offset++] === ":", 400, "invalid_json", "Expected colon in JSON object.");
        obj[key] = parseValue(depth + 1);
        skipWs();
        if (text[offset] === "}") {
          offset++;
          return obj;
        }
        requireCond(text[offset++] === ",", 400, "invalid_json", "Expected comma in JSON object.");
      }
    }
    if (ch === "[") {
      offset++;
      skipWs();
      const arr = [];
      if (text[offset] === "]") {
        offset++;
        return arr;
      }
      while (true) {
        requireCond(arr.length < 256, 400, "invalid_json", "JSON array exceeds maximum length.");
        arr.push(parseValue(depth + 1));
        skipWs();
        if (text[offset] === "]") {
          offset++;
          return arr;
        }
        requireCond(text[offset++] === ",", 400, "invalid_json", "Expected comma in JSON array.");
      }
    }
    const match = /^(?:true|false|null|-?(?:0|[1-9][0-9]*)(?:\.[0-9]+)?(?:[eE][+-]?[0-9]+)?)/.exec(text.slice(offset));
    requireCond(match, 400, "invalid_json", "Invalid JSON token.");
    offset += match[0].length;
    return JSON.parse(match[0]);
  }
  const result = parseValue(0);
  skipWs();
  requireCond(offset === text.length, 400, "invalid_json", "Trailing characters after JSON payload.");
  return result;
}

export function assertAllowedFields(value, allowedSet, requiredList = []) {
  requireCond(value && typeof value === "object" && !Array.isArray(value),
    400, "invalid_request", "Expected a JSON object.");
  for (const key of Object.keys(value)) {
    requireCond(allowedSet.has(key), 400, "unknown_field", "Request contains an unsupported field.");
  }
  for (const req of requiredList) {
    requireCond(Object.hasOwn(value, req), 400, "missing_field", "Request is missing a required field.");
  }
  return value;
}

export function assertExactFields(value, names) {
  requireCond(value && typeof value === "object" && !Array.isArray(value),
    400, "invalid_request", "Expected a JSON object.");
  const keys = Object.keys(value).sort();
  const expected = [...names].sort();
  requireCond(keys.length === expected.length && keys.every((k, i) => k === expected[i]),
    400, "invalid_request", "Request fields do not match schema.");
  return value;
}

export async function bodyBytes(request, maximum = MAX_BODY, allowEmpty = false) {
  const length = request.headers.get("Content-Length");
  requireCond(length === null || (/^(0|[1-9][0-9]*)$/.test(length) && Number(length) <= maximum),
    400, "body_too_large", "Request body exceeds maximum size.");
  const reader = request.body?.getReader();
  if (!reader) {
    requireCond(allowEmpty, 400, "empty_body", "Request body is required.");
    return new Uint8Array();
  }
  let timer;
  let count = 0;
  const chunks = [];
  const timeout = new Promise((_, reject) => {
    timer = setTimeout(() => reject(new ApiError(400, "body_timeout", "Request body read timed out.")), 5000);
  });
  try {
    while (true) {
      const { value, done } = await Promise.race([reader.read(), timeout]);
      if (done) break;
      count += value.length;
      requireCond(count <= maximum, 400, "body_too_large", "Request body exceeds maximum size.");
      chunks.push(value);
    }
  } finally {
    clearTimeout(timer);
    await reader.cancel().catch(() => {});
  }
  requireCond(count > 0 || allowEmpty, 400, "empty_body", "Request body is required.");
  const out = new Uint8Array(count);
  let offset = 0;
  for (const chunk of chunks) {
    out.set(chunk, offset);
    offset += chunk.length;
  }
  return out;
}

export async function readJsonBody(request, { allowEmpty = false, maximum = MAX_BODY } = {}) {
  const ct = request.headers.get("Content-Type");
  requireCond(typeof ct === "string" && ct.split(";")[0].trim().toLowerCase() === "application/json",
    400, "invalid_content_type", "Content-Type must be application/json.");
  const bytes = await bodyBytes(request, maximum, allowEmpty);
  if (bytes.byteLength === 0) return Object.create(null);
  return strictJson(bytes, maximum);
}

export function strictHeaders() {
  return {
    "Content-Type": "application/json",
    "Cache-Control": "no-store",
    "Pragma": "no-cache",
    "X-Content-Type-Options": "nosniff",
    "Content-Encoding": "identity",
  };
}

export function jsonResponse(data, httpStatus = 200) {
  const status = ALLOWED_STATUS_CODES.has(httpStatus) ? httpStatus : 503;
  return new Response(JSON.stringify(data), { status, headers: strictHeaders() });
}

export function errorResponse(httpStatus, errorCode, message) {
  return jsonResponse({ error: errorCode, message }, httpStatus);
}

function handleFailure(error) {
  if (error instanceof ApiError) {
    return errorResponse(error.httpStatus, error.errorCode, error.safeMessage);
  }
  return errorResponse(503, "service_unavailable", "Service is temporarily unavailable.");
}

export function secretKeyBytes(text) {
  requireCond(typeof text === "string" && /^[0-9a-f]{64}$/.test(text),
    503, "service_unavailable", "Service configuration is incomplete.");
  const out = new Uint8Array(32);
  for (let i = 0; i < 32; i++) {
    out[i] = Number.parseInt(text.slice(i * 2, i * 2 + 2), 16);
  }
  return out;
}

async function importHmacKey(bytes) {
  return crypto.subtle.importKey("raw", bytes, { name: "HMAC", hash: "SHA-256" }, false, ["sign", "verify"]);
}

async function sha256Bytes(bytes) {
  return new Uint8Array(await crypto.subtle.digest("SHA-256", bytes));
}

async function sha256Hex(bytes) {
  return hex(await sha256Bytes(bytes));
}

function envPositiveInt(value, fallback, minVal, maxVal) {
  if (value === undefined || value === null || value === "") return fallback;
  const n = Number(value);
  if (!Number.isSafeInteger(n) || n < minVal || n > maxVal) return fallback;
  return n;
}

export function sanitizeCallsign(raw) {
  requireCond(typeof raw === "string" && raw.length <= 32 && /^[A-Za-z0-9 _-]+$/.test(raw),
    400, "invalid_callsign", "Callsign must be 3 to 20 safe ASCII characters.");
  const clean = raw.trim().replace(/ +/g, " ");
  requireCond(clean.length >= 3 && clean.length <= 20,
    400, "invalid_callsign", "Callsign must be 3 to 20 safe ASCII characters.");
  return clean;
}

function sanitizeRoomName(raw) {
  requireCond(typeof raw === "string" && raw.length <= 80 && /^[\x20-\x7e]+$/.test(raw),
    400, "invalid_room_name", "Room name must be 1 to 40 printable ASCII characters.");
  const clean = raw.trim().replace(/ +/g, " ");
  requireCond(clean.length >= 1 && clean.length <= 40,
    400, "invalid_room_name", "Room name must be 1 to 40 printable ASCII characters.");
  return clean;
}

function sanitizeBuildId(raw) {
  if (raw === undefined) return "";
  requireCond(typeof raw === "string" && /^[A-Za-z0-9._-]{0,32}$/.test(raw),
    400, "invalid_build_id", "Invalid build identifier.");
  return raw;
}

function sanitizeContentId(raw) {
  if (raw === undefined) return "";
  requireCond(typeof raw === "string" && /^(?:|[0-9a-f]{64})$/.test(raw),
    400, "invalid_content_id", "Content identifier must be empty or 64 lowercase hex characters.");
  return raw;
}

function sanitizeBeCfg(raw) {
  if (raw === undefined) return "";
  requireCond(typeof raw === "string" && raw.length <= 32 && /^[A-Za-z0-9 _.-]*$/.test(raw),
    400, "invalid_be_cfg", "Invalid Battle Engine configuration name.");
  return raw.trim();
}

function sanitizePassword(raw) {
  if (raw === undefined) return "";
  requireCond(typeof raw === "string" && raw.length <= 64 && /^[\x20-\x7e]*$/.test(raw),
    400, "invalid_password", "Room password must be at most 64 printable ASCII characters.");
  return raw;
}

function sanitizeChatText(raw) {
  requireCond(typeof raw === "string" && raw.length <= 512,
    400, "invalid_chat", "Chat message must be a bounded string.");
  let stripped = "";
  for (let i = 0; i < raw.length; i++) {
    const code = raw.charCodeAt(i);
    if (code >= 0x20 && code <= 0x7e) stripped += raw[i];
  }
  const clean = stripped.trim();
  requireCond(clean.length >= 1, 400, "invalid_chat", "Chat message must contain printable ASCII text.");
  requireCond(clean.length <= CHAT_MAX_BYTES, 400, "chat_too_long", "Chat message exceeds 160 ASCII bytes.");
  return clean;
}

function sanitizeSdp(raw) {
  requireCond(typeof raw === "string" && raw.length >= 1 && raw.length <= SIGNAL_MAX_SDP_BYTES,
    400, "invalid_sdp", "ICE description must be 1 to 4096 ASCII bytes.");
  requireCond(/^[\x20-\x7e\r\n]+$/.test(raw),
    400, "invalid_sdp", "ICE description must contain printable ASCII only.");
  return raw;
}

async function deriveIdentity(pubkeyHex) {
  const pubBytes = unhex(pubkeyHex, 32);
  const digest = await sha256Bytes(pubBytes);
  return {
    pubBytes,
    player_id: crockfordBase32(digest.subarray(0, 16)),
    tag: hex(digest.subarray(0, 2)).toUpperCase(),
  };
}

async function verifyEd25519(pubBytes, signatureHex, messageText) {
  let sigBytes;
  try {
    sigBytes = unhex(signatureHex, 64);
  } catch {
    return false;
  }
  try {
    const key = await crypto.subtle.importKey("raw", pubBytes, { name: "Ed25519" }, false, ["verify"]);
    return await crypto.subtle.verify("Ed25519", key, sigBytes, encoder.encode(messageText));
  } catch {
    return false;
  }
}

async function mintSessionToken(env, claims) {
  const key = await importHmacKey(secretKeyBytes(env.CE_SESSION_SECRET));
  const payload = encoder.encode(canonical(claims));
  const domain = encoder.encode("onslaught-ce-lobby-session-v1\0");
  const msg = new Uint8Array(domain.length + payload.length);
  msg.set(domain);
  msg.set(payload, domain.length);
  const sig = new Uint8Array(await crypto.subtle.sign("HMAC", key, msg));
  return `CES1.${base64UrlEncode(payload)}.${base64UrlEncode(sig)}`;
}

async function verifySessionToken(request, env) {
  const auth = request.headers.get("Authorization");
  requireCond(typeof auth === "string" && /^Bearer [A-Za-z0-9_.-]+$/.test(auth),
    401, "unauthorized", "Bearer session token required.");
  const token = auth.slice(7);
  requireCond(token.length <= 1536, 401, "unauthorized", "Invalid session token.");
  const parts = token.split(".");
  requireCond(parts.length === 3 && parts[0] === "CES1", 401, "unauthorized", "Invalid session token.");
  const key = await importHmacKey(secretKeyBytes(env.CE_SESSION_SECRET));
  let payloadBytes;
  let sigBytes;
  try {
    payloadBytes = base64UrlDecode(parts[1], 1024);
    sigBytes = base64UrlDecode(parts[2], 32);
    requireCond(sigBytes.length === 32, 401, "unauthorized", "Invalid session token.");
  } catch {
    throw new ApiError(401, "unauthorized", "Invalid session token.");
  }
  const domain = encoder.encode("onslaught-ce-lobby-session-v1\0");
  const msg = new Uint8Array(domain.length + payloadBytes.length);
  msg.set(domain);
  msg.set(payloadBytes, domain.length);
  const valid = await crypto.subtle.verify("HMAC", key, sigBytes, msg);
  requireCond(valid, 401, "unauthorized", "Invalid session token.");
  let claims;
  try {
    claims = strictJson(payloadBytes, 1024);
    assertExactFields(claims, ["v", "pid", "tag", "cs", "pk", "bid", "cid", "iat", "exp"]);
    requireCond(claims.v === 1, 401, "unauthorized", "Unsupported session version.");
    requireCond(typeof claims.pid === "string" && /^[0-9a-hjkmnp-tv-z]{26}$/.test(claims.pid),
      401, "unauthorized", "Invalid session identity.");
    requireCond(typeof claims.tag === "string" && /^[0-9A-F]{4}$/.test(claims.tag),
      401, "unauthorized", "Invalid session tag.");
    requireCond(integer(claims.iat) && integer(claims.exp) && claims.iat < claims.exp,
      401, "unauthorized", "Invalid session timestamps.");
  } catch (err) {
    if (err instanceof ApiError && err.httpStatus === 503) throw err;
    throw new ApiError(401, "unauthorized", "Invalid session token.");
  }
  const now = nowSeconds();
  requireCond(claims.iat <= now + 30 && now < claims.exp,
    401, "session_expired", "Session token has expired.");
  return {
    player_id: claims.pid,
    tag: claims.tag,
    callsign: claims.cs,
    pubkey: claims.pk,
    build_id: claims.bid,
    content_id: claims.cid,
    issued_at: claims.iat,
    expires_at: claims.exp,
  };
}

async function hashClientIp(request, env) {
  const cfIp = request.headers.get("CF-Connecting-IP");
  const raw = cfIp ||
    (env.CE_LOCAL_HTTP === "true" ? request.headers.get("X-Forwarded-For")?.split(",")[0].trim() : null) ||
    "127.0.0.1";
  const key = await importHmacKey(secretKeyBytes(env.CE_SESSION_SECRET));
  const sig = new Uint8Array(await crypto.subtle.sign("HMAC", key, encoder.encode(`ce-lobby-ip-v1\0${raw}`)));
  return hex(sig).slice(0, 32);
}

async function hashRoomPassword(saltHex, password) {
  return sha256Hex(encoder.encode(`onslaught-ce-room-pw-v1\0${saltHex}\0${password}`));
}

async function verifyRoomPassword(saltHex, expectedHashHex, candidatePassword) {
  const actual = await hashRoomPassword(saltHex, candidatePassword);
  const a = encoder.encode(actual);
  const b = encoder.encode(expectedHashHex);
  if (a.length !== b.length) return false;
  let diff = 0;
  for (let i = 0; i < a.length; i++) diff |= a[i] ^ b[i];
  return diff === 0;
}

export async function signTurnPermit(env, permit) {
  const key = await importHmacKey(secretKeyBytes(env.CE_ISSUER_PERMIT_KEY));
  const msg = encoder.encode("Onslaught CE online lobby TURN permit v1\0" + canonical(permit));
  return base64UrlEncode(new Uint8Array(await crypto.subtle.sign("HMAC", key, msg)));
}

export async function verifyTurnPermit(env, permit, signature) {
  assertExactFields(permit, [
    "schema", "room_id", "match_id", "attempt", "requester_seat", "guest_seat",
    "require_relay", "issued_at", "expires_at", "lease_until", "ttl",
  ]);
  requireCond(permit.schema === 1, 403, "permit_invalid", "Invalid permit schema.");
  requireCond(typeof permit.room_id === "string" && /^rm_[0-9a-f]{16}$/.test(permit.room_id),
    403, "permit_invalid", "Invalid permit room.");
  requireCond(typeof permit.match_id === "string" && /^mt_[0-9a-f]{16}$/.test(permit.match_id),
    403, "permit_invalid", "Invalid permit match.");
  requireCond(typeof permit.attempt === "string" && /^[0-9a-f]{32}$/.test(permit.attempt),
    403, "permit_invalid", "Invalid permit attempt.");
  requireCond(integer(permit.requester_seat) && permit.requester_seat < WIRE_MAX_SLOTS,
    403, "permit_invalid", "Invalid permit requester seat.");
  requireCond(integer(permit.guest_seat) && permit.guest_seat < WIRE_MAX_SLOTS,
    403, "permit_invalid", "Invalid permit guest seat.");
  requireCond(typeof permit.require_relay === "boolean",
    403, "permit_invalid", "Invalid permit relay policy.");
  requireCond([permit.issued_at, permit.expires_at, permit.lease_until, permit.ttl].every(integer),
    403, "permit_invalid", "Invalid permit timestamps.");
  requireCond(permit.ttl >= 60 && permit.ttl <= MAX_TURN_TTL_SECONDS &&
    permit.expires_at === permit.issued_at + permit.ttl &&
    permit.lease_until <= permit.issued_at + 15 &&
    permit.lease_until <= permit.expires_at,
  403, "permit_invalid", "Invalid permit lifetime.");
  requireCond(typeof signature === "string", 403, "permit_invalid", "Missing permit signature.");
  let sigBytes;
  try {
    sigBytes = base64UrlDecode(signature, 32);
  } catch {
    throw new ApiError(403, "permit_invalid", "Invalid permit signature.");
  }
  requireCond(sigBytes.length === 32, 403, "permit_invalid", "Invalid permit signature.");
  const key = await importHmacKey(secretKeyBytes(env.CE_ISSUER_PERMIT_KEY));
  const msg = encoder.encode("Onslaught CE online lobby TURN permit v1\0" + canonical(permit));
  const ok = await crypto.subtle.verify("HMAC", key, sigBytes, msg);
  requireCond(ok, 403, "permit_invalid", "Invalid permit signature.");
}

function utcPeriodKeys(unixSeconds) {
  const d = new Date(unixSeconds * 1000);
  const yyyy = String(d.getUTCFullYear()).padStart(4, "0");
  const mm = String(d.getUTCMonth() + 1).padStart(2, "0");
  const dd = String(d.getUTCDate()).padStart(2, "0");
  return { dayKey: `day:${yyyy}-${mm}-${dd}`, monthKey: `month:${yyyy}-${mm}` };
}

function buildTeams(mode, maxSeats) {
  if (mode === "versus_2v2") {
    const forsetiCap = Math.ceil(maxSeats / 2);
    const muspellCap = Math.floor(maxSeats / 2);
    return [
      { team: 0, side: "forseti", cap: forsetiCap },
      { team: 1, side: "muspell", cap: muspellCap },
    ];
  }
  return [
    { team: 0, side: "forseti", cap: maxSeats },
    { team: 1, side: "muspell", cap: 0 },
  ];
}

function defaultSeatTeam(mode, seatIndex) {
  return mode === "versus_2v2" ? (seatIndex % 2) : 0;
}

function makeEmptySeat(seatIndex, mode, aiFill) {
  return {
    seat: seatIndex,
    kind: aiFill ? "ai" : "open",
    player_id: "",
    tag: "",
    callsign: aiFill ? `AI Pilot ${seatIndex + 1}` : "",
    team: defaultSeatTeam(mode, seatIndex),
    be_cfg: "",
    ready: false,
    rtt_ms: 0,
    route: "unknown",
  };
}

function summarizeSeats(room) {
  let humanSeats = 0;
  let usedSeats = 0;
  for (const s of room.seats) {
    if (s.kind === "human") {
      humanSeats++;
      usedSeats++;
    } else if (s.kind === "closed") {
      usedSeats++;
    } else if (s.kind === "ai" && !room.ai_fill) {
      usedSeats++;
    }
  }
  return { humanSeats, usedSeats };
}

function generateRoomCode() {
  const bytes = crypto.getRandomValues(new Uint8Array(6));
  let code = "";
  for (let i = 0; i < 6; i++) {
    code += ROOM_CODE_ALPHABET[bytes[i] % ROOM_CODE_ALPHABET.length];
  }
  return code;
}

function parseValidatedUrl(request, env) {
  requireCond(!request.url.includes("..") && !/%2e/i.test(request.url) && !/%2f/i.test(request.url) &&
    !request.url.includes("\\") && !/[\x00-\x1f\x7f]/.test(request.url),
  400, "invalid_path", "Malformed request path.");
  let url;
  try {
    url = new URL(request.url);
  } catch {
    throw new ApiError(400, "invalid_path", "Malformed request URL.");
  }
  const localHttp = env.CE_LOCAL_HTTP === "true" && url.protocol === "http:" &&
    (url.hostname === "127.0.0.1" || url.hostname === "localhost");
  requireCond((url.protocol === "https:" || localHttp) && !url.hash,
    400, "invalid_protocol", "HTTPS is required.");
  requireCond(url.pathname.startsWith(PREFIX), 404, "not_found", "Unknown endpoint.");
  return url;
}

function parseNonNegativeIntParam(raw, fallback, maxVal = 1000000000) {
  if (raw === null || raw === "") return fallback;
  requireCond(/^(0|[1-9][0-9]*)$/.test(raw), 400, "invalid_param", "Query parameter must be a non-negative integer.");
  const value = Number(raw);
  requireCond(Number.isSafeInteger(value) && value >= 0 && value <= maxVal,
    400, "invalid_param", "Query parameter is out of range.");
  return value;
}

function directoryStub(env) {
  requireCond(env.LOBBY_DIRECTORY, 503, "service_unavailable", "Directory binding is missing.");
  return env.LOBBY_DIRECTORY.get(env.LOBBY_DIRECTORY.idFromName("lobby-directory-v1"));
}

function roomStub(env, roomId) {
  requireCond(env.GAME_ROOM, 503, "service_unavailable", "Room binding is missing.");
  return env.GAME_ROOM.get(env.GAME_ROOM.idFromName(roomId));
}

// ---------------------------------------------------------------------------
// Public Worker fetch handler
// ---------------------------------------------------------------------------
export default {
  async fetch(request, env) {
    try {
      const url = parseValidatedUrl(request, env);
      const subPath = url.pathname.slice(PREFIX.length);

      // Kill switch: return 503 for EVERY route when CE_ENABLED !== "true".
      if (env.CE_ENABLED !== "true") {
        if (subPath === "health" && request.method === "GET") {
          return jsonResponse({
            ok: false,
            enabled: false,
            proto: PROTO_VERSION,
            max_match_slots: MAX_MATCH_SLOTS,
            wire_max_slots: WIRE_MAX_SLOTS,
            server_time_ms: Date.now(),
            active_rooms: 0,
            error: "service_unavailable",
            message: "Online lobby service is disabled.",
          }, 503);
        }
        return errorResponse(503, "service_unavailable", "Online lobby service is disabled.");
      }

      // Public unauthenticated health endpoint.
      if (subPath === "health") {
        requireCond(request.method === "GET", 400, "method_not_allowed", "GET required for health.");
        const info = await directoryStub(env).getHealth();
        return jsonResponse({
          ok: true,
          enabled: true,
          proto: PROTO_VERSION,
          max_match_slots: MAX_MATCH_SLOTS,
          wire_max_slots: WIRE_MAX_SLOTS,
          server_time_ms: Date.now(),
          active_rooms: info.active_rooms,
        }, 200);
      }

      // Challenge-response Ed25519 auth endpoints (no Bearer token).
      if (subPath === "auth/challenge") {
        requireCond(request.method === "POST", 400, "method_not_allowed", "POST required.");
        const body = await readJsonBody(request);
        assertAllowedFields(body, new Set(["pubkey", "callsign", "build_id", "content_id"]), ["pubkey", "callsign"]);
        const pubkey = body.pubkey;
        unhex(pubkey, 32);
        const callsign = sanitizeCallsign(body.callsign);
        sanitizeBuildId(body.build_id);
        sanitizeContentId(body.content_id);
        const ipHash = await hashClientIp(request, env);
        const challenge = await directoryStub(env).createChallenge(ipHash, pubkey, callsign);
        if (!challenge.ok) {
          throw new ApiError(challenge.httpStatus, challenge.errorCode, challenge.message);
        }
        return jsonResponse({
          nonce: challenge.nonce,
          issued_at: challenge.issued_at,
          expires_at: challenge.expires_at,
          server_time_ms: Date.now(),
        }, 200);
      }

      if (subPath === "auth/session") {
        requireCond(request.method === "POST", 400, "method_not_allowed", "POST required.");
        const body = await readJsonBody(request);
        assertAllowedFields(body,
          new Set(["pubkey", "callsign", "nonce", "issued_at", "signature", "build_id", "content_id"]),
          ["pubkey", "callsign", "nonce", "issued_at", "signature"]);
        const pubkey = body.pubkey;
        const { pubBytes, player_id, tag } = await deriveIdentity(pubkey);
        const cleanCallsign = sanitizeCallsign(body.callsign);
        requireCond(typeof body.nonce === "string" && /^[0-9a-f]{32}$/.test(body.nonce),
          401, "invalid_nonce", "Invalid authentication nonce.");
        requireCond(integer(body.issued_at), 401, "invalid_timestamp", "Invalid authentication timestamp.");
        requireCond(typeof body.signature === "string" && /^[0-9a-f]{128}$/.test(body.signature),
          401, "invalid_signature", "Invalid Ed25519 signature format.");
        const buildId = sanitizeBuildId(body.build_id);
        const contentId = sanitizeContentId(body.content_id);
        const ipHash = await hashClientIp(request, env);

        const now = nowSeconds();
        requireCond(Math.abs(now - body.issued_at) <= AUTH_WINDOW_SECONDS,
          401, "timestamp_expired", "Authentication timestamp is outside the 120-second window.");

        // Check rate limits & nonce existence BEFORE signature verification, then verify Ed25519,
        // then atomically consume the single-use nonce in SQLite.
        const pre = await directoryStub(env).checkChallenge(ipHash, pubkey, body.nonce, cleanCallsign, body.issued_at);
        if (!pre.ok) {
          throw new ApiError(pre.httpStatus, pre.errorCode, pre.message);
        }

        const canonicalMsg = `onslaught-ce-lobby-auth-v1\n${body.nonce}\n${body.issued_at}\n${pubkey}\n${body.callsign}`;
        const sigOk = await verifyEd25519(pubBytes, body.signature, canonicalMsg);
        requireCond(sigOk, 401, "invalid_signature", "Ed25519 signature verification failed.");

        const consumed = await directoryStub(env).consumeChallenge(pubkey, body.nonce, cleanCallsign, body.issued_at);
        if (!consumed.ok) {
          throw new ApiError(consumed.httpStatus, consumed.errorCode, consumed.message);
        }

        const sessionTtl = envPositiveInt(env.CE_SESSION_SECONDS, DEFAULT_SESSION_SECONDS, 60, MAX_SESSION_SECONDS);
        const issuedAt = nowSeconds();
        const expiresAt = issuedAt + sessionTtl;
        const sessionToken = await mintSessionToken(env, {
          v: 1,
          pid: player_id,
          tag,
          cs: cleanCallsign,
          pk: pubkey,
          bid: buildId,
          cid: contentId,
          iat: issuedAt,
          exp: expiresAt,
        });
        return jsonResponse({
          session_token: sessionToken,
          player_id,
          tag,
          callsign: cleanCallsign,
          expires_at: expiresAt,
          server_time_ms: Date.now(),
        }, 200);
      }

      // All remaining routes require Bearer authentication.
      const session = await verifySessionToken(request, env);

      // Lobby directory routes.
      if (subPath === "lobbies") {
        if (request.method === "GET") {
          const cursor = parseNonNegativeIntParam(url.searchParams.get("cursor"), 0, 100000);
          const limit = parseNonNegativeIntParam(url.searchParams.get("limit"), 16, 32);
          requireCond(limit >= 1 && limit <= 32, 400, "invalid_param", "Limit must be between 1 and 32.");
          const reqBuildId = url.searchParams.has("build_id")
            ? sanitizeBuildId(url.searchParams.get("build_id"))
            : session.build_id;
          const reqContentId = url.searchParams.has("content_id")
            ? sanitizeContentId(url.searchParams.get("content_id"))
            : session.content_id;
          const incRaw = url.searchParams.get("include_incompatible") ?? "0";
          requireCond(incRaw === "0" || incRaw === "1", 400, "invalid_param",
            "include_incompatible must be 0 or 1.");
          const listing = await directoryStub(env).listLobbies({
            cursor,
            limit,
            build_id: reqBuildId,
            content_id: reqContentId,
            include_incompatible: incRaw === "1",
          });
          return jsonResponse({
            proto: PROTO_VERSION,
            server_time_ms: Date.now(),
            next_cursor: listing.next_cursor,
            rooms: listing.rooms,
          }, 200);
        }

        if (request.method === "POST") {
          const body = await readJsonBody(request);
          assertAllowedFields(body, new Set([
            "name", "mode", "map", "max_seats", "topology", "visibility",
            "password", "require_relay", "ai_fill", "build_id", "content_id", "be_cfg",
          ]), ["name", "mode", "map", "max_seats"]);

          const name = sanitizeRoomName(body.name);
          const mode = body.mode;
          requireCond(typeof mode === "string" && VALID_MODES.has(mode),
            400, "invalid_mode", "Unsupported game mode.");
          requireCond(integer(body.map) && VALID_MAPS.has(body.map),
            400, "invalid_map", "Unsupported map identifier.");
          const topology = body.topology === undefined ? "star_lockstep" : body.topology;
          requireCond(topology === "star_lockstep",
            400, "invalid_topology", "Only star_lockstep topology is currently enabled.");
          const maxSeats = body.max_seats;
          requireCond(integer(maxSeats) && maxSeats >= 2 && maxSeats <= MAX_MATCH_SLOTS,
            400, "invalid_max_seats", "max_seats must be 2 to 4 for star_lockstep.");
          const visibility = body.visibility === undefined ? "public" : body.visibility;
          requireCond(typeof visibility === "string" && VALID_VISIBILITIES.has(visibility),
            400, "invalid_visibility", "visibility must be public or unlisted.");
          const password = sanitizePassword(body.password);
          const requireRelay = body.require_relay === undefined ? false : body.require_relay;
          requireCond(typeof requireRelay === "boolean", 400, "invalid_request", "require_relay must be boolean.");
          const aiFill = body.ai_fill === undefined ? false : body.ai_fill;
          requireCond(typeof aiFill === "boolean", 400, "invalid_request", "ai_fill must be boolean.");
          const buildId = body.build_id !== undefined ? sanitizeBuildId(body.build_id) : session.build_id;
          const contentId = body.content_id !== undefined ? sanitizeContentId(body.content_id) : session.content_id;
          const beCfg = sanitizeBeCfg(body.be_cfg);

          const ipHash = await hashClientIp(request, env);
          const alloc = await directoryStub(env).allocateRoomSlot(session.player_id, ipHash);
          if (!alloc.ok) {
            throw new ApiError(alloc.httpStatus, alloc.errorCode, alloc.message);
          }

          let passwordSalt = "";
          let passwordHash = "";
          if (password.length > 0) {
            passwordSalt = randomHex(16);
            passwordHash = await hashRoomPassword(passwordSalt, password);
          }

          const initResult = await roomStub(env, alloc.room_id).createRoom({
            room_id: alloc.room_id,
            code: alloc.code,
            name,
            mode,
            map: body.map,
            max_seats: maxSeats,
            topology,
            visibility,
            has_password: password.length > 0,
            password_salt: passwordSalt,
            password_hash: passwordHash,
            require_relay: requireRelay,
            ai_fill: aiFill,
            build_id: buildId,
            content_id: contentId,
            host: {
              player_id: session.player_id,
              tag: session.tag,
              callsign: session.callsign,
              be_cfg: beCfg,
            },
          });
          if (!initResult.ok) {
            throw new ApiError(initResult.httpStatus, initResult.errorCode, initResult.message);
          }
          await directoryStub(env).upsertRoomSummary(initResult.summary);
          return jsonResponse(initResult.state, 200);
        }

        throw new ApiError(400, "method_not_allowed", "Method not allowed on /lobbies.");
      }

      if (subPath === "lobbies/resolve") {
        requireCond(request.method === "POST", 400, "method_not_allowed", "POST required.");
        const body = await readJsonBody(request);
        assertExactFields(body, ["code"]);
        requireCond(typeof body.code === "string", 400, "invalid_code", "Room code must be a string.");
        const code = body.code.trim().toUpperCase();
        requireCond(/^[A-Z0-9]{6}$/.test(code), 400, "invalid_code", "Room code must be 6 alphanumeric characters.");
        const resolved = await directoryStub(env).resolveCode(code);
        if (!resolved.ok) {
          throw new ApiError(resolved.httpStatus, resolved.errorCode, resolved.message);
        }
        return jsonResponse({
          room_id: resolved.room_id,
          has_password: resolved.has_password,
          state: resolved.state,
        }, 200);
      }

      // Per-room routes: rooms/:id and rooms/:id/:action
      if (subPath.startsWith("rooms/")) {
        const parts = subPath.slice("rooms/".length).split("/");
        requireCond(parts.length === 1 || parts.length === 2, 404, "not_found", "Unknown room route.");
        const roomId = parts[0];
        const action = parts.length === 2 ? parts[1] : "";
        requireCond(/^rm_[0-9a-f]{16}$/.test(roomId), 400, "invalid_room_id", "Invalid room identifier.");
        const stub = roomStub(env, roomId);

        if (action === "") {
          requireCond(request.method === "GET", 400, "method_not_allowed", "GET required for room poll.");
          const hasSince = url.searchParams.has("since");
          const since = hasSince ? parseNonNegativeIntParam(url.searchParams.get("since"), 0, 1000000000) : null;
          const hasRtt = url.searchParams.has("rtt_ms");
          const rttMs = hasRtt ? parseNonNegativeIntParam(url.searchParams.get("rtt_ms"), 0, 9999) : null;
          const res = await stub.pollRoom(session, since, rttMs);
          if (!res.ok) throw new ApiError(res.httpStatus, res.errorCode, res.message);
          return jsonResponse(res.payload, 200);
        }

        if (action === "join") {
          requireCond(request.method === "POST", 400, "method_not_allowed", "POST required.");
          const body = await readJsonBody(request, { allowEmpty: true });
          assertAllowedFields(body, new Set(["password", "build_id", "content_id", "be_cfg"]));
          const payload = {
            password: sanitizePassword(body.password),
            build_id: body.build_id !== undefined ? sanitizeBuildId(body.build_id) : session.build_id,
            content_id: body.content_id !== undefined ? sanitizeContentId(body.content_id) : session.content_id,
            be_cfg: sanitizeBeCfg(body.be_cfg),
          };
          const res = await stub.joinRoom(session, payload);
          if (!res.ok) throw new ApiError(res.httpStatus, res.errorCode, res.message);
          return jsonResponse(res.state, 200);
        }

        if (action === "leave") {
          requireCond(request.method === "POST", 400, "method_not_allowed", "POST required.");
          const body = await readJsonBody(request, { allowEmpty: true });
          assertAllowedFields(body, new Set([]));
          const res = await stub.leaveRoom(session);
          if (!res.ok) throw new ApiError(res.httpStatus, res.errorCode, res.message);
          return jsonResponse(res.state, 200);
        }

        if (action === "update") {
          requireCond(request.method === "POST", 400, "method_not_allowed", "POST required.");
          const body = await readJsonBody(request);
          assertAllowedFields(body, new Set([
            "team", "be_cfg", "ready", "rtt_ms", "route",
            "name", "map", "mode", "max_seats", "visibility",
            "require_relay", "ai_fill", "set_seat_kind", "kick_seat", "state",
          ]));
          // Convert null-prototype parsed body to plain object for DO RPC serialization.
          const plain = JSON.parse(canonical(body));
          const res = await stub.updateRoom(session, plain);
          if (!res.ok) throw new ApiError(res.httpStatus, res.errorCode, res.message);
          return jsonResponse(res.state, 200);
        }

        if (action === "chat") {
          requireCond(request.method === "POST", 400, "method_not_allowed", "POST required.");
          const body = await readJsonBody(request);
          assertExactFields(body, ["text"]);
          const clean = sanitizeChatText(body.text);
          const res = await stub.postChat(session, clean);
          if (!res.ok) throw new ApiError(res.httpStatus, res.errorCode, res.message);
          return jsonResponse(res.state, 200);
        }

        if (action === "start") {
          requireCond(request.method === "POST", 400, "method_not_allowed", "POST required.");
          const body = await readJsonBody(request, { allowEmpty: true });
          assertAllowedFields(body, new Set([]));
          const res = await stub.startMatch(session);
          if (!res.ok) throw new ApiError(res.httpStatus, res.errorCode, res.message);
          return jsonResponse(res.state, 200);
        }

        if (action === "finish") {
          requireCond(request.method === "POST", 400, "method_not_allowed", "POST required.");
          const body = await readJsonBody(request, { allowEmpty: true });
          assertAllowedFields(body, new Set([]));
          const res = await stub.finishMatch(session);
          if (!res.ok) throw new ApiError(res.httpStatus, res.errorCode, res.message);
          return jsonResponse(res.state, 200);
        }

        if (action === "signal") {
          if (request.method === "POST") {
            const body = await readJsonBody(request);
            assertExactFields(body, ["match_id", "from_seat", "to_seat", "kind", "sdp"]);
            requireCond(typeof body.match_id === "string" && /^mt_[0-9a-f]{16}$/.test(body.match_id),
              400, "invalid_match_id", "Invalid match identifier.");
            requireCond(integer(body.from_seat) && body.from_seat < WIRE_MAX_SLOTS,
              400, "invalid_seat", "Invalid from_seat.");
            requireCond(integer(body.to_seat) && body.to_seat < WIRE_MAX_SLOTS,
              400, "invalid_seat", "Invalid to_seat.");
            requireCond(body.kind === "offer" || body.kind === "answer",
              400, "invalid_signal_kind", "Signal kind must be offer or answer.");
            const sdp = sanitizeSdp(body.sdp);
            const res = await stub.postSignal(session, {
              match_id: body.match_id,
              from_seat: body.from_seat,
              to_seat: body.to_seat,
              kind: body.kind,
              sdp,
            });
            if (!res.ok) throw new ApiError(res.httpStatus, res.errorCode, res.message);
            return jsonResponse({ ok: true, seq: res.seq, server_time_ms: Date.now() }, 200);
          }
          if (request.method === "GET") {
            const matchId = url.searchParams.get("match_id");
            requireCond(typeof matchId === "string" && /^mt_[0-9a-f]{16}$/.test(matchId),
              400, "invalid_match_id", "match_id query parameter is required.");
            const since = parseNonNegativeIntParam(url.searchParams.get("since"), 0, 1000000000);
            const res = await stub.getSignals(session, matchId, since);
            if (!res.ok) throw new ApiError(res.httpStatus, res.errorCode, res.message);
            return jsonResponse({ seq: res.seq, messages: res.messages }, 200);
          }
          throw new ApiError(400, "method_not_allowed", "GET or POST required on /signal.");
        }

        if (action === "turn") {
          requireCond(request.method === "POST", 400, "method_not_allowed", "POST required.");
          const body = await readJsonBody(request);
          assertExactFields(body, ["match_id", "guest_seat"]);
          requireCond(typeof body.match_id === "string" && /^mt_[0-9a-f]{16}$/.test(body.match_id),
            400, "invalid_match_id", "Invalid match identifier.");
          requireCond(integer(body.guest_seat) && body.guest_seat < WIRE_MAX_SLOTS,
            400, "invalid_seat", "Invalid guest_seat.");
          const ipHash = await hashClientIp(request, env);
          const res = await stub.requestTurn(session, body.match_id, body.guest_seat, ipHash);
          if (!res.ok) throw new ApiError(res.httpStatus, res.errorCode, res.message);
          return jsonResponse({ iceServers: res.iceServers, ttl: res.ttl }, 200);
        }

        throw new ApiError(404, "not_found", "Unknown room action.");
      }

      throw new ApiError(404, "not_found", "Unknown endpoint.");
    } catch (error) {
      return handleFailure(error);
    }
  },
};

// ---------------------------------------------------------------------------
// Private PermitAuthority Entrypoint (called ONLY via Service Binding by issuer.mjs)
// ---------------------------------------------------------------------------
export class PermitAuthority extends WorkerEntrypoint {
  async fetch(request) {
    try {
      if (this.env.CE_ENABLED !== "true") {
        return errorResponse(503, "service_unavailable", "Service disabled.");
      }
      const url = new URL(request.url);
      if (request.method !== "POST" || url.pathname !== "/consume") {
        return errorResponse(400, "invalid_request", "POST /consume required.");
      }
      const value = strictJson(await bodyBytes(request, 2048), 2048);
      assertExactFields(value, ["permit", "signature"]);
      await verifyTurnPermit(this.env, value.permit, value.signature);
      const now = nowSeconds();
      if (now < value.permit.issued_at || now >= value.permit.lease_until || now >= value.permit.expires_at) {
        return errorResponse(403, "permit_expired", "Permit lease has expired.");
      }

      // Plain object for Durable Object RPC.
      const permit = JSON.parse(canonical(value.permit));

      // Step 1: Atomically verify room state (>=2 humans, starting/in_match) and burn one-time permit.
      const roomRes = await roomStub(this.env, permit.room_id).consumePermit(permit, value.signature);
      if (!roomRes.ok) {
        return errorResponse(403, "permit_rejected", "Permit could not be consumed.");
      }

      // Step 2: Atomically check & increment global daily/monthly and per-IP daily TURN caps in SQLite.
      const budgetRes = await directoryStub(this.env).consumeTurnBudget(now, roomRes.ipHash || "");
      if (!budgetRes.ok) {
        return errorResponse(503, "turn_cap_exceeded", "Global TURN issuance cap reached.");
      }

      return new Response(null, { status: 204, headers: { "Cache-Control": "no-store" } });
    } catch {
      return errorResponse(403, "permit_rejected", "Permit authority rejected request.");
    }
  }
}

// ---------------------------------------------------------------------------
// LobbyDirectory SQLite Durable Object (singleton)
// ---------------------------------------------------------------------------
export class LobbyDirectory extends DurableObject {
  constructor(ctx, env) {
    super(ctx, env);
    this.ctx = ctx;
    this.env = env;
    ctx.blockConcurrencyWhile(() => this.initSchema());
  }

  initSchema() {
    const sql = this.ctx.storage.sql;
    sql.exec(`
      CREATE TABLE IF NOT EXISTS auth_nonces (
        nonce TEXT PRIMARY KEY,
        pubkey TEXT NOT NULL,
        callsign TEXT NOT NULL,
        issued_at INTEGER NOT NULL,
        expires_at INTEGER NOT NULL,
        used INTEGER NOT NULL DEFAULT 0
      );
      CREATE TABLE IF NOT EXISTS rate_limits (
        bucket_key TEXT PRIMARY KEY,
        window_start INTEGER NOT NULL,
        count INTEGER NOT NULL
      );
      CREATE TABLE IF NOT EXISTS rooms (
        room_id TEXT PRIMARY KEY,
        code TEXT UNIQUE NOT NULL,
        name TEXT NOT NULL,
        host_player_id TEXT NOT NULL,
        host_callsign TEXT NOT NULL,
        host_tag TEXT NOT NULL,
        topology TEXT NOT NULL,
        mode TEXT NOT NULL,
        map INTEGER NOT NULL,
        used_seats INTEGER NOT NULL,
        human_seats INTEGER NOT NULL,
        max_seats INTEGER NOT NULL,
        visibility TEXT NOT NULL,
        has_password INTEGER NOT NULL,
        require_relay INTEGER NOT NULL,
        state TEXT NOT NULL,
        proto INTEGER NOT NULL,
        build_id TEXT NOT NULL,
        content_id TEXT NOT NULL,
        created_at INTEGER NOT NULL,
        updated_at INTEGER NOT NULL,
        last_heartbeat_ms INTEGER NOT NULL
      );
      CREATE TABLE IF NOT EXISTS turn_budgets (
        period_key TEXT PRIMARY KEY,
        used INTEGER NOT NULL
      );
    `);
  }

  heartbeatTimeoutMs() {
    return envPositiveInt(this.env.CE_HEARTBEAT_TIMEOUT_MS, DEFAULT_HEARTBEAT_TIMEOUT_MS, 500, 300000);
  }

  pruneStaleRooms(nowMs) {
    const cutoff = nowMs - this.heartbeatTimeoutMs();
    this.ctx.storage.sql.exec(
      "DELETE FROM rooms WHERE state = 'closed' OR last_heartbeat_ms < ?",
      cutoff
    );
  }

  checkAndBumpRate(bucketKey, nowSec, windowSec, limit) {
    const sql = this.ctx.storage.sql;
    const rows = sql.exec("SELECT window_start, count FROM rate_limits WHERE bucket_key = ?", bucketKey).toArray();
    if (rows.length === 0 || nowSec - rows[0].window_start >= windowSec) {
      sql.exec(
        "INSERT INTO rate_limits(bucket_key, window_start, count) VALUES(?, ?, 1) " +
        "ON CONFLICT(bucket_key) DO UPDATE SET window_start = excluded.window_start, count = 1",
        bucketKey, nowSec
      );
      return true;
    }
    if (rows[0].count >= limit) return false;
    sql.exec("UPDATE rate_limits SET count = count + 1 WHERE bucket_key = ?", bucketKey);
    return true;
  }

  pruneRateLimits(nowSec) {
    this.ctx.storage.sql.exec("DELETE FROM rate_limits WHERE window_start < ?", nowSec - 3600);
  }

  getHealth() {
    const nowMs = Date.now();
    return this.ctx.storage.transactionSync(() => {
      this.pruneStaleRooms(nowMs);
      const rows = this.ctx.storage.sql.exec("SELECT COUNT(*) AS cnt FROM rooms WHERE state != 'closed'").toArray();
      return { active_rooms: rows.length ? Number(rows[0].cnt) : 0 };
    });
  }

  createChallenge(ipHash, pubkey, callsign) {
    const now = nowSeconds();
    const ipLimit = envPositiveInt(this.env.CE_AUTH_IP_LIMIT, DEFAULT_AUTH_IP_LIMIT, 1, 1000);
    const pkLimit = envPositiveInt(this.env.CE_AUTH_PUBKEY_LIMIT, DEFAULT_AUTH_PUBKEY_LIMIT, 1, 1000);
    const nonce = randomHex(16);
    const expiresAt = now + AUTH_WINDOW_SECONDS;

    const allowed = this.ctx.storage.transactionSync(() => {
      this.pruneRateLimits(now);
      this.ctx.storage.sql.exec("DELETE FROM auth_nonces WHERE expires_at < ?", now - 30);
      if (!this.checkAndBumpRate(`auth_ip:${ipHash}`, now, AUTH_RATE_WINDOW_SECONDS, ipLimit)) {
        return false;
      }
      if (!this.checkAndBumpRate(`auth_pk:${pubkey}`, now, AUTH_RATE_WINDOW_SECONDS, pkLimit)) {
        return false;
      }
      this.ctx.storage.sql.exec(
        "INSERT INTO auth_nonces(nonce, pubkey, callsign, issued_at, expires_at, used) VALUES(?, ?, ?, ?, ?, 0)",
        nonce, pubkey, callsign, now, expiresAt
      );
      return true;
    });
    if (!allowed) {
      return {
        ok: false,
        httpStatus: 429,
        errorCode: "rate_limited",
        message: "Too many authentication requests. Please wait.",
      };
    }
    return { ok: true, nonce, issued_at: now, expires_at: expiresAt };
  }

  checkChallenge(ipHash, pubkey, nonce, callsign, issuedAt) {
    const now = nowSeconds();
    const ipLimit = envPositiveInt(this.env.CE_AUTH_IP_LIMIT, DEFAULT_AUTH_IP_LIMIT, 1, 1000);
    const pkLimit = envPositiveInt(this.env.CE_AUTH_PUBKEY_LIMIT, DEFAULT_AUTH_PUBKEY_LIMIT, 1, 1000);
    return this.ctx.storage.transactionSync(() => {
      if (!this.checkAndBumpRate(`sess_ip:${ipHash}`, now, AUTH_RATE_WINDOW_SECONDS, ipLimit)) {
        return { ok: false, httpStatus: 429, errorCode: "rate_limited", message: "Too many authentication requests." };
      }
      if (!this.checkAndBumpRate(`sess_pk:${pubkey}`, now, AUTH_RATE_WINDOW_SECONDS, pkLimit)) {
        return { ok: false, httpStatus: 429, errorCode: "rate_limited", message: "Too many authentication requests." };
      }
      const rows = this.ctx.storage.sql.exec(
        "SELECT pubkey, callsign, issued_at, expires_at, used FROM auth_nonces WHERE nonce = ?",
        nonce
      ).toArray();
      if (rows.length === 0) {
        return { ok: false, httpStatus: 401, errorCode: "invalid_nonce", message: "Authentication nonce is unknown or expired." };
      }
      const row = rows[0];
      if (row.used !== 0) {
        return { ok: false, httpStatus: 401, errorCode: "nonce_replayed", message: "Authentication nonce has already been used." };
      }
      if (now > row.expires_at) {
        return { ok: false, httpStatus: 401, errorCode: "nonce_expired", message: "Authentication nonce has expired." };
      }
      if (row.pubkey !== pubkey || row.callsign !== callsign || row.issued_at !== issuedAt) {
        return { ok: false, httpStatus: 401, errorCode: "nonce_mismatch", message: "Authentication challenge fields do not match." };
      }
      return { ok: true };
    });
  }

  consumeChallenge(pubkey, nonce, callsign, issuedAt) {
    const now = nowSeconds();
    return this.ctx.storage.transactionSync(() => {
      const rows = this.ctx.storage.sql.exec(
        "SELECT pubkey, callsign, issued_at, expires_at, used FROM auth_nonces WHERE nonce = ?",
        nonce
      ).toArray();
      if (rows.length === 0 || rows[0].used !== 0 || now > rows[0].expires_at ||
          rows[0].pubkey !== pubkey || rows[0].callsign !== callsign || rows[0].issued_at !== issuedAt) {
        return { ok: false, httpStatus: 401, errorCode: "nonce_replayed", message: "Authentication nonce is no longer valid." };
      }
      this.ctx.storage.sql.exec("UPDATE auth_nonces SET used = 1 WHERE nonce = ?", nonce);
      return { ok: true };
    });
  }

  allocateRoomSlot(playerId, ipHash = "") {
    const now = nowSeconds();
    const nowMs = Date.now();
    const maxRooms = envPositiveInt(this.env.CE_MAX_ACTIVE_ROOMS, DEFAULT_MAX_ACTIVE_ROOMS, 1, 2048);
    const createLimit = envPositiveInt(this.env.CE_ROOM_CREATE_LIMIT, ROOM_CREATE_LIMIT_PER_MIN, 1, 200);
    return this.ctx.storage.transactionSync(() => {
      this.pruneStaleRooms(nowMs);
      if (!this.checkAndBumpRate(`create:${playerId}`, now, 60, createLimit)) {
        return { ok: false, httpStatus: 429, errorCode: "rate_limited", message: "Room creation rate limit exceeded." };
      }
      if (ipHash && !this.checkAndBumpRate(`create_ip:${ipHash}`, now, 60, createLimit)) {
        return { ok: false, httpStatus: 429, errorCode: "rate_limited", message: "Room creation rate limit exceeded." };
      }
      const cntRows = this.ctx.storage.sql.exec("SELECT COUNT(*) AS cnt FROM rooms WHERE state != 'closed'").toArray();
      if (cntRows.length && Number(cntRows[0].cnt) >= maxRooms) {
        return { ok: false, httpStatus: 503, errorCode: "directory_full", message: "Active lobby limit reached." };
      }
      for (let attempt = 0; attempt < 16; attempt++) {
        const roomId = `rm_${randomHex(8)}`;
        const code = generateRoomCode();
        const clash = this.ctx.storage.sql.exec(
          "SELECT 1 FROM rooms WHERE room_id = ? OR code = ?", roomId, code
        ).toArray();
        if (clash.length === 0) {
          return { ok: true, room_id: roomId, code };
        }
      }
      return { ok: false, httpStatus: 503, errorCode: "allocation_failed", message: "Unable to allocate room code." };
    });
  }

  upsertRoomSummary(summary) {
    const nowMs = Date.now();
    this.ctx.storage.transactionSync(() => {
      this.pruneStaleRooms(nowMs);
      if (summary.state === "closed") {
        this.ctx.storage.sql.exec("DELETE FROM rooms WHERE room_id = ?", summary.room_id);
        return;
      }
      this.ctx.storage.sql.exec(
        `INSERT INTO rooms(
          room_id, code, name, host_player_id, host_callsign, host_tag, topology, mode, map,
          used_seats, human_seats, max_seats, visibility, has_password, require_relay, state,
          proto, build_id, content_id, created_at, updated_at, last_heartbeat_ms
        ) VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        ON CONFLICT(room_id) DO UPDATE SET
          name = excluded.name,
          host_callsign = excluded.host_callsign,
          host_tag = excluded.host_tag,
          topology = excluded.topology,
          mode = excluded.mode,
          map = excluded.map,
          used_seats = excluded.used_seats,
          human_seats = excluded.human_seats,
          max_seats = excluded.max_seats,
          visibility = excluded.visibility,
          has_password = excluded.has_password,
          require_relay = excluded.require_relay,
          state = excluded.state,
          build_id = excluded.build_id,
          content_id = excluded.content_id,
          updated_at = excluded.updated_at,
          last_heartbeat_ms = excluded.last_heartbeat_ms`,
        summary.room_id, summary.code, summary.name, summary.host_player_id,
        summary.host_callsign, summary.host_tag, summary.topology, summary.mode, summary.map,
        summary.used_seats, summary.human_seats, summary.max_seats, summary.visibility,
        summary.has_password ? 1 : 0, summary.require_relay ? 1 : 0, summary.state,
        PROTO_VERSION, summary.build_id, summary.content_id,
        summary.created_at, summary.updated_at, nowMs
      );
    });
    return { ok: true };
  }

  touchHeartbeat(roomId) {
    const nowMs = Date.now();
    this.ctx.storage.transactionSync(() => {
      this.ctx.storage.sql.exec(
        "UPDATE rooms SET last_heartbeat_ms = ? WHERE room_id = ? AND state != 'closed'",
        nowMs, roomId
      );
    });
    return { ok: true };
  }

  listLobbies(query) {
    const nowMs = Date.now();
    return this.ctx.storage.transactionSync(() => {
      this.pruneStaleRooms(nowMs);
      const rows = this.ctx.storage.sql.exec(
        `SELECT room_id, code, name, host_callsign, host_tag, topology, mode, map,
                used_seats, human_seats, max_seats, has_password, require_relay, state,
                proto, build_id, content_id, updated_at
         FROM rooms
         WHERE visibility = 'public' AND state != 'closed'
         ORDER BY CASE WHEN state = 'lobby' THEN 0 ELSE 1 END, created_at DESC, room_id ASC`
      ).toArray();

      const filtered = [];
      for (const r of rows) {
        const compatible = (r.build_id === query.build_id) && (r.content_id === query.content_id);
        if (!compatible && !query.include_incompatible) continue;
        filtered.push({
          room_id: r.room_id,
          code: r.code,
          name: r.name,
          host_callsign: r.host_callsign,
          host_tag: r.host_tag,
          topology: r.topology,
          mode: r.mode,
          map: Number(r.map),
          used_seats: Number(r.used_seats),
          human_seats: Number(r.human_seats),
          max_seats: Number(r.max_seats),
          has_password: Boolean(r.has_password),
          require_relay: Boolean(r.require_relay),
          state: r.state,
          proto: Number(r.proto),
          build_id: r.build_id,
          content_id: r.content_id,
          compatible,
          updated_at: Number(r.updated_at),
        });
      }

      const page = filtered.slice(query.cursor, query.cursor + query.limit);
      const nextCursor = (query.cursor + query.limit < filtered.length) ? (query.cursor + query.limit) : 0;
      return { rooms: page, next_cursor: nextCursor };
    });
  }

  resolveCode(code) {
    const nowMs = Date.now();
    return this.ctx.storage.transactionSync(() => {
      this.pruneStaleRooms(nowMs);
      const rows = this.ctx.storage.sql.exec(
        "SELECT room_id, has_password, state FROM rooms WHERE code = ? AND state != 'closed'",
        code
      ).toArray();
      if (rows.length === 0) {
        return { ok: false, httpStatus: 404, errorCode: "room_not_found", message: "No active room matches that code." };
      }
      return {
        ok: true,
        room_id: rows[0].room_id,
        has_password: Boolean(rows[0].has_password),
        state: rows[0].state,
      };
    });
  }

  consumeTurnBudget(unixSeconds, ipHash = "") {
    const dailyLimit = envPositiveInt(this.env.CE_DAILY_TURN_LIMIT, DEFAULT_DAILY_TURN_LIMIT, 0, 1000000);
    const monthlyLimit = envPositiveInt(this.env.CE_MONTHLY_TURN_LIMIT, DEFAULT_MONTHLY_TURN_LIMIT, 0, 10000000);
    const perIpDailyLimit = envPositiveInt(this.env.CE_PER_IP_DAILY_TURN_LIMIT, DEFAULT_PER_IP_DAILY_TURN_LIMIT, 0, 1000000);
    const { dayKey, monthKey } = utcPeriodKeys(unixSeconds);
    const ipDayKey = ipHash ? `ip:${dayKey}:${ipHash}` : "";
    return this.ctx.storage.transactionSync(() => {
      const sql = this.ctx.storage.sql;
      const dayRows = sql.exec("SELECT used FROM turn_budgets WHERE period_key = ?", dayKey).toArray();
      const monthRows = sql.exec("SELECT used FROM turn_budgets WHERE period_key = ?", monthKey).toArray();
      const dayUsed = dayRows.length ? Number(dayRows[0].used) : 0;
      const monthUsed = monthRows.length ? Number(monthRows[0].used) : 0;
      let ipDayUsed = 0;
      if (ipDayKey) {
        const ipRows = sql.exec("SELECT used FROM turn_budgets WHERE period_key = ?", ipDayKey).toArray();
        ipDayUsed = ipRows.length ? Number(ipRows[0].used) : 0;
      }
      if (dayUsed >= dailyLimit || monthUsed >= monthlyLimit || (ipDayKey && ipDayUsed >= perIpDailyLimit)) {
        return { ok: false, dayUsed, monthUsed, ipDayUsed };
      }
      sql.exec(
        "INSERT INTO turn_budgets(period_key, used) VALUES(?, 1) ON CONFLICT(period_key) DO UPDATE SET used = used + 1",
        dayKey
      );
      sql.exec(
        "INSERT INTO turn_budgets(period_key, used) VALUES(?, 1) ON CONFLICT(period_key) DO UPDATE SET used = used + 1",
        monthKey
      );
      if (ipDayKey) {
        sql.exec(
          "INSERT INTO turn_budgets(period_key, used) VALUES(?, 1) ON CONFLICT(period_key) DO UPDATE SET used = used + 1",
          ipDayKey
        );
      }
      return { ok: true, dayUsed: dayUsed + 1, monthUsed: monthUsed + 1, ipDayUsed: ipDayUsed + 1 };
    });
  }
}

// ---------------------------------------------------------------------------
// GameRoom SQLite Durable Object (one per room_id)
// ---------------------------------------------------------------------------
export class GameRoom extends DurableObject {
  constructor(ctx, env) {
    super(ctx, env);
    this.ctx = ctx;
    this.env = env;
    ctx.blockConcurrencyWhile(() => {
      this.ctx.storage.sql.exec("CREATE TABLE IF NOT EXISTS room_kv (name TEXT PRIMARY KEY, value TEXT NOT NULL)");
    });
  }

  readRoom() {
    const rows = this.ctx.storage.sql.exec("SELECT value FROM room_kv WHERE name = 'room'").toArray();
    return rows.length ? JSON.parse(rows[0].value) : null;
  }

  writeRoom(room) {
    this.ctx.storage.sql.exec(
      "INSERT INTO room_kv(name, value) VALUES('room', ?) ON CONFLICT(name) DO UPDATE SET value = excluded.value",
      JSON.stringify(room)
    );
  }

  findMemberSeat(room, playerId) {
    for (const s of room.seats) {
      if (s.kind === "human" && s.player_id === playerId) return s;
    }
    return null;
  }

  buildSummary(room) {
    const { humanSeats, usedSeats } = summarizeSeats(room);
    const hostSeat = room.seats[room.host_seat];
    return {
      room_id: room.room_id,
      code: room.code,
      name: room.name,
      host_player_id: hostSeat ? hostSeat.player_id : "",
      host_callsign: hostSeat ? hostSeat.callsign : "",
      host_tag: hostSeat ? hostSeat.tag : "",
      topology: room.topology,
      mode: room.mode,
      map: room.map,
      used_seats: usedSeats,
      human_seats: humanSeats,
      max_seats: room.max_seats,
      visibility: room.visibility,
      has_password: room.has_password,
      require_relay: room.require_relay,
      state: room.state,
      build_id: room.build_id,
      content_id: room.content_id,
      created_at: room.created_at,
      updated_at: room.updated_at,
    };
  }

  formatRoomState(room, isMember) {
    let ticket = null;
    if (room.start_ticket) {
      ticket = {
        match_id: room.start_ticket.match_id,
        seed: room.start_ticket.seed,
        level: room.start_ticket.level,
        mode: room.start_ticket.mode,
        topology: room.start_ticket.topology,
        require_relay: room.start_ticket.require_relay,
        roster_hash: room.start_ticket.roster_hash,
        match_secret: isMember ? room.start_ticket.match_secret : "",
        issued_at: room.start_ticket.issued_at,
        expires_at: room.start_ticket.expires_at,
        slots: room.start_ticket.slots,
      };
    }
    return {
      proto: PROTO_VERSION,
      room_id: room.room_id,
      code: room.code,
      rev: room.rev,
      state: room.state,
      name: room.name,
      topology: room.topology,
      mode: room.mode,
      map: room.map,
      max_seats: room.max_seats,
      host_seat: room.host_seat,
      visibility: room.visibility,
      has_password: room.has_password,
      require_relay: room.require_relay,
      ai_fill: room.ai_fill,
      build_id: room.build_id,
      content_id: room.content_id,
      server_time_ms: Date.now(),
      teams: buildTeams(room.mode, room.max_seats),
      seats: room.seats.map(s => ({
        seat: s.seat,
        kind: s.kind,
        player_id: s.player_id,
        tag: s.tag,
        callsign: s.callsign,
        team: s.team,
        be_cfg: s.be_cfg,
        ready: s.ready,
        rtt_ms: s.rtt_ms,
        route: s.route,
      })),
      chat: room.chat,
      start_ticket: ticket,
    };
  }

  async syncDirectory(room) {
    await directoryStub(this.env).upsertRoomSummary(this.buildSummary(room));
  }

  createRoom(spec) {
    const now = nowSeconds();
    const nowMs = Date.now();
    return this.ctx.storage.transactionSync(() => {
      const existing = this.readRoom();
      if (existing) {
        return { ok: false, httpStatus: 409, errorCode: "room_exists", message: "Room already exists." };
      }
      const seats = [];
      seats.push({
        seat: 0,
        kind: "human",
        player_id: spec.host.player_id,
        tag: spec.host.tag,
        callsign: spec.host.callsign,
        team: 0,
        be_cfg: spec.host.be_cfg,
        ready: false,
        rtt_ms: 0,
        route: "unknown",
      });
      for (let i = 1; i < spec.max_seats; i++) {
        seats.push(makeEmptySeat(i, spec.mode, spec.ai_fill));
      }
      const room = {
        room_id: spec.room_id,
        code: spec.code,
        rev: 1,
        state: "lobby",
        name: spec.name,
        topology: spec.topology,
        mode: spec.mode,
        map: spec.map,
        max_seats: spec.max_seats,
        host_seat: 0,
        visibility: spec.visibility,
        has_password: spec.has_password,
        password_salt: spec.password_salt,
        password_hash: spec.password_hash,
        require_relay: spec.require_relay,
        ai_fill: spec.ai_fill,
        build_id: spec.build_id,
        content_id: spec.content_id,
        created_at: now,
        updated_at: now,
        last_heartbeat_ms: nowMs,
        last_dir_heartbeat_ms: nowMs,
        seats,
        kicked_players: [],
        chat_seq: 0,
        chat: [],
        chat_rates: {},
        start_ticket: null,
        signal_seq: 0,
        signals: [],
        permits: {},
        turn_counts: {},
      };
      this.writeRoom(room);
      return {
        ok: true,
        state: this.formatRoomState(room, true),
        summary: this.buildSummary(room),
      };
    });
  }

  async pollRoom(session, since, rttMs) {
    const nowMs = Date.now();
    const hbSyncInterval = Math.min(5000, Math.floor(
      envPositiveInt(this.env.CE_HEARTBEAT_TIMEOUT_MS, DEFAULT_HEARTBEAT_TIMEOUT_MS, 500, 300000) / 4
    ));
    const outcome = this.ctx.storage.transactionSync(() => {
      const room = this.readRoom();
      if (!room) {
        return { ok: false, httpStatus: 404, errorCode: "room_not_found", message: "Room does not exist." };
      }
      if (room.kicked_players.includes(session.player_id)) {
        return { ok: false, httpStatus: 403, errorCode: "kicked", message: "You were kicked from this room." };
      }
      const member = this.findMemberSeat(room, session.player_id);
      let touchDir = false;
      if (member) {
        let dirty = false;
        if (rttMs !== null && member.rtt_ms !== rttMs) {
          member.rtt_ms = rttMs;
          room.rev++;
          room.updated_at = nowSeconds();
          dirty = true;
        }
        if (member.seat === room.host_seat && room.state !== "closed") {
          room.last_heartbeat_ms = nowMs;
          if (nowMs - room.last_dir_heartbeat_ms >= hbSyncInterval) {
            room.last_dir_heartbeat_ms = nowMs;
            touchDir = true;
            dirty = true;
          }
        }
        if (dirty) this.writeRoom(room);
      }
      if (since !== null && room.rev <= since) {
        return {
          ok: true,
          touchDir,
          roomId: room.room_id,
          payload: { unchanged: true, rev: room.rev, server_time_ms: Date.now() },
        };
      }
      return {
        ok: true,
        touchDir,
        roomId: room.room_id,
        payload: this.formatRoomState(room, Boolean(member)),
      };
    });
    if (outcome.ok && outcome.touchDir) {
      await directoryStub(this.env).touchHeartbeat(outcome.roomId);
    }
    return outcome;
  }

  async joinRoom(session, payload) {
    const pre = this.ctx.storage.transactionSync(() => {
      const room = this.readRoom();
      if (!room) return { ok: false, httpStatus: 404, errorCode: "room_not_found", message: "Room does not exist." };
      if (room.state === "closed") {
        return { ok: false, httpStatus: 409, errorCode: "room_closed", message: "Room has been closed." };
      }
      if (room.state !== "lobby") {
        return { ok: false, httpStatus: 409, errorCode: "match_in_progress", message: "Room is already in a match." };
      }
      if (room.kicked_players.includes(session.player_id)) {
        return { ok: false, httpStatus: 403, errorCode: "kicked", message: "You were kicked from this room." };
      }
      const existing = this.findMemberSeat(room, session.player_id);
      if (existing) {
        return { ok: true, alreadyMember: true, state: this.formatRoomState(room, true) };
      }
      if (payload.build_id !== room.build_id || payload.content_id !== room.content_id) {
        return {
          ok: false,
          httpStatus: 409,
          errorCode: "incompatible_version",
          message: "Game build or content version does not match the room.",
        };
      }
      return {
        ok: true,
        alreadyMember: false,
        has_password: room.has_password,
        password_salt: room.password_salt,
        password_hash: room.password_hash,
      };
    });
    if (!pre.ok || pre.alreadyMember) return pre;

    if (pre.has_password) {
      const matched = await verifyRoomPassword(pre.password_salt, pre.password_hash, payload.password);
      if (!matched) {
        return { ok: false, httpStatus: 403, errorCode: "wrong_password", message: "Incorrect room password." };
      }
    }

    const joined = this.ctx.storage.transactionSync(() => {
      const room = this.readRoom();
      if (!room || room.state !== "lobby") {
        return { ok: false, httpStatus: 409, errorCode: "match_in_progress", message: "Room is no longer joinable." };
      }
      if (room.kicked_players.includes(session.player_id)) {
        return { ok: false, httpStatus: 403, errorCode: "kicked", message: "You were kicked from this room." };
      }
      const already = this.findMemberSeat(room, session.player_id);
      if (already) return { ok: true, state: this.formatRoomState(room, true), syncRoom: null };

      let target = room.seats.find(s => s.kind === "open");
      if (!target && room.ai_fill) {
        target = room.seats.find(s => s.kind === "ai");
      }
      if (!target) {
        return { ok: false, httpStatus: 409, errorCode: "room_full", message: "Room is full." };
      }
      target.kind = "human";
      target.player_id = session.player_id;
      target.tag = session.tag;
      target.callsign = session.callsign;
      target.be_cfg = payload.be_cfg;
      target.ready = false;
      target.rtt_ms = 0;
      target.route = "unknown";
      room.rev++;
      room.updated_at = nowSeconds();
      this.writeRoom(room);
      return { ok: true, state: this.formatRoomState(room, true), syncRoom: room };
    });
    if (joined.ok && joined.syncRoom) {
      await this.syncDirectory(joined.syncRoom);
    }
    return joined;
  }

  async leaveRoom(session) {
    const res = this.ctx.storage.transactionSync(() => {
      const room = this.readRoom();
      if (!room) return { ok: false, httpStatus: 404, errorCode: "room_not_found", message: "Room does not exist." };
      const member = this.findMemberSeat(room, session.player_id);
      if (!member) {
        return { ok: false, httpStatus: 403, errorCode: "not_a_member", message: "You are not seated in this room." };
      }
      if (member.seat === room.host_seat) {
        room.state = "closed";
        room.start_ticket = null;
        room.signals = [];
        room.permits = {};
      } else {
        const replacement = makeEmptySeat(member.seat, room.mode, room.ai_fill);
        replacement.team = member.team;
        room.seats[member.seat] = replacement;
      }
      room.rev++;
      room.updated_at = nowSeconds();
      this.writeRoom(room);
      return { ok: true, state: this.formatRoomState(room, false), syncRoom: room };
    });
    if (res.ok) await this.syncDirectory(res.syncRoom);
    return res;
  }

  async updateRoom(session, body) {
    const HOST_ONLY_KEYS = [
      "name", "map", "mode", "max_seats", "visibility",
      "require_relay", "ai_fill", "set_seat_kind", "kick_seat", "state",
    ];
    const res = this.ctx.storage.transactionSync(() => {
      const room = this.readRoom();
      if (!room) return { ok: false, httpStatus: 404, errorCode: "room_not_found", message: "Room does not exist." };
      if (room.state === "closed") {
        return { ok: false, httpStatus: 409, errorCode: "room_closed", message: "Room is closed." };
      }
      const member = this.findMemberSeat(room, session.player_id);
      if (!member) {
        return { ok: false, httpStatus: 403, errorCode: "not_a_member", message: "You are not seated in this room." };
      }
      const isHost = member.seat === room.host_seat;
      const hasHostFields = HOST_ONLY_KEYS.some(k => Object.hasOwn(body, k));
      if (hasHostFields && !isHost) {
        return { ok: false, httpStatus: 403, errorCode: "host_only", message: "Only the host may change room settings." };
      }

      let resetGuestReady = false;
      let syncDir = false;

      // Self-seat fields.
      if (Object.hasOwn(body, "team")) {
        if (room.state !== "lobby") {
          return { ok: false, httpStatus: 409, errorCode: "match_in_progress", message: "Cannot change team during match." };
        }
        if (body.team !== 0 && body.team !== 1) {
          return { ok: false, httpStatus: 400, errorCode: "invalid_team", message: "Team must be 0 or 1." };
        }
        if (room.mode !== "versus_2v2" && body.team !== 0) {
          return { ok: false, httpStatus: 400, errorCode: "invalid_team", message: "Side selection is only available in 2v2 Versus." };
        }
        if (room.mode === "versus_2v2") {
          const teamCap = Math.max(1, Math.ceil(room.max_seats / 2));
          if (member.team !== body.team) {
            const humansOnTarget = room.seats.filter(
              s => s.kind === "human" && s.seat !== member.seat && s.team === body.team
            ).length;
            if (humansOnTarget >= teamCap) {
              return { ok: false, httpStatus: 409, errorCode: "team_full", message: "That side is already full." };
            }
            member.team = body.team;
            // Rebalance non-human seats (AI/open) so 2H+2AI stays 2v2 rather than drifting to 3v1.
            let t0 = room.seats.filter(s => s.team === 0).length;
            let t1 = room.seats.filter(s => s.team === 1).length;
            if (body.team === 0 && t0 > teamCap) {
              const swap = room.seats.find(s => s.kind !== "human" && s.team === 0);
              if (swap) { swap.team = 1; t0--; t1++; }
            } else if (body.team === 1 && t1 > teamCap) {
              const swap = room.seats.find(s => s.kind !== "human" && s.team === 1);
              if (swap) { swap.team = 0; t1--; t0++; }
            }
          }
        } else {
          member.team = 0;
        }
      }
      if (Object.hasOwn(body, "be_cfg")) {
        if (room.state !== "lobby") {
          return { ok: false, httpStatus: 409, errorCode: "match_in_progress", message: "Cannot change vehicle during match." };
        }
        try {
          member.be_cfg = sanitizeBeCfg(body.be_cfg);
        } catch (err) {
          if (err instanceof ApiError) {
            return { ok: false, httpStatus: err.httpStatus, errorCode: err.errorCode, message: err.safeMessage };
          }
          throw err;
        }
      }
      if (Object.hasOwn(body, "ready")) {
        if (room.state !== "lobby") {
          return { ok: false, httpStatus: 409, errorCode: "match_in_progress", message: "Cannot change ready state during match." };
        }
        if (typeof body.ready !== "boolean") {
          return { ok: false, httpStatus: 400, errorCode: "invalid_request", message: "ready must be boolean." };
        }
        member.ready = body.ready;
      }
      if (Object.hasOwn(body, "rtt_ms")) {
        if (!integer(body.rtt_ms) || body.rtt_ms > 9999) {
          return { ok: false, httpStatus: 400, errorCode: "invalid_request", message: "rtt_ms must be 0..9999." };
        }
        member.rtt_ms = body.rtt_ms;
      }
      if (Object.hasOwn(body, "route")) {
        if (typeof body.route !== "string" || !VALID_ROUTES.has(body.route)) {
          return { ok: false, httpStatus: 400, errorCode: "invalid_route", message: "Invalid route value." };
        }
        member.route = body.route;
      }

      // Host-only fields.
      if (Object.hasOwn(body, "state")) {
        if (room.state === "starting" && body.state === "in_match") {
          room.state = "in_match";
          syncDir = true;
        } else {
          return { ok: false, httpStatus: 400, errorCode: "invalid_state", message: "Unsupported state transition." };
        }
      }

      const lobbyOnlyHostKeys = [
        "name", "map", "mode", "max_seats", "visibility",
        "require_relay", "ai_fill", "set_seat_kind", "kick_seat",
      ];
      if (lobbyOnlyHostKeys.some(k => Object.hasOwn(body, k)) && room.state !== "lobby") {
        return { ok: false, httpStatus: 409, errorCode: "match_in_progress", message: "Room settings are locked during match." };
      }

      if (Object.hasOwn(body, "name")) {
        try {
          room.name = sanitizeRoomName(body.name);
        } catch (err) {
          if (err instanceof ApiError) {
            return { ok: false, httpStatus: err.httpStatus, errorCode: err.errorCode, message: err.safeMessage };
          }
          throw err;
        }
        syncDir = true;
      }
      if (Object.hasOwn(body, "map")) {
        if (!integer(body.map) || !VALID_MAPS.has(body.map)) {
          return { ok: false, httpStatus: 400, errorCode: "invalid_map", message: "Unsupported map identifier." };
        }
        if (room.map !== body.map) {
          room.map = body.map;
          resetGuestReady = true;
          syncDir = true;
        }
      }
      if (Object.hasOwn(body, "mode")) {
        if (typeof body.mode !== "string" || !VALID_MODES.has(body.mode)) {
          return { ok: false, httpStatus: 400, errorCode: "invalid_mode", message: "Unsupported game mode." };
        }
        if (room.mode !== body.mode) {
          room.mode = body.mode;
          if (room.mode === "versus_2v2") {
            let team0 = 0;
            let team1 = 0;
            const cap = Math.max(1, Math.ceil(room.max_seats / 2));
            for (const s of room.seats) {
              if (s.kind === "human") {
                if (s.team === 1 && team1 < cap) {
                  team1++;
                } else if (team0 < cap) {
                  s.team = 0;
                  team0++;
                } else {
                  s.team = 1;
                  team1++;
                }
              }
            }
            for (const s of room.seats) {
              if (s.kind !== "human") {
                if (team0 <= team1 && team0 < cap) {
                  s.team = 0;
                  team0++;
                } else {
                  s.team = 1;
                  team1++;
                }
              }
            }
          } else {
            for (const s of room.seats) {
              s.team = 0;
            }
          }
          resetGuestReady = true;
          syncDir = true;
        }
      }
      if (Object.hasOwn(body, "require_relay")) {
        if (typeof body.require_relay !== "boolean") {
          return { ok: false, httpStatus: 400, errorCode: "invalid_request", message: "require_relay must be boolean." };
        }
        if (room.require_relay !== body.require_relay) {
          room.require_relay = body.require_relay;
          resetGuestReady = true;
          syncDir = true;
        }
      }
      if (Object.hasOwn(body, "visibility")) {
        if (typeof body.visibility !== "string" || !VALID_VISIBILITIES.has(body.visibility)) {
          return { ok: false, httpStatus: 400, errorCode: "invalid_visibility", message: "Invalid visibility." };
        }
        room.visibility = body.visibility;
        syncDir = true;
      }
      if (Object.hasOwn(body, "ai_fill")) {
        if (typeof body.ai_fill !== "boolean") {
          return { ok: false, httpStatus: 400, errorCode: "invalid_request", message: "ai_fill must be boolean." };
        }
        if (room.ai_fill !== body.ai_fill) {
          room.ai_fill = body.ai_fill;
          for (const s of room.seats) {
            if (room.ai_fill && s.kind === "open") {
              s.kind = "ai";
              s.callsign = `AI Pilot ${s.seat + 1}`;
            } else if (!room.ai_fill && s.kind === "ai") {
              s.kind = "open";
              s.callsign = "";
            }
          }
          syncDir = true;
        }
      }
      if (Object.hasOwn(body, "max_seats")) {
        const cap = room.topology === "star_lockstep" ? MAX_MATCH_SLOTS : WIRE_MAX_SLOTS;
        if (!integer(body.max_seats) || body.max_seats < 2 || body.max_seats > cap) {
          return { ok: false, httpStatus: 400, errorCode: "invalid_max_seats", message: "Invalid max_seats value." };
        }
        if (body.max_seats < room.max_seats) {
          for (let i = body.max_seats; i < room.seats.length; i++) {
            if (room.seats[i].kind === "human") {
              return { ok: false, httpStatus: 409, errorCode: "seat_occupied", message: "Cannot remove an occupied human seat." };
            }
          }
          room.seats.length = body.max_seats;
        } else if (body.max_seats > room.max_seats) {
          for (let i = room.max_seats; i < body.max_seats; i++) {
            room.seats.push(makeEmptySeat(i, room.mode, room.ai_fill));
          }
        }
        room.max_seats = body.max_seats;
        if (room.mode === "versus_2v2") {
          let team0 = 0;
          let team1 = 0;
          const teamCap = Math.max(1, Math.ceil(room.max_seats / 2));
          for (const s of room.seats) {
            if (s.kind === "human") {
              if (s.team === 1 && team1 < teamCap) {
                team1++;
              } else if (team0 < teamCap) {
                s.team = 0;
                team0++;
              } else {
                s.team = 1;
                team1++;
              }
            }
          }
          for (const s of room.seats) {
            if (s.kind !== "human") {
              if (team0 <= team1 && team0 < teamCap) {
                s.team = 0;
                team0++;
              } else {
                s.team = 1;
                team1++;
              }
            }
          }
        }
        syncDir = true;
      }
      if (Object.hasOwn(body, "set_seat_kind")) {
        const spec = body.set_seat_kind;
        if (!spec || typeof spec !== "object" || Array.isArray(spec) ||
            !integer(spec.seat) || spec.seat >= room.seats.length ||
            typeof spec.kind !== "string" || !VALID_SEAT_SET_KINDS.has(spec.kind)) {
          return { ok: false, httpStatus: 400, errorCode: "invalid_seat_kind", message: "Invalid set_seat_kind payload." };
        }
        if (spec.seat === room.host_seat) {
          return { ok: false, httpStatus: 400, errorCode: "invalid_seat", message: "Cannot alter host seat kind." };
        }
        const target = room.seats[spec.seat];
        if (target.kind === "human") {
          return { ok: false, httpStatus: 409, errorCode: "seat_occupied", message: "Kick the human player before changing this seat." };
        }
        target.kind = spec.kind;
        target.player_id = "";
        target.tag = "";
        target.callsign = spec.kind === "ai" ? `AI Pilot ${spec.seat + 1}` : "";
        target.be_cfg = "";
        target.ready = false;
        target.rtt_ms = 0;
        target.route = "unknown";
        syncDir = true;
      }
      if (Object.hasOwn(body, "kick_seat")) {
        if (!integer(body.kick_seat) || body.kick_seat >= room.seats.length || body.kick_seat === room.host_seat) {
          return { ok: false, httpStatus: 400, errorCode: "invalid_seat", message: "Cannot kick that seat." };
        }
        const target = room.seats[body.kick_seat];
        if (target.kind !== "human") {
          return { ok: false, httpStatus: 404, errorCode: "seat_empty", message: "No human player occupies that seat." };
        }
        if (!room.kicked_players.includes(target.player_id)) {
          room.kicked_players.push(target.player_id);
        }
        const freed = makeEmptySeat(target.seat, room.mode, room.ai_fill);
        freed.team = target.team;
        room.seats[target.seat] = freed;
        syncDir = true;
      }

      if (resetGuestReady) {
        for (const s of room.seats) {
          if (s.seat !== room.host_seat) s.ready = false;
        }
      }

      room.rev++;
      room.updated_at = nowSeconds();
      this.writeRoom(room);
      return { ok: true, state: this.formatRoomState(room, true), syncRoom: syncDir ? room : null };
    });
    if (res.ok && res.syncRoom) await this.syncDirectory(res.syncRoom);
    return res;
  }

  postChat(session, cleanText) {
    const nowMs = Date.now();
    const now = nowSeconds();
    return this.ctx.storage.transactionSync(() => {
      const room = this.readRoom();
      if (!room) return { ok: false, httpStatus: 404, errorCode: "room_not_found", message: "Room does not exist." };
      if (room.state === "closed") {
        return { ok: false, httpStatus: 409, errorCode: "room_closed", message: "Room is closed." };
      }
      const member = this.findMemberSeat(room, session.player_id);
      if (!member) {
        return { ok: false, httpStatus: 403, errorCode: "not_a_member", message: "You are not seated in this room." };
      }
      const history = (room.chat_rates[session.player_id] || []).filter(ts => nowMs - ts < CHAT_RATE_WINDOW_MS);
      if (history.length >= CHAT_RATE_MAX) {
        return { ok: false, httpStatus: 429, errorCode: "rate_limited", message: "Chat rate limit exceeded." };
      }
      history.push(nowMs);
      room.chat_rates[session.player_id] = history;

      room.chat_seq++;
      room.chat.push({
        seq: room.chat_seq,
        seat: member.seat,
        player_id: session.player_id,
        callsign: session.callsign,
        tag: session.tag,
        text: cleanText,
        ts: now,
      });
      if (room.chat.length > CHAT_RING_LIMIT) {
        room.chat.splice(0, room.chat.length - CHAT_RING_LIMIT);
      }
      room.rev++;
      room.updated_at = now;
      this.writeRoom(room);
      return { ok: true, state: this.formatRoomState(room, true) };
    });
  }

  async startMatch(session) {
    const now = nowSeconds();
    const pre = this.ctx.storage.transactionSync(() => {
      const room = this.readRoom();
      if (!room) return { ok: false, httpStatus: 404, errorCode: "room_not_found", message: "Room does not exist." };
      if (room.state !== "lobby") {
        return { ok: false, httpStatus: 409, errorCode: "match_in_progress", message: "Match has already started or room is closed." };
      }
      const member = this.findMemberSeat(room, session.player_id);
      if (!member || member.seat !== room.host_seat) {
        return { ok: false, httpStatus: 403, errorCode: "host_only", message: "Only the host may start the match." };
      }
      const humans = room.seats.filter(s => s.kind === "human");
      if (humans.length < 2) {
        return { ok: false, httpStatus: 409, errorCode: "not_enough_players", message: "At least 2 human players are required to start." };
      }
      for (const h of humans) {
        if (h.seat !== room.host_seat && !h.ready) {
          return { ok: false, httpStatus: 409, errorCode: "players_not_ready", message: "All human players must be ready before starting." };
        }
      }
      if (room.mode === "versus_2v2") {
        let active0 = 0;
        let active1 = 0;
        for (const s of room.seats) {
          if (s.kind === "human" || s.kind === "ai") {
            if (s.team === 0) active0++;
            else if (s.team === 1) active1++;
          }
        }
        if (active0 < 1 || active1 < 1) {
          return { ok: false, httpStatus: 409, errorCode: "teams_unbalanced", message: "Both Forseti and Muspell need at least one active pilot in 2v2 Versus." };
        }
      }
      // Build deterministic seat -> sim_slot mapping: host = sim_slot 0, other humans next, then AI bots.
      const ordered = [];
      ordered.push(room.seats[room.host_seat]);
      for (const s of room.seats) {
        if (s.kind === "human" && s.seat !== room.host_seat) ordered.push(s);
      }
      for (const s of room.seats) {
        if (s.kind === "ai") ordered.push(s);
      }
      const slots = ordered.map((s, idx) => ({
        seat: s.seat,
        sim_slot: idx,
        kind: s.kind,
        player_id: s.player_id,
        callsign: s.callsign,
        tag: s.tag,
        team: s.team,
        be_cfg: s.be_cfg,
      }));
      return {
        ok: true,
        rev: room.rev,
        level: room.map,
        mode: room.mode,
        topology: room.topology,
        require_relay: room.require_relay,
        slots,
      };
    });
    if (!pre.ok) return pre;

    const rosterHash = await sha256Hex(encoder.encode(canonical({
      level: pre.level,
      mode: pre.mode,
      topology: pre.topology,
      require_relay: pre.require_relay,
      slots: pre.slots,
    })));
    const seedBytes = crypto.getRandomValues(new Uint8Array(4));
    const seed = new DataView(seedBytes.buffer).getUint32(0, false) || 1;
    const ticket = {
      match_id: `mt_${randomHex(8)}`,
      seed,
      level: pre.level,
      mode: pre.mode,
      topology: pre.topology,
      require_relay: pre.require_relay,
      roster_hash: rosterHash,
      match_secret: randomHex(32),
      issued_at: now,
      expires_at: now + 7200,
      slots: pre.slots,
    };

    const started = this.ctx.storage.transactionSync(() => {
      const room = this.readRoom();
      if (!room || room.state !== "lobby" || room.rev !== pre.rev) {
        return { ok: false, httpStatus: 409, errorCode: "state_changed", message: "Room state changed; retry start." };
      }
      room.seats[room.host_seat].ready = true;
      room.state = "starting";
      room.start_ticket = ticket;
      room.signal_seq = 0;
      room.signals = [];
      room.permits = {};
      room.turn_counts = {};
      room.rev++;
      room.updated_at = now;
      this.writeRoom(room);
      return { ok: true, state: this.formatRoomState(room, true), syncRoom: room };
    });
    if (started.ok) await this.syncDirectory(started.syncRoom);
    return started;
  }

  async finishMatch(session) {
    const now = nowSeconds();
    const res = this.ctx.storage.transactionSync(() => {
      const room = this.readRoom();
      if (!room) return { ok: false, httpStatus: 404, errorCode: "room_not_found", message: "Room does not exist." };
      if (room.state === "closed") {
        return { ok: false, httpStatus: 409, errorCode: "room_closed", message: "Room is closed." };
      }
      const member = this.findMemberSeat(room, session.player_id);
      if (!member || member.seat !== room.host_seat) {
        return { ok: false, httpStatus: 403, errorCode: "host_only", message: "Only the host may finish the match." };
      }
      room.state = "lobby";
      room.start_ticket = null;
      room.signals = [];
      room.signal_seq = 0;
      room.permits = {};
      room.turn_counts = {};
      for (const s of room.seats) s.ready = false;
      room.rev++;
      room.updated_at = now;
      this.writeRoom(room);
      return { ok: true, state: this.formatRoomState(room, true), syncRoom: room };
    });
    if (res.ok) await this.syncDirectory(res.syncRoom);
    return res;
  }

  postSignal(session, payload) {
    const now = nowSeconds();
    return this.ctx.storage.transactionSync(() => {
      const room = this.readRoom();
      if (!room) return { ok: false, httpStatus: 404, errorCode: "room_not_found", message: "Room does not exist." };
      if ((room.state !== "starting" && room.state !== "in_match") || !room.start_ticket) {
        return { ok: false, httpStatus: 409, errorCode: "not_starting", message: "Signaling is only available during match setup." };
      }
      const member = this.findMemberSeat(room, session.player_id);
      if (!member) {
        return { ok: false, httpStatus: 403, errorCode: "not_a_member", message: "You are not seated in this room." };
      }
      if (payload.match_id !== room.start_ticket.match_id) {
        return { ok: false, httpStatus: 409, errorCode: "match_mismatch", message: "Match identifier does not match active match." };
      }
      if (payload.from_seat !== member.seat) {
        return { ok: false, httpStatus: 403, errorCode: "seat_mismatch", message: "from_seat must match your own seat." };
      }
      if (payload.to_seat >= room.seats.length || payload.to_seat === payload.from_seat ||
          room.seats[payload.to_seat].kind !== "human") {
        return { ok: false, httpStatus: 400, errorCode: "invalid_target_seat", message: "to_seat must be another human seat." };
      }
      if (payload.from_seat !== room.host_seat && payload.to_seat !== room.host_seat) {
        return { ok: false, httpStatus: 400, errorCode: "star_topology_only", message: "Signals must travel between host and guest." };
      }
      room.signal_seq++;
      room.signals.push({
        seq: room.signal_seq,
        from_seat: payload.from_seat,
        to_seat: payload.to_seat,
        kind: payload.kind,
        sdp: payload.sdp,
        ts: now,
      });
      if (room.signals.length > SIGNAL_RING_LIMIT) {
        room.signals.splice(0, room.signals.length - SIGNAL_RING_LIMIT);
      }
      this.writeRoom(room);
      return { ok: true, seq: room.signal_seq };
    });
  }

  getSignals(session, matchId, since) {
    return this.ctx.storage.transactionSync(() => {
      const room = this.readRoom();
      if (!room) return { ok: false, httpStatus: 404, errorCode: "room_not_found", message: "Room does not exist." };
      if ((room.state !== "starting" && room.state !== "in_match") || !room.start_ticket) {
        return { ok: false, httpStatus: 409, errorCode: "not_starting", message: "Signaling is only available during match setup." };
      }
      const member = this.findMemberSeat(room, session.player_id);
      if (!member) {
        return { ok: false, httpStatus: 403, errorCode: "not_a_member", message: "You are not seated in this room." };
      }
      if (matchId !== room.start_ticket.match_id) {
        return { ok: false, httpStatus: 409, errorCode: "match_mismatch", message: "Match identifier does not match active match." };
      }
      const messages = room.signals
        .filter(m => m.to_seat === member.seat && m.seq > since)
        .map(m => ({
          seq: m.seq,
          from_seat: m.from_seat,
          to_seat: m.to_seat,
          kind: m.kind,
          sdp: m.sdp,
        }));
      const seq = messages.length > 0 ? messages[messages.length - 1].seq : since;
      return { ok: true, seq, messages };
    });
  }

  async consumePermit(permit, signature) {
    if (this.env.CE_ENABLED !== "true") return { ok: false };
    try {
      await verifyTurnPermit(this.env, permit, signature);
    } catch {
      return { ok: false };
    }
    const now = nowSeconds();
    return this.ctx.storage.transactionSync(() => {
      const room = this.readRoom();
      if (!room || (room.state !== "starting" && room.state !== "in_match") || !room.start_ticket) {
        return { ok: false };
      }
      if (room.start_ticket.match_id !== permit.match_id) return { ok: false };
      const humanCount = room.seats.filter(s => s.kind === "human").length;
      if (humanCount < 2) return { ok: false };
      const entry = room.permits[permit.attempt];
      if (!entry || entry.state !== "reserved" || entry.spent) return { ok: false };
      if (canonical(entry.permit) !== canonical(permit)) return { ok: false };
      if (now >= permit.lease_until || now >= permit.expires_at) return { ok: false };
      entry.spent = true;
      this.writeRoom(room);
      return { ok: true, ipHash: entry.ip_hash || "" };
    });
  }

  async requestTurn(session, matchId, guestSeat, ipHash = "") {
    if (this.env.CE_ENABLED !== "true" || !this.env.TURN_ISSUER) {
      return { ok: false, httpStatus: 503, errorCode: "turn_unavailable", message: "TURN relay service is unavailable." };
    }
    const ttl = envPositiveInt(this.env.CE_TURN_TTL_SECONDS, DEFAULT_TURN_TTL_SECONDS, 60, MAX_TURN_TTL_SECONDS);
    const now = nowSeconds();

    const reservation = this.ctx.storage.transactionSync(() => {
      const room = this.readRoom();
      if (!room) return { ok: false, httpStatus: 404, errorCode: "room_not_found", message: "Room does not exist." };
      if ((room.state !== "starting" && room.state !== "in_match") || !room.start_ticket) {
        return { ok: false, httpStatus: 409, errorCode: "not_starting", message: "TURN credentials require an active match." };
      }
      if (room.start_ticket.match_id !== matchId) {
        return { ok: false, httpStatus: 409, errorCode: "match_mismatch", message: "Match identifier does not match active match." };
      }
      const member = this.findMemberSeat(room, session.player_id);
      if (!member) {
        return { ok: false, httpStatus: 403, errorCode: "not_a_member", message: "You are not seated in this room." };
      }
      const humanCount = room.seats.filter(s => s.kind === "human").length;
      if (humanCount < 2) {
        return { ok: false, httpStatus: 409, errorCode: "not_enough_players", message: "TURN requires at least 2 human players." };
      }
      if (guestSeat >= room.seats.length || guestSeat === room.host_seat || room.seats[guestSeat].kind !== "human") {
        return { ok: false, httpStatus: 400, errorCode: "invalid_guest_seat", message: "guest_seat must be an active human guest." };
      }
      if (member.seat !== room.host_seat && member.seat !== guestSeat) {
        return { ok: false, httpStatus: 403, errorCode: "forbidden_seat", message: "Not authorized to request TURN for that seat." };
      }
      const pairKey = `${member.seat}:${guestSeat}`;
      const usedCount = room.turn_counts[pairKey] || 0;
      if (usedCount >= MAX_TURN_PER_PAIR_PER_MATCH) {
        return { ok: false, httpStatus: 429, errorCode: "rate_limited", message: "Per-match TURN credential limit reached." };
      }
      room.turn_counts[pairKey] = usedCount + 1;

      const permit = {
        schema: 1,
        room_id: room.room_id,
        match_id: matchId,
        attempt: randomHex(16),
        requester_seat: member.seat,
        guest_seat: guestSeat,
        require_relay: Boolean(room.require_relay),
        issued_at: now,
        expires_at: now + ttl,
        lease_until: now + 10,
        ttl,
      };
      room.permits[permit.attempt] = { state: "reserved", spent: false, permit, ip_hash: ipHash || "" };
      this.writeRoom(room);
      return { ok: true, permit };
    });
    if (!reservation.ok) return reservation;

    const { permit } = reservation;
    try {
      const signature = await signTurnPermit(this.env, permit);
      const reply = await this.env.TURN_ISSUER.fetch(new Request("https://issuer.invalid/issue", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ permit, signature }),
        signal: AbortSignal.timeout(10000),
      }));
      if (reply.status !== 200) {
        throw new Error("issuer refused");
      }
      const lease = strictJson(await bodyBytes(reply, 32768), 32768);
      assertExactFields(lease, ["iceServers", "ttl"]);
      requireCond(Array.isArray(lease.iceServers) && lease.iceServers.length >= 1 && lease.iceServers.length <= 12,
        503, "turn_unavailable", "Invalid TURN response.");
      requireCond(lease.ttl === permit.ttl, 503, "turn_unavailable", "Invalid TURN TTL.");

      const finalized = this.ctx.storage.transactionSync(() => {
        const room = this.readRoom();
        const entry = room?.permits?.[permit.attempt];
        if (!entry || !entry.spent || entry.state !== "reserved") return false;
        entry.state = "issued";
        this.writeRoom(room);
        return true;
      });
      if (!finalized) {
        return { ok: false, httpStatus: 503, errorCode: "turn_unavailable", message: "TURN permit was not spent atomically." };
      }
      return { ok: true, iceServers: JSON.parse(canonical(lease.iceServers)), ttl: permit.ttl };
    } catch {
      this.ctx.storage.transactionSync(() => {
        const room = this.readRoom();
        const entry = room?.permits?.[permit.attempt];
        if (entry && entry.state === "reserved") {
          entry.state = "failed";
          this.writeRoom(room);
        }
      });
      return { ok: false, httpStatus: 503, errorCode: "turn_unavailable", message: "TURN relay credentials are currently unavailable." };
    }
  }
}
