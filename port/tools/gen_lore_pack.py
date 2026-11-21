#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Writes port/game/ce_lore_pack.h: the lore articles Onslaught CE's Lore page reads, as plain paragraphs in the characters the game's font has.

The articles are the Onslaught Toolkit's lore library (its lore/*.md; preservation records written by the community and the developers'
own recollections, CC BY-SA 4.0). This script reads the articles named in ARTICLES, leaves out the sections named there (maintainer notes,
the app's own pages, cheat lists), turns the markdown into paragraphs, bullets, quotes and headings, and writes them with the SHA-256 of
each input in the header so that the pack says what it was made from.

    python3 port/tools/gen_lore_pack.py [--source DIR] [--check]

--source names the Toolkit's lore folder (default: the checkout beside this repository). --check writes nothing and exits 1 when the
committed pack differs from what the inputs give.

A block of the pack is one line of text whose first character is its kind: H heading, P paragraph, B bullet, Q quotation. The game wraps
each block to the page.
"""
import argparse
import hashlib
import re
import sys
import unicodedata
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "port/game/ce_lore_pack.h"

# (file, the title the Lore page lists, the sentence under it, the headings whose sections are left out, with everything under them)
ARTICLES = (
    ("world-lore.md", "The World of Allium", "The planet, its two peoples and the war.", ()),
    ("the-campaign.md", "The Campaign", "What the campaign is and how its missions are numbered.", ("The missions", "Why the list is read from your game rather than printed here")),
    ("game-overview.md", "The Game", "When it came out, why it has its name and what is in it.", ("In-Game Cheat Codes", "How It Landed", "The \"Onslaught2\" Mystery")),
    ("battle-engine-tech.md", "The Battle Engine", "The Aquila program, in the words of its head.", ()),
    ("lost-toys-history.md", "Lost Toys, the Studio", "The studio that made the game, in its own words.", ("Historical Contact Information (2003)",)),
    ("cut-content-secrets.md", "Cut Content and Secrets", "What was cut, what players found and the hidden extras.", ()),
)
# words that mean a block is about the tool or the repository and not about the game: a block with one of them is left out
MAINTAINER = re.compile(r"\b(SHA-256|Toolkit|Companion|repository|this app|your install|your game|Discord|Ghidra|Merged here|the app|OCR|sidecar|byte-identical|string table|\.py|\.dat|\.exe)\b|\bmedia/|\btools/")

# what the game's font lacks ($ @ [ \ ] ^ _ { | } ~ and the backtick) and the typographic characters of the sources, written as plain ones
PUNCTUATION = {"\u2018": "'", "\u2019": "'", "\u201c": '"', "\u201d": '"', "\u2013": "-", "\u2014": " - ", "\u2212": "-", "\u2026": "...",
               "\u00a0": " ", "\u2192": "to", "\u00d7": "x", "\u2022": "-", "\u00b7": "-", "\u2248": "about ", "~": "about ",
               "_": " ", "[": "(", "]": ")", "{": "(", "}": ")", "|": "/", "@": " at ", "$": "", "^": "", "`": "", "\\": "/"}


def find_source():
    for base in (ROOT, *ROOT.parents):
        candidate = base.parent / "Onslaught-Career-Editor/lore"
        if (candidate / ARTICLES[0][0]).is_file():
            return candidate
    return None


def plain(text):
    """Markdown inline marks removed and every character one the font has (accents kept to their base letter)."""
    text = re.sub(r"!\[[^\]]*\]\([^)]*\)", "", text)
    text = re.sub(r"\[([^\]]*)\]\([^)]*\)", r"\1", text)
    text = re.sub(r"<[^>]+>", "", text)
    text = text.replace("**", "").replace("__", "")
    text = re.sub(r"(?<![\w*])\*([^*\n]+)\*(?![\w*])", r"\1", text)
    out = []
    for ch in text:
        if ch in PUNCTUATION:
            out.append(PUNCTUATION[ch])
        elif ord(ch) < 128:
            out.append(ch)
        else:
            base = unicodedata.normalize("NFKD", ch).encode("ascii", "ignore").decode()
            out.append(base)
    return re.sub(r"\s+", " ", "".join(out)).strip()


def blocks(markdown, skip_headings):
    """The article's blocks as (kind, text), without its title, its status header and the sections named in skip_headings."""
    lines = markdown.splitlines()
    out, para, quote, skipping = [], [], [], None

    def flush():
        nonlocal para, quote
        if para and skipping is None:
            out.append(("P", plain(" ".join(para))))
        if quote and skipping is None:
            out.append(("Q", plain(" ".join(quote))))
        para, quote = [], []

    i = 0
    while i < len(lines):
        line = lines[i].rstrip()
        i += 1
        head = re.match(r"^(#{1,6})\s+(.*)$", line)
        if head:
            flush()
            level, title = len(head.group(1)), head.group(2).strip()
            if skipping is not None and level <= skipping:
                skipping = None
            if skipping is None and title in skip_headings:
                skipping = level
            if skipping is None and level > 1:
                out.append(("H", plain(title)))
            continue
        if skipping is not None:
            continue
        if re.match(r"^- \*\*(Status|Last updated|Summary):\*\*", line):
            # the article's status header: a bullet and its continuation lines
            while i < len(lines) and lines[i].startswith("  "):
                i += 1
            continue
        if re.match(r"^\s*\|", line):
            flush()
            cells = [c.strip() for c in line.strip().strip("|").split("|")]
            if all(re.fullmatch(r":?-{2,}:?", c) for c in cells if c):
                continue
            nxt = lines[i].strip() if i < len(lines) else ""
            if re.match(r"^\|\s*:?-{2,}", nxt):          # a header row: the rows below say what its words mean
                continue
            cells = [plain(c) for c in cells if c]
            if cells:
                out.append(("B", cells[0] + (": " + "; ".join(cells[1:]) if len(cells) > 1 else "")))
            continue
        if line.startswith(">"):
            if para:
                flush()
            quote.append(line.lstrip("> ").strip())
            continue
        if quote and not line.strip():
            flush()
            continue
        bullet = re.match(r"^\s*[-*]\s+(.*)$", line)
        if bullet:
            flush()
            out.append(("B", plain(bullet.group(1))))
            continue
        if not line.strip() or line.strip() == "---":
            flush()
            continue
        if quote:
            flush()
        para.append(line.strip())
    flush()
    return [(k, t) for k, t in out if t and not MAINTAINER.search(t)]


def article(directory, name, skip):
    return blocks((Path(directory) / name).read_text(), skip)


def build(directory):
    directory = Path(directory)
    sha = ", ".join(f"{name} {hashlib.sha256((directory / name).read_bytes()).hexdigest()}" for name, _, _, _ in ARTICLES)
    lines = ["// SPDX-License-Identifier: CC-BY-SA-4.0",
             "// port/game/ce_lore_pack.h: the lore articles of the Lore page, generated by port/tools/gen_lore_pack.py: do not edit.",
             "// Text: the Onslaught Toolkit's lore library (preservation records by the community and the developers' own recollections),",
             "// CC BY-SA 4.0; quotations stay with their authors. Made from, sha-256: " + sha + ".",
             "// One string for each article: a block to a line, its first character the kind (H heading, P paragraph, B bullet, Q quotation).",
             "#ifndef CE_LORE_PACK_H", "#define CE_LORE_PACK_H", "",
             "struct SCeLoreArticle\n{\n\tconst char\t*mTitle;\n\tconst char\t*mBlurb;\n\tconst char\t*mText;\n};", "",
             f"static const int\tkCeLoreCount = {len(ARTICLES)};", "",
             "static const SCeLoreArticle\tkCeLore[] = {"]
    for name, title, blurb, skip in ARTICLES:
        text = article(directory, name, skip)
        body = "\\n\"\n\t\t\"".join(f"{kind}{t}".replace("\\", "\\\\").replace('"', '\\"') for kind, t in text)
        lines.append(f'\t{{"{title}", "{blurb}",\n\t\t"{body}"}},')
    lines += ["};", "", "#endif", ""]
    return "\n".join(lines)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--source")
    ap.add_argument("--check", action="store_true")
    args = ap.parse_args()
    source = Path(args.source) if args.source else find_source()
    if not source:
        print("the Toolkit's lore folder was not found: pass --source", file=sys.stderr)
        return 2
    text = build(source)
    if args.check:
        same = OUT.is_file() and OUT.read_text() == text
        print("the pack is what its inputs give" if same else "the pack differs from what its inputs give")
        return 0 if same else 1
    OUT.write_text(text)
    print("wrote", OUT, f"({len(text)} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
