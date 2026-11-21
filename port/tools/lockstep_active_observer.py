#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Bounded semantic observations for an owned, active lockstep input exercise.

The debugger observes named CE boundaries and native player/projectile fields.
It never calls an inferior function, assigns game state or injects game input.
Its private samples contain actual positions, not structure-byte comparisons.
Acceptance rests on what the records show: the input script's own release is
intent, so every player's controls must also be seen neutral in both peers'
records after that player's last firing window (final_release).
This module alone launches nothing and contacts no service.
"""
from __future__ import annotations

import json
import math
import hashlib
import os
from pathlib import Path
import re
import signal
import subprocess
import time

MAX_TICK = 1200
MAX_ROWS = 700
MAX_SHOTS = 64
CONTROL_NAMES = ('forward', 'backward', 'charge', 'fire')
FIRE_WINDOW = 120    # Presents after a trigger release in which its fire may still be applied
FINAL_SAMPLES = 8    # observed rows required after each player's firing window


def exercise() -> dict:
    """The input schedule. Presents are on each role's own input clock (they start when its lockstep session runs)."""
    roles = {}
    for role, move, fire, hat in (("host", 120, 540, 1), ("guest", 300, 660, 4)):
        roles[role] = dict(move_on=move, move_off=move + 120, fire_on=fire, fire_off=fire + 30,
                           neutral_after=fire + 30 + FIRE_WINDOW,
                           script=f"{move}:h0:{hat},{move + 120}:h0:0,{fire}:b0:d,{fire + 30}:b0:u")
    return dict(schema=1, roles=roles, max_tick=MAX_TICK, max_rows=MAX_ROWS, max_shots=MAX_SHOTS,
                minimum_distance=1.0, neutral_ratio=4.0, final_samples=FINAL_SAMPLES)


def finite_vector(value):
    return (isinstance(value, list) and len(value) == 3 and
            all(type(v) in (int, float) and math.isfinite(v) and abs(v) <= 1048576 for v in value))


def _distance(rows, player):
    if not rows:
        return 0.0
    p = rows[0]['players'][player]
    return max(math.hypot(row['players'][player][0] - p[0], row['players'][player][1] - p[1]) for row in rows)


def valid_controls(value):
    return (isinstance(value, dict) and set(value) == {'1', '2'} and
            all(isinstance(v, dict) and set(v) == set(CONTROL_NAMES) and
                all(type(n) in (int, float) and math.isfinite(n) and 0 <= n <= 1 for n in v.values())
                for v in value.values()))


def final_release(rows_by_role, spec):
    """Observed release: after a player's firing window every control of that player stays neutral in every record.

    The input script releases the trigger, but a script is intent: only the records can show the release happened.
    Ticks are shared between the peers and Present counts are not (each game draws at its own rate), so a player's
    window closes at the last tick its own peer reached by the window's end on that peer's input clock, and both
    peers' records are judged from that tick on. Each record must hold at least `final_samples` such rows, and
    forward, backward, charge and fire must all read zero in every one of them. Returns (failures, rows per role/player).
    """
    failures = []
    counts = {role: {} for role in rows_by_role}
    for owner, number in (('host', '1'), ('guest', '2')):
        end = spec['roles'][owner]['neutral_after']
        closed = max((row['tick'] for row in rows_by_role[owner] if row['present'] <= end), default=0)
        for role, rows in rows_by_role.items():
            tail = [row for row in rows if row['tick'] > closed]
            counts[role][number] = len(tail)
            if len(tail) < spec['final_samples']:
                failures.append(f'{role}: player {number}: no observed neutral interval after its firing window')
                continue
            held = [name for name in CONTROL_NAMES if any(row['controls'][number][name] for row in tail)]
            if held:
                failures.append(f'{role}: player {number}: {"/".join(held)} still applied after its firing window')
    return failures, counts


