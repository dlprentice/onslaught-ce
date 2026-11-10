// SPDX-License-Identifier: GPL-3.0-only
// Closed-pair HTTP adapter. All spending and encryption counters live in one SQLite object.
import { DurableObject, WorkerEntrypoint } from "cloudflare:workers";
import { ParcelCipher, hex } from "./parcel_crypto.mjs";

const PREFIX = "/ce-signaling/v1/";
const AUDIENCE = "onslaught-ce-closed-pilot";
const ROUTES = new Set(["pair/current", "pair/accept", "pair/close", "stage/request",
  "stage/poll", "stage/record", "stage/ack"]);
const STATUSES = new Set(["admitted", "authentication_required", "awaiting_pair_consent",
  "awaiting_peer_request", "closed", "exchanging", "expired", "grant_expired", "invalid_request",
  "issuance_unknown", "issuing", "not_admitted", "rate_limited", "service_delivery_complete",
  "setup_expired", "unavailable"]);
const CODES = { authentication_required: 401, not_admitted: 403, invalid_request: 400,
  rate_limited: 429, unavailable: 503 };
const encoder = new TextEncoder();
const decoder = new TextDecoder("utf-8", { fatal: true });
const MAX_BODY = 65536;
const SETUP_SECONDS = 120;
const MAX_GRANT_SECONDS = 14400;
const MAX_ENCRYPTIONS = 100000;
const MAX_APPROVALS_PER_DAY = 24;
const PERMITS_PER_APPROVAL = 4;
const MAX_PERMITS_PER_DAY = 96;

export class Refused extends Error {
  constructor(status = "invalid_request") { super(status); this.status = status; }
}

export function require(value, status = "invalid_request") {
  if (!value) throw new Refused(status);
}

export function fields(value, names) {
  require(value && typeof value === "object" && !Array.isArray(value));
  const keys = Object.keys(value).sort();
  require(keys.length === names.length && keys.every((v, i) => v === [...names].sort()[i]));
  return value;
}

export function integer(value) { return Number.isSafeInteger(value) && value >= 0; }
export function nowSeconds() { return Math.floor(Date.now() / 1000); }
export function canonical(value) {
  if (Array.isArray(value)) return `[${value.map(canonical).join(",")}]`;
  if (value && typeof value === "object") return `{${Object.keys(value).sort()
    .map(k => `${JSON.stringify(k)}:${canonical(value[k])}`).join(",")}}`;
  return JSON.stringify(value);
}

// JSON.parse silently accepts duplicate names. This bounded parser rejects them at every level.
export function strictJson(bytes, maximum = MAX_BODY) {
  require(bytes.byteLength > 0 && bytes.byteLength <= maximum);
  let text;
  try { text = decoder.decode(bytes); } catch { throw new Refused(); }
  let offset = 0, tokens = 0;
  const space = () => { while (/[ \t\r\n]/.test(text[offset] || "") && offset < text.length) offset++; };
  function string() {
    const start = offset++;
    while (offset < text.length) {
      const ch = text[offset++];
      if (ch === "\\") offset++;
      else if (ch === '"') {
        try { return JSON.parse(text.slice(start, offset)); } catch { throw new Refused(); }
      }
    }
    throw new Refused();
  }
  function value(depth) {
    require(depth <= 12 && ++tokens <= 1024); space();
    const ch = text[offset];
    if (ch === '"') return string();
    if (ch === "{") {
      offset++; space(); const object = Object.create(null);
      if (text[offset] === "}") { offset++; return object; }
      while (true) {
        space(); require(text[offset] === '"'); const name = string();
        require(!Object.hasOwn(object, name)); space(); require(text[offset++] === ":");
        object[name] = value(depth + 1); space();
        if (text[offset] === "}") { offset++; return object; }
        require(text[offset++] === ",");
      }
    }
    if (ch === "[") {
      offset++; space(); const array = [];
      if (text[offset] === "]") { offset++; return array; }
      while (true) {
        require(array.length < 256); array.push(value(depth + 1)); space();
        if (text[offset] === "]") { offset++; return array; }
        require(text[offset++] === ",");
      }
    }
    const match = /^(?:true|false|null|-?(?:0|[1-9][0-9]*)(?:\.[0-9]+)?(?:[eE][+-]?[0-9]+)?)/.exec(text.slice(offset));
    require(match); offset += match[0].length;
    return JSON.parse(match[0]);
  }
  const result = value(0); space(); require(offset === text.length);
  return result;
}

export function base64(bytes, url = false) {
  let result = btoa(String.fromCharCode(...bytes));
  return url ? result.replaceAll("+", "-").replaceAll("/", "_").replace(/=+$/, "") : result;
}

