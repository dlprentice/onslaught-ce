#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise actual lockstep terminal control with stub world/transport; not a game or socket test."""
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
from network_world_tests import actual_body
import deps
ROOT=Path(__file__).resolve().parents[2]
parent=ROOT/'local-data/port/lockstep-terminal-tests'
parent.mkdir(parents=True,exist_ok=True)
out=Path(tempfile.mkdtemp(prefix='run-',dir=parent))
source=ROOT/'port/game/ce_network_lockstep.cpp'
driver=ROOT/'port/tools/tests/network_lockstep_terminal_driver.cpp'
crypto=deps.SRC/'monocypher-4.0.2/src'
pins={'monocypher.h':'f78bb31255cfb7beba66afd2137f5194c8a025cf40488b6cc1e295234d43f374',
      'monocypher.c':'afe2b098c8569577a84488e0b98d276d1fba6506adea68bb9241a52111734c59'}
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
if {name:sha(crypto/name) for name in pins}!=pins:raise RuntimeError('pinned Monocypher inputs changed')
crypto_obj=out/'monocypher.o'
compiled=subprocess.run(['clang','-std=c99','-O1','-I',str(crypto),'-c',str(crypto/'monocypher.c'),'-o',str(crypto_obj)],capture_output=True,text=True,timeout=60)
(out/'monocypher-build.log').write_text(compiled.stdout+compiled.stderr)
if compiled.returncode:raise RuntimeError('pinned Monocypher compile failed')
# Compile the real world action whitelist, semantic validator and lockstep wrapper, not a duplicated model.
world=(ROOT/'port/game/ce_network_world.cpp').read_text()
start=world.index('const uint32_t native_actions[]=');end=world.index(';',start)+1
validator=out/'world-input-validator.inc'
validator.write_text(world[start:end]+'\n'+actual_body(world,'unsigned ActionRank(')+'\n'+actual_body(world,'bool ValidInput(')+'\nextern "C" '+actual_body(world,'int CE_WorldLockstepValidInput(')+'\n')
native_getter=actual_body((ROOT/'src/game.cpp').read_text(),'BOOL\tCGame::GetSlot(')
if native_getter.count('CGame::GetSlot')!=1:raise RuntimeError('native slot getter signature is ambiguous')
native_getter=native_getter.replace('CGame::GetSlot','NativeGame::GetSlot',1)
getter=out/'native-slot-getter.inc';getter.write_text(native_getter+'\n')
mutants=(
 ('error-graceful-leave', 'if (desync_queued) (ChatRetire(), CE_NetClose(net));', '(ChatRetire(), CE_NetClose(net));', 'local failure aborts without an authenticated voluntary leave'),
 ('drop-queued-desync', 'if (desync_queued) (ChatRetire(), CE_NetClose(net));', 'if (false) (ChatRetire(), CE_NetClose(net));', 'queued state divergence is flushed before graceful close'),
 ('state-desync-backpressure', 'desync_queued = !w.error && CE_NetSend(L.net, CE_NET_CONTROL, h.tick, b, w.size) > 0;', 'desync_queued = !w.error && CE_NetSend(L.net, CE_NET_CONTROL, h.tick, b, w.size) >= 0;', 'unqueued divergence aborts without a voluntary leave'),
 ('mission-desync-backpressure', 'const bool desync_queued = !w.error && CE_NetSend(L.net, CE_NET_CONTROL, L.final_tick, b, w.size) > 0;', 'const bool desync_queued = !w.error && CE_NetSend(L.net, CE_NET_CONTROL, L.final_tick, b, w.size) >= 0;', 'unqueued mission divergence aborts without a voluntary leave'),
 ('discard-pending-desync', 'if (status.phase == CE_NET_CLOSED && status.error == CE_NET_PEER_LEFT) {\n\t\tif (!Receive() || L.stage == FAILED) return false;', 'if (status.phase == CE_NET_CLOSED && status.error == CE_NET_PEER_LEFT) {', 'queued remote divergence wins over later leave'),
 ('leave-by-system','int LeaveCode() { return (L.admission_required || CE_BootLockstep()) ? QT_QUIT_TO_FRONTEND : QT_QUIT_TO_SYSTEM; }','int LeaveCode() { return QT_QUIT_TO_SYSTEM; }','a shipped game leaves for the front end after a desync, with the reason for the lobby'),
 ('left-is-benign','if (CE_BootLockstep()) return Fail("the other player left the co-op game");','/* negative: a left game is not a failure */','a shipped game that is left fails with the reason and leaves for the front end'),
 ('ignore-terminal-hash','L.final_hash != L.final_peer_hash','false','terminal hash mismatch fails'),
 ('accept-missing-comparison','L.hashes_compared != expected','false','missing scheduled final hash cannot pass'),
 ('accept-premature-close','if (L.quit_at && !L.final_confirmed) return Fail("the other player left before final co-op confirmation");\n\t\tif ((L.stage == STOPPING || L.final_peer) && !L.final_confirmed)\n\t\t\treturn Fail("the other player left before final mission confirmation");','if (false) return false;','early peer exit is a failure'),
 ('accept-early-ack','!L.final_ack_sent || tick != L.final_tick || hash != L.final_hash','false || tick != L.final_tick || hash != L.final_hash','ACK before final hash fails'),
 ('disable-timeout','L.stage == STOPPING && CE_NetNowMs() - L.final_started >= kGiveUpMs','false','terminal wait times out as failure'),
 ('omit-unaligned-hash','if ((natural_end || L.quit_at == L.tick) && L.tick % L.hash_every) final_hash = StateHash(L.tick);','if (false) final_hash = StateHash(L.tick);','unaligned final hash precedes deferred presentation placement'),
 ('ignore-hash-envelope','tick % L.hash_every || m.tick != tick','tick % L.hash_every || false','hash transport tick must equal payload tick'),
 ('ignore-hash-window','(uint64_t)tick > now + ahead || (uint64_t)tick + span <= now','false','far-future hash is refused'),
 ('overwrite-remote-ring','if (h.local != h.remote) return Fail("a state hash would replace an unmatched comparison");','if (false) return Fail("a state hash would replace an unmatched comparison");','remote hash cannot overwrite an unmatched ring slot'),
 ('overwrite-local-ring','if (h.local != h.remote) { Fail("a local state hash would replace an unmatched comparison"); return; }','if (false) { Fail("a local state hash would replace an unmatched comparison"); return; }','local hash cannot overwrite an unmatched ring slot'),
 ('accept-local-incomplete','(L.stage == RUNNING && (L.quit_at || L.final_peer))','false','local test teardown before endpoint is incomplete'),
 ('skip-natural-barrier','if (natural_end) {\n\t\t// A naturally ended mission','if (natural_end) { L.stage = FINISHED; return; }\n\tif (false) {\n\t\t// A naturally ended mission','natural end waits and publishes a separate final hash'),
 ('drop-final-done','if (!SendFinal(MSG_FINAL_DONE, L.final_hash)) return false;','if (false) return false;','natural host sends DONE but waits for delivery acknowledgement'),
 ('discard-finished-messages','if (!Receive()) return false; // never discard a delayed mismatch or malformed control after mission confirmation','if (false) return false; // negative: discard finished messages','natural end does not silently discard later invalid messages'),
 ('accept-remote-end-only','L.final_peer && L.final_peer_mission && L.tick >= L.final_peer_tick && !natural_end','false','peer mission end is refused when local state keeps playing at that tick'),
 ('allow-update-after-late-error','if (L.stage == FINISHED) return Poll() ? 1 : 0;','if (L.stage == FINISHED) { Poll(); return 1; }','invalid late message stops the end-screen update'),
 ('lose-aligned-natural-hash','final_hash = hash; // reuse this exact pre-presentation hash if either end occurs on the scheduled tick','if (L.quit_at == L.tick) final_hash = hash;','natural aligned final hash equals scheduled hash'),
 ('skip-natural-deferred','if (deferred_count) {\t// after the snapshot and the hash:','if (deferred_count && !natural_end) {\t// after the snapshot and the hash:','natural end preserves pre-placement hash and completes deferred placement'),
 ('discard-completed-on-close','if (L.stage == FINISHED && L.final_mission && L.final_confirmed) {','if (false) {','host preserves delivered acknowledgement with same-poll peer close'),
 ('timeout-completed-mission','L.stage == STOPPING && CE_NetNowMs() - L.final_started >= kGiveUpMs','CE_NetNowMs() - L.final_started >= kGiveUpMs','completed mission does not fail the incomplete-wait timeout'),
 ('ignore-native-quit-failure','if (L.outcome_error) return native_quit_seen && native_code ? native_code : 1;','if (false) return 1;','outcome error overrides a zero native WM_QUIT code'),
 ('omit-terminal-outcome','all.U32((uint32_t)GAME.GetGameState());','/* negative: omit outcome */','win/loss changes selected terminal hash'),
 ('omit-player-gates','all.Bytes(&gates, sizeof gates);','/* negative: omit player gates */','selected player gate digest changes state hash'),
 ('omit-career-slots','for (int slot=0;slot<MAX_CAREER_SLOTS*8;++slot) all.U32((uint32_t)GAME.GetSlot(slot));','/* negative: omit script slots */','all 256 script-visible career slots change selected hash'),
 ('omit-last-career-slot','for (int slot=0;slot<MAX_CAREER_SLOTS*8;++slot) all.U32((uint32_t)GAME.GetSlot(slot));','for (int slot=0;slot<MAX_CAREER_SLOTS*8-1;++slot) all.U32((uint32_t)GAME.GetSlot(slot));','all 256 script-visible career slots change selected hash'),
 ('omit-pre-run-capture','if (!CapturePreRunSlots()) return Fail("co-op pre-run flags could not be captured");','(void)CapturePreRunSlots(); L.pre_run_slots_captured = false;','matching flags and peer acknowledgement admit one prerun'),
 ('capture-after-pre-run','\tCE_SessionGamePreRun();\n','\tCE_SessionGamePreRun(); CapturePreRunSlots();\n','pre-run slot divergence survives converging current slots'),
 ('omit-pre-run-slots','all.Bytes(L.pre_run_slots, sizeof L.pre_run_slots);','/* negative: omit pre-run slots */','pre-run slot divergence survives converging current slots'),
 ('skip-pre-run-slot','slot<MAX_CAREER_SLOTS*8;++slot) L.pre_run_slots','slot<MAX_CAREER_SLOTS*8-1;++slot) L.pre_run_slots','pre-run slot divergence survives converging current slots'),
 ('allow-missing-pre-run-slots','if (!L.pre_run_slots_captured) { Fail("co-op pre-run slot flags were not captured"); return 0; }','/* negative: allow no snapshot */','missing pre-run slot capture refuses state hash'),
 ('omit-start-hash-wait','if (!Block([] { return L.hashes[0].local && L.hashes[0].remote; }, kStartMs,\n\t\t"the other player did not confirm the co-op start in time")) return 0;','/* negative: skip start confirmation */','local acknowledgement follows matching peer hash'),
 ('accept-local-start-only','return L.hashes[0].local && L.hashes[0].remote;','return L.hashes[0].local;','local acknowledgement follows matching peer hash'),
 ('compare-start-twice','"the other player did not confirm the co-op start in time")) return 0;','"the other player did not confirm the co-op start in time")) return 0;\n\tif (!Compare(h)) return 0;','start waits for delayed matching hash'),
 ('allow-start-reentry','if (L.stage == STARTING || L.stage == LOADED) return 0; // no simulation update may re-enter a waiting stage','if (L.stage == STARTING || L.stage == LOADED) return 1; // negative: allow reentrant update','message-pump reentry cannot advance a waiting game'),
 ('omit-start-stage','\tL.stage = STARTING;\n','\t/* negative: do not fence the pre-prerun wait */\n','matching flags and peer acknowledgement admit one prerun'),
 ('retry-after-connection','status.phase == CE_NET_FAILED && L.stage == STARTING && !L.connection_established && !L.host','status.phase == CE_NET_FAILED && L.stage == STARTING && !L.peer_ready && !L.host','guest failure after connection before readiness stops promptly'),
 ('retry-after-ready','status.phase == CE_NET_FAILED && L.stage == STARTING && !L.connection_established && !L.host','status.phase == CE_NET_FAILED && L.stage == STARTING && !L.host','guest transport failure after readiness stops promptly'),
 ('accept-start-disconnect','if (L.stage == STARTING) return Fail("the other player left before confirming the co-op start");','/* negative: accept early peer departure */','peer close before start hash stops gameplay'),
 ('omit-waiting-teardown','|| L.stage == STARTING || L.stage == LOADED','|| L.stage == LOADED','local cancellation closes the waiting transport'),
 ('accept-start-timeout','if (CE_NetNowMs() - start > limit) return Fail(what);','if (CE_NetNowMs() - start > limit) return true;','missing start hash times out before gameplay'),
 ('omit-start-ack-send','if (!SendStartAck(start)) return 0;','/* negative: no local acknowledgement */','start waits for delayed peer comparison acknowledgement'),
 ('ignore-start-ack-send-failure','if (!SendStartAck(start)) return 0;','(void)SendStartAck(start);','unsendable acknowledgement fails after early peer ack'),
 ('omit-start-ack-wait','if (!Block([] { return L.start_ack_received; }, kStartMs,\n\t\t"the other player did not acknowledge the co-op start in time")) return 0;','/* negative: do not wait for the peer acknowledgement */','start waits for delayed peer comparison acknowledgement'),
 ('accept-unreceived-start-ack','return L.start_ack_received;','return true;','start waits for delayed peer comparison acknowledgement'),
 ('send-start-ack-early','if (!Block([] { return L.hashes[0].local && L.hashes[0].remote; }, kStartMs,\n\t\t"the other player did not confirm the co-op start in time")) return 0;','if (!SendStartAck(start)) return 0;\n\tif (!Block([] { return L.hashes[0].local && L.hashes[0].remote; }, kStartMs,\n\t\t"the other player did not confirm the co-op start in time")) return 0;','local acknowledgement follows matching peer hash'),
 ('accept-start-ack-before-hash','!h.local || !h.remote || !L.hashes_compared || h.mine != h.theirs || hash != h.mine','false','start acknowledgement before peer hash is refused'),
 ('accept-wrong-start-ack-hash','h.mine != h.theirs || hash != h.mine','h.mine != h.theirs','incorrect start acknowledgement hash is refused'),
 ('accept-duplicate-start-ack','|| L.start_ack_received ||','|| false ||','duplicate start acknowledgement is refused'),
 ('ignore-start-ack-payload-tick','m.tick != 0 || tick != 0 || L.stage != STARTING || L.start_ack_received','m.tick != 0 || false || L.stage != STARTING || L.start_ack_received','start acknowledgement payload tick is checked'),
 ('ignore-start-ack-envelope-tick','m.tick != 0 || tick != 0 || L.stage != STARTING || L.start_ack_received','false || tick != 0 || L.stage != STARTING || L.start_ack_received','start acknowledgement transport tick is checked'),
 ('accept-short-start-ack','!CE_NetReadDone(&r) || m.tick != 0 || tick != 0 || L.stage != STARTING || L.start_ack_received','m.tick != 0 || tick != 0 || L.stage != STARTING || L.start_ack_received','truncated acknowledgement fails even when hash is zero'),
 ('accept-start-ack-wrong-stage','L.stage != STARTING || L.start_ack_received','false || L.start_ack_received','start acknowledgement outside start stage is refused'),
 ('send-wrong-start-ack-hash','Encode(b, n, MSG_START_ACK, 0);\n\tCE_NetWriter w; CE_NetWriterInit(&w, b, sizeof b); w.size = n; CE_NetWriteU64(&w, hash);','Encode(b, n, MSG_START_ACK, 0);\n\tCE_NetWriter w; CE_NetWriterInit(&w, b, sizeof b); w.size = n; CE_NetWriteU64(&w, hash ^ 1ull);','local start acknowledgement binds the compared hash'),
 ('ignore-gate-capture-failure','if (!CE_WorldLockstepGates(&gates)) { Fail("co-op player gates or weapon inventory could not be compared"); return 0; }','(void)CE_WorldLockstepGates(&gates);','invalid player gates stop before any hash is sent'),
 ('send-after-gate-failure','uint64_t hash = StateHash(L.tick);\n\t\tif (L.stage == FAILED) return;','uint64_t hash = StateHash(L.tick);','invalid player gates stop before any hash is sent'),
 ('ignore-peer-final-on-teardown','(L.quit_at || L.final_peer)','L.quit_at','local teardown after peer final is incomplete'),
 ('accept-mission-done-loss','(L.stage == STOPPING || L.final_peer) && !L.final_confirmed','false','guest refuses close after receipt when mission DONE is missing'),
 ('ignore-remote-controls','!CE_WorldLockstepValidInput(&in)','false','remote pause action is refused'),
 ('ignore-local-controls','!CE_WorldLockstepValidInput(&sample)','false','invalid local sample is not queued or sent'),
 ('ignore-input-envelope','m.tick != tick || !CE_WorldLockstepValidInput(&in)','false || !CE_WorldLockstepValidInput(&in)','input transport tick must equal payload tick'),
 ('accept-before-done-ack','L.final_done_sent = true;','L.final_done_sent = true; FinalComplete();','natural host waits for guest DONE acknowledgement'),
 ('close-before-done-ack','L.final_done_sent = true;','L.final_done_sent = true; CE_NetClose(L.net);','host keeps transport active while awaiting DONE acknowledgement'),
 ('omit-done-ack','if (!SendFinal(MSG_FINAL_DONE_ACK, L.final_hash)) return false;','if (false) return false;','natural guest acknowledges receiving DONE before completing'),
 ('accept-early-done-ack','!L.host || !L.final_done_sent || L.final_confirmed','!L.host || L.final_confirmed','host refuses DONE acknowledgement before sending DONE'),
 ('accept-synthetic-close-before-done','L.final_done_received && L.final_receipt_sent','L.final_receipt_sent','synthetic guest refuses peer close before DONE'),
 ('ignore-done-ack-send-failure','if (!SendFinal(MSG_FINAL_DONE_ACK, L.final_hash)) return false;','(void)SendFinal(MSG_FINAL_DONE_ACK, L.final_hash);','guest fails if DONE acknowledgement cannot be queued'),
 ('ignore-prerun-digest','memcmp(digest,L.pre_run_digest,sizeof digest)!=0','false','each single-slot peer mismatch stops before prerun and modeled career mutation'),
 ('allow-missing-prerun-ack','return L.prerun_ack_received;','return true;','missing pre-run acknowledgement times out before prerun'),
 ('skip-slot-freshness','if ((uint8_t)(GAME.GetSlot(slot)!=0)!=L.pre_run_slots[slot]) return false;','(void)slot;','local slot change before acknowledgement fails closed'),
 ('skip-early-freshness','if (!Block([] { return L.peer_ready; }, kStartMs, "the other player did not finish loading in time")) return 0;\n\tif (!PreRunSlotsUnchanged()) return Fail("local career flags changed while waiting for pre-run admission");','if (!Block([] { return L.peer_ready; }, kStartMs, "the other player did not finish loading in time")) return 0;','local slot change before acknowledgement fails closed'),
 ('skip-final-freshness','if (!Block([] { return L.prerun_ack_received; }, kStartMs,\n\t\t"the other player did not acknowledge pre-run admission in time")) return 0;\n\tif (!PreRunSlotsUnchanged()) return Fail("local career flags changed while waiting for pre-run admission");','if (!Block([] { return L.prerun_ack_received; }, kStartMs,\n\t\t"the other player did not acknowledge pre-run admission in time")) return 0;','local slot change while awaiting acknowledgement fails closed'),
 ('omit-prerun-rng-restore','PortCrtRandSet(pre_calls,pre_hold); CE_WorldLockstepRandom(0,&pre_value,&pre_seed);','/* negative: retain RNG changes from pumping */','message-pump random changes are restored before prerun'),
 ('allow-early-prerun-ack','L.stage != STARTING || !L.peer_ready ||\n\t\t\t\tL.prerun_ack_received || L.prerun_finished','L.stage != STARTING || false ||\n\t\t\t\tL.prerun_ack_received || L.prerun_finished','pre-run acknowledgement before ready is refused'),
 ('allow-duplicate-prerun-ack','L.prerun_ack_received || L.prerun_finished','false || L.prerun_finished','duplicate pre-run acknowledgement is refused'),
 ('ignore-prerun-payload-tick','m.tick != 0 || tick != 0 || L.stage != STARTING || !L.peer_ready','m.tick != 0 || false || L.stage != STARTING || !L.peer_ready','pre-run acknowledgement payload tick is checked'),
 ('ignore-prerun-envelope-tick','m.tick != 0 || tick != 0 || L.stage != STARTING || !L.peer_ready','false || tick != 0 || L.stage != STARTING || !L.peer_ready','pre-run acknowledgement transport tick is checked'),
 ('allow-prerun-extra-payload','if (!CE_NetReadDone(&r) || m.tick != 0 || tick != 0 || L.stage != STARTING || !L.peer_ready','if (m.tick != 0 || tick != 0 || L.stage != STARTING || !L.peer_ready','pre-run acknowledgement with extra payload is refused'),
 ('allow-late-prerun-ack','L.prerun_ack_received || L.prerun_finished','L.prerun_ack_received || false','late pre-run acknowledgement is refused'),
 ('allow-premature-hash','if (L.stage == STARTING && (!L.prerun_ack_sent || !L.prerun_ack_received))','if (false)','start hash before pre-run acknowledgement is refused'),
 ('allow-loaded-update','if (L.stage == STARTING || L.stage == LOADED) return 0; // no simulation update may re-enter a waiting stage','if (L.stage == STARTING) return 0; if (L.stage == LOADED) return 1; // negative: LOADED is not fenced','only admitted prerun permits a simulation update before running'),
)
campaign_configure_mutants = (
    ('campaign-configure-version', 'launch.version != 1 || launch.mission != L.level ||',
     'launch.mission != L.level ||',
     'unknown campaign launch version refuses before gameplay'),
    ('campaign-configure-mission', 'launch.version != 1 || launch.mission != L.level ||',
     'launch.version != 1 ||',
     'campaign launch for another loaded mission refuses before gameplay'),
    ('campaign-configure-role', 'bool(host) != L.host || !launch.session || !launch.attempt',
     '!launch.session || !launch.attempt',
     'campaign launch for the other player role refuses before gameplay'),
    ('campaign-configure-session', 'bool(host) != L.host || !launch.session || !launch.attempt',
     'bool(host) != L.host || !launch.attempt',
     'campaign launch without a session refuses before gameplay'),
    ('campaign-configure-attempt', 'bool(host) != L.host || !launch.session || !launch.attempt',
     'bool(host) != L.host || !launch.session',
     'campaign launch without an attempt refuses before gameplay'),
    ('campaign-configure-retire', '        CE_BootCampaignClear();\n        return Fail("The campaign launch does not match the loaded session.");',
     '        return Fail("The campaign launch does not match the loaded session.");',
     'refused campaign launch retires its boot admission'),
    ('campaign-configure-clear-return', '    ce_campaign_return_clear();',
     '    /* control: retain a prior mission return */',
     'accepted campaign launch clears the previous mission return'),
)
mutants += campaign_configure_mutants
def run(label, source_path, expected_fail=None, native_getter=getter):
    if label=='actual': src=driver
    else:
        src=out/(label+'-driver.cpp')
        src.write_text(driver.read_text().replace('#include "../../game/ce_network_lockstep.cpp"',f'#include "{source_path}"'))
    exe=out/(label+'-test')
    cmd=['clang++','-std=c++17','-fwrapv','-O1','-DLOCKSTEP_VALIDATOR_INC="'+str(validator)+'"','-DNATIVE_SLOT_GETTER_INC="'+str(native_getter)+'"','-I',str(ROOT/'port/tools/tests/lockstep_stubs'),'-I',str(crypto),str(src),str(ROOT / 'port/game/ce_network_campaign.cpp'), str(ROOT / 'port/game/ce_network_result_exchange.cpp'), str(ROOT/'port/network/codec.cpp'),str(ROOT/'port/network/prerun_flags.cpp'),str(crypto_obj),'-o',str(exe)]
    proc=subprocess.run(cmd,capture_output=True,text=True,env=dict(os.environ,TMPDIR=str(out)),timeout=60)
    (out/(label+'-build.log')).write_text(proc.stdout+proc.stderr)
    if proc.returncode: raise RuntimeError(f'{label}: build failed: '+proc.stderr)
    proc=subprocess.run([str(exe)],capture_output=True,text=True,timeout=10)
    (out/(label+'-result.log')).write_text(proc.stdout+proc.stderr)
    if expected_fail is None:
        print(proc.stdout+proc.stderr,end=''); return proc.returncode==0
    caught=proc.returncode!=0 and ('FAIL '+expected_fail) in proc.stdout
    print(('PASS' if caught else 'FAIL')+' negative control: '+label)
    return caught
