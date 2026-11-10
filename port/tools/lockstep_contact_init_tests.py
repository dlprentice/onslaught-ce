#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise report initialization and the unchanged support consumer with authored inputs.

The actual report-creation snippet and CUnit::Hit body are compiled. Authored passive/static reports
represent counted slots a producer never wrote; later writes model a freshly produced but uncounted
plane. These controls do not establish the frequency or full behavior of any real collision.
"""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'port'))
import build
PATCH='0184-lockstep-contact-report-init.patch'
DRIVER=ROOT/'port/tools/tests/lockstep_contact_init_driver.cpp.in'

def body(text,anchor):
    start=text.find('{',text.index(anchor));depth=0
    for i in range(start,len(text)):
        depth+=(text[i]=='{')-(text[i]=='}')
        if depth==0:return text[start:i+1]
    raise ValueError('unclosed function')

def init(text):
    start=text.index('\tCCollisionReport report;\n',text.index('void CCollisionSeekingThing::Unknown6('))
    return text[start:text.index('\treport.mUnknown00 = 0;',start)]

def expected(compiled):
    rows=[]
    for network in (0,1):
        clears=bool(network and compiled)
        for scenario in range(6):
            support=(0 if clears else 1) if scenario==0 else (0 if scenario==2 else 1)
            rows.append((network,scenario,int(clears),int(not clears),1,1,support))
    return rows

def evaluate(initializer,hit,normal,out):
    source=DRIVER.read_text().replace('@REPORT_INIT@',initializer).replace('@HIT_BODY@',hit).replace('@NORMAL_BODY@',normal)
    file=out/'fixture.cpp';exe=out/'fixture';file.write_text(source)
    subprocess.run([os.environ.get('CXX','g++'),'-std=c++17','-m32','-O2','-mfpmath=387','-mno-sse',str(file),'-o',str(exe)],
                   check=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    return [tuple(map(int,line.split())) for line in subprocess.check_output([str(exe)],text=True).splitlines()]

class ContactInitTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.on=build.patched_text(['collisionseekingthing.cpp'],network=True)['collisionseekingthing.cpp']
        cls.off=build.patched_text(['collisionseekingthing.cpp'],network=False)['collisionseekingthing.cpp']
        cls.before=build.patched_text(['collisionseekingthing.cpp'],skip=(PATCH,),network=False)['collisionseekingthing.cpp']
        cls.consumer=build.patched_text(['Unit.cpp'],network=True)['Unit.cpp']
        cls.consumer_before=build.patched_text(['Unit.cpp'],skip=(PATCH,),network=True)['Unit.cpp']
        cls.normal=body(build.patched_text(['maths.h'],network=True)['maths.h'],'void\tNormaliseXZY()')
        cls.hit=body(cls.consumer,'void CUnit::Hit(CThing *other_thing, CCollisionReport *report)')

    def rows(self,initializer):
        with tempfile.TemporaryDirectory(prefix='ce-contact-init-',dir=ROOT/'local-data') as d:
            return evaluate(initializer,self.hit,self.normal,Path(d))

    def test_compiled_out_source_and_consumer_unchanged(self):
        self.assertEqual(self.off,self.before)
        self.assertEqual(self.consumer,self.consumer_before)
        self.assertEqual(self.rows(init(self.off)),expected(False))

    def test_session_initializes_and_later_producer_writes_survive(self):
        self.assertEqual(self.rows(init(self.on)),expected(True))

    def test_mutants_caught(self):
        initializer=init(self.on)
        variants=(('disabled','CE_LockstepSharing()','false'),
                  ('always','CE_LockstepSharing()','true'),
                  ('partial','i < 6','i < 3'),
                  ('no_z','report.mNormals[i].Z = 0.f;','(void)i;'))
        for label,old,new in variants:
            with self.subTest(label=label):
                self.assertEqual(initializer.count(old),1)
                self.assertNotEqual(self.rows(initializer.replace(old,new,1)),expected(True))

if __name__=='__main__':unittest.main(verbosity=2)