export function unbase64(text, maximum, url = false) {
  require(typeof text === "string" && text.length <= Math.ceil(maximum / 3) * 4);
  require((url ? /^[A-Za-z0-9_-]+$/ : /^[A-Za-z0-9+/]*={0,2}$/).test(text));
  let bytes;
  try { bytes = Uint8Array.from(atob(url ? text.replaceAll("-", "+").replaceAll("_", "/") : text), c => c.charCodeAt(0)); }
  catch { throw new Refused(); }
  require(bytes.length <= maximum && base64(bytes, url) === text);
  return bytes;
}

export function keyBytes(text) {
  require(typeof text === "string" && /^[0-9a-f]{64}$/.test(text), "unavailable");
  return Uint8Array.from(text.match(/../g), part => Number.parseInt(part, 16));
}

export async function sha(bytes) { return hex(new Uint8Array(await crypto.subtle.digest("SHA-256", bytes))); }
async function hmacKey(bytes) {
  return crypto.subtle.importKey("raw", bytes, { name: "HMAC", hash: "SHA-256" }, false, ["sign", "verify"]);
}
export async function mac(key, message) {
  return new Uint8Array(await crypto.subtle.sign("HMAC", key, message));
}

export async function capability(request, env) {
  const authorization = request.headers.get("Authorization");
  require(typeof authorization === "string" && /^Bearer [A-Za-z0-9_.-]+$/.test(authorization), "authentication_required");
  const token = authorization.slice(7);
  require(token.length <= 1024, "authentication_required");
  const parts = token.split(".");
  require(parts.length === 3 && parts[0] === "CEPAIR1", "authentication_required");
  try {
    const payload = unbase64(parts[1], 512, true), signature = unbase64(parts[2], 32, true);
    require(signature.length === 32, "authentication_required");
    const key = await hmacKey(keyBytes(env.CE_ADMISSION_KEY));
    const domain = encoder.encode("Onslaught CE pair capability v1\0");
    const message = new Uint8Array(domain.length + payload.length);
    message.set(domain); message.set(payload, domain.length);
    require(await crypto.subtle.verify("HMAC", key, signature, message), "authentication_required");
    const claims = strictJson(payload, 512);
    fields(claims, ["schema", "audience", "subject", "role", "issuedAt", "expiresAt"]);
    require(claims.schema === 1 && claims.audience === AUDIENCE);
    require(["host", "guest"].includes(claims.role) &&
      new RegExp(`^p_[0-9a-f]{32}_${claims.role}$`).test(claims.subject));
    const now = nowSeconds();
    const maximum = Number(env.CE_CAPABILITY_SECONDS || 7200);
    require(integer(maximum) && maximum >= 1 && maximum <= 14400, "unavailable");
    require(integer(claims.issuedAt) && integer(claims.expiresAt) && claims.issuedAt <= now &&
      now < claims.expiresAt && claims.expiresAt > claims.issuedAt && claims.expiresAt <= claims.issuedAt + maximum);
    require(decoder.decode(payload) === canonical(claims));
    return claims;
  } catch (error) {
    if (error instanceof Refused && error.status === "unavailable") throw error;
    throw new Refused("authentication_required");
  }
}

export async function bodyBytes(request, maximum = MAX_BODY, allowEmpty = false) {
  const length = request.headers.get("Content-Length");
  require(length === null || (/^(0|[1-9][0-9]*)$/.test(length) && Number(length) <= maximum));
  const reader = request.body?.getReader();
  if (!reader) { require(allowEmpty); return new Uint8Array(); }
  let timer, count = 0; const chunks = [];
  const timeout = new Promise((_, reject) => { timer = setTimeout(() => reject(new Refused()), 5000); });
  try {
    while (true) {
      const { value, done } = await Promise.race([reader.read(), timeout]);
      if (done) break;
      count += value.length; require(count <= maximum); chunks.push(value);
    }
  } finally { clearTimeout(timer); await reader.cancel().catch(() => {}); }
  require(count > 0 || allowEmpty);
  const bytes = new Uint8Array(count); let offset = 0;
  for (const chunk of chunks) { bytes.set(chunk, offset); offset += chunk.length; }
  return bytes;
}

export function response(status, extra = {}) {
  require(STATUSES.has(status), "unavailable");
  return new Response(JSON.stringify({ schema: 1, status, ...extra }), {
    status: CODES[status] || 200,
    headers: { "Content-Type": "application/json", "Cache-Control": "no-store",
      "Pragma": "no-cache", "X-Content-Type-Options": "nosniff" },
  });
}

function failure(error) {
  return response(error instanceof Refused ? error.status : "unavailable");
}

