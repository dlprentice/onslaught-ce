#!/usr/bin/env python3
"""Exercise the actual authored native-order adapter using deterministic facts.

No native movement or UI acceptance is claimed. Each mutation must fail at its
named causal assertion; a compilation error, sanitizer report or signal fails
this test instead of counting as a rejected mutation.
"""
from pathlib import Path
import hashlib
import json
import os
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'port/game/ce_cmdorders.cpp'
HEADER = ROOT / 'port/game/ce_cmdorders.h'
LIFETIME = ROOT / 'port/game/ce_command_lifetime.h'
FIXTURE = ROOT / 'port/tools/tests/ce_cmdunit_orders_fixture.h'
DRIVER = ROOT / 'port/tools/tests/ce_cmdunit_orders_driver.cpp'


def edit_function(source, name, old, new):
    pattern = r'^(?:extern "C" )?(?:bool|void|CUnit\*|UnitOrder\*)\s+' + name + r'\([^;]*?\)\s*\{'
    match = re.search(pattern, source, re.M)
    if not match:
        raise RuntimeError('missing mutation function ' + name)
    start = source.index('{', match.start())
    depth = 0
    for end in range(start, len(source)):
        if source[end] == '{':
            depth += 1
        elif source[end] == '}':
            depth -= 1
            if not depth:
                break
    body = source[match.start():end + 1]
    if body.count(old) != 1:
        raise RuntimeError('mutation anchor ' + name)
    return source[:match.start()] + body.replace(old, new) + source[end + 1:]


