# SPDX-License-Identifier: GPL-3.0-only
# Local disabled loopback entrypoint. Automated tests generate an enabled ephemeral configuration.
using Workerd = import "/workerd/workerd.capnp";

const config :Workerd.Config = (
  services = [
    (name = "public", worker = (
      modules = [
        (name = "worker.mjs", esModule = embed "worker.mjs")
      ],
      compatibilityDate = "2026-11-02",
      globalOutbound = "denied",
      bindings = [
        (name = "CE_ENABLED", text = "false"),
        (name = "CE_LOCAL_HTTP", text = "true"),
        (name = "LOBBY_DIRECTORY", durableObjectNamespace = "LobbyDirectory"),
        (name = "GAME_ROOM", durableObjectNamespace = "GameRoom")
      ],
      durableObjectNamespaces = [
        (className = "LobbyDirectory", uniqueKey = "ce-online-lobby-directory-local-v1", enableSql = true),
        (className = "GameRoom", uniqueKey = "ce-online-lobby-room-local-v1", enableSql = true)
      ],
      durableObjectStorage = (localDisk = "state")
    )),
    (name = "denied", network = (allow = [])),
    (name = "state", disk = (path = "../../../local-data/online-lobby-worker-local-state", writable = true))
  ],
  sockets = [(name = "http", address = "127.0.0.1:8788", http = (), service = "public")]
);