function routeFor(request, env) {
  const url = new URL(request.url);
  const local = env.CE_LOCAL_HTTP === "true" && url.protocol === "http:" && url.hostname === "127.0.0.1";
  require((url.protocol === "https:" || local) && !request.url.includes("?") && !url.hash);
  require(request.method === "POST" && url.pathname.startsWith(PREFIX));
  const route = url.pathname.slice(PREFIX.length); require(ROUTES.has(route));
  require(request.headers.get("Content-Type") === "application/json");
  return route;
}

export default {
  async fetch(request, env) {
    try {
      require(env.CE_ENABLED === "true" && env.CLOSED_PAIRS, "unavailable");
      routeFor(request, env);
      await capability(request, env); // Authenticate before reading a body or allocating any object.
      const bytes = await bodyBytes(request, MAX_BODY, true);
      const id = env.CLOSED_PAIRS.idFromName("closed-pilot-v1");
      return await env.CLOSED_PAIRS.get(id).fetch(new Request(request.url, {
        method: "POST", headers: request.headers, body: bytes,
      }));
    } catch (error) { return failure(error); }
  },
};

function randomId() { return hex(crypto.getRandomValues(new Uint8Array(16))); }
function other(role) { return role === "host" ? "guest" : "host"; }
function complete(stage) { return ["host", "guest"].every(role => stage.records[role]?.acked); }
function clearPayloads(pair) {
  for (const stage of Object.values(pair.stages)) {
    for (const role of ["host", "guest"]) {
      if (stage.permits[role]) delete stage.permits[role].grant;
      if (stage.records[role]) delete stage.records[role].payload;
    }
  }
}

export function requestFields(value, role, now) {
  fields(value, ["schema", "role", "context", "requestedAt", "requestExpiresAt", "grantExpiresAt"]);
  require(value.schema === 1 && value.role === role && /^[0-9a-f]{64}$/.test(value.context));
  const start = value.requestedAt, end = value.requestExpiresAt, grant = value.grantExpiresAt;
  require([start, end, grant].every(integer) && start <= now && now < end && end <= grant &&
    end - start <= SETUP_SECONDS && grant - start <= 14400);
  return value;
}

export function recordFields(bytes, role, now, offer = null) {
  require(bytes.length > 99 && bytes.length <= 4194 &&
    encoder.encode("CEICE002").every((v, i) => bytes[i] === v) && bytes[8] === (role === "host" ? 1 : 2));
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const issued = Number(view.getBigUint64(9, true)), expires = Number(view.getBigUint64(17, true));
  const size = view.getUint16(81, true);
  require(integer(issued) && integer(expires) && issued < expires && expires - issued <= 300 &&
    now < expires && issued <= now + 30 && size >= 1 && size <= 4095 && bytes.length === 99 + size);
  if (offer) {
    require(bytes.subarray(25, 57).every((v, i) => v === offer[25 + i]));
    require(expires <= Number(new DataView(offer.buffer, offer.byteOffset, offer.byteLength).getBigUint64(17, true)));
  }
  return { issued, expires };
}

export function grantFields(value, role, context, issued, expires) {
  fields(value, ["schema", "role", "context", "issuedAt", "expiresAt", "iceServers"]);
  require(value.schema === 1 && value.role === role && value.context === context &&
    value.issuedAt === issued && value.expiresAt === expires && Array.isArray(value.iceServers));
  require(value.iceServers.length >= 1 && value.iceServers.length <= 2);
  const seen = new Set();
  for (const server of value.iceServers) {
    fields(server, ["urls", "username", "credential"]);
    require(Array.isArray(server.urls) && server.urls.length === 1);
    const url = server.urls[0];
    require(["turn:turn.cloudflare.com:3478?transport=udp", "turn:turn.cloudflare.com:443?transport=udp"].includes(url) && !seen.has(url));
    seen.add(url);
    require([server.username, server.credential].every(v => typeof v === "string" && /^[!-~]{1,513}$/.test(v)));
  }
  return value;
}

export async function permitSignature(env, permit) {
  const key = await hmacKey(keyBytes(env.CE_ISSUER_PERMIT_KEY));
  return base64(await mac(key, encoder.encode("Onslaught CE issuer permit v1\0" + canonical(permit))), true);
}

