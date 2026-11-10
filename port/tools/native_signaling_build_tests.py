#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Authored build-profile checks; no compiler, game inputs or network."""
import copy
import hashlib
import json
import shlex
from pathlib import Path
import sys
import tempfile
import types
import unittest
from contextlib import ExitStack
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import build
NATIVE_FILES=frozenset(("signaling_provider.cpp","signaling_https.cpp"))
CHAT_FILES=("text_chat.cpp","text_chat_owner.cpp","media_profile.cpp")

class AtMirror(Exception):
    pass

class Fixture(unittest.TestCase):
    def setUp(self):
        scratch=Path(__file__).resolve().parents[2]/"local-data/native-signaling-build-tests"
        scratch.mkdir(parents=True,exist_ok=True)
        temporary=tempfile.TemporaryDirectory(prefix="authored-",dir=scratch)
        self.addCleanup(temporary.cleanup);self.root=Path(temporary.name)
        self.stack=ExitStack();self.addCleanup(self.stack.close)
        self.port=self.root/"port";self.data=self.root/"deps";self.sdk=self.data/"sysroot-sniper"
        for rel in ("network/baseline.cpp","network/ice_game.cpp","network/signaling_provider.cpp","network/signaling_https.cpp",
                    "network/optional/text_chat.cpp","network/optional/text_chat_owner.cpp","network/optional/media_profile.cpp","game/ce_network_chat_ui.cpp"):
            p=self.port/rel;p.parent.mkdir(parents=True,exist_ok=True);p.write_text("// authored fixture\n")
        (self.port/"aliases.tsv").write_text("")
        self.library=self.sdk/"usr/lib/i386-linux-gnu/libcurl-gnutls.so"
        self.library.parent.mkdir(parents=True);self.library.write_bytes(b"authored library\n")
        self.win=self.data/"llvm-mingw/i686-w64-mingw32/lib/libwinhttp.a"
        self.win.parent.mkdir(parents=True);self.win.write_bytes(b"authored import library\n")
        self.mono=self.data/"src/monocypher-4.0.2/src";self.mono.mkdir(parents=True)
        (self.mono/"monocypher.c").write_text("int authored_crypto;\n")
        self.ice=self.root/"ice"
        for rel,data in (("lib/libjuice.a",b"authored archive\n"),("include/juice/juice.h",b"// authored interface\n"),
                         ("licenses/MPL-2.0.txt",b"authored licence\n"),
                         (".bea-ice-build.json",json.dumps({"source_patches":[],"source_tree_sha256":"cd"*32}).encode())):
            p=self.ice/rel;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(data)
        values={"PORT":self.port,"BUILD":self.root/"build","PUBLIC":True,"WITH_NETWORK":True,"WITH_ICE":False,
                "WITH_NATIVE_SIGNALING":False,"WITH_TEXT_CHAT":False,"SYSTEM_CLANG":False,"TEST_AUTOMATION":False,
                "NETWORK_SOURCE_ID":"ab"*32,"CE_DEFINES":[],"TARGETS":copy.deepcopy(build.TARGETS)}
        for k,v in values.items():self.stack.enter_context(patch.object(build,k,v))
        for k,v in {"DATA":self.data,"SRC":self.data/"src","SYSROOT":self.sdk,"SYSROOT_NAME":"sniper"}.items():
            self.stack.enter_context(patch.object(build.deps,k,v))
        self.stack.enter_context(patch.object(build.ice_deps,"prefix",return_value=self.ice))
        self.stack.enter_context(patch.object(build.ice_deps,"require",return_value=self.ice/"lib/libjuice.a"))
        self.stack.enter_context(patch.object(build,"vorbis_lib",return_value=""))
        self.stack.enter_context(patch.object(build.deps,"require_ffmpeg_shared",return_value=[]))

    def parse(self,flags):
        with patch.object(sys,"argv",["build.py",*flags]),patch.object(build,"build_mirror",side_effect=AtMirror):
            with self.assertRaises(AtMirror):build.main()

