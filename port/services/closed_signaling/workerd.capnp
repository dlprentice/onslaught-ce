# SPDX-License-Identifier: GPL-3.0-only
# Local, disabled entrypoint. Authored runtime tests generate their own enabled configuration.
using Workerd = import "/workerd/workerd.capnp";

const config :Workerd.Config = (
  services = [
    (name = "public", worker = (
      modules = [
        (name = "worker.mjs", esModule = embed "worker.mjs"),
        (name = "parcel_crypto.mjs", esModule = embed "parcel_crypto.mjs")
      ],
      compatibilityDate = "2026-11-02",
      globalOutbound = "denied",
      bindings = [(name = "CE_ENABLED", text = "false")],
      durableObjectNamespaces = [(className = "ClosedPairs", uniqueKey = "ce-closed-pilot-local-v1", enableSql = true)],
      durableObjectStorage = (localDisk = "state")
    )),
    (name = "denied", network = (allow = [])),
    (name = "state", disk = (path = "../../../local-data/signaling-worker-local-state", writable = true))
  ],
  sockets = [(name = "http", address = "127.0.0.1:8787", http = (), service = "public")]
);