export async function verifyPermit(env, permit, signature) {
  fields(permit, ["schema", "store", "pair", "stage", "role", "attempt", "issuedAt",
    "expiresAt", "setupExpiresAt", "leaseUntil", "ttl"]);
  require(permit.schema === 1 && [permit.store, permit.pair, permit.attempt]
    .every(v => typeof v === "string" && /^[0-9a-f]{32}$/.test(v)));
  require(["host", "guest"].includes(permit.role) && ["lobby", "game"].includes(permit.stage));
  require([permit.issuedAt, permit.expiresAt, permit.setupExpiresAt, permit.leaseUntil, permit.ttl].every(integer));
  require(permit.ttl > 0 && permit.ttl <= MAX_GRANT_SECONDS && permit.expiresAt === permit.issuedAt + permit.ttl &&
    permit.leaseUntil <= permit.issuedAt + 10 && permit.leaseUntil <= permit.setupExpiresAt);
  const key = await hmacKey(keyBytes(env.CE_ISSUER_PERMIT_KEY));
  require(await crypto.subtle.verify("HMAC", key, unbase64(signature, 32, true),
    encoder.encode("Onslaught CE issuer permit v1\0" + canonical(permit))), "not_admitted");
}

export class PermitAuthority extends WorkerEntrypoint {
  async fetch(request) {
    try {
      require(this.env.CE_ENABLED === "true" && request.method === "POST", "unavailable");
      const value = strictJson(await bodyBytes(request, 2048), 2048);
      fields(value, ["permit", "signature"]);
      await verifyPermit(this.env, value.permit, value.signature);
      const id = this.env.CLOSED_PAIRS.idFromName("closed-pilot-v1");
      // RPC accepts ordinary JSON objects, rather than the parser's null-prototype dictionaries.
      const permit = JSON.parse(canonical(value.permit));
      const allowed = await this.env.CLOSED_PAIRS.get(id).consumePermit(permit, value.signature);
      return new Response(null, { status: allowed ? 204 : 403, headers: { "Cache-Control": "no-store" } });
    } catch { return new Response(null, { status: 403, headers: { "Cache-Control": "no-store" } }); }
  }
}

export class ClosedPairs extends DurableObject {
  constructor(ctx, env) {
    super(ctx, env); this.ctx = ctx; this.env = env;
    this.ready = ctx.blockConcurrencyWhile(() => this.initialize());
  }

  read(name) {
    const rows = this.ctx.storage.sql.exec("SELECT value FROM journal WHERE name=?", name).toArray();
    return rows.length ? JSON.parse(rows[0].value) : null;
  }

  write(name, value) {
    this.ctx.storage.sql.exec("INSERT INTO journal(name,value) VALUES(?,?) ON CONFLICT(name) DO UPDATE SET value=excluded.value",
      name, JSON.stringify(value));
  }

  transaction(change) {
    return this.ctx.storage.transactionSync(() => {
      const state = this.read("state"); require(state && state.schema === 1, "unavailable");
      const result = change(state); this.write("state", state); return result;
    });
  }

  enter(state, now) {
    require(integer(now) && now >= state.lastClock, "unavailable"); state.lastClock = now;
    for (const pair of Object.values(state.pairs)) {
      for (const stage of Object.values(pair.stages)) {
        for (const permit of Object.values(stage.permits)) {
          if (permit.state === "reserved" && permit.leaseUntil <= now) permit.state = "unknown";
          if (now >= permit.expiresAt) delete permit.grant;
        }
        for (const record of Object.values(stage.records)) {
          if (now >= record.expiresAt) delete record.payload;
        }
        if (pair.closed || now >= pair.expiresAt || now >= stage.deadline) {
          for (const permit of Object.values(stage.permits)) delete permit.grant;
          for (const record of Object.values(stage.records)) delete record.payload;
        }
      }
      if (pair.closed || now >= pair.expiresAt) clearPayloads(pair);
      if (now >= pair.expiresAt + 30) delete state.pairs[pair.id];
    }
    const oldest = Math.floor(now / 86400) - 1;
    for (const day of Object.keys(state.budgets)) if (Number(day) < oldest) delete state.budgets[day];
  }

