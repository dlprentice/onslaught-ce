#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Focused actual-Session lifecycle controls; native effects/transport are explicit authored boundaries.

Runs through network_session_tests.py's pinned snapshots, real caller extraction and optional target compilers.
The separate real-World coupling is required for claims about its codec/readiness/factory implementation.
"""
import sys
from network_session_tests import main

CASES = tuple("life-host-" + name for name in (
    "start", "start-fail", "send0", "send-fail", "publication-fail", "window", "ack", "unsent-ack", "source-gap",
    "front-deadline", "ack-deadline", "late-ack", "late-bad-ack", "partial-deadline", "valid-terminal", "valid-closed",
    "future-ack", "wrong-tick", "wrong-baseline", "wrong-epoch", "envelope", "schema4", "short", "long",
    "retired-tick", "world-ack-fail", "leave-first", "leave-last", "terminal", "availability")) + tuple(
    "life-guest-" + name for name in (
    "start", "start-fail", "state-first", "lifecycle-first", "window-service", "availability", "wait",
    "gap", "wrong-baseline", "wrong-epoch", "envelope", "duplicate", "wrong-role-ack", "factory-fail",
    "apply-gap", "ack-send0", "stale", "invalid-frame", "lease", "coalesce"))

MUTATIONS = (
    ("life-send0-published", "if (!sent) { s.lifecycle_blocked = true; return true; }", "if (!sent) { /* negative: publish Send0 */ }", "life-host-send0", "lifecycle Published occurs only after Send1"),
    ("life-send0-unfenced", "return !s.lifecycle_blocked;", "return true;", "life-host-send0", "Send0 fences next native update without re-peeking mutable source"),
    ("life-state-overtakes-prefix", "if (s.lifecycle_blocked) return;\n    uint32_t tick", "/* negative: capture before prefix */\n    uint32_t tick", "life-host-send0", "Send0 never publishes or captures state"),
    ("life-missing-application-credit", "if (s.lifecycle_count == kLifecycleWindow) { s.lifecycle_blocked = true; return true; }", "if (s.lifecycle_count == kLifecycleWindow) { return true; }", "life-host-window", "full unacknowledged window fences native update"),
    ("life-ack-is-send-proof", "if (terminal) return true;\n    if (CE_WorldAcknowledgeLifecycle", "if (terminal) return true;\n    if (false && CE_WorldAcknowledgeLifecycle", "life-host-ack", "exact cumulative application proof retires prefix"),
    ("life-ignore-ack-tick", "info.operation == ack.operation && info.source_tick == ack.source_tick", "info.operation == ack.operation", "life-host-wrong-tick", "bad lifecycle receipt fails closed including terminal queue"),
    ("life-ignore-baseline", "uint32_t baseline = kind == LIFECYCLE_APPLIED ? CE_NetReadU32(&r) : 0;", "uint32_t baseline = kind == LIFECYCLE_APPLIED ? CE_NetReadU32(&r) : 0; if (kind == LIFECYCLE_APPLIED) baseline = s.baseline_sequence;", "life-host-wrong-baseline", "bad lifecycle receipt fails closed including terminal queue"),
    ("life-terminal-skips-validation", "if (kind == LIFECYCLE_APPLIED) return LifecycleReceipt", "if (terminal && kind == LIFECYCLE_APPLIED) return true;\n    if (kind == LIFECYCLE_APPLIED) return LifecycleReceipt", "life-host-leave-first", "bad lifecycle receipt fails closed including terminal queue"),
    ("life-wrong-start-order", "s.stage = BASELINED;\n        if (!Wait([] {", "s.stage = BASELINED;\n        if (!StartLifecycle(s.capture)) return 0;\n        if (!Wait([] {", "life-host-start", "Start follows actual baseline publication/application and exact peer proof"),
    ("life-apply-from-poll", "if (!DriveAvailability()) return false;", "if (!s.host && !ApplyLifecycle()) return false;\n    if (!DriveAvailability()) return false;", "life-guest-window-service", "no lifecycle native effects from Poll or while unavailable"),
    ("life-effects-while-blocked", "if (!RuntimeReady()) return;\n    if (!s.host)", "if (false && !RuntimeReady()) return;\n    if (!s.host)", "life-guest-availability", "readiness precedes native state effects at eligible render boundary"),
    ("life-readiness-bypassed", "int ready = CE_WorldFrameReadiness(s.incoming);", "int ready = CE_WORLD_READY;", "life-guest-state-first", "STATE before lifecycle defers without clock or native Apply"),
    ("life-invalid-defer", "int ready = CE_WorldFrameReadiness(s.incoming);", "int ready = CE_WorldFrameReadiness(s.incoming); if (ready == CE_WORLD_INVALID) ready = CE_WORLD_DEFER;", "life-guest-invalid-frame", "invalid deferred state fails before any native effect"),
    ("life-stale-resurrection", "if (ready == CE_WORLD_STALE) {", "if (ready == CE_WORLD_STALE) { CE_WorldApply(s.incoming);", "life-guest-stale", "validated obsolete watermark is dropped without resurrection"),
    ("life-ack-send0-commits", "if (sent == 1) s.lifecycle_ack_sent", "if (sent >= 0) s.lifecycle_ack_sent", "life-guest-ack-send0", "next Send1 queues retained ACK exactly once"),
    ("life-wire-gap-skipped", "packet.info.operation != s.lifecycle_received + 1", "false", "life-guest-gap", "whole Apply commits only the next source operation"),
    ("life-preflight-after-effects", "int ready = s.have_frame ? ReplicaReadiness() : CE_WORLD_DEFER;", "int ready = CE_WORLD_DEFER;", "life-guest-invalid-frame", "invalid deferred state fails before any native effect"),
    ("life-front-timeout-refreshed", "if (!sent) { s.lifecycle_blocked = true; return true; }", "if (!sent) { s.lifecycle_front_since = CE_NetNowMs(); s.lifecycle_blocked = true; return true; }", "life-host-front-deadline", "pending front fails at30000ms without publication or native update"),
    ("life-ack-timeout-refreshed", "bool expired = LifecycleExpired(CE_NetNowMs()) || StateExpired(CE_NetNowMs());", "if (s.lifecycle_count) s.lifecycle_sent_at[s.lifecycle_first] = CE_NetNowMs(); bool expired = LifecycleExpired(CE_NetNowMs()) || StateExpired(CE_NetNowMs());", "life-host-ack-deadline", "30000ms terminal validation cannot promote late application credit"),
    ("life-late-credit-promoted", "if (!Receive(terminal || expired)) return false;", "if (!Receive(terminal)) return false;", "life-host-late-ack", "30000ms terminal validation cannot promote late application credit"),
)

if __name__ == "__main__":
    sys.argv.append("--lifecycle")
    raise SystemExit(main())
