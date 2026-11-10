// SPDX-FileCopyrightText: 2026 Onslaught contributors
// SPDX-License-Identifier: GPL-3.0-only
// Native Web Crypto binding. No cipher, MAC, KDF or random generator is
// implemented here. This format is separate from the Python CESPAR02 store.
export const MAX_PARCEL = 32768;
export const MAGIC = new TextEncoder().encode("CEWGCM01");
const encoder = new TextEncoder();
const fields = ["store", "pair", "stage", "role", "purpose", "context", "attempt", "expiresAt", "digest"];

export class ParcelRefused extends Error {
  constructor() { super("parcel_refused"); }
}

function requireValue(value) {
  if (!value) throw new ParcelRefused();
}

function bytes(value, max) {
  requireValue(value instanceof Uint8Array && value.byteLength <= max);
  // Copy at the asynchronous boundary so callers cannot change an in-flight
  // input or a view's backing buffer while Web Crypto processes it.
  return new Uint8Array(value);
}

export function hex(value) {
  return Array.from(value, (x) => x.toString(16).padStart(2, "0")).join("");
}

export function bindingBytes(binding, keyId) {
  requireValue(binding && typeof binding === "object" && !Array.isArray(binding));
  requireValue(Object.keys(binding).sort().join(",") === [...fields].sort().join(","));
  requireValue(typeof keyId === "string" && /^[a-f0-9]{64}$/.test(keyId));
  for (const name of ["store", "pair"]) {
    requireValue(typeof binding[name] === "string" && /^[a-f0-9]{32}$/.test(binding[name]));
  }
  requireValue(typeof binding.context === "string" && /^[a-f0-9]{64}$/.test(binding.context));
  requireValue(["lobby", "game"].includes(binding.stage));
  requireValue(["host", "guest"].includes(binding.role));
  requireValue(["request", "grant", "record", "revocation"].includes(binding.purpose));
  // Preserve the existing store's metadata values without padding or hashing
  // identifiers. Empty fields are authenticated values, never omitted fields.
  if (binding.purpose === "request") {
    requireValue(binding.attempt === "" && binding.digest === "" && binding.expiresAt === 0);
  } else {
    requireValue(Number.isSafeInteger(binding.expiresAt) && binding.expiresAt > 0);
    if (binding.purpose === "record") {
      requireValue(binding.attempt === "" && typeof binding.digest === "string" && /^[a-f0-9]{64}$/.test(binding.digest));
    } else {
      requireValue(typeof binding.attempt === "string" && /^[a-f0-9]{32}$/.test(binding.attempt) && binding.digest === "");
    }
  }
  // Fixed array order and a format/algorithm/key domain avoid ambiguous
  // concatenation. The fields are supplied by the trusted storage owner.
  const aad = encoder.encode(JSON.stringify([
    "CEWGCM01", "AES-256-GCM", keyId, ...fields.map((name) => binding[name]),
  ]));
  requireValue(aad.byteLength <= 1024);
  return aad;
}

export class ParcelCipher {
  #key;
  #keyId;

  constructor(key, keyId) {
    requireValue(key instanceof CryptoKey && key.algorithm.name === "AES-GCM"
      && key.algorithm.length === 256 && !key.extractable
      && key.usages.length === 2 && key.usages.includes("encrypt") && key.usages.includes("decrypt"));
    requireValue(typeof keyId === "string" && /^[a-f0-9]{64}$/.test(keyId));
    this.#key = key;
    this.#keyId = keyId;
  }

  static async import(raw) {
    const copy = bytes(raw, 32);
    requireValue(copy.byteLength === 32);
    try {
      const domain = encoder.encode("CEWGCM01 storage key identity\0");
      const material = new Uint8Array(domain.length + copy.length);
      material.set(domain); material.set(copy, domain.length);
      let identity;
      try { identity = hex(new Uint8Array(await crypto.subtle.digest("SHA-256", material))); }
      finally { material.fill(0); }
      const key = await crypto.subtle.importKey("raw", copy, { name: "AES-GCM" }, false, ["encrypt", "decrypt"]);
      return new ParcelCipher(key, identity);
    } catch {
      throw new ParcelRefused();
    } finally {
      copy.fill(0);
    }
  }

  get keyId() { return this.#keyId; }

  close() {
    // Dropping the reference is not a JavaScript/runtime memory-erasure claim.
    this.#key = null;
  }

  async seal(plaintext, binding) {
    requireValue(this.#key !== null);
    const input = bytes(plaintext, MAX_PARCEL);
    const aad = bindingBytes(binding, this.#keyId);
    try {
      const nonce = crypto.getRandomValues(new Uint8Array(12));
      const encrypted = new Uint8Array(await crypto.subtle.encrypt({
        name: "AES-GCM", iv: nonce, additionalData: aad, tagLength: 128,
      }, this.#key, input));
      const envelope = new Uint8Array(MAGIC.length + nonce.length + encrypted.length);
      envelope.set(MAGIC); envelope.set(nonce, MAGIC.length);
      envelope.set(encrypted, MAGIC.length + nonce.length);
      return envelope;
    } catch {
      throw new ParcelRefused();
    } finally {
      input.fill(0);
    }
  }

  async open(envelope, binding) {
    requireValue(this.#key !== null);
    const input = bytes(envelope, MAX_PARCEL + 36);
    requireValue(input.length >= 36 && MAGIC.every((value, i) => value === input[i]));
    const aad = bindingBytes(binding, this.#keyId);
    try {
      return new Uint8Array(await crypto.subtle.decrypt({
        name: "AES-GCM", iv: input.slice(8, 20), additionalData: aad, tagLength: 128,
      }, this.#key, input.slice(20)));
    } catch {
      throw new ParcelRefused();
    }
  }
}
