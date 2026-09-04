"""Pure-Python XXH3_64bits (seed 0, default secret), bit-identical to xxHash 0.8.x and to
src/core/hash/xxh3.h. Every id the engine derives from a string (component-type keys,
shader-name StringIDs, name_id() asset ids) goes through here so scripts and C++ agree.

    python scripts/xxh3.py            # self-test against pinned vectors
"""

import struct

_SECRET = bytes([
    0xb8, 0xfe, 0x6c, 0x39, 0x23, 0xa4, 0x4b, 0xbe, 0x7c, 0x01, 0x81, 0x2c, 0xf7, 0x21, 0xad, 0x1c,
    0xde, 0xd4, 0x6d, 0xe9, 0x83, 0x90, 0x97, 0xdb, 0x72, 0x40, 0xa4, 0xa4, 0xb7, 0xb3, 0x67, 0x1f,
    0xcb, 0x79, 0xe6, 0x4e, 0xcc, 0xc0, 0xe5, 0x78, 0x82, 0x5a, 0xd0, 0x7d, 0xcc, 0xff, 0x72, 0x21,
    0xb8, 0x08, 0x46, 0x74, 0xf7, 0x43, 0x24, 0x8e, 0xe0, 0x35, 0x90, 0xe6, 0x81, 0x3a, 0x26, 0x4c,
    0x3c, 0x28, 0x52, 0xbb, 0x91, 0xc3, 0x00, 0xcb, 0x88, 0xd0, 0x65, 0x8b, 0x1b, 0x53, 0x2e, 0xa3,
    0x71, 0x64, 0x48, 0x97, 0xa2, 0x0d, 0xf9, 0x4e, 0x38, 0x19, 0xef, 0x46, 0xa9, 0xde, 0xac, 0xd8,
    0xa8, 0xfa, 0x76, 0x3f, 0xe3, 0x9c, 0x34, 0x3f, 0xf9, 0xdc, 0xbb, 0xc7, 0xc7, 0x0b, 0x4f, 0x1d,
    0x8a, 0x51, 0xe0, 0x4b, 0xcd, 0xb4, 0x59, 0x31, 0xc8, 0x9f, 0x7e, 0xc9, 0xd9, 0x78, 0x73, 0x64,
    0xea, 0xc5, 0xac, 0x83, 0x34, 0xd3, 0xeb, 0xc3, 0xc5, 0x81, 0xa0, 0xff, 0xfa, 0x13, 0x63, 0xeb,
    0x17, 0x0d, 0xdd, 0x51, 0xb7, 0xf0, 0xda, 0x49, 0xd3, 0x16, 0x55, 0x26, 0x29, 0xd4, 0x68, 0x9e,
    0x2b, 0x16, 0xbe, 0x58, 0x7d, 0x47, 0xa1, 0xfc, 0x8f, 0xf8, 0xb8, 0xd1, 0x7a, 0xd0, 0x31, 0xce,
    0x45, 0xcb, 0x3a, 0x8f, 0x95, 0x16, 0x04, 0x28, 0xaf, 0xd7, 0xfb, 0xca, 0xbb, 0x4b, 0x40, 0x7e,
])

_M64 = 0xFFFFFFFFFFFFFFFF
_M32 = 0xFFFFFFFF
_PRIME32_1 = 0x9E3779B1
_PRIME32_2 = 0x85EBCA77
_PRIME32_3 = 0xC2B2AE3D
_PRIME64_1 = 0x9E3779B185EBCA87
_PRIME64_2 = 0xC2B2AE3D27D4EB4F
_PRIME64_3 = 0x165667B19E3779F9
_PRIME64_4 = 0x85EBCA77C2B2AE63
_PRIME64_5 = 0x27D4EB2F165667C5
_PRIME_MX1 = 0x165667919E3779F9
_PRIME_MX2 = 0x9FB21C651E98DF25