def analyze_pair(records):
    """Fail closed on absent real observations; never infer effects from input alone."""
    failures = []
    metrics = {}
    by_tick = {}
    by_shot = {}
    spec = exercise()
    if not isinstance(records, dict) or set(records) != {'host', 'guest'}:
        return dict(passed=False, failures=['both roles required'])
    for role, item in records.items():
        if not isinstance(item, dict):
            failures.append(role + ': record absent'); continue
        if (type(item.get('schema')) is not int or item['schema'] != 1 or item.get('role') != role or item.get('failures') != [] or
                item.get('attached') is not True or item.get('exited') is not True or
                type(item.get('exit_code')) is not int or item['exit_code'] != 0):
            failures.append(role + ': incomplete observer'); continue
        rows = item.get('rows'); shots = item.get('shots')
        if (not isinstance(rows, list) or not 20 <= len(rows) <= MAX_ROWS or
                not isinstance(shots, list) or not 2 <= len(shots) <= MAX_SHOTS):
            failures.append(role + ': bounded samples absent'); continue
        last = -1; last_present = -1; seen = {}; valid = True
        for row in rows:
            if (not isinstance(row, dict) or type(row.get('tick')) is not int or not last < row['tick'] <= MAX_TICK or
                    type(row.get('present')) is not int or not max(0, last_present) <= row['present'] <= 10000 or
                    not isinstance(row.get('players'), dict) or set(row['players']) != {'1', '2'} or
                    not all(finite_vector(v) for v in row['players'].values()) or not valid_controls(row.get('controls'))):
                valid = False; break
            last = row['tick']; last_present = row['present']; seen[last] = row
        if not valid:
            failures.append(role + ': invalid native pose samples'); continue
        if rows[0]['present'] >= 60:
            failures.append(role + ': observer began after neutral admission window')
        player = '1' if role == 'host' else '2'
        phase = spec['roles'][role]
        direction = 'forward' if role == 'host' else 'backward'
        neutral = [r for r in rows if 16 <= r['present'] < 90 and not any(r['controls'][player].values())]
        moved = [r for r in rows if phase['move_on'] + 16 <= r['present'] <= phase['move_off'] - 8 and r['controls'][player][direction] > .5]
        released = [r for r in rows if phase['move_off'] + 32 <= r['present'] < phase['fire_on'] - 16 and not any(r['controls'][player].values())]
        charged = [r for r in rows if phase['fire_on'] <= r['present'] <= phase['fire_off'] + 16 and r['controls'][player]['charge'] > .5]
        fired = [r for r in rows if phase['fire_on'] <= r['present'] <= phase['neutral_after'] and r['controls'][player]['fire'] > .5]
        if len(charged) < 2 or not fired:
            failures.append(role + ': applied trigger charge/release input absent')
        distance = _distance(moved, player); baseline = _distance(neutral, player)
        if min(len(neutral), len(moved), len(released)) < 8:
            failures.append(role + ': neutral/move/release interval absent')
        if distance < max(spec['minimum_distance'], spec['neutral_ratio'] * baseline):
            failures.append(role + ': movement not above neutral drift')
        by_tick[role] = seen
        identities = {}; local_shots = 0
        for shot in shots:
            if (not isinstance(shot, dict) or shot.get('player') not in ('1', '2') or
                    type(shot.get('tick')) is not int or not 0 < shot['tick'] <= MAX_TICK or
                    type(shot.get('ordinal')) is not int or not 1 <= shot['ordinal'] <= MAX_SHOTS or
                    type(shot.get('kind')) is not int or not 0 <= shot['kind'] <= 0xffffffff or
                    type(shot.get('present')) is not int or not 0 <= shot['present'] <= 10000 or
                    not finite_vector(shot.get('position')) or not finite_vector(shot.get('velocity'))):
                failures.append(role + ': invalid native shot'); valid = False; break
            key = (shot['player'], shot['tick'], shot['ordinal'])
            if key in identities:
                failures.append(role + ': duplicate native shot'); valid = False; break
            identities[key] = shot
            if shot['player'] == player:
                local_shots += 1
                if not phase['fire_on'] - 8 <= shot['present'] <= phase['neutral_after']:
                    failures.append(role + ': own shot outside its own input interval')
            if sum(t > shot['tick'] for t in seen) < 3:
                failures.append(role + ': no tick progression after shot')
        if valid:
            by_shot[role] = identities
            if {key[0] for key in identities} != {'1', '2'} or not local_shots:
                failures.append(role + ': both actual shooters required')
            # A shot is made by an applied fire pulse: no player may have more initialized shots than fire samples
            # in this record, so a shot both processes spawn without any applied input is refused.
            for number in ('1', '2'):
                pulses = sum(1 for row in rows if row['controls'][number]['fire'] > .5)
                if sum(1 for key in identities if key[0] == number) > pulses:
                    failures.append(role + ': player ' + number + ': more native shots than applied fire pulses'); break
        metrics[role] = dict(pose_samples=len(rows), neutral_samples=len(neutral), move_samples=len(moved),
                             release_samples=len(released), own_move_distance=distance, own_neutral_distance=baseline,
                             initialized_shots=len(shots), own_initialized_shots=local_shots,
                             charge_samples=len(charged), fire_release_samples=len(fired))
    shared = 0
    if set(by_tick) == {'host', 'guest'}:
        # The debugger samples every even tick, and an odd tick only when a fire is applied, so a tick one record holds
        # and the other lacks is itself a disagreement (one process applied a fire the other did not) whenever the other
        # record reached that tick. Only ticks past the shorter record's last tick cannot be compared; the final-release
        # rule below still judges the controls on them.
        limit = min(max(by_tick['host']), max(by_tick['guest']))
        held = {role: {tick for tick in ticks if tick <= limit} for role, ticks in by_tick.items()}
        if held['host'] != held['guest']:
            failures.append('native tick sets disagree up to the shorter record')
        common = sorted(held['host'] & held['guest'])
        shared = len(common)
        if shared < 20:
            failures.append('too few corresponding native ticks')
        for tick in common:
            if by_tick['host'][tick]['controls'] != by_tick['guest'][tick]['controls']:
                failures.append('applied player inputs disagree at a shared tick'); break
            for player in ('1', '2'):
                if any(abs(a - b) > 0.0001 for a, b in zip(by_tick['host'][tick]['players'][player], by_tick['guest'][tick]['players'][player])):
                    failures.append('native player poses disagree at a shared tick'); break
            if failures and failures[-1] == 'native player poses disagree at a shared tick': break
        late, tails = final_release({role: list(ticks.values()) for role, ticks in by_tick.items()}, spec)
        failures.extend(late)
        for role, counts in tails.items():
            metrics[role]['final_neutral_samples'] = counts
    if set(by_shot) == {'host', 'guest'}:
        if set(by_shot['host']) != set(by_shot['guest']):
            failures.append('native shot identities disagree')
        else:
            for key, first in by_shot['host'].items():
                second = by_shot['guest'][key]
                if first['kind'] != second['kind'] or any(abs(a - b) > 0.0001 for name in ('position', 'velocity') for a, b in zip(first[name], second[name])):
                    failures.append('native initialized shot semantics disagree'); break
    return dict(passed=not failures and set(metrics) == {'host', 'guest'}, failures=failures,
                shared_native_ticks=shared, roles=metrics,
                scope='Native semantic player poses and initialized shots at corresponding actual lockstep ticks; no damage, complete mission or physical-input acceptance.')


