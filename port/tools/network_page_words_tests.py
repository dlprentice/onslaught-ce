#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The words of the Network Game pages a player meets, asserted word for word and against the house style, with mutants.

    python3 port/tools/network_page_words_tests.py

port/game/ce_netpages.cpp is a game file (it needs the game's menu classes), so this test reads its text, not its behaviour
(network_player_menu_tests.py compiles the menu and drives it). It reads the first page's rows, the hints under them,
Connection Help's paragraphs and the one label every pointer to that page uses (CE_LOBBY_HELP_LABEL in ce_netlobby.h).
The words the lobby itself produces (the guest's "No game accepted this code", the host's "Nobody has joined yet") are
asserted by network_lobby_notice_tests.py; here only the rules that tie the two together are checked.

What it holds the words to:
  - each paragraph of Connection Help is exactly what the game shows (a change of wording is a change of this test);
  - the numbers in it are the real ones: the private ranges 10., 192.168. and 172.16 to 172.31 (RFC 1918) and the
    shared-address range 100.64 to 100.127 (RFC 6598) are checked against the ipaddress module, and the port is the
    lobby's (CE_LobbyPort), never a literal;
  - nothing claims a diagnosis the game cannot make: the words say what the host should do, not what is wrong;
  - the first page is Host Game, Join Game, Connection Help and Back, in that order; every pointer to the help page uses
    the one label, and Back from it returns to its row;
  - the player's words follow the house style: rows in Title Case; no file names, flags, protocol or test words
    (W3); none of the characters the small font lacks (T2); every row of the player's pages has a hint;
  - every paragraph fits the page's text buffers.
Each mutant changes one word, number or label of the page text and the test must fail. No game, window, network or
player data.
"""
import ipaddress
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PAGES = ROOT / "port/game/ce_netpages.cpp"
LOBBY_H = ROOT / "port/platform/common/ce_netlobby.h"
LOBBY = ROOT / "port/platform/common/ce_netlobby.cpp"
LABEL = "Connection Help"

# Connection Help, as a player reads it (the %u is the lobby's port)
EXPECTED = {
    "kHelpNetwork": "On the same network, Host Game and Join Game need nothing else.",
    "kHelpForward": "Over the internet, the host's router must send the game's port, %u (UDP), to the host's PC. Look for Port Forwarding in the router's settings.",
    "kHelpAddress": "The host then gives the friend the code and the router's public address. The friend types both.",
    "kHelpFirewall": "If Windows asks about the firewall, choose Allow Access.",
}
HELP_ORDER = ["kHelpNetwork", "t", "kHelpAddress", "kHelpFirewall"]
# The private-address paragraph is router-manual text: it lives in port/README.md, where its ranges are checked.
README = ROOT / "port/README.md"
PRIVATE_PARAGRAPH = re.compile(r"A public address starting (10\.), (192\.168\.), (172\.16 to 172\.31) or (100\.64 to 100\.127) means another router")
MENU_ROWS = [("Host Game", "NA_HOST"), ("Join Game", "NA_JOIN"), ("CE_LOBBY_HELP_LABEL", "NA_HELP"), ("Back", "NA_BACK")]
# The rows of the player's pages (direct connection): their labels, and the actions whose hint RowHint must give.
PLAYER_LISTS = ("NP_MENU", "NP_HOST_ADDRESS", "NP_HOST", "NP_JOIN", "NP_LOBBY")
WIDEN_BUFFER = 600          # Widen(buf, 600, text) in Height() and Lines(): a paragraph longer than this is cut
NOTICE_BYTES = 320          # CE_LobbyStatus::notice and the page's mNotice
# W3: what a player's page never says. T2: what the small font cannot draw.
FORBIDDEN_WORDS = re.compile(r"ce\.ini|README|--[a-z]|\.(ini|txt|json|exe|bea|bes)\b|\b(relay|signal(l)?ing|origin|HTTPS|pair approval|"
                             r"connection records?|carrier|identity|bounded|experimental|prototype|test(s|ed|ing)?|best effort|private)\b", re.I)
FORBIDDEN_CHARS = set("$@[\\]^_{|}~`")
SMALL_WORDS = {"a", "an", "the", "and", "or", "of", "to", "in", "on", "for"}

MUTANTS = [
    ("forward-protocol", "port, %u (UDP), to the host's PC", "port, %u (TCP), to the host's PC"),
    ("forward-port-literal", "send the game's port, %u (UDP)", "send the game's port, 27652 (UDP)"),
    ("forward-router-words", "Look for Port Forwarding in the router's settings", "Look for the settings"),
    ("firewall-button", "choose Allow Access.", "choose Block Access."),
    ("address-public", "and the router's public address", "and the address of this PC"),
    ("help-port-literal", "snprintf(t, sizeof t, kHelpForward, (unsigned)CE_LobbyPort());", "snprintf(t, sizeof t, kHelpForward, 27652u);"),
    ("help-order", "\t\t\ty = Lines(kHelpAddress, y, kCream, FONT_SMALL, kPageWidth) + 14.f;\n\t\t\tLines(kHelpFirewall, y, kCream, FONT_SMALL, kPageWidth);\n",
     "\t\t\ty = Lines(kHelpFirewall, y, kCream, FONT_SMALL, kPageWidth) + 14.f;\n\t\t\tLines(kHelpAddress, y, kCream, FONT_SMALL, kPageWidth);\n"),
    ("help-back-elsewhere", "\t\tcase NP_HELP:\n\t\t\tShowMenu(NA_HELP);", "\t\tcase NP_HELP:\n\t\t\tShowMenu(NA_HOST);"),
    ("menu-row-lost", "\t\tl->AddItem(new (MEMTYPE_MENUITEM) CNetItem(CE_LOBBY_HELP_LABEL, NA_HELP));\n", ""),
    ("menu-rows-swapped", 'CNetItem("Host Game", NA_HOST)', 'CNetItem("Join Game", NA_HOST)'),
    ("row-lower-case", 'mRoomCopy = new (MEMTYPE_MENUITEM) CNetItem("Copy Code", NA_COPY)',
     'mRoomCopy = new (MEMTYPE_MENUITEM) CNetItem("Copy code", NA_COPY)'),
    ("row-protocol-word", 'CNetItem("Paste Address", NA_PASTE_ADDRESS)', 'CNetItem("Paste Relay Address", NA_PASTE_ADDRESS)'),
    ("hint-lost", '\tcase NA_COPY:\t\t\treturn "Copies the code so you can paste it in a message.";\n', ""),
    ("hint-file-name", '"Copies the code so you can paste it in a message."', '"Puts the code in ce.ini."'),
    ("hint-bracket", '"Letters and digits. You can leave out the dashes."', '"Letters and digits [A-Z, 0-9]."'),
    ("page-experimental", '"Play a mission with a friend on another PC. You both need the same version of Onslaught CE."',
     '"Experimental: play a mission together with a friend on another PC."'),
    ("lobby-row-lower-case", 'CNetItem("Start Campaign", NA_CAMPAIGN_START)', 'CNetItem("Start campaign co-op", NA_CAMPAIGN_START)'),
    ("lobby-test-word", '"Waiting for your friend to start."', '"Waiting for the test host to start."'),
    ("lobby-hint-lost", '\tcase NA_START:\t\t\treturn "Starts the mission for you and your friend.";\n', ""),
    ("careers-said", '"A network game does not change your careers."', '"Careers."'),
]
README_MUTANTS = [
    ("private-ten", "starting 10., 192.168.", "starting 11., 192.168."),
    ("private-172", "172.16 to 172.31 or", "172.16 to 172.32 or"),
    ("shared-range", "100.127 means another", "100.128 means another"),
]
LABEL_MUTANT = ("label-changed", "#define CE_LOBBY_HELP_LABEL\t\"Connection Help\"", "#define CE_LOBBY_HELP_LABEL\t\"How to connect over the Internet\"")


def constants(text):
    """name -> text of every `const char *const kName = "..." "...";` in the file."""
    out = {}
    for m in re.finditer(r'const char \*const (k[A-Za-z]+) = ((?:\s*"(?:[^"\\]|\\.)*")+);', text):
        out[m.group(1)] = "".join(re.findall(r'"((?:[^"\\]|\\.)*)"', m.group(2)))
    return out


def list_block(pages, name):
    m = re.search(r"mList\[%s\] = new.*?(?=\n\s*(?://[^\n]*\n\s*)*l = mList\[|\n\t\tfor \(int p = 0)" % name, pages, re.S)
    return m.group(0) if m else ""


def rows_of(block):
    return [(a.strip('"'), b) for a, b in re.findall(r'C(?:NetItem|NetValue|NetField)\(("[^"]*"|CE_LOBBY_HELP_LABEL), (NA_[A-Z_]+)', block)]


def title_case(label):
    words = label.split()
    return all(w[0].isupper() or (i and w in SMALL_WORDS) for i, w in enumerate(words))


def player_words(pages):
    """The literals of the player's direct path: row labels, hints, Connection Help, and the first and host pages' words."""
    words = []
    for name in PLAYER_LISTS:
        words += [label for label, _ in rows_of(list_block(pages, name)) if label != "CE_LOBBY_HELP_LABEL"]
    hint = re.search(r"const char \*RowHint\(int action\)\s*\{(.*?)\n\}", pages, re.S)
    words += re.findall(r'"((?:[^"\\]|\\.)*)"', hint.group(1)) if hint else ["(RowHint is missing)"]
    consts = constants(pages)
    words += [consts.get(n, "") for n in EXPECTED]
    for sentence in ('"Play a mission with a friend on another PC. You both need the same version of Onslaught CE."',
                     '"A network game does not change your careers."', '"Your Join Code"', '"Read it out or send it to your friend."',
                     '"Waiting for your friend to join."', '"Your friend is connecting..."', '"Type your friend\'s code, then choose Connect."',
                     '"Code copied."', '"The code could not be copied. Read it out instead."', '"Player 1"', '"Player 2"',
                     '"You"', '"Your Friend"', '"Waiting for your friend to start."', '"Stops trying to connect."',
                     '"Type your friend\'s code first."', '"The code has a typo. Check it with your friend."'):
        words.append(sentence.strip('"') if sentence in pages else "(missing sentence %s)" % sentence)
    return words


def check(pages, header, lobby, readme=None):
    """The failures of the rules for these three texts (empty: all hold)."""
    bad = []
    consts = constants(pages)
    macro = re.search(r'#define CE_LOBBY_HELP_LABEL\s+"([^"]*)"', header)
    if not macro or macro.group(1) != LABEL:
        bad.append("the label macro is not %r" % LABEL)
    for name, want in EXPECTED.items():
        if consts.get(name) != want:
            bad.append("%s is not its words: %r" % (name, consts.get(name)))
    # the label: one macro, no copies of its text
    for where, text in (("ce_netpages.cpp", pages), ("ce_netlobby.cpp", lobby)):
        code = re.sub(r"(?m)^\s*//[^\n]*|\t//[^\n]*", "", text).replace('" CE_LOBBY_HELP_LABEL "', "")
        if re.search(r'"[^"\n]*' + LABEL + r'[^"\n]*"', code):
            bad.append("%s repeats the label's text instead of using CE_LOBBY_HELP_LABEL" % where)
    if lobby.count("see \" CE_LOBBY_HELP_LABEL \".") != 1:
        bad.append("the host's notice after 45 seconds with nobody there points to the help by the label")
    # the first page's rows (the operator's invitation row of an ICE build sits between Join Game and the help)
    rows = [r for r in rows_of(list_block(pages, "NP_MENU")) if r[1] != "NA_IMPORT_OPEN"]
    if rows != MENU_ROWS:
        bad.append("the first page's rows are not Host Game, Join Game, the help, Back: %r" % rows)
    # Connection Help: its paragraphs in order, the lobby's port, and Back to its row
    help_case = re.search(r'\t\t\ty = Lines\(kHelpNetwork,.*?\n\t\t\tbreak;', pages, re.S)
    used = re.findall(r'Lines\((k[A-Za-z]+|t),', help_case.group(0)) if help_case else []
    if used != HELP_ORDER:
        bad.append("Connection Help does not draw its paragraphs in order: %r" % used)
    if not help_case or "snprintf(t, sizeof t, kHelpForward, (unsigned)CE_LobbyPort());" not in pages:
        bad.append("Connection Help's port is not the lobby's")
    if "27652" in "".join(consts.get(n, "") for n in EXPECTED):
        bad.append("a help paragraph names the port as a literal")
    if not re.search(r"\t\tcase NP_HELP:\n\t\t\tShowMenu\(NA_HELP\);", pages):
        bad.append("Back on Connection Help does not return to its row")
    # the numbers: the private-address paragraph, in port/README.md
    nets = {"10.": ipaddress.ip_network("10.0.0.0/8"), "192.168.": ipaddress.ip_network("192.168.0.0/16"),
            "172.16 to 172.31": ipaddress.ip_network("172.16.0.0/12"), "100.64 to 100.127": ipaddress.ip_network("100.64.0.0/10")}
    readme = README.read_text(encoding="utf-8") if readme is None else readme
    m = PRIVATE_PARAGRAPH.search(" ".join(readme.split()))
    private = " ".join(m.groups()) if m else ""
    if not m:
        bad.append("port/README.md lacks the private-address paragraph")
    for words, net in nets.items():
        if words not in private:
            bad.append("the private-address paragraph lacks %r" % words)
        elif " to " in words:
            m = re.fullmatch(r"(\d+)\.(\d+) to (\d+)\.(\d+)", words)
            head = str(net.network_address).split(".")[0]
            first, last = int(str(net.network_address).split(".")[1]), int(str(net.broadcast_address).split(".")[1])
            if not m or m.group(1) != m.group(3) or m.group(1) != head or (int(m.group(2)), int(m.group(4))) != (first, last):
                bad.append("%r is not the range of %s" % (words, net))
    for name in EXPECTED:
        if re.search(r"\b(your router blocks|your firewall blocks|is blocking|you have|you are behind|is broken)\b", consts.get(name, "")):
            bad.append("%s claims a diagnosis" % name)
    # house style over the player's words
    for text in player_words(pages):
        if text.startswith("(missing") or text.startswith("(RowHint"):
            bad.append(text)
            continue
        if FORBIDDEN_WORDS.search(text):
            bad.append("player text uses a forbidden word: %r" % text)
        if set(text) & FORBIDDEN_CHARS:
            bad.append("player text uses a character the small font lacks: %r" % text)
    for name in PLAYER_LISTS:
        for label, action in rows_of(list_block(pages, name)):
            if label != "CE_LOBBY_HELP_LABEL" and not title_case(label):
                bad.append("row %r is not in Title Case" % label)
            if action != "NA_BACK" and not re.search(r"\tcase %s:\t+return \"" % action, pages):
                bad.append("row %r has no hint" % label)
    if not title_case(LABEL):
        bad.append("the help label is not in Title Case")
    # fit
    for name, text in consts.items():
        if len(text) >= WIDEN_BUFFER:
            bad.append("%s is %d characters: the page's text buffer holds %d" % (name, len(text), WIDEN_BUFFER - 1))
    size = re.search(r"char notice\[(\d+)\]", header)
    if not size or int(size.group(1)) != NOTICE_BYTES:
        bad.append("the notice buffer is not %d bytes" % NOTICE_BYTES)
    if "char\t\tmNotice[320]" not in pages:
        bad.append("the page's own notice buffer is not 320 bytes")
    return bad


def main():
    pages, header, lobby = PAGES.read_text(encoding="utf-8"), LOBBY_H.read_text(encoding="utf-8"), LOBBY.read_text(encoding="utf-8")
    results = []

    def report(ok, label):
        print(("PASS " if ok else "FAIL ") + label, flush=True)
        results.append(bool(ok))

    failures = check(pages, header, lobby)
    report(not failures, "the page words: every rule holds (%d paragraphs, %d player strings)" % (len(EXPECTED), len(player_words(pages))))
    for line in failures:
        print("   FAIL " + line)
    for name, needle, replacement in MUTANTS:
        if pages.count(needle) != 1:
            report(False, "mutant %s: its text is found %d times, not once" % (name, pages.count(needle)))
            continue
        report(bool(check(pages.replace(needle, replacement, 1), header, lobby)), "mutant %s is caught" % name)
    readme = README.read_text(encoding="utf-8")
    for name, needle, replacement in README_MUTANTS:
        if readme.count(needle) != 1:
            report(False, "mutant %s: its text is found %d times in port/README.md, not once" % (name, readme.count(needle)))
            continue
        report(bool(check(pages, header, lobby, readme.replace(needle, replacement, 1))), "mutant %s is caught" % name)
    name, needle, replacement = LABEL_MUTANT
    if header.count(needle) != 1:
        report(False, "mutant %s: its text is not found once" % name)
    else:
        report(bool(check(pages, header.replace(needle, replacement, 1), lobby)), "mutant %s is caught" % name)
    ok = all(results)
    print("page words: " + ("OK" if ok else "FAILED"), "(%d checks)" % len(results))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
