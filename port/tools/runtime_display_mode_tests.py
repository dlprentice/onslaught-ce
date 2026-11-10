#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Actual deferred menu/settings and SDL rollback code with authored boundaries; no desktop or game process."""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import resource
import subprocess
import tempfile
import unittest
import ce_display_mode_tests as existing
import public_d3dapp_tests

ROOT=Path(__file__).resolve().parents[2]
PORT=ROOT/'port'

def prepare_window(out, source=None, pump_source=None):
    text=(source or PORT/'platform/common/d3d9hooks.cpp').read_text()
    start=text.index('struct CeDisplayWindowState\n')
    end=text.index('unsigned ceDisplayInputLogged;',start)+len('unsigned ceDisplayInputLogged;')
    pieces=[text[start:end]]
    for name in ('PortCeDisplayWindowBegin','PortCeDisplayWindowReady','PortCeDisplayInputReady','PortCeDisplayWindowEnd'):
        pieces.append(public_d3dapp_tests.function(text,rf'^extern "C" int {name}\('))
    pieces.append(public_d3dapp_tests.function(text,r'^extern "C" void PortCeRetryDisplayInput\('))
    pieces.append(public_d3dapp_tests.function((PORT/'platform/common/dinput.cpp').read_text(),r'^void PortMouseKeepRelative\('))
    pump=(pump_source or PORT/'platform/common/window.cpp').read_text()
    begin=pump.index('\tPortMouseKeepRelative();')
    end=pump.index('\n#endif',begin)+len('\n#endif')
    pieces.append('void FixturePump() {\n'+pump[begin:end]+'\n}')
    (out/'runtime_display_window.inc').write_text('\n'.join(pieces)+'\n')
    existing.run(['clang++','-std=c++17','-O1','-g','-DCE_VERSION=1','-DBEA_PUBLIC_GRAPHICS_HOST=1','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I',out,
                  PORT/'tools/tests/runtime_display_window_driver.cpp','-o',out/'window'])

class RuntimeDisplayTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        resource.setrlimit(resource.RLIMIT_CORE,(0,0))
        owner=ROOT/'local-data/runtime-display-mode-tests';owner.mkdir(exist_ok=True,parents=True)
        cls.out=Path(tempfile.mkdtemp(prefix='run-',dir=owner))
        existing.prepare(cls.out);existing.build_fixtures(cls.out,targets=('runtime',));prepare_window(cls.out)
    def setUp(self):
        self.user=Path(tempfile.mkdtemp(prefix='user-',dir=self.out));self.ini=self.user/'ce.ini'
        self.env={k:v for k,v in os.environ.items() if not k.startswith(('BEA_PORT_','BEA_USER_DIR','SDL_'))}
        self.env.update(BEA_USER_DIR=str(self.user),BEA_REPORT_NO_OPEN='1',BROWSER='/bin/true')
    def call(self,binary,*args,env=None):return existing.run([self.out/binary,*args],env=env or self.env)
    def test_native_menu_apply_is_deferred_and_persists_both_directions(self):
        for before in (0,1):
            with self.subTest(before=before):
                self.ini.write_bytes(b'unknown=retained\nwindowed='+str(1-before).encode()+b'\n')
                self.assertEqual(self.call('runtime','runtime-menu',str(before),'none'),'runtime-menu pass')
                self.assertEqual(self.ini.read_bytes(),b'unknown=retained\nwindowed='+str(1-before).encode()+b'\n')
    def test_refused_window_or_device_keeps_current_mode_and_file(self):
        for before in (0,1):
            for fault in ('window','window-rollback','device'):
                with self.subTest(before=before,fault=fault):
                    data=b'unknown=retained\nwindowed='+str(before).encode()+b'\n';self.ini.write_bytes(data)
                    self.assertEqual(self.call('runtime','runtime-menu',str(before),fault),'runtime-menu pass')
                    self.assertEqual(self.ini.read_bytes(),data)
    def test_settings_or_input_refusal_keeps_live_mode_and_next_launch_preference(self):
        for before in (0,1):
            for fault in ('short','rename','input'):
                with self.subTest(before=before,fault=fault):
                    data=b'unknown=retained\nwindowed='+str(before).encode()+b'\n';self.ini.write_bytes(data)
                    self.assertEqual(self.call('runtime','runtime-menu',str(before),fault),'runtime-menu pass')
                    self.assertEqual(self.ini.read_bytes(),data)
                    self.assertEqual(self.call('runtime','runtime-next-launch',str(before)),'runtime-next-launch pass')
    def test_recording_probe_and_clock_fences_never_apply(self):
        data=b'windowed=1\n';self.ini.write_bytes(data)
        for key in ('BEA_PORT_CLOCK','BEA_PORT_TAPE','BEA_PORT_PROBES','BEA_PORT_RECORD'):
            with self.subTest(key=key):
                env=dict(self.env,BEA_PORT_CE_LAYER='1',**{key:'fixture'})
                self.assertEqual(self.call('runtime','runtime-fence',env=env),'runtime-fence pass')
                self.assertEqual(self.ini.read_bytes(),data)
    def test_same_mode_request_does_not_save_while_input_repair_is_pending(self):
        data=b'windowed=0\nunknown=retained\n';self.ini.write_bytes(data)
        self.assertEqual(self.call('runtime','runtime-pending-same-mode'),'runtime-pending-same-mode pass')
        self.assertEqual(self.ini.read_bytes(),data)
        self.assertEqual(self.call('runtime','runtime-next-launch','0'),'runtime-next-launch pass')
    def test_ready_same_mode_request_saves_without_graphics_work(self):
        for mode in (0,1):
            with self.subTest(mode=mode):
                self.ini.write_bytes(b'windowed='+str(1-mode).encode()+b'\nunknown=retained\n')
                self.assertEqual(self.call('runtime','runtime-ready-same-mode',str(mode)),'runtime-ready-same-mode pass')
                self.assertEqual(self.ini.read_bytes(),b'windowed='+str(mode).encode()+b'\nunknown=retained\n')
                self.assertEqual(self.call('runtime','runtime-next-launch',str(mode)),'runtime-next-launch pass')
    def test_second_switch_does_not_hide_pending_input_repair(self):
        data=b'windowed=1\nunknown=retained\n';self.ini.write_bytes(data)
        self.assertEqual(self.call('runtime','runtime-pending-changed-mode'),'runtime-pending-changed-mode pass')
        self.assertEqual(self.ini.read_bytes(),data)
        self.assertEqual(self.call('runtime','runtime-next-launch','1'),'runtime-next-launch pass')
    def test_failed_restoration_is_reported_and_does_not_save(self):
        for fault in ('backend-rollback',):
            with self.subTest(fault=fault):
                data=b'windowed=0\nunknown=retained\n';self.ini.write_bytes(data)
                self.assertEqual(self.call('runtime','runtime-restore-failure',fault),'runtime-restore-failure pass')
                self.assertEqual(self.ini.read_bytes(),data)
    def test_actual_sdl_transaction_boundaries(self):
        for name in ('windowed','fullscreen','rollback','mode-copy','nested','replacement-id','replacement-pointer',
                     'prepare-fullscreen','prepare-size','prepare-sync','prepare-rollback-fails','relative-commit-fails',
                     'position-unavailable','position-compositor','invalid','input-enable','input-disable',
                     'input-repeated-failure','input-owner-release','input-owner-acquire','input-replaced-id',
                     'input-replaced-pointer','input-cancel','input-minimized','input-zero-clock','input-clock-back'):
            with self.subTest(name=name):self.assertEqual(self.call('window',name),'window transaction pass')
    def test_causal_controls(self):
        cases=[
            ('queue-discarded','video','pendingMode = value;','pendingMode = -1;',('runtime-menu','0','none'),False),
            ('saved-before-device','video','pendingMode = value;','CE_DisplayModeStore(value); pendingMode = value;',('runtime-menu','0','device'),True),
            ('second-graphics-transaction','video','const bool saved = inputReady && CE_DisplayModeStore(requested);','const bool saved = inputReady && CE_DisplayModeStore(requested); if (!saved) LT.ForceWindowed(previous);',('runtime-menu','0','short'),False),
            ('failed-host-rollback-hidden','video','LT.m_bReady && windowRestored','windowRestored',('runtime-restore-failure','backend-rollback'),False),
            ('input-refusal-ignored','video','PortCeDisplayWindowReady() != 0','true',('runtime-menu','0','input'),False),
            ('input-refusal-persisted','video','const bool saved = inputReady && CE_DisplayModeStore(requested);','const bool saved = CE_DisplayModeStore(requested);',('runtime-menu','0','input'),True),
            ('input-retry-not-retained','window','ceDisplayInputRetry = ceDisplayWindow;','/* injected missing retry */',('input-disable',),False),
            ('stale-input-owner','window','const bool relative = PortDinputMouseAcquired();','const bool relative = false;',('input-owner-acquire',),False),
            ('input-retry-rate-limit-removed','window','if (now >= ceDisplayInputLastTry && now - ceDisplayInputLastTry < 1000) return;','/* injected no backoff */',('input-repeated-failure',),False),
            ('normal-pump-retry-omitted','pump','\tPortCeRetryDisplayInput();','\t/* injected missing pump call */',('input-disable',),False),
            ('same-mode-input-guard-omitted','video','!PortCeDisplayInputReady() ? DisplayInputFailed :','false ? DisplayInputFailed :',('runtime-pending-same-mode',),False),
            ('changed-mode-input-guard-omitted','video',' && PortCeDisplayInputReady() != 0','',('runtime-pending-changed-mode',),False),
            ('window-rollback-omitted','window','if (!commit) ok = ceDisplayWindow.Restore();','if (!commit) ok = true;',('rollback',),False),
            ('pixel-density-ignored','window','(double)width / density','(double)width',('windowed',),False),
            ('replaced-window-id-ignored','window','SDL_GetWindowID(window) == id','true',('replacement-id',),False),
            ('fullscreen-mode-copy-omitted','window','if (mode) saved.mode = *mode;','if (mode) {}',('mode-copy',),False),
            ('mouse-box-rollback-omitted','window','gBoxW = boxWidth; gBoxH = boxHeight; gBoxWindow = boxWindow;','/* injected missing mouse-box restoration */',('rollback',),False),
            ('window-fence-omitted','window','!CE_DisplayModeAvailable() || ceDisplayWindow.active','false || ceDisplayWindow.active',('invalid',),False),
        ]
        results={}
        for label,kind,old,new,args,file_control in cases:
            with self.subTest(mutant=label):
                out=self.out/('mutant-'+label);out.mkdir()
                source=PORT/({'video':'game/ce_video_options.cpp','window':'platform/common/d3d9hooks.cpp','pump':'platform/common/window.cpp'}[kind])
                text=source.read_text();self.assertEqual(text.count(old),1,label)
                mutant=out/'changed.cpp';mutant.write_text(text.replace(old,new,1))
                if kind=='video':
                    existing.prepare(out);existing.build_fixtures(out,{'video':mutant},targets=('runtime',));binary=out/'runtime'
                elif kind=='window':
                    prepare_window(out,mutant);binary=out/'window'
                else:
                    prepare_window(out,pump_source=mutant);binary=out/'window'
                user=out/'user';user.mkdir();data=b'windowed=0\nunknown=retained\n';(user/'ce.ini').write_bytes(data)
                p=subprocess.run([str(binary),*args],env=dict(self.env,BEA_USER_DIR=str(user)),text=True,capture_output=True,timeout=10)
                (out/'stdout.log').write_text(p.stdout);(out/'stderr.log').write_text(p.stderr)
                if file_control:
                    self.assertEqual(p.returncode,0,label);self.assertNotEqual((user/'ce.ini').read_bytes(),data,label)
                    reason='refused device changed the settings file'
                else:
                    self.assertEqual(p.returncode,2,label);self.assertIn('FAIL:',p.stderr,label)
                    reason='named authored boundary assertion failed'
                results[label]={'killed':True,'exit':p.returncode,'reason':reason,'mutant_sha256':hashlib.sha256(mutant.read_bytes()).hexdigest()}
        (self.out/'MUTANTS.private.json').write_text(json.dumps(results,indent=2)+'\n')

if __name__=='__main__':
    unittest.main(verbosity=2)