# GDB owns only execution observation. All values written by this program are
# its own bounded records, never inferior fields or counters. Callback failures
# remain explicit; a partial debugger record cannot become a passing exercise.
GDB_BODY = r'''
import gdb, json, math, os
state = dict(schema=1, role=ROLE, inferior_pid=0, attached=False, exited=False, exit_code=None, failures=[], rows=[], shots=[])
shot_stack=[]; shot_ordinals={}; last_tick=-1
def flush():
    data=json.dumps(state,sort_keys=True,separators=(',',':')).encode()+b'\n'
    if len(data)>512*1024: raise RuntimeError('observer output bound')
    temporary=OUTPUT+'.incomplete'
    with open(temporary,'wb') as stream: stream.write(data)
    os.chmod(temporary,0o600);os.replace(temporary,OUTPUT)
def fail(label):
    if label not in state['failures'] and len(state['failures'])<16: state['failures'].append(label)
    flush()
def lock():
    return gdb.parse_and_eval("'ce_network_lockstep.cpp'::L")
def is_running():
    value=lock();kind=value['stage'].type.strip_typedefs()
    wanted=next(f.enumval for f in kind.fields() if f.name.split('::')[-1]=='RUNNING')
    return int(value['stage'])==wanted and bool(value['ran'])
def ticket():
    value=lock()
    if bool(value['host'])!=(ROLE=='host'): raise RuntimeError('role')
    pad=gdb.parse_and_eval("'dinput.cpp'::gFakePads[0]")
    # The first RUNNING update can precede the next SDL poll which arms the
    # exercise. Waiting here does not count as a sample or as input acceptance.
    if not bool(pad['game_started']) or bool(pad['game_stopped']): return None
    # The read uses the existing d3d9hooks counter, never an inferior function.
    current=int(gdb.parse_and_eval("'d3d9hooks.cpp'::gFrames"))
    relative=(current-int(pad['game_present']))&0xffffffff
    if relative>10000: raise RuntimeError('input clock')
    return int(value['tick']),relative
def vector(value):
    result=[float(value[n]) for n in ('X','Y','Z')]
    if not all(math.isfinite(x) and abs(x)<=1048576 for x in result): raise RuntimeError('vector')
    return result
def players():
    game=gdb.parse_and_eval('GAME');result={};addresses={}
    if int(game['mPlayers'])!=2: raise RuntimeError('player count')
    typ=gdb.lookup_type('CBattleEngine').pointer();unit_type=gdb.lookup_type('CUnit').pointer()
    for i in range(2):
        pointer=game['mPlayer'][i]
        if not int(pointer): raise RuntimeError('player missing')
        player=pointer.dereference()
        if int(player['mNumber'])!=i+1: raise RuntimeError('player identity')
        engine=player['mBattleEngine']['mToRead'].cast(typ)
        if not int(engine): raise RuntimeError('engine missing')
        native=engine.dereference()
        if int(native['mPlayer']['mToRead'])!=int(pointer): raise RuntimeError('engine ownership')
        result[str(i+1)]=vector(native['mPos']);addresses[int(engine.cast(unit_type))]=str(i+1)
    if len(addresses)!=2: raise RuntimeError('aliased engines')
    return result,addresses
def controls():
    value=lock();result={}
    names=dict(forward='BUTTON_MECH_FORWARD',backward='BUTTON_MECH_BACKWARD',
               charge='BUTTON_MECH_CHARGE_GUN_POD',fire='BUTTON_MECH_FIRE_GUN_POD')
    wanted={BUTTONS[symbol]:name for name,symbol in names.items()}
    for player in range(2):
        sample=value['current'][player];count=int(sample['count'])
        if not 0<=count<=32: raise RuntimeError('input count')
        row={name:0.0 for name in names}
        for i in range(count):
            action=sample['actions'][i];name=wanted.get(int(action['button']))
            if name is not None:
                number=abs(float(action['value']))    # the analogue buttons report -1 for forward: only the amount is recorded
                if not math.isfinite(number) or not 0<=number<=1: raise RuntimeError('input value')
                row[name]=max(row[name],number)
        result[str(player+1)]=row
    return result
class FinishShot(gdb.FinishBreakpoint):
    def __init__(self,frame,weapon):
        super().__init__(frame,internal=True);self.weapon=weapon
    def _pop(self):
        if not shot_stack or shot_stack[-1][0]!=self.weapon: fail('native_shot_end_observation_failed:RuntimeError:shot scope')
        else: shot_stack.pop()
    def stop(self):
        self._pop();return False
    def out_of_scope(self):
        self._pop()
class FinishRound(gdb.FinishBreakpoint):
    def __init__(self,frame,player,tick,present,round_ptr):
        super().__init__(frame,internal=True);self.player=player;self.tick=tick;self.present=present;self.round_ptr=round_ptr
    def stop(self):
        try:
            if not is_running(): return False
            if len(state['shots'])>=MAX_SHOTS: raise RuntimeError('shot bound')
            native=self.round_ptr.dereference()
            key=(self.player,self.tick);ordinal=shot_ordinals.get(key,0)+1;shot_ordinals[key]=ordinal
            state['shots'].append(dict(player=self.player,tick=self.tick,ordinal=ordinal,present=self.present,
                kind=int(native['mThingType'])&0xffffffff,position=vector(native['mPos']),velocity=vector(native['mVelocity'])))
            flush()
        except Exception as e:
            fail('native_round_observation_failed:'+type(e).__name__+(':'+str(e)[:40] if type(e) is RuntimeError else ''))
        return False
class Observe(gdb.Breakpoint):
    def __init__(self,name,kind):
        super().__init__(name,internal=True);self.kind=kind
    def stop(self):
        global last_tick
        try:
            if not is_running(): return False
            current=ticket()
            if current is None: return False
            tick,present=current
            if not 0<=tick<=MAX_TICK: return False
            frame=gdb.newest_frame()
            if self.kind=='tick':
                if tick==last_tick or tick==0: return False
                applied=controls()
                if tick%2 and not any(row['fire'] for row in applied.values()): return False
                if len(state['rows'])>=MAX_ROWS: raise RuntimeError('sample bound')
                poses,_=players();last_tick=tick
                state['rows'].append(dict(tick=tick,present=present,players=poses,controls=applied))
                if len(state['rows'])%10==0: flush()
            elif self.kind=='shot_begin':
                _,owners=players();weapon=frame.read_var('this')
                if len(shot_stack)>=8: raise RuntimeError('shot nesting')
                shot_stack.append((int(weapon),owners.get(int(weapon.dereference()['mAttachedTo']))))
                FinishShot(frame,int(weapon))
            elif self.kind=='round' and shot_stack and shot_stack[-1][1]:
                FinishRound(frame,shot_stack[-1][1],tick,present,frame.read_var('this'))
        except Exception as e:    # the label names the failure class and, for the observer's own refusals, its fixed words
            fail('native_'+self.kind+'_observation_failed:'+type(e).__name__+(':'+str(e)[:40] if type(e) is RuntimeError else ''))
        return False
def exited(event):
    state['exited']=True;state['exit_code']=getattr(event,'exit_code',None)
    if shot_stack: fail('unfinished_native_shot_scope')
    flush()
gdb.events.exited.connect(exited)
try:
    for name,kind in (('CE_LockstepAfterUpdate','tick'),('CWeapon::SpawnRound','shot_begin'),
                      ('CRound::Init(CInitThing*)','round')):
        Observe(name,kind)
    gdb.execute('starti')
    state['inferior_pid']=gdb.selected_inferior().pid
    if state['inferior_pid']<=0: raise RuntimeError('owned inferior absent')
    state['attached']=True;flush()
    gdb.execute('continue')
except BaseException:
    fail('observer_interrupted_or_failed')
finally:
    if gdb.selected_inferior().pid:
        try:gdb.execute('detach')
        except BaseException:fail('observer_detach_failed')
    flush()
'''