  async initialize() {
    const names = ["CE_ADMISSION_KEY", "CE_IDENTITY_KEY", "CE_PARCEL_KEY", "CE_ISSUER_PERMIT_KEY"];
    const keys = names.map(name => keyBytes(this.env[name]));
    const fingerprints = await Promise.all(keys.map(sha));
    require(new Set(fingerprints).size === names.length, "unavailable");
    const keyId = this.env.CE_PARCEL_KEY_ID || fingerprints[2];
    require(keyId === fingerprints[2], "unavailable");
    const limit = Number(this.env.CE_ENCRYPTION_LIMIT);
    require(Number.isSafeInteger(limit) && limit > 0 && limit <= MAX_ENCRYPTIONS, "unavailable");
    this.grantSeconds = Number(this.env.CE_GRANT_SECONDS || 7200);
    require(integer(this.grantSeconds) && this.grantSeconds >= 1 && this.grantSeconds <= MAX_GRANT_SECONDS, "unavailable");
    this.identityKey = await hmacKey(keys[1]);
    this.cipher = await ParcelCipher.import(keys[2]);
    this.ctx.storage.sql.exec("CREATE TABLE IF NOT EXISTS journal(name TEXT PRIMARY KEY,value TEXT NOT NULL)");
    const configuration = { fingerprints, keyId, limit };
    this.ctx.storage.transactionSync(() => {
      const security = this.read("security");
      if (security) require(canonical(security.configuration) === canonical(configuration), "unavailable");
      else {
        require(!this.read("state"), "unavailable");
        this.write("security", { configuration, used: 0 });
        this.write("state", { schema: 1, store: randomId(), lastClock: 0, budgets: {}, admissions: {}, pairs: {} });
      }
    });
    const admission = strictJson(encoder.encode(this.env.CE_APPROVED_PAIR || ""), 2048);
    fields(admission, ["schema", "approval", "host", "guest", "issuedAt", "expiresAt"]);
    require(admission.schema === 1 && /^[0-9a-f]{32}$/.test(admission.approval) &&
      /^p_[0-9a-f]{32}_host$/.test(admission.host) && /^p_[0-9a-f]{32}_guest$/.test(admission.guest) &&
      admission.host !== admission.guest && integer(admission.issuedAt) && integer(admission.expiresAt) &&
      admission.issuedAt < admission.expiresAt, "unavailable");
    const admissionHash = await sha(encoder.encode(canonical(admission)));
    const subjects = await Promise.all([admission.host, admission.guest].map(async s => hex(await mac(this.identityKey, encoder.encode(s)))));
    const now = nowSeconds();
    const fresh = this.transaction(state => {
      this.enter(state, now);
      const prior = state.admissions[admission.approval];
      if (prior) {
        require(prior.hash === admissionHash && prior.activated, "unavailable"); return false;
      }
      require(admission.issuedAt <= now && now < admission.expiresAt && admission.expiresAt <= now + 14400, "unavailable");
      require(!Object.values(state.pairs).some(p => !p.closed && now < p.expiresAt), "rate_limited");
      const day = Math.floor(now / 86400), budget = state.budgets[day] || { approvals: 0, permits: 0 };
      require(budget.approvals < MAX_APPROVALS_PER_DAY && budget.permits + PERMITS_PER_APPROVAL <= MAX_PERMITS_PER_DAY, "rate_limited");
      budget.approvals++; budget.permits += PERMITS_PER_APPROVAL; state.budgets[day] = budget;
      state.admissions[admission.approval] = { hash: admissionHash, activated: false, expiresAt: admission.expiresAt };
      return true;
    });
    if (fresh) this.transaction(state => {
      // This second commit never refunds the already reserved global budget on failure.
      const entry = state.admissions[admission.approval]; require(entry && !entry.activated, "unavailable");
      state.pairs[admission.approval] = { id: admission.approval, host: subjects[0], guest: subjects[1],
        expiresAt: admission.expiresAt, consent: { host: false, guest: false }, closed: false, calls: 0, stages: {} };
      entry.activated = true;
    });
    this.approval = admission.approval;
    this.store = this.read("state").store;
    await this.ctx.storage.setAlarm((now + 30) * 1000);
  }

  binding(pair, stage, role, purpose, attempt = "", expiresAt = 0, digest = "") {
    return { store: this.store, pair: pair.id, stage: stage.name, role, purpose,
      context: stage.context, attempt, expiresAt, digest };
  }

  async seal(bytes, binding) {
    this.ctx.storage.transactionSync(() => {
      const security = this.read("security");
      require(security && security.used < security.configuration.limit, "unavailable");
      security.used++; this.write("security", security);
    });
    return base64(await this.cipher.seal(bytes, binding));
  }

  async open(envelope, binding) { return this.cipher.open(unbase64(envelope, 32812), binding); }

  pair(state, id, now, allowClosed = false) {
    this.enter(state, now);
    const pair = state.pairs[id]; require(pair, "not_admitted");
    if (!allowClosed) {
      require(now < pair.expiresAt, "expired"); require(!pair.closed, "closed");
    }
    return pair;
  }

  stage(pair, name, now) {
    require(["lobby", "game"].includes(name));
    const stage = pair.stages[name]; require(stage);
    require(now < stage.deadline || complete(stage), "setup_expired");
    return stage;
  }

