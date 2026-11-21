#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The map editor's screen furniture (port/game/ce_editor_ui.cpp) and its words.

The production layout and hit tests are compiled against a stand-in font (nine units a character for the small face,
twelve for the large) and laid out at 640x480, 1280x720, 1920x1080 and 3840x2160: every panel inside the window, the corner
panels apart, the title top left, the tools a column at the left edge, the card bottom right above a bottom-centre prompt
line, rows 20 units apart, clicks finding the tool, row or button under them and the level behind none of them. Every
notice the editor shows while it stays open (the EditorNote texts of port/game/ce_editor.cpp that no exit follows) must fit
one capsule at 640x480. Every player string of the editor is checked against the font's missing characters and the
house style's forbidden words. Authored mutants of the layout must each be caught. What this does not show: the drawing
itself, the real font's widths and the look, which need frames."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import re
import resource
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
UI = ROOT / 'port/game/ce_editor_ui.cpp'
EDITOR = ROOT / 'port/game/ce_editor.cpp'
DRIVER = ROOT / 'port/tools/tests/ce_editor_ui_driver.cpp'
LITERAL = re.compile(r'"((?:\\.|[^"\\])*)"')
MISSING = set('$@[\\]^_{|}~`')   # the small face has none of these
FORBIDDEN = [r'revision', r'placement', r'\blinked\b', r'\brecords?\b', r'installed map', r'experimental', r'prototype',
             r'authoring', r'ceproject', r'ce\.ini', r'\bmap \d', r' - ', r'(?<!Onslaught )\bCE\b', r'\bfile\b', r'session']
# The worst cases of the two formatted notices.
FORMATTED = ['Saved as Project 999999. Earlier saves are kept.', 'Project 999999 opened.']


def literals(text):
    return [m.group(1) for m in LITERAL.finditer(text)]


def messages(editor):
    """(notices that stay in the editor, messages that end up on the menu page)."""
    notices, page = [], []
    begin = editor.index('extern "C" bool PortCeEditorBegin(')
    begin_end = editor.index('\n}\n', begin)
    for m in re.finditer(r'EditorNote\((.*?)\);', editor):
        after = editor[m.end():m.end() + 60]
        if begin <= m.start() < begin_end:   # a refusal at the menu, shown in the menu's message box
            page.extend(x for x in literals(m.group(1)) if x)
            continue
        found = [x for x in literals(m.group(1)) if x]
        (page if re.match(r'\s*EditorExit\(\)', after) else notices).extend(found)
    for m in re.finditer(r'EditorRefuse\((.*?)\);', editor):
        page.extend(x for x in literals(m.group(1)) if x)
    return notices, page


def player_strings(editor, ui):
    strings = []
    for line in ui.splitlines():
        if line.lstrip().startswith('#include'):
            continue
        strings += [x for x in literals(line) if x and '%' not in x]
    body = editor[editor.index('void EditorBuildModel('):]
    body = body[:body.index('\n}\n')]
    strings += [x for x in literals(body) if x and '%' not in x]
    tools = re.search(r'kEditorTools\[\d+\] = \{([^}]*)\}', editor)
    strings += literals(tools.group(1)) if tools else ['<no tool list>']
    return strings