BUTTON_HEADER = Path(__file__).resolve().parents[2] / 'src' / 'Controller.h'
BUTTON_SYMBOLS = ('BUTTON_MECH_FORWARD', 'BUTTON_MECH_BACKWARD', 'BUTTON_MECH_CHARGE_GUN_POD', 'BUTTON_MECH_FIRE_GUN_POD')


def button_values(header=BUTTON_HEADER):
    """The numbers of the four mech buttons, read from the game's own header.

    They are preprocessor constants, so the game's debug information does not name them and the debugger cannot look
    them up (the first real run of the observer failed on exactly that). A name that is missing, defined twice or not
    a plain small integer, or two names with one number, refuses the observer.
    """
    text = Path(header).read_text(errors='replace')
    values = {}
    for name in BUTTON_SYMBOLS:
        found = re.findall(r'^[ \t]*#[ \t]*define[ \t]+' + name + r'[ \t]+(\d{1,4})\b', text, re.M)
        if len(found) != 1 or text.count(name) != 1 or int(found[0]) > 255:
            raise ValueError('button constant refused: ' + name)
        values[name] = int(found[0])
    if len(set(values.values())) != len(values):
        raise ValueError('button constants alias')
    return values


def observer_source(role, output):
    if role not in ('host', 'guest') or not isinstance(output, str) or not output.startswith('/') or '\n' in output:
        raise ValueError('observer role/path refused')
    preface = '\n'.join(('ROLE=' + repr(role), 'OUTPUT=' + repr(output), f'MAX_TICK={MAX_TICK}',
                         f'MAX_ROWS={MAX_ROWS}', f'MAX_SHOTS={MAX_SHOTS}', 'BUTTONS=' + repr(button_values())))
    return 'python\n' + preface + GDB_BODY + '\nend\n'


