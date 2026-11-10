// SPDX-License-Identifier: GPL-3.0-only
// Private service binding only. No public route or caller-selected provider URL.
import { bodyBytes, fields, integer, nowSeconds, require, strictJson, verifyPermit } from "./worker.mjs";

const UDP = new Set(["turn:turn.cloudflare.com:3478?transport=udp", "turn:turn.cloudflare.com:443?transport=udp"]);
const OTHER_TURN = new Set(["turn:turn.cloudflare.com:3478?transport=tcp", "turn:turn.cloudflare.com:80?transport=tcp",
  "turns:turn.cloudflare.com:5349?transport=tcp", "turns:turn.cloudflare.com:443?transport=tcp"]);
const STUN = new Set(["stun:stun.cloudflare.com:3478", "stun:stun.cloudflare.com:443"]);

export function providerLease(value, masterToken) {
  fields(value, ["iceServers"]);
  require(Array.isArray(value.iceServers) && value.iceServers.length >= 1 && value.iceServers.length <= 8);
  const retained = [], seen = new Set();
  for (const entry of value.iceServers) {
    const credentialed = Object.hasOwn(entry, "username") || Object.hasOwn(entry, "credential");
    fields(entry, credentialed ? ["urls", "username", "credential"] : ["urls"]);
    require(Array.isArray(entry.urls) && entry.urls.length >= 1 && entry.urls.length <= 8 &&
      new Set(entry.urls).size === entry.urls.length && entry.urls.every(v => typeof v === "string"));
    if (!credentialed) { require(entry.urls.every(url => STUN.has(url))); continue; }
    require([entry.username, entry.credential].every(v => typeof v === "string" && /^[!-~]{1,513}$/.test(v)));
    require(entry.urls.every(url => UDP.has(url) || OTHER_TURN.has(url)));
    for (const url of entry.urls) {
      if (!UDP.has(url)) continue;
      require(!seen.has(url)); seen.add(url);
      require(!entry.username.includes(masterToken) && !entry.credential.includes(masterToken));
      retained.push({ urls: [url], username: entry.username, credential: entry.credential });
    }
  }
  require(retained.length >= 1 && retained.length <= 2);
  return { iceServers: retained };
}

export default {
  async fetch(request, env) {
    try {
      require(env.CE_ENABLED === "true" && request.method === "POST" &&
        new URL(request.url).pathname === "/issue" && env.PERMIT_AUTHORITY);
      const value = strictJson(await bodyBytes(request, 2048), 2048);
      fields(value, ["permit", "signature"]);
      await verifyPermit(env, value.permit, value.signature);
      const now = nowSeconds(), permit = value.permit;
      const maximum = Number(env.CE_GRANT_SECONDS || 7200);
      require(integer(maximum) && maximum >= 1 && maximum <= 14400 && permit.ttl <= maximum);
      require(permit.issuedAt <= now && now < permit.leaseUntil && now < permit.setupExpiresAt && now < permit.expiresAt);
      require(typeof env.TURN_KEY_ID === "string" && /^[A-Za-z0-9_-]{1,128}$/.test(env.TURN_KEY_ID));
      require(typeof env.TURN_API_TOKEN === "string" && /^[!-~]{1,513}$/.test(env.TURN_API_TOKEN));
      const consumed = await env.PERMIT_AUTHORITY.fetch(new Request("https://authority.invalid/consume", {
        method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(value),
        signal: AbortSignal.timeout(10000),
      }));
      require(consumed.status === 204); // A durable, exact one-time reservation was spent before egress.
      const endpoint = `https://rtc.live.cloudflare.com/v1/turn/keys/${env.TURN_KEY_ID}/credentials/generate-ice-servers`;
      const reply = await fetch(endpoint, { method: "POST", redirect: "manual", signal: AbortSignal.timeout(10000),
        headers: { "Authorization": `Bearer ${env.TURN_API_TOKEN}`, "Content-Type": "application/json",
          "Accept": "application/json", "Accept-Encoding": "identity" }, body: JSON.stringify({ ttl: permit.ttl }) });
      require([200, 201].includes(reply.status) && !reply.redirected && (!reply.url || reply.url === endpoint));
      require([null, "identity"].includes(reply.headers.get("Content-Encoding")) &&
        reply.headers.get("Content-Type")?.split(";")[0].trim().toLowerCase() === "application/json");
      const lease = providerLease(strictJson(await bodyBytes(reply, 32768), 32768), env.TURN_API_TOKEN);
      return new Response(JSON.stringify(lease), { status: 200,
        headers: { "Content-Type": "application/json", "Cache-Control": "no-store", "Pragma": "no-cache" } });
    } catch {
      return new Response(null, { status: 503, headers: { "Cache-Control": "no-store" } });
    }
  },
};