class CliTests(Fixture):
    def setUp(self):
        super().setUp();build.PUBLIC=False;build.WITH_NETWORK=False

    def test_native_requires_all_three_explicit_flags_before_inputs(self):
        required=("--public","--with-network","--with-ice")
        for mask in range(7):
            flags=[required[i] for i in range(3) if mask&(1<<i)]
            with self.subTest(flags=flags),patch.object(sys,"argv",["build.py","--with-native-signaling",*flags]), \
                 patch.object(build,"build_mirror",side_effect=AssertionError("unexpected mirror entry")) as mirror,patch.object(build,"native_signaling_dependency") as dep:
                with self.assertRaisesRegex(SystemExit,"--with-native-signaling requires"):build.main()
                mirror.assert_not_called();dep.assert_not_called()

    def test_native_and_chat_flags_reach_global_compile_defines(self):
        self.parse(["--public","--with-network","--with-ice","--with-native-signaling","--with-text-chat"])
        self.assertTrue(build.WITH_NATIVE_SIGNALING);self.assertTrue(build.WITH_TEXT_CHAT)
        for flag in ("-DCE_NETWORK_SIGNALING=1","-DCE_NETWORK_TEXT=1","-DCE_NETWORK_ICE=1"):
            self.assertEqual(1,build.CE_DEFINES.count(flag))

    def test_ordinary_ice_does_not_probe_or_enable_https(self):
        with patch.object(build,"native_signaling_dependency") as dep:
            self.parse(["--public","--with-network","--with-ice"]);dep.assert_not_called()
        self.assertFalse(build.WITH_NATIVE_SIGNALING);self.assertFalse(build.WITH_TEXT_CHAT)

    def test_default_public_chat_is_refused(self):
        with patch.object(sys,"argv",["build.py","--public","--with-text-chat"]),patch.object(build,"build_mirror",side_effect=AssertionError("unexpected mirror entry")) as mirror:
            with self.assertRaisesRegex(SystemExit,"requires networking compiled in"):build.main()
            mirror.assert_not_called()

    def test_private_network_chat_is_allowed_without_public_switch(self):
        self.parse(["--with-text-chat"]);self.assertTrue(build.WITH_TEXT_CHAT)
        self.assertIn("-DCE_NETWORK_TEXT=1",build.CE_DEFINES)

