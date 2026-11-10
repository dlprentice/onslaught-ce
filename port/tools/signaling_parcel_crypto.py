#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Bounded offline binding to the existing pinned Monocypher AEAD primitive.

The fixture builder supplies a locally verified library and its exact hash.
This is not a library downloader, cloud key manager, or service adapter.
Python objects may retain plaintext copies: this module makes no memory-erasure
or cryptographic-deletion claim. No raw key, parcel or dependency error is logged.
"""
from __future__ import annotations

import ctypes
import hashlib
import os
from pathlib import Path
import re
import stat
from threading import RLock

MAGIC = b"CESPAR02"
KEY_BYTES, NONCE_BYTES, TAG_BYTES = 32, 24, 16
MAX_PLAIN, MAX_AD = 32768, 1024
OVERHEAD = len(MAGIC) + NONCE_BYTES + TAG_BYTES


class Refused(Exception):
    def __init__(self):
        super().__init__("stored parcel refused")


def require(value: bool) -> None:
    if not value:
        raise Refused()


def _buffer(value: bytes):
    return (ctypes.c_uint8 * max(1, len(value))).from_buffer_copy(value or b"\0")


class Monocypher:
    """Load only the explicitly pinned local fixture build, through its open fd."""

    def __init__(self, library: Path, expected_sha256: str):
        fd = -1
        try:
            require(type(expected_sha256) is str and re.fullmatch(r"[0-9a-f]{64}", expected_sha256) is not None)
            fd = os.open(Path(library).absolute(), os.O_RDONLY | os.O_NOFOLLOW | os.O_CLOEXEC)
            info = os.fstat(fd)
            require(stat.S_ISREG(info.st_mode) and info.st_uid == os.geteuid() and info.st_nlink == 1)
            require(0 < info.st_size <= 4 * 1024 * 1024 and stat.S_IMODE(info.st_mode) & 0o022 == 0)
            digest = hashlib.sha256()
            while chunk := os.read(fd, 65536):
                digest.update(chunk)
            require(digest.hexdigest() == expected_sha256)
            self._lib = ctypes.CDLL("/proc/self/fd/" + str(fd))
            byte_pointer = ctypes.POINTER(ctypes.c_uint8)
            args = [byte_pointer] * 5 + [ctypes.c_size_t, byte_pointer, ctypes.c_size_t]
            self._lib.crypto_aead_lock.argtypes = args
            self._lib.crypto_aead_lock.restype = None
            self._lib.crypto_aead_unlock.argtypes = args
            self._lib.crypto_aead_unlock.restype = ctypes.c_int
            self._lib.crypto_wipe.argtypes = [ctypes.c_void_p, ctypes.c_size_t]
            self._lib.crypto_wipe.restype = None
        except Exception:
            raise Refused() from None
        finally:
            if fd >= 0:
                os.close(fd)

    def wipe(self, value) -> None:
        self._lib.crypto_wipe(value, ctypes.sizeof(value))


class ParcelCipher:
    """Owned by one store; its key must be separate from identity and CE keys.

    XChaCha20-Poly1305 uses an OS-generated 192-bit random nonce for every seal.
    No nonce counter can roll back with a failed SQLite transaction. Sampling
    checks do not prove impossible collisions; uniqueness relies on OS entropy.
    """

    def __init__(self, primitive: Monocypher, synthetic_key: bytes):
        require(isinstance(primitive, Monocypher))
        require(type(synthetic_key) is bytes and len(synthetic_key) == KEY_BYTES)
        self._primitive, self._lock = primitive, RLock()
        self._key = _buffer(synthetic_key)
        self.identity = hashlib.sha256(b"CE parcel key identity v2\0" + synthetic_key).hexdigest()

    def close(self) -> None:
        with self._lock:
            if self._key is not None:
                self._primitive.wipe(self._key)
                self._key = None

    def matches_key(self, key: bytes) -> bool:
        require(type(key) is bytes and len(key) == KEY_BYTES)
        return self.identity == hashlib.sha256(b"CE parcel key identity v2\0" + key).hexdigest()

    def seal(self, plaintext: bytes, associated: bytes) -> bytes:
        with self._lock:
            require(self._key is not None and type(plaintext) is bytes and type(associated) is bytes)
            require(len(plaintext) <= MAX_PLAIN and len(associated) <= MAX_AD)
            nonce = os.urandom(NONCE_BYTES)
            require(type(nonce) is bytes and len(nonce) == NONCE_BYTES)
            plain, ad, iv = _buffer(plaintext), _buffer(MAGIC + associated), _buffer(nonce)
            cipher, tag = _buffer(bytes(len(plaintext))), _buffer(bytes(TAG_BYTES))
            try:
                self._primitive._lib.crypto_aead_lock(cipher, tag, self._key, iv,
                                                     ad, len(MAGIC + associated), plain, len(plaintext))
                return MAGIC + nonce + bytes(tag) + bytes(cipher)[:len(plaintext)]
            finally:
                self._primitive.wipe(plain)

    def open(self, parcel: bytes, associated: bytes) -> bytes:
        with self._lock:
            require(self._key is not None and type(parcel) is bytes and type(associated) is bytes)
            require(OVERHEAD <= len(parcel) <= OVERHEAD + MAX_PLAIN and len(associated) <= MAX_AD)
            require(parcel[:len(MAGIC)] == MAGIC)
            nonce = parcel[len(MAGIC):len(MAGIC) + NONCE_BYTES]
            mac = parcel[len(MAGIC) + NONCE_BYTES:OVERHEAD]
            ciphertext = parcel[OVERHEAD:]
            plain, ad, iv = _buffer(bytes(len(ciphertext))), _buffer(MAGIC + associated), _buffer(nonce)
            cipher, tag = _buffer(ciphertext), _buffer(mac)
            try:
                valid = self._primitive._lib.crypto_aead_unlock(plain, tag, self._key, iv,
                                                              ad, len(MAGIC + associated), cipher, len(ciphertext))
                require(valid == 0)
                return bytes(plain)[:len(ciphertext)]
            finally:
                self._primitive.wipe(plain)