def main():
    parent = ROOT / 'local-data/ce-cmdunit-orders-tests'
    parent.mkdir(parents=True, exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix='run-', dir=parent))
    source = SOURCE.read_text()
    edits = {
        'shutdown_squad_retained': ('Current', '!unit->GetSquad()->IsShuttingDown() &&', 'true &&', 'shutdown squad releases routine guide writes'),
        'shutdown_owner_retained': ('Current', '!unit->IsShuttingDown() &&', 'true &&', 'shutdown owner releases routine guide writes'),
        'routine_steals_point': ('PortCeCmdGuidePoint', 'return false;', 'return true;', 'routine maintenance cannot replace selected point'),
        'native_override_retains_owner': ('PortCeCmdGuidePoint', 'if (nativeOverride) { Clear(order); return true; }', 'if (nativeOverride) { return true; }', 'explicit native override wins and releases ownership'),
        'destruction_keeps_identity': ('PortCeCmdThingGone', 'gIdentities.Forget(reinterpret_cast<uintptr_t>(thing));', '(void)thing;', 'destroyed lifetime key is invalid'),
        'world_keeps_identity': ('CeCmdUnitOrdersWorldEnd', 'gIdentities.EndWorld();', '(void)0;', 'world reload rejects old key'),
        'ignores_squad_transfer': ('Current', 'unit->GetSquad() == order.squad && unit->GetSquad() &&', 'unit->GetSquad() &&', 'squad transfer releases the point'),
        'ignores_script_transfer': ('Current', 'unit->GetMissionScript() == order.script &&', 'true &&', 'stale return cannot stop a new script owner'),
        'ignores_guide_replacement': ('Current', 'unit == order.unit && !unit->IsShuttingDown() && unit->mGuide == order.guide &&', 'unit == order.unit && !unit->IsShuttingDown() &&', 'obsolete guide is not held by stale ownership'),
        'ignores_runtime_fence': ('PortCeCmdGuidePoint', '!CeCmdUnitOrdersEligible() || !Current(order, guide->GetUnit())', '!Current(order, guide->GetUnit())', 'disabled recording or network context preserves native point'),
        'invalid_shore_overwrites': ('CeCmdUnitMove', '!CeCmdUnitGroundPoint(x, y, point)', '(CeCmdUnitGroundPoint(x, y, point), false)', 'invalid shore point refused'),
        'stale_return_stops_script': ('CeCmdUnitReturnAI', 'if (!current) return false;', '(void)current;', 'stale return cannot stop a new script owner'),
        'dispatch_reports_destroyed_order': ('CeCmdUnitMove', 'return Complete(*order, key, serial, unit, applying.consumed);', 'return true;', 'world end during dispatch cannot report a live owned order'),
        'hold_reports_destroyed_order': ('CeCmdUnitHold', 'return Complete(*order, key, serial, unit, true);', 'return true;', 'world end during hold cannot retain ownership'),
        'return_retains_owner': ('CeCmdUnitReturnAI', 'Clear(*order);\n    if (!current)', '(void)0;\n    if (!current)', 'return AI clears only temporary guide point'),
        'release_noop': ('CeCmdUnitOrderRelease', 'if (!gOrderCount || !unit) return;', 'if (true) return;', 'released member follows later current squad point'),
        'release_clears_neighbour': ('CeCmdUnitOrderRelease', 'gOrders[i].unit == unit', 'true', 'release affects only its native unit'),
        'destruction_clears_neighbour': ('PortCeCmdThingGone', '(gOrders[i].unit == thing || gOrders[i].squad == thing)', 'true', 'unit destruction preserves neighbour order'),
        'squad_destruction_keeps_order': ('PortCeCmdThingGone', ' || gOrders[i].squad == thing', '', 'squad destruction releases only affected orders'),
        'hook_ignores_guide': ('PortCeCmdGuidePoint', '!order.used || order.guide != guide', '!order.used', 'guide override cannot clear neighbour'),
        'dispatch_scope_reusable': ('PortCeCmdGuidePoint', 'gApplying && !gApplying->consumed &&', 'gApplying &&', 'one-shot move retains actual requested point'),
        'dispatch_missing_write_accepted': ('CeCmdUnitMove', 'unit, applying.consumed);', 'unit, true);', 'missing native guide write refuses ownership'),
        'failed_dispatch_retains_owner': ('Complete', 'Clear(order);', '(void)0;', 'unobserved dispatch releases routine writes'),
        'nested_order_serial_ignored': ('Complete', ' || order.serial != serial', '', 'outer dispatch cannot claim a newer nested order'),
        'dying_owner_retained': ('Current', '!unit->IsDying() &&', 'true &&', 'dying hook retires ownership'),
        'dead_owner_retained': ('Current', 'unit->GetLife() > 0 &&', 'true &&', 'zero-life hook retires ownership'),
        'full_table_never_reclaims': ('Space', '!unit || !authority.commandable || !Current(gOrders[i], unit)', 'false', 'full ownership reclaims only stale slot'),
        'stored_point_changed': ('Assign', 'order.point[i] = point[i]', 'order.point[i] = point[i] + 1', 'one-shot move retains actual requested point'),
    }
    cases = {'control': source}
    for name, args in edits.items():
        cases[name] = edit_function(source, *args[:3])
    compiler = shutil.which('c++')
    if not compiler:
        raise RuntimeError('C++ compiler required')
    results = {}
    for name, text in cases.items():
        folder = out / name
        folder.mkdir()
        (folder / 'ce_cmdorders.cpp').write_text(text)
        shutil.copyfile(FIXTURE, folder / FIXTURE.name)
        for header in ('common.h', 'unit.h', 'squad.h', 'Guide.h'):
            (folder / header).write_text('#include "ce_cmdunit_orders_fixture.h"\n')
        command = [compiler, '-std=c++17', '-DCE_VERSION=1', '-O1', '-g', '-fsanitize=address,undefined',
                   '-fno-omit-frame-pointer', '-I' + str(folder), '-I' + str(ROOT / 'port/game'),
                   str(DRIVER), str(folder / 'ce_cmdorders.cpp'), '-o', str(folder / 'check')]
        built = subprocess.run(command, capture_output=True, text=True)
        (folder / 'compile.log').write_text(built.stdout + built.stderr)
        record = {'compiled': built.returncode == 0, 'compile_exit': built.returncode}
        if built.returncode == 0:
            env = dict(os.environ, ASAN_OPTIONS='detect_leaks=1:halt_on_error=1', UBSAN_OPTIONS='halt_on_error=1')
            ran = subprocess.run([str(folder / 'check')], capture_output=True, text=True, env=env)
            output = ran.stdout + ran.stderr
            (folder / 'run.log').write_text(output)
            match = re.search(r'checks=(\d+) failures=(\d+) skips=(\d+)', output)
            record.update(exit=ran.returncode,
                          check_counts=list(map(int, match.groups())) if match else None,
                          sanitizer_error='Sanitizer' in output or 'runtime error:' in output,
                          failure_label=next((line[5:] for line in output.splitlines() if line.startswith('FAIL ')), None),
                          expected_label=edits[name][3] if name != 'control' else None)
        results[name] = record
        if name == 'control' and (not record['compiled'] or record.get('exit') != 0):
            break
    passed = results.get('control', {}).get('exit') == 0 and results['control'].get('check_counts') == [270, 0, 0] and len(results) == len(cases) and all(
        record['compiled'] and record.get('exit') == 1 and not record.get('sanitizer_error') and
        record.get('failure_label') == record.get('expected_label')
        for name, record in results.items() if name != 'control')
    report = {'passed': passed, 'scope': 'Actual authored adapter with synthetic native facts; no native engine movement or UI acceptance',
              'inputs': {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest()
                         for p in (SOURCE, HEADER, LIFETIME, FIXTURE, DRIVER, ROOT / 'port/game/ce_commander.h')},
              'results': results}
    (out / 'RESULT.private.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({'passed': passed, 'checks': results.get('control', {}).get('check_counts'),
                      'compiled_mutants': sum(record['compiled'] for name, record in results.items() if name != 'control'),
                      'rejected_mutants': sum(record.get('exit') == 1 and not record.get('sanitizer_error') and
                                              record.get('failure_label') == record.get('expected_label')
                                              for name, record in results.items() if name != 'control'),
                      'receipt': str(out / 'RESULT.private.json')}))
    return 0 if passed else 1


if __name__ == '__main__':
    raise SystemExit(main())