class RegistrationTests(Fixture):
    def test_native_sources_are_absent_from_network_and_ordinary_ice(self):
        for ice in (False,True):
            build.WITH_ICE=ice
            for target in ("linux","windows"):
                names=[p.name for p,_,_ in build.port_sources(target)]
                self.assertFalse(NATIVE_FILES&set(names))
                self.assertNotIn("native-signaling-feature.c",names)
                self.assertFalse(set(CHAT_FILES)&set(names));self.assertEqual(1,names.count("sha256.c"))

    def test_native_reuses_port_abi_crypto_and_retained_marker(self):
        build.WITH_ICE=True;build.WITH_NATIVE_SIGNALING=True
        for target in ("linux","windows"):
            rows=build.port_sources(target);table={p.name:(p,rule,flags) for p,rule,flags in rows}
            for name in NATIVE_FILES:
                self.assertEqual("pcxx",table[name][1]);self.assertIn(str(self.mono),table[name][2])
                self.assertIn(str(self.ice/"include"),table[name][2])
            marker,rule,_=table["native-signaling-feature.c"]
            self.assertEqual("pcc",rule);self.assertIn(build.NATIVE_SIGNALING_TAG,marker.read_text())
            self.assertEqual(1,sum(p.name=="sha256.c" for p,_,_ in rows))

    def test_chat_registration_adds_no_libraries(self):
        before={target:build.link_flags(target) for target in ("linux","windows")};build.WITH_TEXT_CHAT=True
        for target in ("linux","windows"):
            selected=build.port_sources(target);rows={p.name:(rule,extra) for p,rule,extra in selected}
            self.assertEqual({"network/optional/"+name for name in CHAT_FILES},
                             {str(p.relative_to(self.port)) for p,_,_ in selected if p.name in CHAT_FILES})
            for name in CHAT_FILES:
                self.assertEqual("pcxx",rows[name][0]);self.assertIn(str(self.mono),rows[name][1]);self.assertIn(str(self.port/"network"),rows[name][1])
            self.assertEqual(before[target],build.link_flags(target))
            self.assertFalse(NATIVE_FILES&set(rows))

    def test_default_public_still_excludes_network_ui(self):
        build.WITH_NETWORK=False
        self.assertFalse(any(p.name=="ce_network_chat_ui.cpp" for p,_,_ in build.port_sources("linux")))

    def test_backend_link_and_retained_symbol_are_target_specific(self):
        build.WITH_ICE=True;build.WITH_NATIVE_SIGNALING=True
        for target,library,prefix in (("linux",self.library,""),("windows",self.win,"_")):
            flags=build.link_flags(target)
            tokens=shlex.split(flags)
            self.assertIn(str(library),tokens)
            self.assertNotIn("-lcurl-gnutls",tokens);self.assertNotIn("-lwinhttp",tokens)
            self.assertIn("-Wl,--undefined="+prefix+"ce_network_native_signaling_feature_marker",flags)
            self.assertEqual(library,build.native_signaling_dependency(target));self.assertNotIn("-lcurl ",flags)

    def test_exact_backend_path_is_one_shell_argument_with_spaces(self):
        build.WITH_ICE=True;build.WITH_NATIVE_SIGNALING=True
        for target in ("linux","windows"):
            expected=self.root/"approved cache"/("curl-gnutls.so" if target=="linux" else "winhttp.a")
            with patch.object(build,"native_signaling_dependency",return_value=expected):
                flags=build.link_flags(target)
            self.assertIn(shlex.quote(str(expected)),flags);self.assertIn(str(expected),shlex.split(flags))

    def test_disabled_link_never_probes_https(self):
        with patch.object(build,"native_signaling_dependency") as dep:
            for target in ("linux","windows"):
                flags=build.link_flags(target);self.assertNotIn("-lcurl",flags);self.assertNotIn("-lwinhttp",flags)
            dep.assert_not_called()

    def test_absent_or_escaping_sdk_library_cannot_fall_back_to_host(self):
        self.library.unlink()
        with self.assertRaisesRegex(SystemExit,"pinned HTTPS dependency"):build.native_signaling_dependency("linux")
        outside=self.root/"unapproved-curl.so";outside.write_bytes(b"authored outsider\n");self.library.symlink_to(outside)
        with self.assertRaisesRegex(SystemExit,"pinned HTTPS dependency"):build.native_signaling_dependency("linux")
        with patch.object(build.deps,"SYSROOT_NAME","host"):
            with self.assertRaisesRegex(SystemExit,"pinned sniper SDK"):build.native_signaling_dependency("linux")

    def test_ninja_binds_https_input_and_shared_feature_defines(self):
        build.WITH_ICE=True;build.WITH_NATIVE_SIGNALING=True;build.CE_DEFINES=["-DCE_NETWORK_SIGNALING=1","-DCE_NETWORK_TEXT=1"]
        for target in ("linux","windows"):
            text=build.ninja_file(target,[],False,True).read_text()
            self.assertIn("-DCE_NETWORK_SIGNALING=1",text);self.assertIn("-DCE_NETWORK_TEXT=1",text)
            link=[line for line in text.splitlines() if ": link " in line][0]
            self.assertIn(str(build.native_signaling_dependency(target)),link.split(" | ")[1])
            self.assertEqual(1,text.count("/port/updater/sha256.c.o: pcc "))

class ReceiptTests(Fixture):
    def binary(self,target,native):
        out=build.BUILD/target;out.mkdir(parents=True,exist_ok=True)
        game,launcher=("bea","onslaught-ce-launch") if target=="linux" else ("bea.exe","onslaught-ce.exe")
        data=b"authored binary\0"+build.ICE_BINARY_TAG.encode()+b"\0"
        if native:data+=build.NATIVE_SIGNALING_TAG.encode()+b"\0"
        (out/game).write_bytes(data);(out/launcher).write_bytes(b"authored launcher\n");return out

    def test_receipt_object_is_exact_optional_and_preserves_profile(self):
        build.WITH_ICE=True
        for target,backend in (("linux","libcurl-gnutls"),("windows","winhttp")):
            for native in (False,True):
                build.WITH_NATIVE_SIGNALING=native;out=self.binary(target,native);build.write_ice_link_receipt(target)
                saved=json.loads((out/".bea-ice-link.json").read_text())
                self.assertEqual(2,saved["schema"]);self.assertEqual("public-network-ice",saved["profile"])
                self.assertEqual("ab"*32,saved["source_id"]);self.assertNotIn(str(self.root),json.dumps(saved))
                if native:self.assertEqual({"enabled":True,"marker":"CE-NATIVE-SIGNALING-V1","https_backend":backend},saved["native_signaling"])
                else:self.assertNotIn("native_signaling",saved)
                game=out/saved["binaries"]["game"]["name"]
                self.assertEqual(hashlib.sha256(game.read_bytes()).hexdigest(),saved["binaries"]["game"]["sha256"])

    def test_wrong_marker_profile_cannot_be_attested(self):
        build.WITH_ICE=True
        for enabled,observed in ((True,False),(False,True)):
            build.WITH_NATIVE_SIGNALING=enabled;self.binary("linux",observed)
            with self.assertRaisesRegex(SystemExit,"marker does not match"):build.write_ice_link_receipt("linux")

