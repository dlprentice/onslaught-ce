// SPDX-License-Identifier: GPL-3.0-only
// Private non-public TURN credential issuer Worker. Service Binding only; never exposes public routes.
import {
  assertExactFields,
  bodyBytes,
  errorResponse,
  integer,
  jsonResponse,
  nowSeconds,
  requireCond,
  strictJson,
  verifyTurnPermit,
} from "./worker.mjs";

const ALLOWED_STUN = new Set([
  "stun:stun.cloudflare.com:3478",
  "stun:stun.cloudflare.com:443",
  "stun:stun.cloudflare.com:53",
]);

const ALLOWED_UDP_TURN = new Set([
  "turn:turn.cloudflare.com:3478?transport=udp",
  "turn:turn.cloudflare.com:443?transport=udp",
  "turn:turn.cloudflare.com:53?transport=udp",
]);

const ALLOWED_OTHER_TURN = new Set([
  "turn:turn.cloudflare.com:3478?transport=tcp",
  "turn:turn.cloudflare.com:80?transport=tcp",
  "turn:turn.cloudflare.com:443?transport=tcp",
  "turns:turn.cloudflare.com:5349?transport=tcp",
  "turns:turn.cloudflare.com:443?transport=tcp",
]);

const MAX_TURN_TTL_SECONDS = 14400;

export function filterProviderIceServers(value, masterToken, requireRelay) {
  assertExactFields(value, ["iceServers"]);
  const entries = Array.isArray(value.iceServers)
    ? value.iceServers
    : (value.iceServers && typeof value.iceServers === "object" ? [value.iceServers] : null);
  requireCond(Array.isArray(entries) && entries.length >= 1 && entries.length <= 16,
    503, "turn_unavailable", "Invalid upstream iceServers payload.");

  const stunServers = [];
  const turnServers = [];
  const seenStun = new Set();
  const seenTurn = new Set();

  for (const entry of entries) {
    requireCond(entry && typeof entry === "object" && !Array.isArray(entry),
      503, "turn_unavailable", "Invalid iceServer entry.");
    const hasCreds = Object.hasOwn(entry, "username") || Object.hasOwn(entry, "credential");
    assertExactFields(entry, hasCreds ? ["urls", "username", "credential"] : ["urls"]);
    const urls = typeof entry.urls === "string" ? [entry.urls] : entry.urls;
    requireCond(Array.isArray(urls) && urls.length >= 1 && urls.length <= 16 &&
      new Set(urls).size === urls.length && urls.every(u => typeof u === "string"),
    503, "turn_unavailable", "Invalid iceServer urls.");

    if (hasCreds) {
      requireCond(typeof entry.username === "string" && /^[!-~]{1,513}$/.test(entry.username) &&
        typeof entry.credential === "string" && /^[!-~]{1,513}$/.test(entry.credential),
      503, "turn_unavailable", "Invalid TURN credentials.");
      requireCond(!entry.username.includes(masterToken) && !entry.credential.includes(masterToken),
        503, "turn_unavailable", "Master token echo rejected.");
    }

    for (const url of urls) {
      if (ALLOWED_STUN.has(url)) {
        if (!requireRelay && !seenStun.has(url)) {
          seenStun.add(url);
          stunServers.push({ urls: [url] });
        }
        continue;
      }
      if (ALLOWED_UDP_TURN.has(url)) {
        requireCond(hasCreds, 503, "turn_unavailable", "TURN server missing credentials.");
        if (!seenTurn.has(url)) {
          seenTurn.add(url);
          turnServers.push({ urls: [url], username: entry.username, credential: entry.credential });
        }
        continue;
      }
      if (ALLOWED_OTHER_TURN.has(url)) {
        requireCond(hasCreds, 503, "turn_unavailable", "TURN server missing credentials.");
        continue;
      }
      requireCond(false, 503, "turn_unavailable", "Upstream returned untrusted ICE server URL.");
    }
  }

  requireCond(turnServers.length >= 1, 503, "turn_unavailable", "Upstream returned no usable UDP TURN server.");
  return requireRelay ? turnServers : [...stunServers, ...turnServers];
}

export default {
  async fetch(request, env) {
    try {
      requireCond(env.CE_ENABLED === "true", 503, "service_unavailable", "TURN issuer is disabled.");
      const url = new URL(request.url);
      requireCond(request.method === "POST" && url.pathname === "/issue" && env.PERMIT_AUTHORITY,
        503, "service_unavailable", "Invalid issuer request.");

      const value = strictJson(await bodyBytes(request, 2048), 2048);
      assertExactFields(value, ["permit", "signature"]);
      await verifyTurnPermit(env, value.permit, value.signature);

      const now = nowSeconds();
      const permit = value.permit;
      const maxTtl = Number(env.CE_TURN_TTL_SECONDS || 3600);
      requireCond(integer(maxTtl) && maxTtl >= 60 && maxTtl <= MAX_TURN_TTL_SECONDS && permit.ttl <= maxTtl,
        503, "service_unavailable", "TURN TTL exceeds policy.");
      requireCond(permit.issued_at <= now && now < permit.lease_until && now < permit.expires_at,
        503, "service_unavailable", "TURN permit lease expired.");
      requireCond(typeof env.TURN_KEY_ID === "string" && /^[A-Za-z0-9_-]{1,128}$/.test(env.TURN_KEY_ID),
        503, "service_unavailable", "TURN key configuration invalid.");
      requireCond(typeof env.TURN_API_TOKEN === "string" && /^[!-~]{1,513}$/.test(env.TURN_API_TOKEN),
        503, "service_unavailable", "TURN token configuration invalid.");

      // Atomically spend the one-time permit and global daily/monthly budget BEFORE any egress call.
      const consumed = await env.PERMIT_AUTHORITY.fetch(new Request("https://authority.invalid/consume", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(value),
        signal: AbortSignal.timeout(10000),
      }));
      requireCond(consumed.status === 204, 503, "turn_unavailable", "Permit authority denied issuance.");

      // Fixed Cloudflare Realtime TURN endpoint (never caller-selected).
      const endpoint = `https://rtc.live.cloudflare.com/v1/turn/keys/${env.TURN_KEY_ID}/credentials/generate-ice-servers`;
      const reply = await fetch(endpoint, {
        method: "POST",
        redirect: "manual",
        signal: AbortSignal.timeout(10000),
        headers: {
          "Authorization": `Bearer ${env.TURN_API_TOKEN}`,
          "Content-Type": "application/json",
          "Accept": "application/json",
          "Accept-Encoding": "identity",
        },
        body: JSON.stringify({ ttl: permit.ttl }),
      });
      requireCond([200, 201].includes(reply.status) && !reply.redirected && (!reply.url || reply.url === endpoint),
        503, "turn_unavailable", "Upstream TURN request failed.");
      requireCond([null, "identity"].includes(reply.headers.get("Content-Encoding")) &&
        reply.headers.get("Content-Type")?.split(";")[0].trim().toLowerCase() === "application/json",
      503, "turn_unavailable", "Upstream TURN returned invalid headers.");

      const upstream = strictJson(await bodyBytes(reply, 32768), 32768);
      const iceServers = filterProviderIceServers(upstream, env.TURN_API_TOKEN, permit.require_relay);
      return jsonResponse({ iceServers, ttl: permit.ttl }, 200);
    } catch {
      return errorResponse(503, "turn_unavailable", "TURN credential issuance failed.");
    }
  },
};