_STRIPE_LEN = 64
_SECRET_CONSUME_RATE = 8
_SECRET_SIZE = len(_SECRET)
_SECRET_SIZE_MIN = 136
_MIDSIZE_MAX = 240
_MIDSIZE_STARTOFFSET = 3
_MIDSIZE_LASTOFFSET = 17
_SECRET_LASTACC_START = 7
_SECRET_MERGEACCS_START = 11
_NB_STRIPES_PER_BLOCK = (_SECRET_SIZE - _STRIPE_LEN) // _SECRET_CONSUME_RATE
_BLOCK_LEN = _STRIPE_LEN * _NB_STRIPES_PER_BLOCK

_u64 = struct.Struct("<Q").unpack_from
_u32 = struct.Struct("<I").unpack_from


def _r64(b, o):
    return _u64(b, o)[0]


def _r32(b, o):
    return _u32(b, o)[0]


def _swap64(x):
    return int.from_bytes(x.to_bytes(8, "little"), "big")


def _rotl64(x, r):
    return ((x << r) | (x >> (64 - r))) & _M64


def _mul128_fold64(a, b):
    p = a * b
    return (p & _M64) ^ (p >> 64)


def _avalanche(h):
    h ^= h >> 37
    h = (h * _PRIME_MX1) & _M64
    return h ^ (h >> 32)


def _avalanche64(h):
    h ^= h >> 33
    h = (h * _PRIME64_2) & _M64
    h ^= h >> 29
    h = (h * _PRIME64_3) & _M64
    return h ^ (h >> 32)


def _rrmxmx(h, n):
    h ^= _rotl64(h, 49) ^ _rotl64(h, 24)
    h = (h * _PRIME_MX2) & _M64
    h ^= ((h >> 35) + n) & _M64
    h = (h * _PRIME_MX2) & _M64
    return h ^ (h >> 28)


def _len_0to16(b, n):
    if n > 8:
        bitflip1 = _r64(_SECRET, 24) ^ _r64(_SECRET, 32)
        bitflip2 = _r64(_SECRET, 40) ^ _r64(_SECRET, 48)
        lo = _r64(b, 0) ^ bitflip1
        hi = _r64(b, n - 8) ^ bitflip2
        acc = (n + _swap64(lo) + hi + _mul128_fold64(lo, hi)) & _M64
        return _avalanche(acc)
    if n >= 4:
        i1 = _r32(b, 0)
        i2 = _r32(b, n - 4)
        bitflip = _r64(_SECRET, 8) ^ _r64(_SECRET, 16)
        keyed = ((i2 + (i1 << 32)) & _M64) ^ bitflip
        return _rrmxmx(keyed, n)
    if n > 0:
        c1, c2, c3 = b[0], b[n >> 1], b[n - 1]
        combined = (c1 << 16) | (c2 << 24) | c3 | (n << 8)
        bitflip = _r32(_SECRET, 0) ^ _r32(_SECRET, 4)
        return _avalanche64(combined ^ bitflip)
    return _avalanche64(_r64(_SECRET, 56) ^ _r64(_SECRET, 64))


def _mix16(b, o, s):
    lo = _r64(b, o)
    hi = _r64(b, o + 8)
    return _mul128_fold64(lo ^ _r64(_SECRET, s), hi ^ _r64(_SECRET, s + 8))


def _len_17to128(b, n):
    acc = (n * _PRIME64_1) & _M64
    if n > 32:
        if n > 64:
            if n > 96:
                acc += _mix16(b, 48, 96)
                acc += _mix16(b, n - 64, 112)
            acc += _mix16(b, 32, 64)
            acc += _mix16(b, n - 48, 80)
        acc += _mix16(b, 16, 32)
        acc += _mix16(b, n - 32, 48)
    acc += _mix16(b, 0, 0)
    acc += _mix16(b, n - 16, 16)
    return _avalanche(acc & _M64)


