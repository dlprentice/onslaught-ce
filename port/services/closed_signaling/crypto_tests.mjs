// SPDX-FileCopyrightText: 2026 Onslaught contributors
// SPDX-License-Identifier: GPL-3.0-only
// Authored offline fixture. Inputs and key material are invented or pinned
// public algorithm test vectors; no player/provider/authentication is involved.
import { DurableObject } from "cloudflare:workers";
import { ParcelCipher, ParcelRefused, MAX_PARCEL, hex, bindingBytes } from "./parcel_crypto.mjs";
import vectors from "./vectors.json";

const encoder = new TextEncoder();
const decoder = new TextDecoder();
const keyBytes = encoder.encode("invented-worker-storage-key-0001");
const marker = "invented-credential-marker-worker-ephemeral";
const payload = encoder.encode(JSON.stringify({ username: marker, credential: marker + "-secret" }));
const binding = { store: "1".repeat(32), pair: "2".repeat(32), stage: "lobby", role: "host",
  purpose: "grant", context: "3".repeat(64), attempt: "4".repeat(32), expiresAt: 1800000120, digest: "" };

function check(value) { if (!value) throw new Error("crypto_fixture_refused"); }
function fromHex(value) { return Uint8Array.from(value.match(/../g) ?? [], (x) => parseInt(x, 16)); }
async function refused(action) {
  try { await action(); } catch (error) { check(error instanceof ParcelRefused); return; }
  throw new Error("crypto_fixture_expected_refusal");
}

async function primitiveVectors() {
  check(vectors.length > 0);
  for (const item of vectors) {
    const key = await crypto.subtle.importKey("raw", fromHex(item.KEY), "AES-GCM", false, ["encrypt", "decrypt"]);
    const args = { name: "AES-GCM", iv: fromHex(item.NONCE), additionalData: fromHex(item.AD), tagLength: 128 };
    const expected = fromHex(item.CT + item.TAG);
    const encrypted = new Uint8Array(await crypto.subtle.encrypt(args, key, fromHex(item.IN)));
    check(hex(encrypted) === hex(expected));
    check(hex(new Uint8Array(await crypto.subtle.decrypt(args, key, expected))) === item.IN);
    expected[expected.length - 1] ^= 1;
    let denied = false;
    try { await crypto.subtle.decrypt(args, key, expected); } catch { denied = true; }
    check(denied);
  }
}

async function parcelCases() {
  const cipher = await ParcelCipher.import(keyBytes);
  const wrong = await ParcelCipher.import(new Uint8Array(32).fill(7));
  const envelope = await cipher.seal(payload, binding);
  check(decoder.decode(await cipher.open(envelope, binding)) === decoder.decode(payload));
  const nonces = await Promise.all(Array.from({ length: 256 }, () => cipher.seal(payload, binding)));
  check(new Set(nonces.map((value) => hex(value.slice(8, 20)))).size === 256);
  for (const sealed of nonces) check(hex(await cipher.open(sealed, binding)) === hex(payload));
  for (const at of [0, 7, 8, 19, 20, envelope.length - 1]) {
    const changed = new Uint8Array(envelope); changed[at] ^= 1;
    await refused(() => cipher.open(changed, binding));
  }
  for (const field of ["store", "pair", "attempt"]) {
    await refused(() => cipher.open(envelope, { ...binding, [field]: "6".repeat(32) }));
  }
  await refused(() => cipher.open(envelope, { ...binding, context: "6".repeat(64) }));
  for (const [field, value] of [["stage", "game"], ["role", "guest"], ["purpose", "revocation"], ["expiresAt", binding.expiresAt + 1]]) {
    await refused(() => cipher.open(envelope, { ...binding, [field]: value }));
  }
  await refused(() => cipher.open(envelope, { ...binding, extra: true }));
  const purposes = [
    { ...binding, purpose: "request", attempt: "", expiresAt: 0 },
    binding,
    { ...binding, purpose: "record", attempt: "", digest: "5".repeat(64) },
    { ...binding, purpose: "revocation" },
  ];
  const encoded = purposes.map((item) => decoder.decode(bindingBytes(item, cipher.keyId)));
  check(new Set(encoded).size === purposes.length);
  for (let i = 0; i < purposes.length; ++i) {
    const sealed = await cipher.seal(payload, purposes[i]);
    check(hex(await cipher.open(sealed, purposes[i])) === hex(payload));
    for (let j = 0; j < purposes.length; ++j) {
      if (i !== j) await refused(() => cipher.open(sealed, purposes[j]));
    }
  }
  for (const changed of [
    { ...binding, store: "1".repeat(64) }, { ...binding, pair: "2".repeat(64) },
    { ...binding, attempt: "4".repeat(64) }, { ...binding, digest: "5".repeat(64) },
    { ...purposes[0], attempt: "4".repeat(32) }, { ...purposes[0], digest: "5".repeat(64) },
    { ...purposes[0], expiresAt: 1 }, { ...purposes[2], digest: "" },
    { ...purposes[2], attempt: "4".repeat(32) }, { ...purposes[2], expiresAt: 0 },
  ]) await refused(() => cipher.seal(payload, changed));
  await refused(() => wrong.open(envelope, binding));
  await refused(() => cipher.open(envelope.slice(0, -1), binding));
  await refused(() => cipher.open(new Uint8Array(MAX_PARCEL + 37), binding));
  await refused(() => cipher.open(new Uint8Array(35), binding));
  const legacy = new Uint8Array(envelope); legacy.set(encoder.encode("CESPAR02"));
  await refused(() => cipher.open(legacy, binding));
  for (const size of [0, 31, 33]) await refused(() => ParcelCipher.import(new Uint8Array(size)));
  await refused(() => cipher.seal(new Uint8Array(MAX_PARCEL + 1), binding));
  const maximum = new Uint8Array(MAX_PARCEL).fill(73);
  check(hex(await cipher.open(await cipher.seal(maximum, binding), binding)) === hex(maximum));
  check((await cipher.open(await cipher.seal(new Uint8Array(), binding), binding)).length === 0);
  const originalRandom = crypto.getRandomValues;
  try {
    crypto.getRandomValues = () => { throw new Error("invented_entropy_failure"); };
    await refused(() => cipher.seal(payload, binding));
  } finally { crypto.getRandomValues = originalRandom; }
  cipher.close(); wrong.close();
  await refused(() => cipher.seal(payload, binding));
  await refused(() => cipher.open(envelope, binding));
}

