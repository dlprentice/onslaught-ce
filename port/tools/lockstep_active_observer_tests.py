#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Authored positive/negative controls for semantic active-pilot evidence."""
import ast
import copy
import math
from pathlib import Path
import signal
import subprocess
import tempfile
import time
import unittest
from unittest import mock

import lockstep_active_observer as observer
from lockstep_active_observer import analyze_pair, exercise, observer_source, GDB_BODY


def authored_pair():
    spec = exercise(); rows = []; shots = []
    for p in range(0, 900, 6):
        players = {}; controls = {}
        for number, role in (('1', 'host'), ('2', 'guest')):
            s = spec['roles'][role]
            travelled = max(0, min(p - s['move_on'], 120)) / 10
            players[number] = [travelled if role == 'host' else -travelled, int(number) * 10., 0.]
            controls[number] = dict(forward=0., backward=0., charge=0., fire=0.)
            if s['move_on'] <= p < s['move_off']:
                controls[number]['forward' if role == 'host' else 'backward'] = 1.
            if s['fire_on'] <= p < s['fire_off']:
                controls[number]['charge'] = 1.
            if p == s['fire_off']:
                controls[number]['fire'] = 1.
        rows.append(dict(tick=p // 3 + 2, present=p, players=players, controls=controls))
    for player, role in (('1', 'host'), ('2', 'guest')):
        p = spec['roles'][role]['fire_off']
        shots.append(dict(player=player, tick=p // 3 + 2, ordinal=1, present=p, kind=7,
                          position=[float(player), 2., 3.], velocity=[10., 0., 0.]))
    return {role: dict(schema=1, role=role, failures=[], attached=True, exited=True, exit_code=0,
                       rows=copy.deepcopy(rows), shots=copy.deepcopy(shots)) for role in ('host', 'guest')}


class EvidenceTests(unittest.TestCase):
    def test_authored_positive(self):
        result = analyze_pair(authored_pair())
        self.assertTrue(result['passed'], result)
        self.assertGreaterEqual(result['shared_native_ticks'], 20)

    def refuse(self, mutate, witness):
        pair = authored_pair(); mutate(pair)
        result = analyze_pair(pair)
        self.assertFalse(result['passed'])
        self.assertTrue(any(witness in s for s in result['failures']), result)

    def test_no_movement(self):
        def change(p):
            for role in p:
                for row in p[role]['rows']: row['players']['1'] = [0., 10., 0.]
        self.refuse(change, 'movement not above neutral drift')

    def test_motion_without_applied_input(self):
        def change(p):
            for role in p:
                for row in p[role]['rows']: row['controls']['1']['forward'] = 0.
        self.refuse(change, 'neutral/move/release interval absent')

    def test_shots_without_trigger(self):
        def change(p):
            for role in p:
                for row in p[role]['rows']:
                    row['controls']['1']['charge'] = row['controls']['1']['fire'] = 0.
        self.refuse(change, 'applied trigger charge/release input absent')

    def test_guest_pose_divergence(self):
        self.refuse(lambda p: p['guest']['rows'][50]['players']['2'].__setitem__(1, 30.), 'native player poses disagree')

    def test_guest_input_divergence(self):
        self.refuse(lambda p: p['guest']['rows'][50]['controls']['2'].__setitem__('forward', .25), 'applied player inputs disagree')

    def test_initialized_shot_semantics_diverge(self):
        self.refuse(lambda p: p['guest']['shots'][0]['velocity'].__setitem__(0, 11.), 'native initialized shot semantics disagree')

    def test_missing_remote_shooter(self):
        self.refuse(lambda p: p['guest']['shots'].pop(), 'bounded samples absent')

    def test_wrong_shot_tick(self):
        self.refuse(lambda p: p['guest']['shots'][0].__setitem__('tick', 42), 'native shot identities disagree')

    def test_no_post_shot_ticks(self):
        def change(p):
            for role in p:
                for shot in p[role]['shots']: shot['tick'] = p[role]['rows'][-1]['tick']
        self.refuse(change, 'no tick progression after shot')

    def test_other_peers_present_clock_is_not_compared(self):
        # A remote shot has its own peer-local Present, while native tick identity
        # and projectile semantics must still agree. Only owned input is timed.
        pair = authored_pair()
        pair['host']['shots'][1]['present'] = 999
        pair['guest']['shots'][0]['present'] = 888
        self.assertTrue(analyze_pair(pair)['passed'])

    def test_own_shot_must_match_own_input_interval(self):
        self.refuse(lambda p: p['host']['shots'][0].__setitem__('present', 999), 'own shot outside')

    def test_partial_observer_is_not_pass(self):
        self.refuse(lambda p: p['host'].__setitem__('exited', False), 'incomplete observer')

    def test_boolean_exit_is_not_an_exit_code(self):
        self.refuse(lambda p: p['host'].__setitem__('exit_code', False), 'incomplete observer')

    def test_reversed_local_clock_is_rejected(self):
        self.refuse(lambda p: p['host']['rows'][40].__setitem__('present', 0), 'invalid native pose')

    def test_nonfinite_native_pose(self):
        self.refuse(lambda p: p['host']['rows'][40]['players']['1'].__setitem__(0, math.nan), 'invalid native pose')

    def test_repeated_ticks_do_not_inflate_evidence(self):
        self.refuse(lambda p: p['host']['rows'][40].__setitem__('tick', p['host']['rows'][39]['tick']), 'invalid native pose')

    def test_late_attach_is_not_accepted(self):
        def change(p):
            for role in p: p[role]['rows'] = [r for r in p[role]['rows'] if r['present'] >= 60]
        self.refuse(change, 'observer began after neutral admission')

    def test_independent_roles_and_no_held_controls_at_end(self):
        spec = exercise()
        self.assertNotEqual(spec['roles']['host']['script'], spec['roles']['guest']['script'])
        for row in spec['roles'].values():
            self.assertIn(f"{row['move_off']}:h0:0", row['script'])
            self.assertTrue(row['script'].endswith(f"{row['fire_off']}:b0:u"))

    def test_observer_has_no_inferior_calls_or_memory_writes(self):
        tree = ast.parse(GDB_BODY)
        commands = []
        for node in ast.walk(tree):
            if isinstance(node, ast.Call) and isinstance(node.func, ast.Attribute):
                self.assertNotIn(node.func.attr, ('write_memory', 'call'))
                if isinstance(node.func.value, ast.Name) and node.func.value.id == 'gdb' and node.func.attr == 'execute':
                    self.assertIsInstance(node.args[0], ast.Constant)
                    commands.append(node.args[0].value)
        self.assertEqual(commands, ['starti', 'continue', 'detach'])
        source = observer_source('guest', '/private/observations.json')
        ast.parse(source.removeprefix('python\n').removesuffix('\nend\n'))
        with self.assertRaises(ValueError): observer_source('other', '/private/out')
        with self.assertRaises(ValueError): observer_source('host', 'relative')

    def test_first_running_update_may_precede_pad_poll(self):
        tree = ast.parse(GDB_BODY)
        function = next(n for n in tree.body if isinstance(n, ast.FunctionDef) and n.name == 'ticket')
        pad = dict(game_started=False, game_stopped=False, game_present=0)
        debugger = mock.Mock()
        debugger.parse_and_eval.side_effect = lambda name: pad if 'gFakePads' in name else 12
        scope = dict(gdb=debugger, ROLE='host', lock=lambda:dict(host=True, tick=1))
        exec(compile(ast.Module(body=[function], type_ignores=[]), '<authored-ticket-control>', 'exec'), scope)
        self.assertIsNone(scope['ticket']())
        pad.update(game_started=True, game_present=10)
        self.assertEqual(scope['ticket'](), (1, 2))
        pad['game_stopped'] = True
        self.assertIsNone(scope['ticket']())


class LifecycleTests(unittest.TestCase):
    """Fault injection stays in Python mocks: no debugger, process or signal."""
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.child = mock.Mock()
        self.child.directory = self.root / 'debugger'; self.child.directory.mkdir()
        self.child.child.pid = 456
        self.child.child.poll.return_value = 0; self.child.child.wait.return_value = 0
        self.child.close.return_value = dict(collected=True, forced=False, exit_code=0, failures=[])
        self.child.inferior = (123, 77, (b'7', b'/authored/game\0', 456))
        self.factory = mock.Mock(return_value=self.child)
        self.target = dict(pid=123, start='7', executable='/authored/game', command=b'/authored/game\0')
        self.record = dict(attached=True, inferior_pid=123, failures=[], exited=False)
        self.identity = mock.patch.object(observer, 'process_identity', return_value=self.target); self.identity.start()
        self.read = mock.patch.object(observer.ObservedGame, 'read', side_effect=lambda:self.record); self.read.start()
        self.signal = mock.patch.object(observer.signal, 'pidfd_send_signal'); self.send_signal = self.signal.start()

    def tearDown(self):
        self.signal.stop(); self.read.stop(); self.identity.stop(); self.temp.cleanup()

    def make(self):
        return observer.ObservedGame(['/authored/game'], dict(PATH='/usr/bin:/bin'), 'host', self.root / 'observer',
                                     '/authored/gdb', self.factory, time.monotonic()+10, [('/authored/game','next')])

    def pin_debugger(self, owner):
        command = b'\0'.join(x.encode() for x in owner.command) + b'\0'
        self.child.identity = (b'9', command, 789); self.child.current.return_value = self.child.identity
        self.child.pidfd = 88

    def test_natural_exit_collects_without_signaling(self):
        owner = self.make(); self.record.update(exited=True,exit_code=0)
        result = owner.close(time.monotonic()+5)
        self.assertTrue(result['collected']); self.assertFalse(result['forced'])
        self.assertFalse(result['failures']); self.send_signal.assert_not_called(); self.child.close.assert_called_once()
        self.child.pin_inferior.assert_called_once_with(123, ['/authored/game'])
        self.assertIn('handle SIGINT nostop noprint pass', owner.command)
        self.assertNotIn('attach 123', owner.command)

    def test_interrupt_is_not_accepted_as_natural(self):
        owner = self.make(); self.pin_debugger(owner); self.child.child.poll.return_value = None
        self.child.child.wait.side_effect = [subprocess.TimeoutExpired('authored',1), 0]
        result = owner.close(time.monotonic()+5)
        self.assertTrue(result['collected']); self.assertTrue(result['forced'])
        self.send_signal.assert_called_once_with(88, signal.SIGINT); self.child.close.assert_called_once()

    def test_identity_error_still_collects_recorded_child(self):
        owner = self.make(); self.pin_debugger(owner); self.child.child.poll.return_value = None
        self.child.child.wait.side_effect = subprocess.TimeoutExpired('authored',1)
        self.child.current.side_effect = OSError('authored identity error')
        result = owner.close(time.monotonic()+5)
        self.assertIn('observer_detach_or_wait_failed', result['failures'])
        self.send_signal.assert_not_called(); self.child.close.assert_called_once()

    def test_failed_interrupt_still_collects_recorded_child(self):
        owner = self.make(); self.pin_debugger(owner); self.child.child.poll.return_value = None
        self.child.child.wait.side_effect = subprocess.TimeoutExpired('authored',1)
        self.send_signal.side_effect = OSError('authored signal error')
        result = owner.close(time.monotonic()+5)
        self.assertIn('observer_detach_or_wait_failed', result['failures']); self.child.close.assert_called_once()

    def test_live_child_cannot_be_reported_collected(self):
        owner = self.make()
        self.child.close.return_value = dict(collected=False, forced=True, exit_code=None, failures=['owned_cleanup_refused'])
        result = owner.close(time.monotonic()+5)
        self.assertFalse(result['collected']); self.assertTrue(result['forced'])
        self.assertIn('owned_cleanup_refused', result['failures'])

    def test_failed_inferior_pin_collects_debugger(self):
        self.child.pin_inferior.side_effect = OSError('authored pin failure')
        with self.assertRaisesRegex(OSError,'pin failure'):self.make()
        self.child.close.assert_called_once()

    def test_early_debugger_exit_cannot_fake_completion(self):
        self.record.clear()
        with self.assertRaisesRegex(RuntimeError,'startup incomplete'):self.make()
        self.child.close.assert_called_once()

    def test_exec_stage_cleanup_record_requires_allowlisted_command(self):
        owner = self.make(); current=dict(self.target,command=b'/authored/game\0next\0')
        owner._refresh_cleanup(current)
        self.assertEqual(self.child.inferior[2][1],current['command'])
        with self.assertRaisesRegex(RuntimeError,'outside reviewed lifecycle'):
            owner._refresh_cleanup(dict(current,command=b'/unrelated/program\0'))

    def test_output_is_private_and_bounded(self):
        owner = self.make(); self.read.stop()
        self.assertEqual(owner.script.stat().st_mode & 0o777,0o600); self.assertIsNone(owner.read())
        owner.output.write_text('{"schema":1}'); self.assertEqual(owner.read(),{'schema':1})
        owner.output.write_bytes(b' '*(512*1024+1))
        with self.assertRaisesRegex(RuntimeError,'record bound'):owner.read()


OWNER = {'1': 'host', '2': 'guest'}


def closed_tick(pair, player):
    """The last tick the player's own peer reached inside that player's firing window, read from that peer's record."""
    end = exercise()['roles'][OWNER[player]]['neutral_after']
    return max(r['tick'] for r in pair[OWNER[player]]['rows'] if r['present'] <= end)


def hold(pair, player, name, roles=('host', 'guest'), value=1., last_only=False):
    """Keep one applied control non-neutral in the named peers' records once its player's firing window is over."""
    after = closed_tick(pair, player)
    for role in roles:
        rows = [r for r in pair[role]['rows'] if r['tick'] > after]
        for row in (rows[-1:] if last_only else rows):
            row['controls'][player][name] = value


def trim(pair, last_present):
    for item in pair.values():
        item['rows'] = [r for r in item['rows'] if r['present'] <= last_present]
    return pair


def skewed_pair(ppt):
    """Two peers on one 20 Hz tick axis whose games draw at different rates: tick t is Present t * ppt[role] there.

    Every player's controls and pose follow the input clock of the peer that owns it (its script is in that peer's
    Presents), so both records agree tick by tick while one tick is a different local Present in each of them."""
    spec = exercise(); rows = {'host': [], 'guest': []}; shots = []
    for tick in range(2, 470, 2):
        players = {}; controls = {}
        for number, owner in OWNER.items():
            s = spec['roles'][owner]; p = tick * ppt[owner]
            travelled = max(0, min(p - s['move_on'], 120)) / 10
            players[number] = [travelled if number == '1' else -travelled, int(number) * 10., 0.]
            controls[number] = dict(forward=0., backward=0., charge=0., fire=0.)
            if s['move_on'] <= p < s['move_off']:
                controls[number]['forward' if number == '1' else 'backward'] = 1.
            if s['fire_on'] <= p < s['fire_off']:
                controls[number]['charge'] = 1.
            if s['fire_off'] <= p < s['fire_off'] + 2 * ppt[owner]:    # the one sampled row at the release
                controls[number]['fire'] = 1.
                shots.append(dict(player=number, tick=tick, ordinal=1, present=p, kind=7,
                                  position=[float(number), 2., 3.], velocity=[10., 0., 0.]))
        for role in rows:
            rows[role].append(dict(tick=tick, present=tick * ppt[role],
                                   players=copy.deepcopy(players), controls=copy.deepcopy(controls)))
    return {role: dict(schema=1, role=role, failures=[], attached=True, exited=True, exit_code=0,
                       rows=rows[role], shots=copy.deepcopy(shots)) for role in rows}


class FinalReleaseTests(unittest.TestCase):
    """After each player's last firing window the records themselves must show every control released.

    The input script's own release (test_independent_roles_and_no_held_controls_at_end) is intent. These controls
    change only what the records show after the window and leave the two-peer comparisons satisfied wherever they can.
    """
    def failures_of(self, pair):
        result = analyze_pair(pair)
        self.assertFalse(result['passed'], result)
        return result['failures']

    @staticmethod
    def witness(role, player, name):
        return f'{role}: player {player}: {name} still applied after its firing window'

    def test_released_pair_shows_its_own_neutral_tail(self):
        pair = authored_pair(); result = analyze_pair(pair)
        self.assertTrue(result['passed'], result)
        for role, metrics in result['roles'].items():
            for player in ('1', '2'):
                tail = [r for r in pair[role]['rows'] if r['tick'] > closed_tick(pair, player)]
                self.assertEqual(metrics['final_neutral_samples'][player], len(tail))
                self.assertGreaterEqual(len(tail), observer.FINAL_SAMPLES)
                self.assertFalse(any(v for r in tail for v in r['controls'][player].values()))

    def test_review_reproducer_held_trigger_in_both_records_is_rejected(self):
        # Finding R1 of the review of 5bd0c870: both peers' records keep the charge control applied after the last
        # firing window. The two records agree with each other, so only the release check can reject them.
        pair = authored_pair(); self.assertTrue(analyze_pair(pair)['passed'])
        for player in ('1', '2'): hold(pair, player, 'charge')
        self.assertEqual(sorted(self.failures_of(pair)),
                         sorted(self.witness(role, player, 'charge') for role in ('host', 'guest') for player in ('1', '2')))

    def test_only_the_release_check_rejects_the_held_pair(self):
        # Negative control of the control: without the check the held pair passes, as it did at 5bd0c870.
        pair = authored_pair()
        for player in ('1', '2'): hold(pair, player, 'charge')
        with mock.patch.object(observer, 'final_release', return_value=([], {role: {} for role in pair})):
            self.assertTrue(analyze_pair(pair)['passed'])
        self.assertFalse(analyze_pair(pair)['passed'])

    def test_every_control_of_every_player_must_be_neutral_after_its_window(self):
        for player in ('1', '2'):
            for name in observer.CONTROL_NAMES:
                for last_only in (False, True):
                    with self.subTest(player=player, control=name, last_only=last_only):
                        pair = authored_pair(); self.assertTrue(analyze_pair(pair)['passed'])
                        hold(pair, player, name, last_only=last_only)
                        self.assertEqual(sorted(self.failures_of(pair)),
                                         sorted(self.witness(role, player, name) for role in ('host', 'guest')))

    def test_any_nonzero_value_is_not_neutral(self):
        for value in (1e-6, .25, 1.):
            with self.subTest(value=value):
                pair = authored_pair(); hold(pair, '1', 'forward', value=value, last_only=True)
                self.assertEqual(sorted(self.failures_of(pair)),
                                 sorted(self.witness(role, '1', 'forward') for role in ('host', 'guest')))

    def test_one_peer_released_and_the_other_held_is_rejected(self):
        for holder, other in (('host', 'guest'), ('guest', 'host')):
            with self.subTest(holder=holder):
                pair = authored_pair(); self.assertTrue(analyze_pair(pair)['passed'])
                for player in ('1', '2'): hold(pair, player, 'charge', roles=(holder,))
                failures = self.failures_of(pair)
                for player in ('1', '2'): self.assertIn(self.witness(holder, player, 'charge'), failures)
                self.assertFalse([f for f in failures if f.startswith(other + ':')])
                self.assertIn('applied player inputs disagree at a shared tick', failures)

    def test_hold_on_ticks_only_one_peer_observed_is_rejected_by_the_release_check_alone(self):
        # The host outlives the guest. Its extra rows share no tick with the guest's record, so no two-peer
        # comparison can see them.
        pair = authored_pair(); self.assertTrue(analyze_pair(pair)['passed'])
        last = pair['host']['rows'][-1]
        for step in range(1, 12):
            row = copy.deepcopy(last); row['tick'] += 2 * step; row['present'] += 6 * step
            row['controls']['1']['charge'] = 1.
            pair['host']['rows'].append(row)
        self.assertEqual(self.failures_of(pair), [self.witness('host', '1', 'charge')])

    def test_the_window_ends_where_the_fire_window_ends(self):
        spec = exercise(); self.assertEqual(spec['final_samples'], observer.FINAL_SAMPLES)
        for player, owner in OWNER.items():
            s = spec['roles'][owner]
            self.assertEqual(s['neutral_after'], s['fire_off'] + observer.FIRE_WINDOW)
            # the shot's fire on the window's last Present still belongs to the window; one row later it is neither
            for present, accepted in ((s['neutral_after'], True), (s['neutral_after'] + 6, False)):
                with self.subTest(player=player, fire_at=present):
                    pair = authored_pair()
                    for item in pair.values():
                        for row in item['rows']: row['controls'][player]['fire'] = 1. if row['present'] == present else 0.
                    self.assertEqual(analyze_pair(pair)['passed'], accepted)
            # and the own shot's Present may be the window's last, not one past it
            for present, accepted in ((s['neutral_after'], True), (s['neutral_after'] + 1, False)):
                with self.subTest(player=player, shot_at=present):
                    pair = authored_pair()
                    for item in pair.values():
                        next(x for x in item['shots'] if x['player'] == player)['present'] = present
                    self.assertEqual(analyze_pair(pair)['passed'], accepted)
            # a hold on the window's last Present is inside it; on the next row it is a held control
            for present, accepted in ((s['neutral_after'], True), (s['neutral_after'] + 6, False)):
                with self.subTest(player=player, charge_at=present):
                    pair = authored_pair()
                    for item in pair.values():
                        next(r for r in item['rows'] if r['present'] == present)['controls'][player]['charge'] = 1.
                    self.assertEqual(analyze_pair(pair)['passed'], accepted)

    def test_a_release_the_records_never_show_is_not_accepted(self):
        later = exercise()['roles']['guest']['neutral_after']
        self.assertTrue(analyze_pair(trim(authored_pair(), later + 6 * observer.FINAL_SAMPLES))['passed'])
        short = self.failures_of(trim(authored_pair(), later + 6 * (observer.FINAL_SAMPLES - 1)))
        self.assertEqual(short, [f'{role}: player 2: no observed neutral interval after its firing window' for role in ('host', 'guest')])
        # records that stop inside the firing windows show no release for either player
        failures = self.failures_of(trim(authored_pair(), exercise()['roles']['host']['neutral_after']))
        for role in ('host', 'guest'):
            for player in ('1', '2'):
                self.assertIn(f'{role}: player {player}: no observed neutral interval after its firing window', failures)

    def test_peers_drawing_at_different_rates_are_judged_on_the_shared_tick_axis(self):
        # One tick is Present 3t on a peer drawing at 3 per tick and 2t on one drawing at 2 per tick, so a player's
        # firing appears at different local Presents in the two records. A rule that judged each record by its own
        # Present clock would reject these healthy pairs; `by_local_clock` shows that such a record exists.
        spec = exercise()
        for ppt in (dict(host=3, guest=2), dict(host=2, guest=3)):
            with self.subTest(ppt=ppt):
                pair = skewed_pair(ppt); result = analyze_pair(pair)
                self.assertTrue(result['passed'], result)
                by_local_clock = [(role, number) for role, item in pair.items() for number, owner in OWNER.items()
                                  if any(any(r['controls'][number].values()) for r in item['rows']
                                         if r['present'] > spec['roles'][owner]['neutral_after'])]
                self.assertTrue(by_local_clock)
                for player in ('1', '2'):
                    held = skewed_pair(ppt); hold(held, player, 'charge')
                    self.assertEqual(sorted(self.failures_of(held)),
                                     sorted(self.witness(role, player, 'charge') for role in ('host', 'guest')))

class ButtonConstantTests(unittest.TestCase):
    """The mech buttons are preprocessor constants: the debugger cannot look them up, so they come from the header.

    The first run of the observer on the real game recorded no sample at all because the debugger was asked for a
    name its debug information does not hold (No symbol ... in current context). The authored controls had never
    run the applied-controls reader, so none of them could show it.
    """
    HEADER = ''.join('#define %s %d\n' % (name, 40 + i) for i, name in enumerate(observer.BUTTON_SYMBOLS))

    @staticmethod
    def controls_function(body=GDB_BODY, buttons=None, lock=None):
        tree = ast.parse(body)
        function = next(n for n in tree.body if isinstance(n, ast.FunctionDef) and n.name == 'controls')
        debugger = mock.Mock()
        def evaluate(name):
            raise RuntimeError('No symbol "%s" in current context.' % name)    # what the real debugger answers
        debugger.parse_and_eval.side_effect = evaluate
        scope = dict(gdb=debugger, math=math, BUTTONS=buttons or observer.button_values(), lock=lock)
        exec(compile(ast.Module(body=[function], type_ignores=[]), '<authored-controls-control>', 'exec'), scope)
        return scope['controls']

    @staticmethod
    def sample(*actions):
        return dict(count=len(actions), actions=[dict(button=b, value=v) for b, v in actions])

    def header(self, text):
        folder = tempfile.mkdtemp(prefix='button-header-')
        path = Path(folder) / 'Controller.h'; path.write_text(text)
        return path

    def test_the_real_header_gives_four_distinct_numbers(self):
        values = observer.button_values()
        self.assertEqual(sorted(values), sorted(observer.BUTTON_SYMBOLS))
        self.assertEqual(len(set(values.values())), 4)

    def test_applied_controls_are_read_by_number_with_a_debugger_that_knows_no_constants(self):
        values = observer.button_values()
        lock = lambda: dict(current=[
            self.sample((values['BUTTON_MECH_FORWARD'], 0.5), (values['BUTTON_MECH_FIRE_GUN_POD'], 1.), (200, 1.)),
            self.sample((values['BUTTON_MECH_BACKWARD'], 1.), (values['BUTTON_MECH_CHARGE_GUN_POD'], .25))])
        got = self.controls_function(lock=lock)()
        self.assertEqual(got['1'], dict(forward=.5, backward=0., charge=0., fire=1.))
        self.assertEqual(got['2'], dict(forward=0., backward=1., charge=.25, fire=0.))

    def test_an_analogue_forward_reads_as_its_amount(self):
        # The real game reports forward as -1 (it is the negative direction of an axis): recorded as 1.
        values = observer.button_values()
        lock = lambda: dict(current=[self.sample((values['BUTTON_MECH_FORWARD'], -1.)),
                                     self.sample((values['BUTTON_MECH_BACKWARD'], 1.))])
        got = self.controls_function(lock=lock)()
        self.assertEqual((got['1']['forward'], got['2']['backward']), (1., 1.))
        for bad in (-1.5, 1.5, float('nan'), float('inf')):
            with self.subTest(bad), self.assertRaises(RuntimeError):
                self.controls_function(lock=lambda: dict(current=[self.sample((values['BUTTON_MECH_FORWARD'], bad)),
                                                                 self.sample()]))()

    def test_a_failed_sample_names_its_class_and_never_a_debugger_message(self):
        tree = ast.parse(GDB_BODY)
        text = next(t for t in (ast.get_source_segment(GDB_BODY, n) for n in ast.walk(tree) if isinstance(n, ast.ExceptHandler))
                    if 'observation_failed' in t)
        self.assertIn('type(e).__name__', text)
        self.assertIn("type(e) is RuntimeError", text)

    def test_the_old_lookup_by_debugger_symbol_fails_with_the_same_debugger(self):
        # Negative control of the control: the body as it was at 47ab1b08 cannot read a single control.
        old = GDB_BODY.replace('BUTTONS[symbol]', 'int(gdb.parse_and_eval(symbol))')
        self.assertNotEqual(old, GDB_BODY)
        lock = lambda: dict(current=[self.sample(), self.sample()])
        with self.assertRaises(RuntimeError): self.controls_function(body=old, lock=lock)()
        self.assertEqual(self.controls_function(lock=lock)()['1']['fire'], 0.)

    def test_the_observer_text_carries_the_numbers(self):
        source = observer_source('host', '/private/observations.json')
        self.assertIn('BUTTONS=' + repr(observer.button_values()), source)

    def test_a_header_with_the_right_names_is_accepted(self):
        values = observer.button_values(self.header(self.HEADER))
        self.assertEqual(values, {name: 40 + i for i, name in enumerate(observer.BUTTON_SYMBOLS)})

    def test_a_missing_or_repeated_or_aliased_or_odd_constant_is_refused(self):
        lines = self.HEADER.splitlines(True)
        for label, text in (
                ('missing', ''.join(lines[1:])),
                ('repeated', self.HEADER + lines[0]),
                ('aliased', self.HEADER.replace('41', '40')),
                ('not a number', self.HEADER.replace('40', 'FORWARD', 1)),
                ('too large', self.HEADER.replace('40', '4000', 1)),
                ('commented out', '// ' + lines[0] + ''.join(lines[1:])),
                ('mentioned elsewhere', self.HEADER + '// ' + observer.BUTTON_SYMBOLS[0] + '\n')):
            with self.subTest(label), self.assertRaises(ValueError):
                observer.button_values(self.header(text))


def add_row(item, tick, **fire):
    """A row at `tick` for one record: the previous row's poses and Present, so only the controls can differ."""
    previous = max((r for r in item['rows'] if r['tick'] < tick), key=lambda r: r['tick'])
    row = copy.deepcopy(previous); row['tick'] = tick
    for number, value in fire.items():
        row['controls'][number]['fire'] = value
    item['rows'].append(row); item['rows'].sort(key=lambda r: r['tick'])
    return row


class CrossPeerCoverageTests(unittest.TestCase):
    """Every tick either record holds is compared, and no shot exists without an applied fire pulse.

    The debugger samples every even tick and an odd tick only when a fire is applied, so a fire pulse one process applied
    on an odd tick and the other did not is a row only one record holds. Each case here is a refusal the review of
    4d30d28e showed the observer did not make; the first control of each family shows the same shape accepted when both
    peers agree."""
    def failures_of(self, pair):
        result = analyze_pair(pair)
        self.assertFalse(result['passed'], result)
        return result['failures']

    def test_a_pulse_both_peers_applied_on_an_odd_tick_is_accepted(self):
        pair = authored_pair()
        for item in pair.values(): add_row(item, 201, **{'1': 1.})
        self.assertTrue(analyze_pair(pair)['passed'], analyze_pair(pair))

    def test_a_fire_pulse_only_one_peer_applied_on_an_odd_tick_is_refused(self):
        for role in ('host', 'guest'):
            for number in ('1', '2'):
                with self.subTest(role=role, player=number):
                    pair = authored_pair(); add_row(pair[role], 201, **{number: 1.})
                    self.assertIn('native tick sets disagree up to the shorter record', self.failures_of(pair))

    def test_many_one_sided_pulses_are_refused(self):
        pair = authored_pair()
        for tick in range(101, 181, 2): add_row(pair['guest'], tick, **{'1': 1.})
        self.assertIn('native tick sets disagree up to the shorter record', self.failures_of(pair))

    def test_a_record_with_a_row_missing_inside_the_run_is_refused(self):
        for role in ('host', 'guest'):
            with self.subTest(role=role):
                pair = authored_pair(); del pair[role]['rows'][60]
                self.assertIn('native tick sets disagree up to the shorter record', self.failures_of(pair))

    def test_sparse_sampling_inside_the_firing_window_is_refused(self):
        pair = authored_pair(); spec = exercise()['roles']['host']
        pair['guest']['rows'] = [r for i, r in enumerate(pair['guest']['rows'])
                                 if not (spec['fire_on'] <= r['present'] <= spec['fire_off'] and i % 2)]
        self.assertIn('native tick sets disagree up to the shorter record', self.failures_of(pair))

    def test_the_longer_record_may_end_later_and_its_extra_rows_are_still_judged(self):
        pair = authored_pair(); last = pair['host']['rows'][-1]
        for step in range(1, 4):
            row = copy.deepcopy(last); row['tick'] += 2 * step; row['present'] += 6 * step
            pair['host']['rows'].append(row)
        self.assertTrue(analyze_pair(pair)['passed'], analyze_pair(pair))
        pair['host']['rows'][-1]['controls']['2']['fire'] = 1.
        self.assertEqual(self.failures_of(pair), ['host: player 2: fire still applied after its firing window'])

    def test_a_second_shot_in_both_records_without_a_second_pulse_is_refused(self):
        for number in ('1', '2'):
            with self.subTest(player=number):
                pair = authored_pair()
                for item in pair.values():
                    second = copy.deepcopy(next(s for s in item['shots'] if s['player'] == number)); second['ordinal'] = 2
                    item['shots'].append(second)
                failures = self.failures_of(pair)
                for role in ('host', 'guest'):
                    self.assertIn(f'{role}: player {number}: more native shots than applied fire pulses', failures)

    def test_a_shot_needs_the_pulse_of_its_own_player(self):
        # player 2's shot, relabelled as a second shot of player 1, has no second pulse of player 1
        pair = authored_pair()
        for item in pair.values():
            item['shots'][1]['player'] = '1'; item['shots'][1]['ordinal'] = 2
        failures = self.failures_of(pair)
        self.assertIn('host: player 1: more native shots than applied fire pulses', failures)
        self.assertIn('host: both actual shooters required', failures)

    def test_a_second_shot_of_player_two_is_not_paid_for_by_player_ones_pulse(self):
        pair = authored_pair()
        for item in pair.values():
            second = copy.deepcopy(next(s for s in item['shots'] if s['player'] == '2')); second['ordinal'] = 2
            item['shots'].append(second)
            add_row(item, 201, **{'1': 1.})
        failures = self.failures_of(pair)
        for role in ('host', 'guest'):
            self.assertIn(f'{role}: player 2: more native shots than applied fire pulses', failures)
            self.assertNotIn(f'{role}: player 1: more native shots than applied fire pulses', failures)

    def test_a_pulse_for_every_shot_is_enough(self):
        pair = authored_pair()
        for item in pair.values():
            second = copy.deepcopy(item['shots'][0]); second['ordinal'] = 2; item['shots'].append(second)
            add_row(item, 201, **{'1': 1.})
        self.assertTrue(analyze_pair(pair)['passed'], analyze_pair(pair))

    def test_the_longer_record_lacking_the_shorter_records_final_row_is_refused(self):
        # the comparison reaches the shorter record's last tick itself, not the tick before it or two before it
        for longer, shorter in (('host', 'guest'), ('guest', 'host')):
            with self.subTest(longer=longer):
                pair = authored_pair(); last = pair[longer]['rows'][-1]
                for step in range(1, 4):
                    row = copy.deepcopy(last); row['tick'] += 2 * step; row['present'] += 6 * step
                    pair[longer]['rows'].append(row)
                final = pair[shorter]['rows'][-1]['tick']
                pair[longer]['rows'] = [r for r in pair[longer]['rows'] if r['tick'] != final]
                self.assertIn('native tick sets disagree up to the shorter record', self.failures_of(pair))

    def test_a_row_missing_one_tick_before_the_shorter_records_end_is_refused(self):
        pair = authored_pair(); final = pair['guest']['rows'][-1]['tick']
        pair['host']['rows'] = [r for r in pair['host']['rows'] if r['tick'] != final - 2]
        self.assertIn('native tick sets disagree up to the shorter record', self.failures_of(pair))

    def test_the_amount_of_a_fire_decides_whether_it_is_a_pulse(self):
        # more than half is a pulse; half, or a trace, is not
        def second_shot_with_fire(amount):
            pair = authored_pair()
            for item in pair.values():
                second = copy.deepcopy(item['shots'][0]); second['ordinal'] = 2; item['shots'].append(second)
                add_row(item, 201, **{'1': amount})
            return pair
        self.assertTrue(analyze_pair(second_shot_with_fire(.6))['passed'], analyze_pair(second_shot_with_fire(.6)))
        for amount in (.3, .5):
            with self.subTest(amount=amount):
                failures = self.failures_of(second_shot_with_fire(amount))
                for role in ('host', 'guest'):
                    self.assertIn(f'{role}: player 1: more native shots than applied fire pulses', failures)


class RecordAdmissionTests(unittest.TestCase):
    """Behaviour of the record checks that no other test pinned (survivors of the review's mutation sweep)."""
    def refuse(self, mutate, witness):
        pair = authored_pair(); mutate(pair)
        result = analyze_pair(pair)
        self.assertFalse(result['passed'])
        self.assertTrue(any(witness in f for f in result['failures']), result)

    def test_the_debuggers_own_failure_list_refuses_the_record(self):
        # a label written after the harness stopped reading (an unfinished shot scope, a failed detach)
        for label in ('unfinished_native_shot_scope', 'observer_detach_failed'):
            self.refuse(lambda p: p['guest'].__setitem__('failures', [label]), 'guest: incomplete observer')

    def test_a_record_that_never_attached_is_refused(self):
        self.refuse(lambda p: p['host'].__setitem__('attached', False), 'host: incomplete observer')

    def test_too_few_rows_are_refused(self):
        self.refuse(lambda p: p['host'].__setitem__('rows', p['host']['rows'][:19]), 'host: bounded samples absent')
        self.assertNotIn('host: bounded samples absent', analyze_pair(authored_pair())['failures'])

    def test_a_third_player_key_is_refused(self):
        self.refuse(lambda p: p['guest']['rows'][30]['players'].__setitem__('3', [0., 0., 0.]), 'guest: invalid native pose')
        self.refuse(lambda p: p['guest']['rows'][30]['controls'].__setitem__('3', dict(forward=0., backward=0., charge=0., fire=0.)),
                    'guest: invalid native pose')

    def test_a_control_outside_zero_to_one_is_refused(self):
        for value in (5., -1., 1.0001, math.inf, math.nan):
            with self.subTest(value=value):
                self.refuse(lambda p: p['host']['rows'][30]['controls']['1'].__setitem__('charge', value), 'host: invalid native pose')
        self.refuse(lambda p: p['host']['rows'][30]['controls']['1'].__setitem__('mystery', 0.), 'host: invalid native pose')

    def test_movement_must_beat_the_neutral_drift_by_the_ratio(self):
        # both peers agree on a neutral wobble of 3 units; the 9.6 units of movement is above the floor of 1 but not 4 x 3
        def wobble(pair):
            for item in pair.values():
                for row in item['rows']:
                    if 48 <= row['present'] < 90: row['players']['1'][1] += 3.
        self.refuse(wobble, 'host: movement not above neutral drift')
        pair = authored_pair()
        for item in pair.values():
            for row in item['rows']:
                if 48 <= row['present'] < 90: row['players']['1'][1] += 2.
        self.assertTrue(analyze_pair(pair)['passed'], analyze_pair(pair))     # 4 x 2 = 8 < 9.6

    def test_a_repeated_shot_identity_is_refused(self):
        def repeat(pair):
            pair['host']['shots'].append(copy.deepcopy(pair['host']['shots'][0]))
        self.refuse(repeat, 'host: duplicate native shot')

    def test_both_players_must_have_shot(self):
        def only_player_one(pair):
            for item in pair.values():
                item['shots'][1]['player'] = '1'; item['shots'][1]['ordinal'] = 2
        self.refuse(only_player_one, 'host: both actual shooters required')
        self.refuse(only_player_one, 'guest: both actual shooters required')

    def test_two_records_with_almost_nothing_in_common_are_refused(self):
        def disjoint(pair):
            pair['host']['rows'] = pair['host']['rows'][:20]; pair['guest']['rows'] = pair['guest']['rows'][-20:]
        self.refuse(disjoint, 'too few corresponding native ticks')
        pair = authored_pair(); self.assertGreaterEqual(analyze_pair(pair)['shared_native_ticks'], 20)

    def test_nineteen_shared_ticks_are_too_few_and_twenty_are_enough(self):
        def twenty_rows(pair, guest_last_tick=None):
            for item in pair.values(): item['rows'] = item['rows'][:20]
            if guest_last_tick is not None: pair['guest']['rows'][-1]['tick'] = guest_last_tick
            return pair
        enough = analyze_pair(twenty_rows(authored_pair()))
        self.assertEqual(enough['shared_native_ticks'], 20)
        self.assertNotIn('too few corresponding native ticks', enough['failures'])
        # the guest's twentieth row is one tick later than the host's, so the records share 19 ticks
        pair = authored_pair(); host_last = pair['host']['rows'][19]['tick']
        short = analyze_pair(twenty_rows(pair, host_last + 1))
        self.assertEqual(short['shared_native_ticks'], 19)
        self.assertIn('too few corresponding native ticks', short['failures'])


class SamplingRuleTests(unittest.TestCase):
    """The debugger's even/odd tick rule, run on its own class with every neighbour faked."""
    def breakpoint(self, ticks_and_fires):
        tree = ast.parse(GDB_BODY)
        node = next(n for n in tree.body if isinstance(n, ast.ClassDef) and n.name == 'Observe')
        current = dict(tick=0, fire=False)
        # True fires player 1; a string names the players that fire, e.g. '2'
        firing = lambda fire: '1' if fire is True else (fire or '')
        debugger = mock.Mock(); debugger.Breakpoint = type('Breakpoint', (), {'__init__': lambda self, name, internal=False: None})
        scope = dict(gdb=debugger, MAX_TICK=1200, MAX_ROWS=700, MAX_SHOTS=64, last_tick=-1, shot_stack=[], shot_ordinals={},
                     state=dict(rows=[], shots=[], failures=[]), flush=lambda: None, fail=mock.Mock(),
                     is_running=lambda: True, ticket=lambda: (current['tick'], current['tick'] * 3),
                     controls=lambda: {n: dict(forward=0., backward=0., charge=0., fire=float(n in firing(current['fire'])))
                                       for n in ('1', '2')},
                     players=lambda: ({'1': [0., 0., 0.], '2': [1., 1., 1.]}, {}))
        exec(compile(ast.Module(body=[node], type_ignores=[]), '<authored-observe-control>', 'exec'), scope)
        watch = scope['Observe']('CE_LockstepAfterUpdate', 'tick')
        for tick, fire in ticks_and_fires:
            current.update(tick=tick, fire=fire); self.assertFalse(watch.stop())
        scope['fail'].assert_not_called()
        return [row['tick'] for row in scope['state']['rows']]

    def test_even_ticks_are_sampled_and_odd_ticks_only_with_a_fire(self):
        got = self.breakpoint([(1, False), (2, False), (3, True), (4, False), (5, False), (6, False), (7, True)])
        self.assertEqual(got, [2, 3, 4, 6, 7])

    def test_an_odd_tick_is_sampled_for_a_fire_of_either_player(self):
        for who in ('1', '2', '12'):
            with self.subTest(fire=who):
                self.assertEqual(self.breakpoint([(1, False), (2, False), (3, who), (4, False), (5, False)]), [2, 3, 4])

    def test_tick_zero_and_a_repeated_tick_are_not_samples(self):
        got = self.breakpoint([(0, False), (0, True), (2, False), (2, False), (3, True), (3, True), (4, False)])
        self.assertEqual(got, [2, 3, 4])


if __name__ == '__main__':
    unittest.main()
