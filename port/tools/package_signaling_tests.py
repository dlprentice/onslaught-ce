#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Authored, offline controls for native-signaling source and notice inputs."""
import contextlib
import copy
import io
import json
from pathlib import Path
import tarfile
import tempfile
import unittest
from unittest.mock import patch

import package_signaling_inputs as inputs


class Response(io.BytesIO):
    def __init__(self, data, url):
        super().__init__(data)
        self.url = url

    def geturl(self):
        return self.url


class SignalingInputsTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.cache = self.root / "sources"
        self.notices = self.root / "notices"
        self.downloads = self.root / "downloads"
        self.sdk = self.root / "sdk"
        self.pin_file = self.root / "pins.json"
        self.source = b"authored source archive fixture"
        self.library = b"authored shared-library fixture"
        self.notice = "Authored notice fixture.\n"
        self.snapshot = "https://repo.steampowered.com/steamrt-images-sniper/snapshots/authored"
        self.lock = dict(name="sysroot-sniper", version="authored", sha="1" * 64,
                         url=self.snapshot + "/sdk.tar.gz", file=self.downloads / "sdk.tar.gz",
                         licence="GPL-3.0-only")
        self.source_row = dict(filename="tiny_1.orig.tar.gz", bytes=len(self.source),
                               sha256=inputs.digest(self.source),
                               url=self.snapshot + "/sources/tiny_1.orig.tar.gz")
        self.library_row = dict(soname="libtiny.so.1", path="usr/lib/i386-linux-gnu/libtiny.so.1.0",
                                bytes=len(self.library), sha256=inputs.digest(self.library),
                                needed=["libc.so.6"])
        self.pins = dict(schema=1,
                         sdk=dict(name="sysroot-sniper", version="authored", sha256=self.lock["sha"],
                                  url=self.lock["url"], file=self.lock["file"].name,
                                  licence=self.lock["licence"], bytes=0),
                         source_metadata=dict(url=self.snapshot + "/sources.deb822.gz", sha256="2" * 64),
                         source_packages=[dict(package="tiny", version="1", files=[self.source_row])],
                         libraries=[self.library_row],
                         notices=[dict(package="libtiny", version="1", sdk_path="usr/share/doc/libtiny/copyright",
                                       archive_member="usr/share/doc/libtiny/copyright",
                                       licence_reference="usr/share/doc/libtiny/copyright",
                                       output="licenses/native-signaling/libtiny-copyright.txt",
                                       bytes=len(self.notice.encode()), sha256=inputs.digest(self.notice.encode()))])
        self.stack = contextlib.ExitStack()
        self.addCleanup(self.stack.close)
        self.stack.enter_context(patch.multiple(inputs, PINS=self.pin_file, SOURCES=self.cache,
                                               NOTICES=self.notices))
        self.stack.enter_context(patch.object(inputs.deps, "SYSROOT", self.sdk))
        self.stack.enter_context(patch.object(inputs.deps, "DOWNLOADS", self.downloads))
        self.stack.enter_context(patch.object(inputs.deps, "lock", return_value={"sysroot-sniper": self.lock}))
        self.stack.enter_context(patch.object(inputs, "REVIEWED_INPUTS", ""))
        self.write_sdk({self.pins["notices"][0]["archive_member"]: self.notice.encode()})
        self.source_path = self.cache / "tiny" / self.source_row["filename"]
        self.source_path.parent.mkdir(parents=True)
        self.source_path.write_bytes(self.source)
        self.library_path = self.sdk / self.library_row["path"]
        self.library_path.parent.mkdir(parents=True)
        self.library_path.write_bytes(self.library)

    def write_sdk(self, members):
        output = io.BytesIO()
        with tarfile.open(fileobj=output, mode="w:gz") as archive:
            for name, data in members.items():
                member = tarfile.TarInfo(name)
                member.size = len(data)
                archive.addfile(member, io.BytesIO(data))
        raw = output.getvalue()
        self.downloads.mkdir(exist_ok=True)
        self.lock["file"].write_bytes(raw)
        self.lock["sha"] = inputs.digest(raw)
        self.pins["sdk"].update(sha256=self.lock["sha"], bytes=len(raw))
        self.write_pins()

    def write_pins(self, reviewed=True):
        raw = (json.dumps(self.pins, indent=2) + "\n").encode()
        self.pin_file.write_bytes(raw)
        if reviewed:
            inputs.REVIEWED_INPUTS = inputs.digest(raw)

    def test_offline_snapshot_contains_exact_source_notices_and_target_libraries(self):
        with patch.object(inputs.urllib.request, "urlopen", side_effect=AssertionError("offline preparation")):
            payloads = inputs.prepare()
        linux = payloads["linux"]
        self.assertEqual(self.library, linux["libraries"]["libtiny.so.1"])
        self.assertEqual(self.source, linux["files"]["source/native-signaling/tiny/tiny_1.orig.tar.gz"])
        self.assertEqual(self.notice.encode(), linux["files"]["licenses/native-signaling/libtiny-copyright.txt"])
        self.assertIn(b"--with-native-signaling", linux["files"]["NATIVE-SIGNALING-SOURCE-NOTICE.txt"])
        self.assertIn("source/native-signaling/SOURCES.json", linux["files"])
        self.assertFalse(payloads["windows"]["libraries"])
        self.assertIn(b"WinHTTP", payloads["windows"]["notice"])
        self.assertFalse(any(name.startswith("source/") for name in payloads["windows"]["files"]))

    def test_snapshot_bytes_do_not_follow_later_cache_changes(self):
        payload = inputs.prepare()["linux"]
        self.source_path.write_bytes(b"changed")
        self.library_path.write_bytes(b"changed")
        self.assertEqual(self.source, payload["files"]["source/native-signaling/tiny/tiny_1.orig.tar.gz"])
        self.assertEqual(self.library, payload["libraries"]["libtiny.so.1"])
        with self.assertRaises(ValueError):
            inputs.prepare()

    def test_missing_sdk_is_refused_without_network_or_partial_notices(self):
        self.lock["file"].unlink()
        with patch.object(inputs.urllib.request, "urlopen", side_effect=AssertionError("offline notices")):
            with self.assertRaises(FileNotFoundError):
                inputs.prepare()
        self.assertFalse(self.notices.exists())

    def test_changed_sdk_hash_is_refused_before_notice_materialization(self):
        archive = self.lock["file"]
        data = archive.read_bytes()
        archive.write_bytes(bytes([data[0] ^ 1]) + data[1:])
        with self.assertRaisesRegex(ValueError, "SDK archive has the wrong hash"):
            inputs.prepare()
        self.assertFalse(self.notices.exists())

    def test_missing_sdk_member_and_changed_notice_body_are_refused(self):
        row = self.pins["notices"][0]
        for members in ({"usr/share/doc/unrelated/copyright": self.notice.encode()},
                        {row["archive_member"]: b"x" * row["bytes"]}):
            with self.subTest(missing=row["archive_member"] not in members):
                self.write_sdk(members)
                with self.assertRaises(ValueError):
                    inputs.prepare()
                self.assertFalse(self.notices.exists())

    def test_verified_notice_cache_is_offline_and_corruption_is_preserved(self):
        row = self.pins["notices"][0]
        inputs.prepare()
        self.lock["file"].unlink()
        with patch.object(inputs, "sdk_notices", side_effect=AssertionError("cache needs no archive")):
            payload = inputs.prepare()
            self.assertEqual(self.notice.encode(), payload["linux"]["files"][row["output"]])
            notice_path = self.notices / row["output"]
            notice_path.write_bytes(b"unrelated authored notice")
            with self.assertRaises(ValueError):
                inputs.prepare()
            self.assertEqual(b"unrelated authored notice", notice_path.read_bytes())

    def test_missing_or_changed_source_and_library_inputs_are_refused_offline(self):
        for path, original in ((self.source_path, self.source), (self.library_path, self.library)):
            for replacement in (None, b"bad", b"x" * len(original)):
                with self.subTest(path=path.name, missing=replacement is None):
                    if replacement is None:
                        path.unlink()
                    else:
                        path.write_bytes(replacement)
                    with patch.object(inputs.urllib.request, "urlopen", side_effect=AssertionError("no implicit download")):
                        with self.assertRaises((ValueError, OSError)):
                            inputs.prepare()
                    path.write_bytes(original)

    def test_unreviewed_manifest_and_wrong_sdk_are_refused(self):
        self.pin_file.write_bytes(self.pin_file.read_bytes() + b" ")
        with self.assertRaises(ValueError):
            inputs.load_manifest()
        self.write_pins()
        self.lock["sha"] = "3" * 64
        with self.assertRaises(ValueError):
            inputs.load_manifest()

    def test_rehashed_invalid_paths_urls_closure_and_notices_are_refused(self):
        original = copy.deepcopy(self.pins)
        changes = (
            lambda: self.source_row.update(filename="../escape.tar.gz"),
            lambda: self.source_row.update(url="https://elsewhere.invalid/source.tar.gz"),
            lambda: self.source_row.update(bytes=inputs.MAX_SOURCE_BYTES + 1),
            lambda: self.library_row.update(path="../outside"),
            lambda: self.library_row.update(needed=["libmissing.so.1"]),
            lambda: self.pins["notices"][0].update(text="changed notice"),
            lambda: self.pins.update(notices=[]),
        )
        for change in changes:
            self.pins = copy.deepcopy(original)
            self.source_row = self.pins["source_packages"][0]["files"][0]
            self.library_row = self.pins["libraries"][0]
            change()
            self.write_pins()
            with self.subTest(change=changes.index(change)):
                with self.assertRaises(ValueError):
                    inputs.load_manifest()

    def test_materialize_is_explicit_and_existing_verified_inputs_need_no_network(self):
        self.source_path.unlink()
        with patch.object(inputs.urllib.request, "urlopen", return_value=Response(self.source, self.source_row["url"])) as request:
            self.assertEqual((1, len(self.source)), inputs.materialize())
            request.assert_called_once()
        self.assertEqual(self.source, self.source_path.read_bytes())
        with patch.object(inputs.urllib.request, "urlopen", side_effect=AssertionError("cache is complete")):
            self.assertEqual((1, len(self.source)), inputs.materialize())

    def test_bad_downloads_and_redirects_never_install_an_unverified_file(self):
        for data, url in ((b"x" * len(self.source), self.source_row["url"]),
                          (self.source + b"extra", self.source_row["url"]),
                          (self.source, "http://repo.steampowered.com/source"),
                          (self.source, "https://elsewhere.invalid/source")):
            with self.subTest(url=url):
                self.source_path.unlink(missing_ok=True)
                with patch.object(inputs.urllib.request, "urlopen", return_value=Response(data, url)):
                    with self.assertRaises(ValueError):
                        inputs.materialize()
                self.assertFalse(self.source_path.exists())
                self.assertFalse(list(self.source_path.parent.glob("download-*")))

    def test_existing_unverified_file_and_linked_cache_are_preserved(self):
        self.source_path.write_bytes(b"unrelated authored data")
        with patch.object(inputs.urllib.request, "urlopen", side_effect=AssertionError("must not replace")):
            with self.assertRaises(ValueError):
                inputs.materialize()
        self.assertEqual(b"unrelated authored data", self.source_path.read_bytes())
        outside = self.root / "outside"
        outside.mkdir()
        linked = self.root / "linked"
        linked.symlink_to(outside, target_is_directory=True)
        with patch.object(inputs, "SOURCES", linked):
            with self.assertRaises(ValueError):
                inputs.materialize()
        self.assertFalse(list(outside.iterdir()))

    def test_receipts_require_exact_typed_opt_in_and_target_backend(self):
        for target in ("linux", "windows"):
            correct = inputs.receipt_fields(target)
            inputs.check_receipt(correct, target)
            for value in (None, {}, dict(correct, enabled=1), dict(correct, enabled=False),
                          dict(correct, marker="different"), dict(correct, https_backend="different"),
                          dict(correct, extra=True)):
                with self.subTest(target=target, value_type=type(value).__name__):
                    with self.assertRaises(ValueError):
                        inputs.check_receipt(value, target)


if __name__ == "__main__":
    unittest.main()
