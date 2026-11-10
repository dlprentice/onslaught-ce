#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The words of the Network game pages that tell a host how to get another player in over the Internet, asserted word for word, with mutants.

    python3 port/tools/network_page_words_tests.py

port/game/ce_netpages.cpp is a game file (it needs the game's menu classes), so this test reads its text, not its behaviour: the help page's paragraphs, the host page's two optional lines, the
menu row that opens the help page and the one label every pointer to it uses (CE_LOBBY_HELP_LABEL in ce_netlobby.h). The words that the lobby itself produces (the guest's "No game answered",
the host's "Nobody has reached this PC yet") are asserted by network_lobby_notice_tests.py; here only the rules that tie the two together are checked.

What it holds the words to:
  - each paragraph is exactly what the game shows, word for word (a change of wording is a change of this test);
  - the numbers in them are the real ones: the private ranges 10., 192.168. and 172.16 to 172.31 (RFC 1918) and the shared-address range 100.64 to 100.127 (RFC 6598) are checked against the
    ipaddress module, and the port is the lobby's (CE_LobbyPort), never a literal;
  - nothing claims a diagnosis the game cannot make: the lobby's causes are introduced with "may", and the page words say what the host should do, not what is wrong;
  - every pointer to the help page uses the one label, which is the text of the menu row between Join a game and Back;
  - every paragraph fits the page's text buffers.
Each mutant changes one word, number or label of the page text and the test must fail. No game, window, network or player data.
"""
import ipaddress
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PAGES = ROOT / "port/game/ce_netpages.cpp"
LOBBY_H = ROOT / "port/platform/common/ce_netlobby.h"
LOBBY = ROOT / "port/platform/common/ce_netlobby.cpp"
LABEL = "How to connect over the Internet"

# the words, as a player reads them (the %u is the lobby's port)
EXPECTED = {
    "kHelpForward": "1. Forward UDP port %u on your router to this PC (Port Forwarding or Virtual Server; this PC's address is on the host page).",
    "kHelpFirewall": "2. If Windows asks about the firewall, choose Allow access.",
    "kHelpAddress": "3. Give the other player the join code and your PUBLIC address (the WAN or Internet address on the router's status page). They type the code, then the address.",
    "kHelpPrivate": "An address starting 10., 192.168. or 172.16 to 172.31 is private, and one starting 100.64 to 100.127 is shared by your provider: another router is in front of yours, so a port "
                    "forward is not enough. Use a VPN or overlay network both players already use, or play on one network.",
    "kHelpUntested": "Not tested: play through routers over the real internet, more than two players, Windows on real hardware.",
    "kHostForward": "Over the internet: forward UDP port %u on your router to this PC and give your public address too (see " + LABEL + ").",
    "kHostFirewall": "If a firewall asks about Onslaught CE, choose Allow.",
}
MENU_LINE = "There is no matchmaking server: play on one network, or read " + LABEL + " (below)."
HELP_ORDER = ["kHelpForward", "kHelpFirewall", "kHelpAddress", "kHelpPrivate", "kHelpUntested"]
WIDEN_BUFFER = 600          # Widen(buf, 600, text) in Height() and Lines(): a paragraph longer than this is cut
NOTICE_BYTES = 320          # CE_LobbyStatus::notice and the page's mNotice

# (name, text in ce_netpages.cpp, replacement): each must make the test fail
MUTANTS = [
    ("invite-inline-label", 'CNetField("", NA_IMPORT_ORIGIN', 'CNetField("Service address", NA_IMPORT_ORIGIN'),
    ("invite-heading-selectable", "mImportAddressLabel->mEnabled = FALSE;", "mImportAddressLabel->mEnabled = TRUE;"),
    ("invite-offline-wording", "Import stays offline. Continue later contacts the service.", "Import connects immediately."),
    ("forward-pointer", "this PC's address is on the host page", "this PC's address is on the router"),
    ("forward-protocol", "1. Forward UDP port %u on your router", "1. Forward TCP port %u on your router"),
    ("forward-port-literal", "1. Forward UDP port %u on your router", "1. Forward UDP port 27652 on your router"),
    ("forward-step-number", "\"1. Forward UDP port", "\"2. Forward UDP port"),
    ("firewall-button", "choose Allow access.", "choose Block access."),
    ("address-private", "your PUBLIC address", "your address"),
    ("address-status-page", "on the router's status page", "on the router's box"),
    ("private-ten", "starting 10., 192.168.", "starting 11., 192.168."),
    ("private-172", "172.16 to 172.31 is private", "172.16 to 172.32 is private"),
    ("shared-range", "100.64 to 100.127", "100.64 to 100.128"),
    ("shared-claim", "is shared by your provider", "may be shared by your provider"),
    ("another-router", "another router is in front of yours", "your router is broken"),
    ("host-page-address", "; this PC's address is on the host page", ""),
    ("heading-lost", "\t\t\ty = Lines(CE_LOBBY_HELP_LABEL, y, kWarn, FONT_SMALL, kHelpWidth) + 6.f;\n", ""),
    ("untested-lost", "Not tested: play through routers over the real internet, more than two players, Windows on real hardware.", "Tested."),
    ("host-forward-pointer", "(see \" CE_LOBBY_HELP_LABEL \").", "."),
    ("host-forward-port-literal", "forward UDP port %u on your router to this PC and give", "forward UDP port 27652 on your router to this PC and give"),
    ("host-firewall-word", "choose Allow.", "choose Block."),
    ("menu-pointer-literal", "or read \" CE_LOBBY_HELP_LABEL \" (below).", "or read How to connect over the Internet (below)."),
    ("menu-row-lost", "\t\tl->AddItem(new (MEMTYPE_MENUITEM) CNetItem(CE_LOBBY_HELP_LABEL, NA_HELP));\n", ""),
    ("menu-row-after-back", "\t\tl->AddItem(new (MEMTYPE_MENUITEM) CNetItem(CE_LOBBY_HELP_LABEL, NA_HELP));\n\t\tl->AddItem(new (MEMTYPE_MENUITEM) CNetItem(\"Back\", NA_BACK));\n",
     "\t\tl->AddItem(new (MEMTYPE_MENUITEM) CNetItem(\"Back\", NA_BACK));\n\t\tl->AddItem(new (MEMTYPE_MENUITEM) CNetItem(CE_LOBBY_HELP_LABEL, NA_HELP));\n"),
    ("help-order", "\t\t\ty = Lines(kHelpFirewall, y, kWhite, FONT_SMALL, kHelpWidth) + 4.f;\n\t\t\ty = Lines(kHelpAddress, y, kWhite, FONT_SMALL, kHelpWidth) + 8.f;\n",
     "\t\t\ty = Lines(kHelpAddress, y, kWhite, FONT_SMALL, kHelpWidth) + 8.f;\n\t\t\ty = Lines(kHelpFirewall, y, kWhite, FONT_SMALL, kHelpWidth) + 4.f;\n"),
    ("help-port-literal", "snprintf(t, sizeof t, kHelpForward, (unsigned)CE_LobbyPort());", "snprintf(t, sizeof t, kHelpForward, 27652u);"),
    ("help-back-lost", "\t\tcase NP_HELP:\n\t\t\tShow(NP_CONNECTION);\n", "\t\tcase NP_HELP:\n\t\t\tbreak;\n"),
]
LABEL_MUTANT = ("label-changed", "header", "#define CE_LOBBY_HELP_LABEL\t\"How to connect over the Internet\"", "#define CE_LOBBY_HELP_LABEL\t\"Help\"")


def constants(text):
    """The page's string constants as the compiler joins them (adjacent literals), with CE_LOBBY_HELP_LABEL expanded: name -> text."""
    found = {}
    for m in re.finditer(r'const char \*const (k[A-Za-z]+)\s*=\s*((?:"(?:[^"\\]|\\.)*"|CE_LOBBY_HELP_LABEL|\s)+);', text):
        parts = re.findall(r'"((?:[^"\\]|\\.)*)"|(CE_LOBBY_HELP_LABEL)', m.group(2))
        found[m.group(1)] = "".join(lit if lit or not macro else LABEL for lit, macro in parts)
    return found


def manual_help_pages(pages):
    """Check the retained direct/manual help after its native-only preface."""
    draw = re.search(r"\bvoid\s+DrawText\s*\(", pages)
    if not draw:
        return pages
    start = pages.find("case NP_HELP:", draw.start())
    if start < 0:
        return pages
    start += len("case NP_HELP:")
    branch = re.match(r"\s*if \(SignalingSelected\(\)\) \{", pages[start:])
    if not branch:
        return pages
    left = start + branch.end() - 1
    depth, quote, index = 1, None, left + 1
    while depth and index < len(pages):
        char = pages[index]
        if quote:
            if char == "\\":
                index += 1
            elif char == quote:
                quote = None
        elif char in ("\"", "'"):
            quote = char
        elif char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
        index += 1
    return pages[:start] + pages[index:] if depth == 0 else pages


def signaling_words(pages):
    bad = []
    draw = re.search(r"\bvoid\s+DrawText\s*\(", pages)
    if not draw:
        return ["the menu drawing owner is missing"]
    text = pages[draw.start():]
    match = re.search(r"case NP_SIGNAL_DISCLOSURE:(.*?)case NP_SIGNAL_STATUS:", text, re.S)
    if not match:
        return ["the approved-pair disclosure page is missing"]
    disclosure = match.group(1)
    if "SignalingOrigin()" not in disclosure or "Lines(destination" not in disclosure:
        bad.append("the disclosure must draw the validated HTTPS signaling origin")
    if "Cloudflare TURN" not in disclosure:
        bad.append("the disclosure must name the fixed Cloudflare TURN recipient")
    begin = re.search(r"case NA_SIGNAL_DISCLOSE:(.*?)case NA_SIGNAL_ACCEPT:", pages, re.S)
    accept = re.search(r"case NA_SIGNAL_ACCEPT:(.*?)case NA_SIGNAL_PROCEED:", pages, re.S)
    if not begin or "CE_LobbySignalingBeginPreflight" not in begin.group(1) or "CE_LobbySignalingAccept" in begin.group(1):
        bad.append("network disclosure Continue must begin preflight without accepting the pair")
    if not accept or "CE_LobbySignalingAccept" not in accept.group(1) or "NeedsConsent" not in accept.group(1):
        bad.append("pair consent must remain an explicit separately gated action")
    if "CE must still establish and validate the connection" not in pages:
        bad.append("service delivery must not claim CE readiness")
    return bad


def invitation_input_words(pages):
    """Keep the small import form readable and its local-only step explicit."""
    draw = re.search(r"\bvoid\s+DrawText\s*\(", pages)
    match = re.search(r"case NP_INVITATION_INPUT:(.*?)case NP_INVITATION_PREVIEW:", pages[draw.start():], re.S) if draw else None
    if not match:
        return ["the invitation input page is missing"]
    bad = []
    body = match.group(1)
    if 'Lines("Online invitation"' not in body:
        bad.append("the invitation page needs its short player-facing title")
    if "Import stays offline. Continue later contacts the service." not in body:
        bad.append("the invitation page must keep import separate from service contact")
    paragraphs = re.findall(r'Lines\("([^"\n]*)"', body)
    if any(len(text) > 80 for text in paragraphs):
        bad.append("the invitation input instructions must stay short")
    if not re.search(r'CNetItem\("Service address", NA_NONE\)', pages) or "mImportAddressLabel->mEnabled = FALSE;" not in pages:
        bad.append("the address needs a separate nonselectable label row")
    if not re.search(r'CNetField\("", NA_IMPORT_ORIGIN', pages):
        bad.append("the editable address must not concatenate its label and value")
    return bad


def check(pages, header, lobby):
    original_pages = pages
    pages = manual_help_pages(pages)
    """The failures of the rules for these three texts (empty: all hold)."""
    bad = []
    consts = constants(pages)
    macro = re.search(r'#define CE_LOBBY_HELP_LABEL\s+"([^"]*)"', header)
    if not macro or macro.group(1) != LABEL:
        bad.append("the label macro is not %r" % LABEL)
    for name, want in EXPECTED.items():
        if consts.get(name) != want:
            bad.append("%s is not its words: %r" % (name, consts.get(name)))
    # the page text that is not a constant
    menu = re.findall(r'Lines\("((?:[^"\\]|\\.)*)" CE_LOBBY_HELP_LABEL "((?:[^"\\]|\\.)*)", y \+ 4\.f, kWhite\);', pages)
    if [a + LABEL + b for a, b in menu] != [MENU_LINE]:
        bad.append("the menu's pointer to the help is not %r: %r" % (MENU_LINE, menu))
    # the label: one macro, no copies of its text
    for where, text in (("ce_netpages.cpp", pages), ("ce_netlobby.cpp", lobby)):
        if LABEL in text.replace('" CE_LOBBY_HELP_LABEL "', ""):
            bad.append("%s repeats the label's text instead of using CE_LOBBY_HELP_LABEL" % where)
    if "See \" CE_LOBBY_HELP_LABEL \".\"" not in lobby or lobby.count("CE_LOBBY_HELP_LABEL") != 2:
        bad.append("the lobby's two notices (no answer, nobody has come) each point to the help by the label")
    # the row: between Join a game and Back, in the menu's list
    # Stop at the next list, including the separate host-address chooser.
    menu_list = re.search(r'mList\[NP_CONNECTION\] = .*?(?=\n\s*l = mList\[)', pages, re.S)
    rows = re.findall(r'CNetItem\(("[^"]*"|CE_LOBBY_HELP_LABEL), (NA_[A-Z_]+)\)', menu_list.group(0)) if menu_list else []
    if [(a.strip('"'), b) for a, b in rows] != [("Host a game", "NA_HOST"), ("Join a game", "NA_JOIN"), ("CE_LOBBY_HELP_LABEL", "NA_HELP"), ("Back", "NA_BACK")]:
        bad.append("the menu's rows are not Host, Join, the help, Back: %r" % rows)
    # the page: its words in the order a host does the steps, and the lobby's port
    help_case = re.search(r'case NP_HELP:\s*\{(.*?)\n\t\tcase |case NP_HELP:\s*\{(.*?)\n\t\t\}\n\t\t\}', pages, re.S)
    body = (help_case.group(1) or help_case.group(2)) if help_case else ""
    used = re.findall(r'Lines\((k[A-Za-z]+|t|CE_LOBBY_HELP_LABEL),', body)
    if ["CE_LOBBY_HELP_LABEL", "t", "kHelpFirewall", "kHelpAddress", "kHelpPrivate", "kHelpUntested"] != used:
        bad.append("the help page does not draw its paragraphs in order: %r" % used)
    if "snprintf(t, sizeof t, kHelpForward, (unsigned)CE_LobbyPort());" not in body:
        bad.append("the help page's step 1 does not use the lobby's port")
    if "27652" in "".join(consts.get(n, "") for n in EXPECTED) or "27652" in pages.split("namespace {", 1)[-1].split("class CNetItem", 1)[0]:
        bad.append("a page paragraph names the port as a literal")
    if not re.search(r"case\s+NP_HELP:.*?Show\s*\(mConnectionSelected\s*\?\s*NP_CONNECTION\s*:\s*NP_MENU\)", pages, re.S):
        bad.append("Back on the help page does not return to the menu")
    # the numbers
    nets = {"10.": ipaddress.ip_network("10.0.0.0/8"), "192.168.": ipaddress.ip_network("192.168.0.0/16"), "172.16 to 172.31": ipaddress.ip_network("172.16.0.0/12"),
            "100.64 to 100.127": ipaddress.ip_network("100.64.0.0/10")}
    private = consts.get("kHelpPrivate", "")
    for words, net in nets.items():
        if words not in private:
            bad.append("the private-address paragraph lacks %r" % words)
        elif " to " in words:
            m = re.fullmatch(r"(\d+)\.(\d+) to (\d+)\.(\d+)", words)
            first, last = int(str(net.network_address).split(".")[1]), int(str(net.broadcast_address).split(".")[1])
            if not m or m.group(1) != m.group(3) or m.group(1) != str(net.network_address).split(".")[0] or (int(m.group(2)), int(m.group(4))) != (first, last):
                bad.append("%r is not the range of %s (%d.%d to %d.%d)" % (words, net, int(str(net.network_address).split(".")[0]), first, int(str(net.network_address).split(".")[0]), last))
    # no diagnosis: the page words are instructions or conditionals, and the lobby's causes are 'may' clauses
    for name in HELP_ORDER:
        text = consts.get(name, "")
        if re.search(r"\b(your router blocks|your firewall blocks|is blocking|you have|you are behind)\b", text):
            bad.append("%s claims a diagnosis" % name)
    for marker in ("The game cannot tell why: the code or address may be wrong, the host may not be waiting", "The game cannot tell why: the other player may not have joined yet, may have typed"):
        if marker not in lobby:
            bad.append("the lobby's notice does not say the game cannot tell why: %r" % marker[:60])
    # fit
    for name, text in consts.items():
        if len(text) >= WIDEN_BUFFER:
            bad.append("%s is %d characters: the page's text buffer holds %d" % (name, len(text), WIDEN_BUFFER - 1))
    worst = "No game answered at 255.255.255.255, port 65535. The game cannot tell why: the code or address may be wrong, the host may not be waiting (a host makes a new code after five failed attempts), or a router or firewall may be blocking UDP port 65535. See " + LABEL + "."
    if len(worst) >= NOTICE_BYTES:
        bad.append("the longest no-answer notice is %d bytes: the notice buffer holds %d" % (len(worst), NOTICE_BYTES - 1))
    size = re.search(r"char notice\[(\d+)\]", header)
    if not size or int(size.group(1)) != NOTICE_BYTES:
        bad.append("the notice buffer is not %d bytes" % NOTICE_BYTES)
    if "char\t\tmNotice[320]" not in pages:
        bad.append("the page's own notice buffer is not 320 bytes")
    for label in ("Local / direct connection", "Online relay (experimental)",
                  "Public lobbies (unavailable)", "Public browsing is unavailable"):
        if label not in pages:
            bad.append("configured transport entry must state its choices and unavailable browsing")
    bad.extend(signaling_words(original_pages))
    bad.extend(invitation_input_words(original_pages))
    return bad


def main():
    pages, header, lobby = PAGES.read_text(encoding="utf-8"), LOBBY_H.read_text(encoding="utf-8"), LOBBY.read_text(encoding="utf-8")
    results = []

    def report(ok, label):
        print(("PASS " if ok else "FAIL ") + label, flush=True)
        results.append(bool(ok))

    failures = check(pages, header, lobby)
    report(not failures, "the page words: every rule holds (%d paragraphs and lines checked)" % len(EXPECTED))
    for line in failures:
        print("   FAIL " + line)
    for name, needle, replacement in MUTANTS:
        if name == "help-back-lost":
            help_return = re.search(r"case\s+NP_HELP\s*:.*?break;", pages, re.S)
            if not help_return:
                raise RuntimeError("Help return scope is unavailable for its causal control")
            needle = help_return[0]
            replacement = re.sub(r"Show\s*\(mConnectionSelected\s*\?\s*NP_CONNECTION\s*:\s*NP_MENU\)", "Close()", needle, count=1)
        mutation_source, mutation_prefix, mutation_suffix = pages, "", ""
        if name.startswith("menu-row-"):
            start = re.search(r"mList\s*\[\s*NP_CONNECTION\s*\]", pages)
            end = re.search(r"mList\s*\[\s*NP_HOST_CHOOSE\s*\]", pages[start.end():]) if start else None
            if not start or not end:
                raise RuntimeError("Host/Join list scope is unavailable for its causal control")
            stop = start.end() + end.start()
            mutation_prefix, mutation_source, mutation_suffix = pages[:start.start()], pages[start.start():stop], pages[stop:]
        if mutation_source.count(needle) != 1:
            raise RuntimeError("mutation is not unique (%d found): %s" % (mutation_source.count(needle), name))
        report(bool(check((mutation_prefix + mutation_source.replace(needle, replacement, 1) + mutation_suffix), header, lobby)), "mutant %s is caught" % name)
    name, target, needle, replacement = LABEL_MUTANT
    if header.count(needle) != 1:
        raise RuntimeError("mutation is not unique: " + name)
    report(bool(check(pages, header.replace(needle, replacement, 1), lobby)), "mutant %s is caught" % name)
    ok = all(results)
    print("page words: " + ("OK" if ok else "FAILED"), "(%d checks)" % len(results))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