class StartupTests(unittest.TestCase):
    def test_initialization_is_opt_in_before_workers_and_preserves_exit(self):
        source=(Path(__file__).resolve().parents[1]/"platform/linux/main.cpp").read_text()
        self.assertEqual(1,source.count("CeInitializeSignalHttps()"))
        self.assertLess(source.index("CeInitializeSignalHttps()"),source.index("PortAudioGuardCheck();"))
        self.assertLess(source.index("CeInitializeSignalHttps()"),source.index("if (!SDL_Init("))
        self.assertNotIn("CeShutdownSignalHttps",source);self.assertIn("\t_exit(rc);",source)
        off=build.network_view(source,off=True)
        self.assertNotIn("CeInitializeSignalHttps",off);self.assertNotIn("signaling_https.h",off)

def controls():
    """In-memory faults; every affected fixture blocks before mirror/compiler IO."""
    source=Path(build.__file__).read_text()
    faults=(
        ("prerequisites","if a.with_native_signaling and not (a.public and a.with_network and a.with_ice):","if False:",CliTests,"test_native_requires_all_three_explicit_flags_before_inputs"),
        ("default source exclusion","if WITH_NATIVE_SIGNALING or s.name not in NATIVE_SIGNALING_SOURCES]","if True]",RegistrationTests,"test_native_sources_are_absent_from_network_and_ordinary_ice"),
        ("link scheduling","link_inputs.append(native_signaling_dependency(target))","pass",RegistrationTests,"test_ninja_binds_https_input_and_shared_feature_defines"),
        ("receipt marker binding","if native_marker != WITH_NATIVE_SIGNALING:","if False:",ReceiptTests,"test_wrong_marker_profile_cannot_be_attested"),
        ("global define",'CE_DEFINES.append("-DCE_NETWORK_SIGNALING=1")','CE_DEFINES.append("-DAUTHORED_CONTROL=1")',CliTests,"test_native_and_chat_flags_reach_global_compile_defines"),
        ("linker marker root",'+ "ce_network_native_signaling_feature_marker "','+ ""',RegistrationTests,"test_backend_link_and_retained_symbol_are_target_specific"),
        ("chat prerequisite","if not network_compiled_in():","if False:",CliTests,"test_default_public_chat_is_refused"),
        ("exact HTTPS dependency",'network += shlex.quote(str(dependency)) + " "',
         'network += "-L" + shlex.quote(str(dependency.parent)) + (" -lcurl-gnutls " if target == "linux" else " -lwinhttp ")',
         RegistrationTests,"test_backend_link_and_retained_symbol_are_target_specific"),
        ("pinned dependency","if not path.is_file() or not path.resolve().is_relative_to(base.resolve()):","if False:",RegistrationTests,"test_absent_or_escaping_sdk_library_cannot_fall_back_to_host"),
    )
    for name,before,after,case,method in faults:
        if source.count(before)!=1:raise RuntimeError("control anchor is ambiguous")
        module=types.ModuleType("authored_build_control");module.__file__=build.__file__
        exec(compile(source.replace(before,after),"authored-build-control","exec"),module.__dict__)
        with patch.object(sys.modules[__name__],"build",module):
            result=unittest.TestResult();case(method).run(result)
        if result.wasSuccessful():raise RuntimeError("focused control was not detected")
        print("Detected",name)
    print("Detected",len(faults),"authored build-registration controls")

if __name__=="__main__":
    if sys.argv[1:]==["--controls"]:controls()
    else:unittest.main()
