#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Compile the actual network menu with in-process providers; no game or sockets.

The menu (port/game/ce_netpages.cpp) is compiled twice with the real lobby, code, ICE, boot and invitation headers: once as
the --with-network build has it (Host Game, Join Game and Connection Help over a direct connection, the relay refused) and
once with CE_NETWORK_ICE as the --with-ice build has it (adding the Online invitation import, its preview, the network
disclosure and the signaling status page). Every case
must pass with its exact number of checks in its build, and every control (a mutant of the menu source) must fail its case
at the check named for it, so a check that cannot fail is caught.

The fixture covers menu decisions and dispatch only. Native display/input, transport, the signaling service, gameplay,
and Linux/Windows player acceptance require separate work.
"""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
VARIANTS = {"direct": [], "ice": ["-DCE_NETWORK_ICE=1"]}
# Cases of both builds; then each build's own. Values: the number of checks the case makes when it passes.
COMMON = {'multi-address': 6,
 'single-address': 3,
 'no-address': 2,
 'stale-address': 5,
 'join-input': 3,
 'join-back': 3,
 'waiting-back': 4,
 'start-not-ready': 4,
 'ready-start': 6,
 'offered-guard': 1,
 'entry-direct': 7,
 'help-back': 2,
 'campaign-guest-disabled': 8,
 'campaign-not-ready-disabled': 4,
 'campaign-late-back': 6,
 'campaign-queued': 10,
 'campaign-late-not-ready': 8,
 'campaign-late-not-host': 8,
 'campaign-unavailable': 8,
 'campaign-failed': 8,
 'campaign-stale': 13,
 'campaign-unrendered': 5,
 'campaign-unrendered-reentry': 7,
 'campaign-ordinary-start': 6,
 'campaign-row-absent': 2,
 'wrap-no-orphan': 3,
 'leave-during-starting': 4,
 'connect-incomplete-once': 4,
 'notice-box-join': 4,
 'refused-box-version': 3,
 'cheats-refused': 4,
 'notice-line-host': 4,
 'code-field-fits': 4,
 'address-hint': 4,
 'box-click-through': 3,
 'notice-box-outcome': 1,
 "rematch-host-result": 6,
        "rematch-guest-result": 6,
        "rematch-guest-cancel": 5,
        "rematch-host-lost": 8,
        "rematch-guest-failed": 5,
 'mouse-join': 4,
 'lobby-back-host': 2,
 'lobby-back-guest': 2}
CASES = {"direct": {**COMMON, **{'relay-unavailable': 4}},
         "ice": {**COMMON, **{'ice-invitation-refused': 8,
 'ice-wrong-role': 13,
 'ice-expired-at-use': 13,
 'ice-preview-back': 12,
 'ice-host': 25,
 'ice-join': 16,
 'ice-guest-awaiting-code': 30,
 'ice-practice-fields': 19,
 'ice-terminal': 16,
 'ice-disclosure-cancel': 18}}}
# A control: (build, case, the check at which that case must fail).
CONTROLS = {'automatic-host': ('direct', 'multi-address', 2),
 'no-refresh-before-host': ('direct', 'multi-address', 1),
 'waiting-back-keeps-lobby': ('direct', 'waiting-back', 4),
 'address-change-keeps-lobby': ('direct', 'multi-address', 4),
 'address-focus-lost': ('direct', 'multi-address', 5),
 'single-address-row': ('direct', 'single-address', 3),
 'join-back-focus': ('direct', 'join-back', 2),
 'help-back-focus': ('direct', 'help-back', 2),
 'campaign-current-mission': ('direct', 'campaign-stale', 6),
 'campaign-refusal-fallback': ('direct', 'campaign-unavailable', 7),
 'campaign-queued-quit': ('direct', 'campaign-queued', 7),
 'campaign-cache-before-render': ('direct', 'campaign-stale', 6),
 'campaign-unrendered-admit': ('direct', 'campaign-unrendered', 4),
 'campaign-reentry-retains-intent': ('direct', 'campaign-unrendered-reentry', 5),
 'campaign-stale-auto-retry': ('direct', 'campaign-stale', 6),
 'campaign-refusal-cleanup': ('direct', 'campaign-unavailable', 7),
 'relay-guard-removed': ('direct', 'relay-unavailable', 2),
 'connection-kept-on-return': ('direct', 'entry-direct', 7),
 'lobby-back-focus': ('direct', 'lobby-back-guest', 2),
 'join-notice-not-boxed': ('direct', 'notice-box-join', 2),
 'refused-versions-swapped': ('direct', 'refused-box-version', 2),
 'cheats-ignored': ('direct', 'cheats-refused', 2),
 'host-notice-boxed': ('direct', 'notice-line-host', 2),
 'host-line-dropped': ('direct', 'notice-line-host', 2),
 'host-line-overflows': ('direct', 'notice-line-host', 4),
 'code-field-uncapped': ('direct', 'code-field-fits', 2),
 'value-not-fitted': ('direct', 'code-field-fits', 4),
 'address-hint-absent': ('direct', 'address-hint', 2),
 'box-click-reaches-row': ('direct', 'box-click-through', 2),
 'room-no-pending': ('direct', 'rematch-guest-result', 2),
 'room-hidden-on-ready': ('direct', 'rematch-host-result', 4),
 'room-wrong-next-role': ('direct', 'rematch-guest-result', 4),
 'room-stale-after-leave': ('direct', 'rematch-host-lost', 7),
 'outcome-not-boxed': ('direct', 'notice-box-outcome', 1),
 'connect-incomplete-joins': ('direct', 'connect-incomplete-once', 2),
 'trap-swallows-mouse': ('direct', 'mouse-join', 2),
 'no-trapped-hover': ('direct', 'mouse-join', 3),
 'orphan-kept': ('direct', 'wrap-no-orphan', 2),
 'leave-while-starting': ('direct', 'leave-during-starting', 3),
 'no-player-slots': ('direct', 'leave-during-starting', 1),
 'ice-eager-preflight': ('ice', 'ice-host', 15),
 'ice-guest-needs-code-for-consent': ('ice', 'ice-guest-awaiting-code', 19),
 'ice-disclosure-keeps-pair': ('ice', 'ice-disclosure-cancel', 17),
 'ice-use-skips-expiry': ('ice', 'ice-expired-at-use', 13),
 'ice-any-role': ('ice', 'ice-wrong-role', 11),
 'ice-proceed-before-admission': ('ice', 'ice-host', 19),
 'ice-relay-host-chooser': ('ice', 'ice-host', 25),
 'ice-join-address-enabled': ('ice', 'ice-join', 15)}


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


# Each control: the exact text of the menu it replaces (it must occur once) and its replacement.
MUTANTS = {
    "ice-guest-needs-code-for-consent": (
        "if (!mCode->mValue[0] && SignalingSelected() && RequireSignaling(NA_CONNECT, NP_JOIN)) break;",
        "/* require a join code even before invited-guest consent */"),
    "automatic-host": ("CE_LobbyHostAt(mHostAddresses[mHostAddressIndex])", "CE_LobbyHost()"),
    # Host Game must list the addresses before it hosts, or it hosts with none.
    "no-refresh-before-host": ("            RefreshHostAddresses();\n            HostHere();", "            HostHere();"),
    # Back from the host's page stops hosting.
    "waiting-back-keeps-lobby": ("		case NP_HOST:\n		case NP_HOST_ADDRESS:\n			CE_LobbyLeave();\n", "		case NP_HOST:\n		case NP_HOST_ADDRESS:\n"),
    # A new address is a new lobby: the old one is left first.
    "address-change-keeps-lobby": ("        HostAddressText();\n        CE_LobbyLeave();\n", "        HostAddressText();\n"),
    # Changing the address keeps the focus on the address row.
    "address-focus-lost": ("            mList[NP_HOST_ADDRESS]->mSelected = 0;\n", "            (void)0;\n"),
    # One address: no address row.
    "single-address-row": ("	void	ShowHost() { Show(mHostAddressCount > 1 && !RelayRequested() ? NP_HOST_ADDRESS : NP_HOST); }",
                           "	void	ShowHost() { Show(!RelayRequested() ? NP_HOST_ADDRESS : NP_HOST); }"),
    # Back returns the focus to the row that opened the page (house style F2).
    "join-back-focus": ("		case NP_JOIN:\n			CE_LobbyLeave();\n			ShowMenu(NA_JOIN);", "		case NP_JOIN:\n			CE_LobbyLeave();\n			ShowMenu(NA_HOST);"),
    "help-back-focus": ("		case NP_HELP:\n			ShowMenu(NA_HELP);", "		case NP_HELP:\n			Show(NP_MENU);"),
    "campaign-current-mission": ("CE_LobbyCampaignStart(mCampaignMission)",
                                 "CE_LobbyCampaignStart(([&]() { CE_LobbyStatus current; CE_LobbyStatusGet(&current); return current.level; })())"),
    "campaign-refusal-fallback": ("const CE_LobbyCampaignStartResult result = CE_LobbyCampaignStart(mCampaignMission);",
                                  "const CE_LobbyCampaignStartResult result = CE_LobbyCampaignStart(mCampaignMission);\n"
                                  "            if (result != CE_LOBBY_CAMPAIGN_START_QUEUED) CE_LobbyStart();"),
    "campaign-queued-quit": ("case CE_LOBBY_CAMPAIGN_START_QUEUED:\n                Notice(",
                             "case CE_LOBBY_CAMPAIGN_START_QUEUED:\n                PostMessage(LT.GetHWnd(), WM_CLOSE, 0, 0);\n                Notice("),
    "campaign-cache-before-render": ("if (mCampaignMissionPendingKnown) mCampaignMissionPending = s.level;",
                                     "if (mCampaignMissionPendingKnown) mCampaignMissionPending = s.level;\n"
                                     "        if (mCampaignMissionPendingKnown) mCampaignMission = mCampaignMissionPending;"),
    "campaign-unrendered-admit": ("if (mPage != NP_LOBBY || !mCampaignMissionKnown) return;", "if (mPage != NP_LOBBY) return;"),
    "campaign-reentry-retains-intent": ("mPage = page;\n        mCampaignMissionKnown = false;", "mPage = page;"),
    "campaign-stale-auto-retry": ('Notice("The selected mission changed. Check it, then confirm again.");',
                                  'Notice("The selected mission changed. Check it, then confirm again.");\n'
                                  "                (void)[]() { CE_LobbyStatus current; CE_LobbyStatusGet(&current); return CE_LobbyCampaignStart(current.level); }();"),
    "campaign-refusal-cleanup": ('Notice("Campaign co-op is unavailable for this build or mission.");',
                                 'Notice("Campaign co-op is unavailable for this build or mission.");\n                CE_LobbyLeave();'),
    # A build without the relay must refuse a launch prepared for it.
    "relay-guard-removed": ("    if (RelayRequested())\n    {\n        snprintf(why, size, \"%s\", \"Experimental relay",
                            "    if (false)\n    {\n        snprintf(why, size, \"%s\", \"Experimental relay"),
    # The first page forgets the chosen connection: Host Game and Join Game choose it again.
    "connection-kept-on-return": ("        if (page == NP_MENU) mConnectionSelected = false;\n", ""),
    # Leave from the lobby: the focus returns to the row that led there (review finding 4, mutant M1).
    "lobby-back-focus": ("			CE_LobbyLeave();\n			ShowMenu(s.host ? NA_HOST : NA_JOIN);", "			CE_LobbyLeave();\n			ShowMenu(NA_HOST);"),
    # A failed join's notice must be in the message box, not fitted above the list (review finding 1).
    "join-notice-not-boxed": ("				LobbyNotice(s.notice);\n				CE_LobbyLeave();", "				Notice(s.notice);\n				CE_LobbyLeave();"),
    # While the host waits, what the lobby says is a line under the code, not a box over it (review R2 finding 1).
    "host-notice-boxed": ("				mLastLobbyNotice[0] = 0;	// while the host waits", "				FollowLobbyNotice(s.notice);	// while the host waits"),
    "host-line-dropped": ("			if (*lobby)\n				y = mHostLineBottom = Lines(lobby, y + 8.f, kWhite, FONT_SMALL, kPageWidth);\n", ""),
    "host-line-overflows": ("const float drop = need + 36.f <= room ? 36.f : (need < room ? room - need : 0.f);", "const float drop = 36.f;"),
    # The Code row takes 23 letters and digits; every value row ends by the arc (review R2 finding 2).
    "code-field-uncapped": ("			if (mAction == NA_CODE && c != '-' && c != ' ' && symbols >= CE_CODE_SYMBOLS)\n				continue;\n", ""),
    "value-not-fitted": ("			if (!*from || float(size.cx) * squeeze <= width)", "			if (true)"),
    # An impossible address says so while it is typed (review R2 polish 3).
    "address-hint-absent": ("		if (item == mAddress && AddressImpossible(mAddress->mValue))", "		if (false)"),
    # The click that closed a box is not also the page's (review R2 note 4).
    "box-click-reaches-row": ("			(void)MouseClickedInRect(0.f, 0.f, 640.f, 480.f);", "			(void)0;"),
    # Back from a network game the outcome is in the box.
    'room-no-pending': ('if (PlayerPages() && mReturningRoom && !s.host && s.state == CE_LOBBY_JOIN_CONNECTING)', 'if (false && PlayerPages() && mReturningRoom && !s.host && s.state == CE_LOBBY_JOIN_CONNECTING)'),
    'room-hidden-on-ready': ('else if (mReturningRoom && ready)', 'else if (false && mReturningRoom && ready)'),
    'room-wrong-next-role': ('Lines(s.host ? "Back in the lobby together."', 'Lines(true ? "Back in the lobby together."'),
    'cheats-ignored': ('		if (CE_CheatOn(cheat))\n', '		if (false && CE_CheatOn(cheat))\n'),
    'refused-versions-swapped': ('"Update, then join again.", s.local_version, s.peer_version);', '"Update, then join again.", s.peer_version, s.local_version);'),
    'room-stale-after-leave': ('(mPage == NP_LOBBY && s.state != CE_LOBBY_READY && s.state != CE_LOBBY_STARTING)))', '(false && mPage == NP_LOBBY && s.state != CE_LOBBY_READY && s.state != CE_LOBBY_STARTING)))'),
    "outcome-not-boxed": ("	if (outcome && !rematch)\n		Box(sMenu->mOutcome);", "	(void)outcome;"),
    # Connect with a code that is not complete joins nothing and says it once (review finding 2).
    "connect-incomplete-joins": ("			if (CE_CodeDecode(mCode->mValue, NULL) != CE_CODE_OK)\n			{", "			if (false)\n			{"),
    # The key trap answers the game's mouse question false, or every click on the Join page is lost (review finding 3).
    "trap-swallows-mouse": ("(event != KEY_EVENT_DOWN && event != KEY_EVENT_UP && event != KEY_EVENT_CHAR))\n		return false;",
                            "(event != KEY_EVENT_DOWN && event != KEY_EVENT_UP && event != KEY_EVENT_CHAR))\n		return true;"),
    # With the trap set the game's hover is off: the page follows the pointer itself.
    "no-trapped-hover": ("				PointerOnTrappedPage();\n", ""),
    # A start both games agreed cannot be left: the other game is already starting (PLAN C3).
    "leave-while-starting": ("			if (now.state == CE_LOBBY_STARTING)\n				return;", "			if (false)\n				return;"),
    # The lobby draws its two player slots.
    "no-player-slots": ("					PortCeNetDrawSlot(sx, kSlotY, true);\n", ""),
    # No line of a hint or paragraph ends on a single word (house rule 6).
    "orphan-kept": ("	for (float w = width - 16.f; n >= 2 && w >= width * 0.7f; w -= 16.f)", "	for (float w = width - 16.f; false; w -= 16.f)"),
    # The service is contacted only by the disclosure's Continue.
    "ice-eager-preflight": ("        mSignalAction = action; mSignalReturnPage = returnPage;\n",
                            "        mSignalAction = action; mSignalReturnPage = returnPage;\n#if defined(CE_NETWORK_ICE)\n"
                            "        (void)CE_LobbySignalingBeginPreflight();\n#endif\n"),
    # Cancel on the disclosure closes the selected pair.
    "ice-disclosure-keeps-pair": ("CE_LobbyLeave(); ClearSignalUi(); Show(returnPage);", "ClearSignalUi(); Show(returnPage);"),
    # Use invitation checks the preview again at the press: it may have expired since the last frame.
    "ice-use-skips-expiry": ("if (!InvitationPreviewValid()) { Notice(\"The invitation preview expired or changed. Choose the folder again.\"); break; }",
                             "/* preview not checked again */"),
    # The preview must be for the role that was chosen.
    "ice-any-role": ("mInvitation.state == CE_INVITATION_PREVIEW_READY && mInvitation.role == mInvitationRole &&",
                     "mInvitation.state == CE_INVITATION_PREVIEW_READY &&"),
    # Continue on the status page needs the pair admitted.
    "ice-proceed-before-admission": ("mSignalProceed->mEnabled = selected && disclosed && status == CeIceSignalStatus::Admitted &&",
                                     "mSignalProceed->mEnabled = selected && disclosed &&"),
    # A relay host never hosts on a chosen local address.
    "ice-relay-host-chooser": ("            if (RelayRequested())\n            {\n                if (CE_LobbyHost()) Show(NP_HOST);",
                               "            if (false)\n            {\n                if (CE_LobbyHost()) Show(NP_HOST);"),
    # The relay join sends no address.
    "ice-join-address-enabled": ("mAddress->mEnabled = mPasteAddress->mEnabled = !connecting && !RelayRequested();",
                                 "mAddress->mEnabled = mPasteAddress->mEnabled = !connecting;"),
}


def control(source: str, name: str) -> str:
    old, new = MUTANTS[name]
    if source.count(old) != 1:
        raise ValueError("control anchor does not match exactly once: " + name)
    return source.replace(old, new, 1)


def main() -> int:
    parent = ROOT / "local-data/port/network-player-menu-tests"
    parent.mkdir(parents=True, exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix="run-", dir=parent))
    out.chmod(0o700)
    source_path = ROOT / "port/game/ce_netpages.cpp"
    driver_path = ROOT / "port/tools/tests/network_player_menu_driver.cpp"
    source = source_path.read_bytes()
    driver = driver_path.read_bytes()
    headers = [ROOT / "port/platform/common/ce_netlobby.h", ROOT / "port/platform/common/ce_netcode.h",
               ROOT / "port/game/ce_network_stl.h", ROOT / "port/network/ice_game.h", ROOT / "port/network/ice_signaling.h",
               ROOT / "port/game/ce_network_boot.h", ROOT / "port/game/ce_network_invitation.h"]
    compiler = shutil.which("c++")
    receipt = {
        "schema": "network-player-menu-v2",
        "source_sha256": sha(source), "driver_sha256": sha(driver),
        "headers_sha256": {str(p.relative_to(ROOT)): sha(p.read_bytes()) for p in headers},
        "cases": [], "controls": [], "compile": [],
        "limits": ["In-process synthetic menu/provider checks only; no game, sockets, devices, signaling service or VM.",
                   "No public gameplay or native Linux/Windows display, input or player acceptance claim."],
    }
    env = {k: v for k, v in os.environ.items()
           if not k.startswith(("BEA_", "SDL_")) and k not in ("DISPLAY", "WAYLAND_DISPLAY", "DBUS_SESSION_BUS_ADDRESS")}
    isolation = out / "isolation"
    for folder in ("home", "config", "data", "cache", "state", "runtime", "tmp"):
        (isolation / folder).mkdir(parents=True, mode=0o700)
    env.update({"HOME": str(isolation / "home"),
                "XDG_CONFIG_HOME": str(isolation / "config"),
                "XDG_DATA_HOME": str(isolation / "data"),
                "XDG_CACHE_HOME": str(isolation / "cache"),
                "XDG_STATE_HOME": str(isolation / "state"),
                "XDG_RUNTIME_DIR": str(isolation / "runtime"),
                "TMPDIR": str(isolation / "tmp"),
                "SDL_AUDIO_DRIVER": "dummy", "BEA_REPORT_NO_OPEN": "1",
                "BROWSER": "/bin/true"})
    env.pop("DBUS_SESSION_BUS_ADDRESS", None)
    builds = [(variant, None) for variant in VARIANTS] + [(CONTROLS[name][0], name) for name in CONTROLS]
    if compiler is None:
        receipt["pass"] = False
        receipt["setup_error"] = "C++ compiler unavailable"
    else:
        for variant, name in builds:
            work = out / (variant if name is None else name)
            work.mkdir()
            text = source.decode() if name is None else control(source.decode(), name)
            # Keep the entire production implementation; only its project include directives
            # are supplied by the fixture, which includes the same headers itself.
            text = re.sub(r'^\s*#include\s+"[^"\n]+"\s*$', "", text, flags=re.MULTILINE)
            (work / "menu.inc").write_text(text)
            (work / "driver.cpp").write_bytes(driver)
            binary = work / "driver"
            command = [compiler, "-std=c++17", "-O0", "-DCE_VERSION=\"fixture\"", *VARIANTS[variant],
                       "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
                       "-I" + str(ROOT / "port/platform/common"), "-I" + str(ROOT / "port/game"),
                       "-I" + str(ROOT / "port/network"), str(work / "driver.cpp"), "-o", str(binary)]
            with (work / "compile.stdout.private").open("w") as stdout, (work / "compile.stderr.private").open("w") as stderr:
                result = subprocess.run(command, env=env, stdout=stdout, stderr=stderr, timeout=120, check=False)
            receipt["compile"].append({"variant": variant, "control": name, "exit": result.returncode,
                                       "menu_sha256": sha(text.encode())})
            if result.returncode:
                continue
            selected = CASES[variant] if name is None else [CONTROLS[name][1]]
            for case in selected:
                with (work / (case + ".stdout.private")).open("w") as stdout, (work / (case + ".stderr.private")).open("w") as stderr:
                    result = subprocess.run([str(binary), case], env=env, stdout=stdout, stderr=stderr, timeout=10, check=False)
                rows = [line.removeprefix("MENU_RESULT ") for line in (work / (case + ".stdout.private")).read_text().splitlines()
                        if line.startswith("MENU_RESULT ")]
                try:
                    counts = json.loads(rows[0]) if len(rows) == 1 else None
                except json.JSONDecodeError:
                    counts = None
                expected = {"checks": CASES[variant][case] if name is None else CONTROLS[name][2],
                            "failures": int(name is not None), "skipped": 0}
                expected_exit = int(name is not None)
                passed = result.returncode == expected_exit and counts == expected
                row = {"case": case, "variant": variant, "control": name, "exit": result.returncode,
                       "counts": counts, "expected": expected, "pass": passed}
                receipt["cases" if name is None else "controls"].append(row)
        receipt["pass"] = (len(receipt["compile"]) == len(builds)
                           and all(c["exit"] == 0 for c in receipt["compile"])
                           and len(receipt["cases"]) == sum(len(cases) for cases in CASES.values())
                           and len(receipt["controls"]) == len(CONTROLS)
                           and all(c["pass"] for c in receipt["cases"] + receipt["controls"]))
    (out / "result.private.json").write_text(json.dumps(receipt, indent=2) + "\n")
    failed = [f'{c["variant"]}:{c.get("control") or ""}:{c["case"]}:{c["counts"]}' for c in receipt["cases"] + receipt["controls"] if not c["pass"]]
    print(json.dumps({"pass": receipt["pass"], "cases": len(receipt["cases"]), "controls": len(receipt["controls"]),
                      "compile_failures": [c["control"] or c["variant"] for c in receipt["compile"] if c["exit"]],
                      "failed": failed, "receipt": str(out / "result.private.json")}))
    return 0 if receipt["pass"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