  async consumePermit(permit, signature) {
    await this.ready; await verifyPermit(this.env, permit, signature);
    return this.transaction(state => {
      const now = nowSeconds(), pair = this.pair(state, permit.pair, now);
      const stage = this.stage(pair, permit.stage, now), current = stage.permits[permit.role];
      if (!current || current.state !== "reserved" || current.spent ||
          canonical(current.permit) !== canonical(permit) || permit.store !== state.store ||
          now >= current.leaseUntil || now >= stage.deadline) return false;
      current.spent = true; return true;
    });
  }

  async fetch(request) {
    try {
      await this.ready; require(this.env.CE_ENABLED === "true", "unavailable");
      const route = routeFor(request, this.env), principal = await capability(request, this.env);
      const subject = hex(await mac(this.identityKey, encoder.encode(principal.subject)));
      const member = this.transaction(state => {
        const now = nowSeconds(), pair = this.pair(state, this.approval, now, true);
        const role = pair.host === subject ? "host" : pair.guest === subject ? "guest" : null;
        require(role === principal.role, "not_admitted");
        const terminal = pair.closed ? "closed" : now >= pair.expiresAt ? "expired" : null;
        if (terminal) return { pair: pair.id, role, terminal };
        require(route === "pair/close" || pair.calls < 1024, "rate_limited");
        if (pair.calls < 1024) pair.calls++;
        return { pair: pair.id, role };
      });
      if (member.terminal) return response(member.terminal, { role: member.role });
      // The durable call charge precedes JSON parsing, and cannot be undone by a bad operation.
      const body = strictJson(await bodyBytes(request));
      if (route === "pair/current") fields(body, []);
      else {
        require(body && body.approval === member.pair);
        delete body.approval;
      }
      if (route.startsWith("pair/")) return this.pairOperation(route, body, member);
      const consent = this.transaction(state => {
        const pair = this.pair(state, member.pair, nowSeconds());
        return pair.consent.host && pair.consent.guest;
      });
      if (!consent) return response("awaiting_pair_consent", { role: member.role });
      if (route === "stage/request") await this.stageRequest(body, member);
      else if (route === "stage/poll") {
        fields(body, ["stage"]);
        const poll = this.transaction(state => {
          const now = nowSeconds(), pair = this.pair(state, member.pair, now);
          require(["lobby", "game"].includes(body.stage));
          if (!pair.stages[body.stage]) return { awaiting: true };
          const stage = this.stage(pair, body.stage, now);
          if (!stage.requests[member.role]) return { awaiting: true, deadline: stage.deadline };
          require(stage.polls[member.role] !== now, "rate_limited"); stage.polls[member.role] = now;
          return { awaiting: false };
        });
        if (poll.awaiting) return response("awaiting_peer_request", { role: member.role,
          ...(poll.deadline === undefined ? {} : { setupExpiresAt: poll.deadline }) });
      } else if (route === "stage/record") await this.stageRecord(body, member);
      else if (route === "stage/ack") {
        fields(body, ["stage", "digest"]);
        this.transaction(state => {
          const now = nowSeconds(), pair = this.pair(state, member.pair, now);
          const stage = this.stage(pair, body.stage, now), record = stage.records[other(member.role)];
          require(record && record.digest === body.digest); record.acked = true;
          if (complete(stage)) for (const item of Object.values(stage.records)) delete item.payload;
        });
      }
      if (route === "stage/poll") await this.issue(member, body.stage);
      return await this.view(member, body.stage);
    } catch (error) { return failure(error); }
  }

  pairOperation(route, body, member) {
    fields(body, []);
    const result = this.transaction(state => {
      const now = nowSeconds(), pair = this.pair(state, member.pair, now, route === "pair/close");
      if (route === "pair/close") { pair.closed = true; clearPayloads(pair); }
      if (route === "pair/accept") pair.consent[member.role] = true;
      return { status: pair.closed ? "closed" : pair.consent.host && pair.consent.guest ? "admitted" : "awaiting_pair_consent",
        role: member.role, approval: pair.id, pairExpiresAt: pair.expiresAt };
    });
    const { status, ...extra } = result;
    return response(status, status === "closed" ? { role: member.role } : extra);
  }