def process_identity(pid, parent):
    """Only a same-user child of the named owned parent is eligible."""
    root = Path('/proc') / str(pid)
    fields = (root / 'stat').read_text().rsplit(')', 1)[1].split()
    uids = next(line for line in (root / 'status').read_text().splitlines() if line.startswith('Uid:')).split()[1:]
    if len(uids) != 4 or any(int(v) != os.getuid() for v in uids) or int(fields[1]) != parent:
        raise RuntimeError('observer owned-child identity refused')
    return dict(pid=pid, start=fields[19], executable=os.readlink(root / 'exe'), command=(root / 'cmdline').read_bytes())


class ObservedGame:
    """Launch one game through its owned debugger, with bounded private output.

    The debugger is the inferior's parent. No sibling attach, host ptrace change
    or inferior function call is needed. The supplied reviewed child owner also
    owns final collection of the recorded inferior and debugger.
    """
    def __init__(self, launch_command, env, role, folder, gdb_binary, child_factory, deadline, game_commands):
        self.debugger = None; self.identity = None; self.pid = None; self.pidfd = None
        self.allowed_commands = [tuple(launch_command), *map(tuple, game_commands)]
        self.forced = False; self.failures = []; self.folder = Path(folder)
        self.folder.mkdir(mode=0o700, parents=False, exist_ok=False)
        self.output = self.folder / 'native.private.json'
        self.script = self.folder / 'observer.private.gdb'
        self.script.write_text(observer_source(role, str(self.output)))
        self.script.chmod(0o600)
        self.command = [str(gdb_binary), '--batch', '--nx',
                        '-ex', 'set auto-load off', '-ex', 'set debuginfod enabled off',
                        '-ex', 'set pagination off', '-ex', 'set confirm off',
                        '-ex', 'set startup-with-shell off', '-ex', 'set breakpoint pending on',
                        '-ex', 'set follow-exec-mode same',
                        '-ex', 'set print thread-events off', '-ex', 'set detach-on-fork on',
                        '-ex', 'handle SIGPIPE nostop noprint pass',
                        '-ex', 'handle SIGINT nostop noprint pass',
                        '-x', str(self.script), '--args', *launch_command]
        try:
            self.debugger = child_factory(self.command, self.folder / 'debugger', env, deadline)
            ready_by = min(deadline, time.monotonic() + 10)
            while True:
                record = self.read()
                if record and record.get('failures'): raise RuntimeError('observer startup refused')
                if record and record.get('attached') is True and type(record.get('inferior_pid')) is int and record['inferior_pid'] > 0:
                    self.pid = record['inferior_pid']; break
                if self.debugger.child.poll() is not None or time.monotonic() >= ready_by:
                    raise RuntimeError('observer startup incomplete')
                time.sleep(.01)
            self.debugger.pin_inferior(self.pid, launch_command)
            self.pidfd = self.debugger.inferior[1]
            self.identity = process_identity(self.pid, self.debugger.child.pid)
            if self.identity['command'] != self._command_bytes(launch_command):
                raise RuntimeError('observer launch command changed')
        except BaseException as error:
            cleanup = self.close(deadline)
            (self.folder / 'startup-failure.private.json').write_text(json.dumps(
                dict(exception_class=type(error).__name__, cleanup=cleanup), sort_keys=True) + '\n')
            raise

    @staticmethod
    def _command_bytes(command):
        return b'\0'.join(os.fsencode(v) for v in command) + b'\0'

    def current(self):
        value = process_identity(self.pid, self.debugger.child.pid)
        if self.identity is not None and value['start'] != self.identity['start']:
            raise RuntimeError('observed process identity changed')
        return value

    def is_command(self, command):
        if self.poll() is not None: return False
        current = self.current()
        if current['command'] != self._command_bytes(command): return False
        a = Path(current['executable']).stat(); b = Path(command[0]).stat()
        if (a.st_dev, a.st_ino) != (b.st_dev, b.st_ino): return False
        self._refresh_cleanup(current)
        return True

    def _refresh_cleanup(self, current):
        if current['command'] not in [self._command_bytes(command) for command in self.allowed_commands]:
            raise RuntimeError('observed command outside reviewed lifecycle')
        # Exec changes argv but preserves this recorded process and PID handle.
        # The existing child owner's final cleanup must check the current stage.
        self.debugger.inferior = (self.pid, self.pidfd,
            (current['start'].encode(), current['command'], self.debugger.child.pid))

    def poll(self):
        record = self.read()
        if record and record.get('exited') is True:
            if type(record.get('exit_code')) is not int: raise RuntimeError('native exit status absent')
            return record['exit_code']
        if self.debugger and self.debugger.child.poll() is not None:
            raise RuntimeError('debugger exited before native completion')
        return None

    def wait(self, timeout):
        end = time.monotonic() + max(0, timeout)
        while self.poll() is None:
            if time.monotonic() >= end: raise subprocess.TimeoutExpired('owned observed game', timeout)
            time.sleep(.01)
        return self.poll()

    def text(self):
        return self.debugger.text()

    def read(self):
        if self.debugger:
            for path in self.debugger.directory.glob('*.private.log'):
                if path.stat().st_size > 16 * 1024 * 1024:
                    raise RuntimeError('observer debugger output bound')
        if not self.output.exists():
            return None
        if self.output.is_symlink() or self.output.stat().st_size > 512 * 1024:
            raise RuntimeError('observer record bound')
        return json.loads(self.output.read_bytes())

    def close(self, deadline):
        cleanup = dict(collected=True, forced=False, exit_code=None, failures=[])
        if self.debugger is not None:
            try:
                record = self.read()
                if self.identity is not None and not (record and record.get('exited') is True) and Path('/proc', str(self.pid)).exists():
                    self._refresh_cleanup(self.current())
                if self.debugger.child.poll() is None:
                    try:
                        self.debugger.child.wait(timeout=max(0, min(1, deadline - time.monotonic())))
                    except subprocess.TimeoutExpired:
                        self.forced = True
                        identity = self.debugger.current()
                        if identity != self.debugger.identity:
                            raise RuntimeError('observer identity changed')
                        if identity[1] != b'\0'.join(os.fsencode(v) for v in self.command) + b'\0':
                            raise RuntimeError('observer command changed')
                        signal.pidfd_send_signal(self.debugger.pidfd, signal.SIGINT)
                        self.debugger.child.wait(timeout=max(0, min(2, deadline - time.monotonic())))
            except BaseException:
                self.failures.append('observer_detach_or_wait_failed')
            finally:
                cleanup = self.debugger.close(deadline=deadline)
        return dict(collected=self.debugger is not None and cleanup['collected'], exit_code=cleanup['exit_code'],
                    forced=self.forced or cleanup['forced'], failures=list(self.failures) + cleanup['failures'], record=str(self.output),
                    script_sha256=hashlib.sha256(self.script.read_bytes()).hexdigest())