ok=run('actual',source)
if sys.argv[1:] == ['--campaign-return-only']:
    import json
    return_mutants = (
        ('return-omit-capture', '    CampaignCaptureReturn();', '    /* control: omit capture */',
         'confirmed campaign teardown retains exact identity and result'),
        ('return-skip-terminal-confirmation', 'L.final_confirmed && CampaignReady(), L.outcome_error',
         'CampaignReady(), L.outcome_error',
         'result agreement without terminal confirmation is not returned'),
        ('return-ignore-runtime-error', 'L.final_confirmed && CampaignReady(), L.outcome_error',
         'L.final_confirmed && CampaignReady(), false',
         'runtime error suppresses confirmed campaign return'),
        ('return-omit-clear', '    ce_campaign_return_clear();', '    /* control: keep stale return */',
         'new campaign configuration clears previous return'),
        ('return-drop-latch-guard', 'L.campaign && !ce_campaign_return_get(&saved)', 'L.campaign',
         'duplicate teardown preserves latched campaign result'),
    )
    text = source.read_text()
    records = [{'label': 'actual', 'passed': bool(ok)}]
    for label, old, new, reason in return_mutants:
        if old not in text:
            raise SystemExit('missing return control anchor: ' + label)
        root = out / label
        (root / 'game').mkdir(parents=True)
        (root / 'network').mkdir()
        for f in (ROOT / 'port/game').glob('*.h'):
            shutil.copyfile(f, root / 'game' / f.name)
        shutil.copyfile(ROOT / 'port/network/network.h', root / 'network/network.h')
        shutil.copyfile(ROOT / 'port/network/compatibility.h', root / 'network/compatibility.h')
        for name in ('ice_game.h', 'ice_signaling.h'):
            shutil.copyfile(ROOT / 'port/network' / name, root / 'network' / name)
        (root / 'platform/common').mkdir(parents=True)
        shutil.copyfile(ROOT / 'port/platform/common/ce_netcode.h', root / 'platform/common/ce_netcode.h')
        path = root / 'game/ce_network_lockstep.cpp'
        path.write_text(text.replace(old, new))
        passed = run(label, path, reason)
        records.append({'label': label, 'passed': passed, 'expected_assertion': reason})
        ok = passed and ok
    result = out / 'campaign-return-result.json'
    result.write_text(json.dumps({'passed': bool(ok), 'source_sha256': sha(source),
        'driver_sha256': sha(driver), 'results': records,
        'scope': 'actual lockstep terminal fixture and five return-seam controls; no game runtime'}, indent=2) + '\n')
    print(result)
    raise SystemExit(0 if ok else 1)