  async stageRequest(body, member) {
    fields(body, ["stage", "request", "afterAuthenticatedStart"]);
    require(["lobby", "game"].includes(body.stage) && body.afterAuthenticatedStart === (body.stage === "game"));
    const now = nowSeconds(), request = requestFields(body.request, member.role, now);
    // The existing courier reserializes requests in field order with a trailing newline.
    const raw = encoder.encode(JSON.stringify(request) + "\n"), digest = await sha(raw);
    const before = this.transaction(state => {
      const pair = this.pair(state, member.pair, nowSeconds());
      if (body.stage === "game") require(pair.stages.lobby && complete(pair.stages.lobby) && pair.stages.lobby.context !== request.context);
      const current = pair.stages[body.stage];
      if (current) {
        this.stage(pair, body.stage, nowSeconds()); require(current.context === request.context);
        if (current.requests[member.role]) {
          require(current.requests[member.role].digest === digest); return null;
        }
      }
      return { pair, stage: current || { name: body.stage, context: request.context } };
    });
    if (!before) return;
    const payload = await this.seal(raw, this.binding(before.pair, before.stage, member.role, "request"));
    this.transaction(state => {
      const pair = this.pair(state, member.pair, nowSeconds());
      let stage = pair.stages[body.stage];
      if (!stage) stage = pair.stages[body.stage] = { name: body.stage, context: request.context,
        deadline: Math.min(request.requestExpiresAt, now + SETUP_SECONDS, pair.expiresAt),
        requests: {}, permits: {}, records: {}, polls: {} };
      require(stage.context === request.context && nowSeconds() < stage.deadline, "setup_expired");
      const prior = stage.requests[member.role];
      if (prior) require(prior.digest === digest);
      else {
        stage.deadline = Math.min(stage.deadline, request.requestExpiresAt);
        stage.requests[member.role] = { digest, payload, grantEnd: request.grantExpiresAt };
      }
    });
  }

  async stageRecord(body, member) {
    fields(body, ["stage", "record"]);
    require(typeof body.record === "string" && body.record.length <= 5600);
    const bytes = unbase64(body.record, 4194), digest = await sha(bytes), now = nowSeconds();
    let info = recordFields(bytes, member.role, now);
    const before = this.transaction(state => {
      const pair = this.pair(state, member.pair, now), stage = this.stage(pair, body.stage, now);
      const own = stage.permits[member.role];
      require(own?.state === "issued" && now < own.expiresAt);
      const prior = stage.records[member.role];
      if (prior) { require(prior.digest === digest); return null; }
      if (member.role === "guest") require(stage.records.host?.payload && stage.records.host.acked);
      return { pair, stage };
    });
    if (!before) return;
    if (member.role === "guest") {
      const offer = before.stage.records.host;
      const bytesOffer = await this.open(offer.payload,
        this.binding(before.pair, before.stage, "host", "record", "", offer.expiresAt, offer.digest));
      info = recordFields(bytes, member.role, nowSeconds(), bytesOffer);
    }
    const payload = await this.seal(bytes,
      this.binding(before.pair, before.stage, member.role, "record", "", info.expires, digest));
    this.transaction(state => {
      const now = nowSeconds(), pair = this.pair(state, member.pair, now), stage = this.stage(pair, body.stage, now);
      require(stage.permits[member.role]?.state === "issued" && now < stage.permits[member.role].expiresAt);
      if (member.role === "guest") require(stage.records.host?.payload && stage.records.host.acked);
      const prior = stage.records[member.role];
      if (prior) require(prior.digest === digest);
      else stage.records[member.role] = { digest, payload, expiresAt: info.expires, acked: false };
    });
  }