def _len_129to240(b, n):
    acc = (n * _PRIME64_1) & _M64
    rounds = n // 16
    for i in range(8):
        acc += _mix16(b, 16 * i, 16 * i)
    acc_end = _mix16(b, n - 16, _SECRET_SIZE_MIN - _MIDSIZE_LASTOFFSET)
    acc = _avalanche(acc & _M64)
    for i in range(8, rounds):
        acc_end += _mix16(b, 16 * i, 16 * (i - 8) + _MIDSIZE_STARTOFFSET)
    return _avalanche((acc + acc_end) & _M64)


def _accumulate_512(acc, b, o, s):
    for i in range(8):
        val = _r64(b, o + 8 * i)
        key = val ^ _r64(_SECRET, s + 8 * i)
        acc[i ^ 1] = (acc[i ^ 1] + val) & _M64
        acc[i] = (acc[i] + (key & _M32) * (key >> 32)) & _M64


def _scramble(acc, s):
    for i in range(8):
        a = acc[i]
        a ^= a >> 47
        a ^= _r64(_SECRET, s + 8 * i)
        acc[i] = (a * _PRIME32_1) & _M64


def _hash_long(b, n):
    acc = [_PRIME32_3, _PRIME64_1, _PRIME64_2, _PRIME64_3, _PRIME64_4, _PRIME32_2, _PRIME64_5, _PRIME32_1]
    blocks = (n - 1) // _BLOCK_LEN
    for blk in range(blocks):
        base = blk * _BLOCK_LEN
        for st in range(_NB_STRIPES_PER_BLOCK):
            _accumulate_512(acc, b, base + st * _STRIPE_LEN, st * _SECRET_CONSUME_RATE)
        _scramble(acc, _SECRET_SIZE - _STRIPE_LEN)
    stripes = ((n - 1) - _BLOCK_LEN * blocks) // _STRIPE_LEN
    base = blocks * _BLOCK_LEN
    for st in range(stripes):
        _accumulate_512(acc, b, base + st * _STRIPE_LEN, st * _SECRET_CONSUME_RATE)
    _accumulate_512(acc, b, n - _STRIPE_LEN, _SECRET_SIZE - _STRIPE_LEN - _SECRET_LASTACC_START)
    result = (n * _PRIME64_1) & _M64
    for i in range(4):
        s = _SECRET_MERGEACCS_START + 16 * i
        result += _mul128_fold64(acc[2 * i] ^ _r64(_SECRET, s), acc[2 * i + 1] ^ _r64(_SECRET, s + 8))
    return _avalanche(result & _M64)


def xxh3_64(data):
    """XXH3_64bits of a bytes-like object."""
    b = bytes(data)
    n = len(b)
    if n <= 16:
        return _len_0to16(b, n)
    if n <= 128:
        return _len_17to128(b, n)
    if n <= _MIDSIZE_MAX:
        return _len_129to240(b, n)
    return _hash_long(b, n)


def string_id(text):
    """StringID of a str: the full 64-bit hash of its UTF-8 bytes, same as SID(text) in C++."""
    return xxh3_64(text.encode("utf-8"))


# Pinned against xxHash 0.8.3 (tests/core/hash_tests.cpp checks the same values from C++).
VECTORS = {
    "": 3244421341483603138,
    "a": 16629034431890738719,
    "abc": 8696274497037089104,
    "TransformComponent": 8357514868041474787,
    "default_lit": 16532098932897623660,
    "default_pbr": 14720002576866434405,
    "default_pbr_restir": 6423489698308471953,
    "Jump": 8077407883072430183,
    "will-engine": 14241076315294974437,
    "gbuffer": 17000617961446832639,
    ("The quick brown fox jumps over the lazy dog. " * 5 + "The quick brown fox jumps over the lazy dog!"): 9703144984165482491,
}


def _self_test():
    bad = [k for k, v in VECTORS.items() if string_id(k) != v]
    if bad:
        raise SystemExit("xxh3.py vector mismatch: " + ", ".join(repr(k)[:40] for k in bad))
    print("xxh3.py: %d vectors ok" % len(VECTORS))


if __name__ == "__main__":
    _self_test()