def lint(strings):
    problems = []
    for text in strings:
        bad = sorted(set(text) & MISSING)
        if bad:
            problems.append(f'missing character {bad} in: {text}')
        for word in FORBIDDEN:
            if re.search(word, text, re.I):
                problems.append(f'forbidden {word!r} in: {text}')
    return problems


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path)
    args = parser.parse_args()
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    parent = ROOT / 'local-data/ce-editor-ui-tests'
    parent.mkdir(parents=True, exist_ok=True)
    out = args.out or Path(tempfile.mkdtemp(prefix='run-', dir=parent))
    out.mkdir(parents=True, exist_ok=True)
    compiler = shutil.which('clang++') or shutil.which('g++')
    if not compiler:
        raise SystemExit('A C++ compiler is required.')
    editor, ui = EDITOR.read_text(), UI.read_text()
    notices, page = messages(editor)
    notices += FORMATTED

    def compile_run(folder, source):
        binary = folder / 'ui-check'
        built = subprocess.run([compiler, '-std=c++14', '-Wall', '-Wextra', '-Werror', '-I' + str(ROOT / 'port/game'),
                                str(DRIVER), str(source), '-o', str(binary)], capture_output=True, timeout=90, check=False)
        (folder / 'compile.stderr').write_bytes(built.stderr)
        if built.returncode:
            return {'compiled': False, 'exit': None}
        ran = subprocess.run([str(binary), *notices], capture_output=True, timeout=30, check=False)
        (folder / 'run.stdout').write_bytes(ran.stdout)
        (folder / 'run.stderr').write_bytes(ran.stderr)
        return {'compiled': True, 'exit': ran.returncode}

    baseline = compile_run(out, UI)
    start_driver = ROOT / 'port/tools/tests/ce_editor_start_ui_driver.cpp'
    start_bin = out / 'start-ui-check'
    start_built = subprocess.run([compiler, '-std=c++14', '-Wall', '-Wextra', '-Werror', '-I' + str(ROOT / 'port/game'),
                                  str(start_driver), str(UI), '-o', str(start_bin)], capture_output=True, timeout=90, check=False)
    (out / 'start-compile.stderr').write_bytes(start_built.stderr)
    if start_built.returncode:
        start_baseline = {'compiled': False, 'exit': None}
    else:
        start_ran = subprocess.run([str(start_bin)], capture_output=True, timeout=30, check=False)
        (out / 'start-run.stdout').write_bytes(start_ran.stdout)
        (out / 'start-run.stderr').write_bytes(start_ran.stderr)
        start_baseline = {'compiled': True, 'exit': start_ran.returncode}
    variants = {
        'card-over-prompt': ('l.card = Rect(m.width - l.margin - w, cardBottom - cardH - noteH, w, cardH + noteH);', 'l.card = Rect(m.width - l.margin - w, cardBottom + (m.height - l.margin - cardBottom) - cardH - noteH, w, cardH + noteH);'),
        'tool-hit-shifted': ('if (l.tool[i].Contains(x, y)) return i;', 'if (l.tool[i].Contains(x, y)) return i + 1 < m.toolCount ? i + 1 : i;'),
        'tools-clicked-behind-dialog': ('if (m.playing || m.dialog != CeEditorUiNoDialog || m.help) return -1;', 'if (m.playing) return -1;'),
        'world-is-furniture': ('if (m.dialog != CeEditorUiNoDialog || m.help) return true;\n    return l.title', 'if (m.dialog != CeEditorUiNoDialog || m.help || x >= -1e30f) return true;\n    return l.title'),
        'card-not-furniture': ('l.tools.Contains(x, y) || (CeEditorUiWhatShows(m).card && l.card.Contains(x, y))', 'l.tools.Contains(x, y)'),
        'notice-moves-with-card': ('const float y = m.playing ? playTop + kCapsuleGap * k : lane;', 'const float y = m.playing ? playTop + kCapsuleGap * k : (m.card ? l.card.y : lane) - kGap * k - capH;'),
        'start-label-ignores-marker': ('int bestHits = MarkerOverlaps(m, l, below, -1);', 'int bestHits = 0;'),
        'play-notice-overlaps-objective': ('const float playTop = l.objective.h > 0.f ? l.objective.y + l.objective.h : l.banner.y + l.banner.h;', 'const float playTop = l.banner.y + l.banner.h;'),
        'top-right-caps-collide': ('if (l.mode.h > 0.f) l.objective.y = l.mode.y + l.mode.h + kCapsuleGap * k;', 'if (l.mode.h > 0.f) l.objective.y = l.mode.y + l.mode.h + 3.f * k;'),
        'gamepad-prompt-ignored': ('if (m.gamepad) {', 'if (false && m.gamepad) {'),
        'card-follows-prompt': ('const float cardBottom = lane - kGap * k;', 'const float cardBottom = m.promptCount > 0 ? l.prompt.y - kGap * k : lane - kGap * k;'),
        'card-follows-value': ('values = Measure(measure, context, CeEditorUiSmall, kWidestPosition);', 'values = Measure(measure, context, CeEditorUiSmall, kWidestPosition) * 0.f;'),
        'compact-preview-hides-heading-on-wide-screen': ('l.bannerTitle = withTitle <= rightSafe - leftSafe;', 'l.bannerTitle = false;'),
        'banner-over-left-hud': ('const float leftSafe = Max(l.margin, inset + kHudLeft * k);', 'const float leftSafe = Max(l.margin, (inset + kHudLeft * k) * 0.f);'),
        'top-right-cap-clipped': ('const float rightEdge = m.width - l.margin - kCapOverhang * k;', 'const float rightEdge = m.width - l.margin - kCapOverhang * 0.f * k;'),
        'banner-under-hud': ('if (x + w > m.width - kHudRight * k) x = m.width - kHudRight * k - w;', '(void)kHudRight;'),
        'text-off-capsule': ('l.textDrop = (capH - l.lineHeight) * 0.5f;', 'l.textDrop = (l.title.h - l.lineHeight) * 0.5f;'),
        'unscaled': ('l.scale = s > 0.f ? s : 1.f;', 'l.scale = s > 0.f ? 1.f : 1.f;'),
        'tight-rows': ('l.rowPitch = kRowPitch * k;', 'l.rowPitch = 16.f * k;'),
        'no-above-yes': ('first + (i == 1 ? 0.f : 1.f) * answerPitch * k', 'first + (i == 1 ? 1.f : 0.f) * answerPitch * k'),
        'banner-over-game-screen': ('        if (m.gameScreen) return s;\n', ''),
        'marker-through-title': ('return Cross(r, l.title) || Cross(r, l.project)', 'return Cross(r, l.project)'),
        'marker-through-card': ('(s.card && Cross(r, l.card)) || ', ''),
        'marker-ring-unsized': ('const float r = kMarkReach * l.scale;', 'const float r = 0.f * l.scale;'),
        'markers-through-list': ('s.markers = m.markerCount > 0 && !reading;', 's.markers = m.markerCount > 0;'),
        'notice-through-list': ('s.notice = m.notice && m.notice[0] && m.noticeAlpha > 0.f && !reading;', 's.notice = m.notice && m.notice[0] && m.noticeAlpha > 0.f && !m.help;'),
        'card-through-question': ('s.card = m.card && !reading && !m.placingPlayerStart;', 's.card = m.card && !m.help && !m.placingPlayerStart;'),
        'scroll-marks-swapped': ('if (m.moreAbove && l.scroll[0].Contains(x, y)) return -1;', 'if (m.moreAbove && l.scroll[0].Contains(x, y)) return 1;'),
        'scroll-mark-on-rows': ('if (m.moreAbove) l.scroll[0] = Rect(l.dialog.x + kPanelPadX * k, top - kScrollBand * k,', 'if (m.moreAbove) l.scroll[0] = Rect(l.dialog.x + kPanelPadX * k, top,'),
        'narrow-question': ('const float w = Max(Max(text, title) + 2.f * 16.f, 260.f) * k;', 'const float w = Max(Max(text, title) * 0.f + 2.f * 16.f, 260.f) * k;'),
        "card-through-placement": ('s.card = m.card && !reading && !m.placingPlayerStart;',
                                   's.card = m.card && !reading;'),
        'card-bounds-during-placement': ('if (m.card && !m.placingPlayerStart)', 'if (m.card)'),
        'card-hit-during-placement': ('(CeEditorUiWhatShows(m).card && l.card.Contains(x, y))', 'l.card.Contains(x, y)'),
    }
    controls = {}
    for name, (before, after) in variants.items():
        folder = out / name
        folder.mkdir()
        if ui.count(before) != 1:
            raise ValueError('mutation anchor changed: ' + name)
        candidate = folder / 'ce_editor_ui.cpp'
        candidate.write_text(ui.replace(before, after, 1))
        result = compile_run(folder, candidate)
        controls[name] = dict(result, rejected=result == {'compiled': True, 'exit': 1})
    problems = lint(player_strings(editor, ui) + notices + page)
    # The lint must see: a mutant string with a missing character and one with a forbidden word are caught.
    lint_controls = {'missing-character': bool(lint(['Press [Enter]'])), 'forbidden-word': bool(lint(['Saved a new revision.']))}
    passed = baseline == {'compiled': True, 'exit': 0} and start_baseline == {'compiled': True, 'exit': 0} and \
        all(v['rejected'] for v in controls.values()) and not problems and \
        all(lint_controls.values()) and len(notices) > 10 and len(page) > 5
    receipt = {'passed': passed, 'scope': __doc__, 'baseline': baseline, 'start_baseline': start_baseline,
               'causal_controls': controls, 'lint_problems': problems,
               'lint_controls': lint_controls, 'notices': len(notices), 'page_messages': len(page),
               'sha256': {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in (UI, EDITOR, DRIVER, start_driver, Path(__file__).resolve())}}
    (out / 'RESULT.private.json').write_text(json.dumps(receipt, indent=2) + '\n')
    for problem in problems:
        print('FAIL', problem)
    for name, value in controls.items():
        if not value['rejected']:
            print('FAIL mutant survived:', name, value)
    print('map editor furniture:', 'PASS' if passed else 'FAIL', f'| {len(notices)} notices, {len(page)} page messages |', out)
    return 0 if passed else 1


if __name__ == '__main__':
    raise SystemExit(main())