  async issue(member, name) {
    const before = this.transaction(state => {
      const now = nowSeconds(), pair = this.pair(state, member.pair, now), stage = this.stage(pair, name, now);
      if (!stage.requests.host || !stage.requests.guest || stage.permits[member.role]) return null;
      return { pair, stage, request: stage.requests[member.role] };
    });
    if (!before) return;
    let request;
    try {
      const plaintext = await this.open(before.request.payload,
        this.binding(before.pair, before.stage, member.role, "request"));
      require(await sha(plaintext) === before.request.digest, "unavailable");
      request = requestFields(strictJson(plaintext, 4096), member.role, nowSeconds());
      require(request.context === before.stage.context && request.grantExpiresAt === before.request.grantEnd &&
        before.stage.deadline <= request.requestExpiresAt, "unavailable");
    } catch { throw new Refused("unavailable"); }
    const pending = this.transaction(state => {
      const now = nowSeconds(), pair = this.pair(state, member.pair, now), stage = this.stage(pair, name, now);
      if (!stage.requests.host || !stage.requests.guest || stage.permits[member.role]) return null;
      require(stage.requests[member.role].payload === before.request.payload &&
        stage.requests[member.role].digest === before.request.digest && stage.context === request.context, "unavailable");
      const ttl = Math.min(this.grantSeconds, request.grantExpiresAt - now, pair.expiresAt - now);
      require(ttl > 0, "grant_expired");
      const permit = { schema: 1, store: state.store, pair: pair.id, stage: name, role: member.role,
        attempt: randomId(), issuedAt: now, expiresAt: now + ttl, setupExpiresAt: stage.deadline,
        leaseUntil: Math.min(now + 10, stage.deadline), ttl };
      stage.permits[member.role] = { state: "reserved", spent: false, permit,
        attempt: permit.attempt, expiresAt: permit.expiresAt, leaseUntil: permit.leaseUntil };
      return { pair, stage, permit };
    });
    if (!pending) return;
    const { permit } = pending;
    try {
      require(this.env.TURN_ISSUER, "unavailable");
      const signature = await permitSignature(this.env, permit);
      const reply = await this.env.TURN_ISSUER.fetch(new Request("https://issuer.invalid/issue", {
        method: "POST", headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ permit, signature }), signal: AbortSignal.timeout(10000),
      }));
      require(reply.status === 200, "unavailable");
      const lease = strictJson(await bodyBytes(reply, 32768), 32768);
      fields(lease, ["iceServers"]);
      const grant = grantFields({ schema: 1, role: member.role, context: pending.stage.context,
        issuedAt: permit.issuedAt, expiresAt: permit.expiresAt, iceServers: lease.iceServers },
      member.role, pending.stage.context, permit.issuedAt, permit.expiresAt);
      const revocation = await this.seal(encoder.encode(JSON.stringify([...new Set(grant.iceServers.map(server => server.username))])),
        this.binding(pending.pair, pending.stage, member.role, "revocation", permit.attempt, permit.expiresAt));
      const payload = await this.seal(encoder.encode(canonical(grant)),
        this.binding(pending.pair, pending.stage, member.role, "grant", permit.attempt, permit.expiresAt));
      this.transaction(state => {
        const now = nowSeconds(); this.enter(state, now);
        const pair = state.pairs[member.pair], stage = pair?.stages[name], current = stage?.permits[member.role];
        require(current?.attempt === permit.attempt, "unavailable"); current.revocation = revocation;
        if (current.state === "reserved" && current.spent && pair && !pair.closed &&
            now < pair.expiresAt && now < stage.deadline && now < current.leaseUntil) {
          current.grant = payload; current.state = "issued";
        } else current.state = "unknown";
      });
    } catch {
      this.transaction(state => {
        this.enter(state, nowSeconds());
        const current = state.pairs[member.pair]?.stages[name]?.permits[member.role];
        if (current?.attempt === permit.attempt && current.state === "reserved") current.state = "unknown";
      });
    }
  }

  async view(member, name) {
    const before = this.transaction(state => {
      const now = nowSeconds(), pair = this.pair(state, member.pair, now), stage = this.stage(pair, name, now);
      return { pair, stage };
    });
    const own = before.stage.permits[member.role], peer = before.stage.records[other(member.role)];
    let grant = null, record = null;
    if (own?.state === "issued" && own.grant && nowSeconds() < own.expiresAt) {
      grant = strictJson(await this.open(own.grant,
        this.binding(before.pair, before.stage, member.role, "grant", own.attempt, own.expiresAt)), 32768);
    }
    if (peer?.payload) record = await this.open(peer.payload,
      this.binding(before.pair, before.stage, other(member.role), "record", "", peer.expiresAt, peer.digest));
    const current = this.transaction(state => {
      const now = nowSeconds(), pair = this.pair(state, member.pair, now), stage = this.stage(pair, name, now);
      const permit = stage.permits[member.role], received = stage.records[other(member.role)];
      const status = permit && now >= permit.expiresAt ? "grant_expired" : permit?.state === "unknown" ? "issuance_unknown" :
        permit?.state === "reserved" ? "issuing" : complete(stage) ? "service_delivery_complete" :
          permit?.state === "issued" ? "exchanging" : "awaiting_peer_request";
      return { status, deadline: stage.deadline, permit, received };
    });
    const extra = { role: member.role, setupExpiresAt: current.deadline };
    if (current.permit) extra.grantExpiresAt = current.permit.expiresAt;
    if (["exchanging", "service_delivery_complete"].includes(current.status)) {
      if (grant && current.permit?.grant === own?.grant) extra.grant = grant;
      if (record && current.received?.payload === peer?.payload) {
        extra.record = base64(record); extra.recordDigest = peer.digest;
      }
    }
    return response(current.status, extra);
  }

  async alarm() {
    await this.ready; const now = nowSeconds();
    const alive = this.transaction(state => {
      this.enter(state, now);
      for (const [id, entry] of Object.entries(state.admissions)) {
        if (now >= entry.expiresAt + 30) delete state.admissions[id];
      }
      return Object.keys(state.pairs).length > 0;
    });
    if (alive) await this.ctx.storage.setAlarm((now + 30) * 1000);
  }
}