export class CryptoStoreProbe extends DurableObject {
  constructor(ctx, env) {
    super(ctx, env);
    ctx.storage.sql.exec("CREATE TABLE IF NOT EXISTS encrypted_parcels (id INTEGER PRIMARY KEY, body BLOB, nonce TEXT NOT NULL)");
  }

  async seed() {
    check(this.ctx.storage.sql.exec("SELECT COUNT(*) AS n FROM encrypted_parcels").one().n === 0);
    const cipher = await ParcelCipher.import(keyBytes);
    for (let i = 0; i < 32; ++i) {
      const sealed = await cipher.seal(payload, { ...binding, expiresAt: binding.expiresAt + i });
      this.ctx.storage.sql.exec("INSERT INTO encrypted_parcels VALUES(?,?,?)", i, sealed, hex(sealed.slice(8, 20)));
    }
    cipher.close();
    await this.ctx.storage.sync();
    return true;
  }

  async reopen() {
    const cipher = await ParcelCipher.import(keyBytes);
    const rows = this.ctx.storage.sql.exec("SELECT * FROM encrypted_parcels ORDER BY id").toArray();
    check(rows.length === 32);
    const seen = new Set(rows.map((row) => row.nonce));
    check(seen.size === 32);
    for (const row of rows) {
      check(hex(await cipher.open(new Uint8Array(row.body), { ...binding, expiresAt: binding.expiresAt + row.id })) === hex(payload));
      const next = await cipher.seal(payload, binding);
      check(!seen.has(hex(next.slice(8, 20)))); seen.add(hex(next.slice(8, 20)));
    }
    cipher.close();
    return true;
  }

  async remove() {
    this.ctx.storage.sql.exec("UPDATE encrypted_parcels SET body=NULL");
    await this.ctx.storage.sync();
    return true;
  }

  gone() {
    const rows = this.ctx.storage.sql.exec("SELECT body FROM encrypted_parcels").toArray();
    check(rows.length === 32 && rows.every((row) => row.body === null));
    return true;
  }
}

export default {
  fetch() { return new Response(null, { status: 503, headers: { "Cache-Control": "no-store" } }); },
  async test(ctrl, env) {
    if (env.PHASE === "crypto") {
      const denied = await fetch("https://signaling-fixture.invalid/denied");
      check(denied.status === 503);
      await primitiveVectors();
      await parcelCases();
    } else {
      const store = env.STORE.get(env.STORE.idFromName("owned-crypto-fixture"));
      check(["seed", "reopen", "remove", "gone"].includes(env.PHASE));
      check(await store[env.PHASE]());
    }
  },
};