if sys.argv[1:]==['--actual-only']:
    print('evidence:',out)
    raise SystemExit(0 if ok else 1)
configure_only = sys.argv[1:] == ['--campaign-configure-only']
if sys.argv[1:] and not configure_only:
    raise SystemExit('usage: network_lockstep_terminal_tests.py [--actual-only | --campaign-return-only | --campaign-configure-only]')
text=source.read_text()
selected_mutants = campaign_configure_mutants if configure_only else mutants
configure_records = [{'label': 'actual', 'passed': bool(ok)}]
for label,old,new,reason in selected_mutants:
    if text.count(old)!=1: raise RuntimeError(f'{label}: expected unique mutation, got {text.count(old)}')
    root=out/label
    (root/'game').mkdir(parents=True);(root/'network').mkdir()
    for f in (ROOT/'port/game').glob('*.h'): shutil.copyfile(f,root/'game'/f.name)
    shutil.copyfile(ROOT/'port/network/network.h',root/'network/network.h')
    shutil.copyfile(ROOT/'port/network/ice_game.h',root/'network/ice_game.h')
    shutil.copyfile(ROOT/'port/network/ice_signaling.h',root/'network/ice_signaling.h')
    shutil.copyfile(ROOT/'port/network/compatibility.h',root/'network/compatibility.h')	# ce_network_boot.h (the controller's admission) includes it
    (root / 'platform/common').mkdir(parents=True, exist_ok=True)
    shutil.copyfile(ROOT / 'port/platform/common/ce_netcode.h', root / 'platform/common/ce_netcode.h')
    path=root/'game/ce_network_lockstep.cpp';path.write_text(text.replace(old,new))
    caught=run(label,path,reason)
    configure_records.append({'label':label,'passed':bool(caught),'expected_assertion':reason})
    ok=caught and ok
