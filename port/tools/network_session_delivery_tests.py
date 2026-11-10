#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Actual Session CSD1/state-delivery/quiet-WAIT repair orchestration with controlled native and transport owners."""
import sys
from network_session_tests import main
CASES=("delivery-context-override","delivery-host-admission-before-world","delivery-guest-admission-before-world",
       "delivery-host-admission-before-prerun","delivery-guest-admission-before-prerun","delivery-boot-missing","delivery-compat-missing","delivery-admission-world","delivery-admission-poll","delivery-admission-render")+tuple(
    "delivery-host-"+name for name in ("delta","state-timeout","late-proof","repair","focus","device","peer","timeout","backwards","quiet-changed",
    "shell-prefix","gate-freeze","announcement-send0","no-ready","mutated-frame","raw-send0","apply-pending","close"))+tuple(
    "delivery-guest-"+name for name in ("delta","repair","no-wait","wrong-generation","wrong-watermark","wrong-tick",
    "terminal","terminal-bad","focus","device","ready-send0","early-resume","timeout","wrong-size","corrupt-raw",
    "apply-fail","duplicate-raw","pending-input","admission-after-apply","offer-hold","offer-expiry"))
MUTATIONS=(
    ("offer-hold-removed", "CE_NetNowMs() - s.state_offer_since < kOfferHoldMs) return true;", "false) return true;", "delivery-guest-offer-hold", "newer ordinary image cannot replace a held offer"),
    ("repair-shell-prefix-unchecked", "if (repair_render && !RepairHeldGate()) {", "if (false) {", "delivery-host-shell-prefix", "shell refuses a changed held repair prefix before reaching render"),
    ("repair-generation-not-frozen", "s.repair_gate.wait_generation = generation;", "/* negative: defer generation proof beyond the shell */", "delivery-host-gate-freeze", "ready acknowledged repair routes render while native update stays fenced"),
    ("repair-update-unfenced", "if (s.repair_phase != REPAIR_OFF) return 0;", "/* negative: allow native update during repair */", "delivery-host-repair", "repair quiescence blocks native update"),
    ("repair-shell-starved", "bool repair_render = RepairHold()", "bool repair_render = false && RepairHold()", "delivery-host-repair", "ready acknowledged repair routes render while native update stays fenced"),
    ("repair-capture-unavailable", "bool DriveRepairHost() {\n    if (!RepairLocalReady()) return true;", "bool DriveRepairHost() { /* negative: ignore local device/window availability */", "delivery-host-focus", "unavailable repair cannot capture or apply"),
    ("repair-installed-from-poll", "if (!DriveAvailability()) return false;", "if (!s.host && s.repair_phase != REPAIR_OFF && !DriveRepairGuest()) return false;\n    if (!DriveAvailability()) return false;", "delivery-guest-repair", "announcement Poll validates without installing, applying or emitting READY"),
    ("repair-lease-unchecked", "bytes != image.size || memcmp(s.wire, image.data, bytes)", "false", "delivery-host-mutated-frame", "changed retained repair frame refuses before raw send"),
    ("repair-send0-published", "if (!sent) return true;\n    if (CE_WorldFramePublished(s.capture)", "/* negative: publish unqueued raw */\n    if (CE_WorldFramePublished(s.capture)", "delivery-host-raw-send0", "publication follows an accepted transport enqueue"),
    ("repair-ready-send0-credited", "if (sent && !s.host) {", "if (!s.host) {", "delivery-guest-ready-send0", "READY Send0 retains installed transaction without claiming receipt"),
    ("repair-early-resume", "if (s.repair_phase != REPAIR_OFF && (s.repair_phase != REPAIR_APPLIED || !s.repair_ack_sent))", "if (false)", "delivery-guest-early-resume", "resume cannot precede whole repair Apply ACK"),
    ("repair-terminal-installs", "int admitted = CE_StateReceiverCheckRepair(s.state_receiver, &control, &s.authorized_repair_gate, CE_NetNowMs());", "int admitted = CE_StateReceiverExpectRepair(s.state_receiver, &control, &s.authorized_repair_gate, CE_NetNowMs());", "delivery-guest-terminal", "terminal repair cannot install a cache announcement or queue READY"),
    ("state-late-proof-promoted", "if (!Receive(terminal || expired)) return false;", "if (!Receive(terminal)) return false;", "delivery-host-late-proof", "exact30000ms cannot be rescued by late valid whole-Apply proof"),
    ("repair-apply-admission-lost", "if (!AdmissionCurrent()) return false;\n    int applied = CE_StateReceiverApplied", "/* negative: ignore post-Apply content refusal */\n    int applied = CE_StateReceiverApplied", "delivery-guest-admission-after-apply", "lost content proof after native Apply grants no delivery ACK or resume credit"),
)
if __name__=="__main__":
    sys.argv.append("--delivery")
    raise SystemExit(main())