if configure_only:
    import json
    result=out/'campaign-configure-result.json'
    result.write_text(json.dumps({'passed':bool(ok),'source_sha256':sha(source),
        'driver_sha256':sha(driver),'results':configure_records,
        'scope':'actual campaign launch-to-loaded-session checks with stub boot/world/transport; no game, save or network run'},indent=2)+'\n')
    print('evidence:',result)
    if {name:sha(crypto/name) for name in pins}!=pins:
        raise RuntimeError('pinned Monocypher inputs changed during controls')
    raise SystemExit(0 if ok else 1)
for label,old,new in (
 ('native-slot-wrong-word','int i = num >> 5','int i = num >> 4'),
 ('native-slot-wrong-bit','int b = num & 31','int b = num & 15'),
 ('native-slot-off-by-one','num >= MAX_CAREER_SLOTS*8','num >= MAX_CAREER_SLOTS*8-1'),
):
    if native_getter.count(old)!=1:raise RuntimeError(label+': native slot mutation is ambiguous')
    path=out/(label+'.inc');path.write_text(native_getter.replace(old,new)+'\n')
    ok=run(label,source,'native and mock getters address all 256 script-visible slot bits',path) and ok
print('evidence:',out)
if {name:sha(crypto/name) for name in pins}!=pins:raise RuntimeError('pinned Monocypher inputs changed during controls')
raise SystemExit(0 if ok else 1)
