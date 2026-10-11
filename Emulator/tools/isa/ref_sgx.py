#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
ref_sgx.py -- independent reference model of the Intel SGX measurement, signature and key
material (Python 3 stdlib only; ledgers U1260-U1299, docs/sgx.md)

Written from the Intel documents only (not from the C implementation):
  * SDM 092 Vol3D (332831-092) 38.7 SECS, 38.8 TCS, 38.12 SECINFO, 38.13 PCMD, 38.14 SIGSTRUCT
    (Table 38-20, RSA-3072, e = 3, EMSA-PKCS1-v1.5 with the DER DigestInfo of SHA-256,
    q1 = floor(S^2 / N), q2 = floor((S^3 - q1 * S * N) / N)), 38.15 EINITTOKEN, 38.16 REPORT,
    38.17 TARGETINFO, 38.18 KEYREQUEST;
  * the Operation sections of ECREATE (p. 41-28: "ECREATE" || SSAFRAMESIZE || SIZE || 0),
    EADD (p. 41-16: "EADD" || offset || SECINFO[375:0]), EEXTEND (p. 41-40: "EEXTEND" ||
    offset || 0, then the 256 bytes), EINIT (p. 41-44..41-47: SHA256FINAL, MRSIGNER =
    SHA256(MODULUS), CONTROLLED_ATTRIBUTES, the EINITTOKEN key dependencies, CMAC over the
    first 192 bytes of the token), EREPORT (p. 41-115: report key dependencies, CMAC over the
    first 384 bytes), EGETKEY (p. 41-102..41-107: Table 41-62), EWB / ELDU (p. 41-72 / 41-51:
    AES-GCM with CR_BASE_PK, "TMP_VER << 32", the 128-byte MAC header).
  * FIPS 180-4 (SHA-256, via hashlib), FIPS 197 (AES), NIST SP 800-38B / RFC 4493 (AES-CMAC),
    NIST SP 800-38D (GCM), RFC 3447 (EMSA-PKCS1-v1_5).

Model choices where the SDM is silent (the same choices as the emulator, docs/sgx.md section 3;
they are NOT Intel's values):
  * derivekey(KEYDEPENDENCIES) = AES-CMAC-128(PLATFORM_ROOT_KEY, serialize(KEYDEPENDENCIES)) with
    the 648-byte little-endian layout of keydep_bytes() below; PLATFORM_ROOT_KEY is host-provided
    (UC_CTL_X86_SGX_PLATFORM, default 16 zero bytes);
  * the EWB / ELDU GCM IV is the 96-bit little-endian integer TMP_VER << 32 (bytes 0-3 zero,
    bytes 4-11 = TMP_VER); the MAC header is SECINFO (64 bytes) || EID (8) || RSVD (40, the PCMD
    reserved bytes) || LINADDR (8) || 0 (8); TMP_VER = ++CR_PAGING_VERSION (first version 1);
  * the SIGSTRUCT signature is computed over bytes 0-127 || 900-1027 (the "Y" rows of Table
    38-20); MRENCLAVE / MRSIGNER are the FIPS 180-4 digest byte strings.

Usage:
  python ref_sgx.py --selftest   known-answer tests (FIPS 197, RFC 4493, GCM, RSA identities)
                                  and the consistency of the test-enclave vectors; exit 0 on pass
  python ref_sgx.py --cinc       unicorn/tests/unit/x86_sgx_vectors.inc (stdout)
"""

import hashlib
import os
import random
import struct
import sys

# ---------------------------------------------------------------------------------------------
# AES-128 (FIPS 197)
# ---------------------------------------------------------------------------------------------
_SBOX = []


def _init_sbox():
    # multiplicative inverse in GF(2^8) followed by the affine transform (FIPS 197 5.1.1)
    def gmul(a, b):
        p = 0
        for _ in range(8):
            if b & 1:
                p ^= a
            hi = a & 0x80
            a = (a << 1) & 0xff
            if hi:
                a ^= 0x1b
            b >>= 1
        return p
    inv = [0] * 256
    for x in range(1, 256):
        for y in range(1, 256):
            if gmul(x, y) == 1:
                inv[x] = y
                break
    for x in range(256):
        b = inv[x]
        s = b
        for i in range(1, 5):
            s ^= ((b << i) | (b >> (8 - i))) & 0xff
        _SBOX.append(s ^ 0x63)


_init_sbox()


def _xtime(a):
    a <<= 1
    return (a ^ 0x1b) & 0xff if a & 0x100 else a


def aes128_expand(key):
    assert len(key) == 16
    w = [list(key[4 * i:4 * i + 4]) for i in range(4)]
    rcon = 1
    for i in range(4, 44):
        t = list(w[i - 1])
        if i % 4 == 0:
            t = t[1:] + t[:1]
            t = [_SBOX[b] for b in t]
            t[0] ^= rcon
            rcon = _xtime(rcon)
        w.append([w[i - 4][j] ^ t[j] for j in range(4)])
    return [sum((w[4 * r + c] for c in range(4)), []) for r in range(11)]


def aes128_encrypt_block(rk, block):
    s = [b ^ k for b, k in zip(block, rk[0])]
    for rnd in range(1, 11):
        s = [_SBOX[b] for b in s]
        # ShiftRows (state is column-major: s[4*c + r])
        s = [s[4 * ((c + r) % 4) + r] for c in range(4) for r in range(4)]
        if rnd != 10:
            t = []
            for c in range(4):
                a = s[4 * c:4 * c + 4]
                x = a[0] ^ a[1] ^ a[2] ^ a[3]
                t += [a[0] ^ x ^ _xtime(a[0] ^ a[1]), a[1] ^ x ^ _xtime(a[1] ^ a[2]),
                      a[2] ^ x ^ _xtime(a[2] ^ a[3]), a[3] ^ x ^ _xtime(a[3] ^ a[0])]
            s = t
        s = [b ^ k for b, k in zip(s, rk[rnd])]
    return bytes(s)


def aes128(key, block):
    return aes128_encrypt_block(aes128_expand(key), block)


# ---------------------------------------------------------------------------------------------
# AES-CMAC (NIST SP 800-38B, RFC 4493)
# ---------------------------------------------------------------------------------------------
def _dbl(b):
    v = int.from_bytes(b, 'big') << 1
    if v >> 128:
        v = (v ^ 0x87) & ((1 << 128) - 1)
    return v.to_bytes(16, 'big')


def cmac(key, msg):
    rk = aes128_expand(key)
    l = aes128_encrypt_block(rk, bytes(16))
    k1 = _dbl(l)
    k2 = _dbl(k1)
    n = (len(msg) + 15) // 16
    if n == 0:
        n = 1
        last = bytes([0x80]) + bytes(15)
        last = bytes(a ^ b for a, b in zip(last, k2))
    elif len(msg) % 16 == 0:
        last = bytes(a ^ b for a, b in zip(msg[16 * (n - 1):], k1))
    else:
        tail = msg[16 * (n - 1):]
        tail = tail + bytes([0x80]) + bytes(15 - len(tail))
        last = bytes(a ^ b for a, b in zip(tail, k2))
    x = bytes(16)
    for i in range(n - 1):
        x = aes128_encrypt_block(rk, bytes(a ^ b for a, b in zip(x, msg[16 * i:16 * i + 16])))
    return aes128_encrypt_block(rk, bytes(a ^ b for a, b in zip(x, last)))


# ---------------------------------------------------------------------------------------------
# AES-128-GCM (NIST SP 800-38D), 96-bit IV, 128-bit tag
# ---------------------------------------------------------------------------------------------
def _gf_mul(x, y):
    r = 0xe1 << 120
    z = 0
    v = y
    for i in range(127, -1, -1):
        if (x >> i) & 1:
            z ^= v
        v = (v >> 1) ^ r if v & 1 else v >> 1
    return z


def _ghash(h, data):
    hv = int.from_bytes(h, 'big')
    x = 0
    for i in range(0, len(data), 16):
        blk = data[i:i + 16]
        blk = blk + bytes(16 - len(blk))
        x = _gf_mul(x ^ int.from_bytes(blk, 'big'), hv)
    return x


def gcm(key, iv, aad, data, decrypt=False):
    """returns (output, tag); the tag is computed over the ciphertext"""
    assert len(iv) == 12
    rk = aes128_expand(key)
    h = aes128_encrypt_block(rk, bytes(16))
    j0 = iv + b'\x00\x00\x00\x01'
    out = bytearray()
    ctr = 1
    for i in range(0, len(data), 16):
        ctr += 1
        ks = aes128_encrypt_block(rk, iv + struct.pack('>I', ctr & 0xffffffff))
        out += bytes(a ^ b for a, b in zip(data[i:i + 16], ks))
    ct = bytes(data) if decrypt else bytes(out)
    lens = struct.pack('>QQ', len(aad) * 8, len(ct) * 8)
    pad_a = aad + bytes((16 - len(aad) % 16) % 16)
    pad_c = ct + bytes((16 - len(ct) % 16) % 16)
    s = _ghash(h, pad_a + pad_c + lens)
    tag = bytes(a ^ b for a, b in zip(aes128_encrypt_block(rk, j0), s.to_bytes(16, 'big')))
    return bytes(out), tag


# ---------------------------------------------------------------------------------------------
# RSA-3072, e = 3 (SDM Vol3D 38.14)
# ---------------------------------------------------------------------------------------------
DER_SHA256 = bytes.fromhex('3031300d060960864801650304020105000420')


def _is_probable_prime(n, rng):
    if n < 2:
        return False
    for p in (2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37):
        if n % p == 0:
            return n == p
    d, s = n - 1, 0
    while d % 2 == 0:
        d //= 2
        s += 1
    for _ in range(40):
        a = rng.randrange(2, n - 2)
        x = pow(a, d, n)
        if x in (1, n - 1):
            continue
        for _ in range(s - 1):
            x = x * x % n
            if x == n - 1:
                break
        else:
            return False
    return True


def rsa3_keygen(seed):
    rng = random.Random(seed)
    while True:
        ps = []
        while len(ps) < 2:
            c = rng.getrandbits(1536) | (3 << 1534) | 1
            if c % 3 == 2 and _is_probable_prime(c, rng):
                ps.append(c)
        n = ps[0] * ps[1]
        if n.bit_length() == 3072 and ps[0] != ps[1]:
            phi = (ps[0] - 1) * (ps[1] - 1)
            return n, pow(3, -1, phi)


def emsa_pkcs1_v15(digest):
    t = DER_SHA256 + digest
    return b'\x00\x01' + b'\xff' * (384 - len(t) - 3) + b'\x00' + t


HARDCODED_PKCS1_5_PADDING = emsa_pkcs1_v15(bytes(32))[:352]


def le384(v):
    return v.to_bytes(384, 'little')


# ---------------------------------------------------------------------------------------------
# Structures
# ---------------------------------------------------------------------------------------------
PT_SECS, PT_TCS, PT_REG, PT_VA, PT_TRIM = 0, 1, 2, 3, 4


def secinfo(flags_rwx, pt, extra=0):
    return struct.pack('<Q', flags_rwx | (pt << 8) | extra) + bytes(56)


def sigstruct_signed_bytes(ss):
    return bytes(ss[0:128]) + bytes(ss[900:1028])


def make_sigstruct(n, d, mrenclave, attributes, attributemask, miscselect, miscmask,
                   isvprodid, isvsvn, vendor=0, date=0x20261010, swdefined=0):
    ss = bytearray(1808)
    ss[0:16] = bytes.fromhex('06000000E10000000000010000000000')
    struct.pack_into('<II', ss, 16, vendor, date)
    ss[24:40] = bytes.fromhex('01010000600000006000000001000000')
    struct.pack_into('<I', ss, 40, swdefined)
    ss[128:512] = le384(n)
    struct.pack_into('<I', ss, 512, 3)
    struct.pack_into('<II', ss, 900, miscselect, miscmask)
    ss[928:944] = attributes
    ss[944:960] = attributemask
    ss[960:992] = mrenclave
    struct.pack_into('<HH', ss, 1024, isvprodid, isvsvn)
    em = emsa_pkcs1_v15(hashlib.sha256(sigstruct_signed_bytes(ss)).digest())
    s = pow(int.from_bytes(em, 'big'), d, n)
    q1 = s * s // n
    q2 = (s * s * s - q1 * s * n) // n
    ss[516:900] = le384(s)
    ss[1040:1424] = le384(q1)
    ss[1424:1808] = le384(q2)
    return bytes(ss)


def verify_sigstruct(ss):
    """the EINIT signature check with q1 / q2 (our reading, docs/sgx.md 3): True / False"""
    n = int.from_bytes(ss[128:512], 'little')
    s = int.from_bytes(ss[516:900], 'little')
    q1 = int.from_bytes(ss[1040:1424], 'little')
    q2 = int.from_bytes(ss[1424:1808], 'little')
    if s >= n:
        return False
    r1 = s * s - q1 * n
    if not 0 <= r1 < n:
        return False
    r2 = s * r1 - q2 * n
    if not 0 <= r2 < n:
        return False
    em = r2.to_bytes(384, 'big')
    return em == emsa_pkcs1_v15(hashlib.sha256(sigstruct_signed_bytes(ss)).digest())


# ---------------------------------------------------------------------------------------------
# MRENCLAVE (ECREATE / EADD / EEXTEND / EINIT)
# ---------------------------------------------------------------------------------------------
class Measure:
    def __init__(self, ssaframesize, size):
        self.stream = b'ECREATE\x00' + struct.pack('<IQ', ssaframesize, size) + bytes(44)

    def eadd(self, offset, secinfo_bytes):
        self.stream += b'EADD\x00\x00\x00\x00' + struct.pack('<Q', offset) + secinfo_bytes[:48]

    def eextend(self, offset, chunk256):
        assert len(chunk256) == 256
        self.stream += b'EEXTEND\x00' + struct.pack('<Q', offset) + bytes(48) + chunk256

    def final(self):
        assert len(self.stream) % 64 == 0
        return hashlib.sha256(self.stream).digest()


# ---------------------------------------------------------------------------------------------
# Key derivation (model placeholder, docs/sgx.md 3)
# ---------------------------------------------------------------------------------------------
KEYNAME_EINITTOKEN, KEYNAME_PROVISION, KEYNAME_PROVISION_SEAL, KEYNAME_REPORT, KEYNAME_SEAL = range(5)


def keydep_bytes(kd):
    b = bytearray(648)
    struct.pack_into('<HHHH', b, 0, kd.get('keyname', 0), kd.get('isvprodid', 0),
                     kd.get('isvsvn', 0), kd.get('configsvn', 0))
    b[8:24] = kd.get('ownerepoch', bytes(16))
    b[24:40] = kd.get('attributes', bytes(16))
    b[40:56] = kd.get('attributesmask', bytes(16))
    b[56:88] = kd.get('mrenclave', bytes(32))
    b[88:120] = kd.get('mrsigner', bytes(32))
    b[120:152] = kd.get('keyid', bytes(32))
    b[152:168] = kd.get('seal_fuses', bytes(16))
    b[168:184] = kd.get('cpusvn', bytes(16))
    struct.pack_into('<IIH', b, 184, kd.get('miscselect', 0), kd.get('miscmask', 0),
                     kd.get('keypolicy', 0))
    b[196:212] = kd.get('isvfamilyid', bytes(16))
    b[212:228] = kd.get('isvextprodid', bytes(16))
    b[228:292] = kd.get('configid', bytes(64))
    b[292:644] = kd.get('padding', bytes(352))
    return bytes(b)


def derivekey(root, kd):
    return cmac(root, keydep_bytes(kd))


def einittoken(root, platform, le, mrenclave, mrsigner, attributes):
    """EINITTOKEN (Table 38-21) MACed with the EINITTOKEN key EINIT derives (p. 41-46)"""
    t = bytearray(304)
    struct.pack_into('<I', t, 0, 1)
    t[48:64] = attributes
    t[64:96] = mrenclave
    t[128:160] = mrsigner
    t[192:208] = le['cpusvn']
    struct.pack_into('<HH', t, 208, le['isvprodid'], le['isvsvn'])
    struct.pack_into('<I', t, 236, le['maskedmiscselect'])
    t[240:256] = le['maskedattributes']
    t[256:288] = le['keyid']
    key = derivekey(root, dict(keyname=KEYNAME_EINITTOKEN, isvprodid=le['isvprodid'],
                               isvsvn=le['isvsvn'], ownerepoch=platform['ownerepoch'],
                               attributes=le['maskedattributes'],
                               mrsigner=platform['lepubkeyhash'], keyid=le['keyid'],
                               seal_fuses=platform['seal_fuses'], cpusvn=le['cpusvn'],
                               miscselect=le['maskedmiscselect'],
                               padding=HARDCODED_PKCS1_5_PADDING))
    t[288:304] = cmac(key, bytes(t[0:192]))
    return bytes(t), key


# Table 41-3 error codes EGETKEY returns
SGX_INVALID_ATTRIBUTE, SGX_INVALID_CPUSVN, SGX_INVALID_ISVSVN, SGX_INVALID_KEYNAME = 2, 32, 64, 256
ATTR_PROVISIONKEY = 1 << 4


def ereport(root, platform, secs, targetinfo, reportdata):
    """EREPORT (Vol3D p. 41-112..41-116): REPORT (Table 38-22) and its MAC with the report key of
    the TARGETINFO (Table 38-23) enclave. 'secs': the reporting enclave's SECS fields."""
    r = bytearray(432)
    r[0:16] = platform['cpusvn']
    struct.pack_into('<I', r, 16, secs['miscselect'])
    r[32:48] = secs.get('isvextprodid', bytes(16))
    r[48:64] = secs['attributes']
    r[64:96] = secs['mrenclave']
    r[128:160] = secs['mrsigner']
    r[192:256] = secs.get('configid', bytes(64))
    struct.pack_into('<HHH', r, 256, secs['isvprodid'], secs['isvsvn'], secs.get('configsvn', 0))
    r[304:320] = secs.get('isvfamilyid', bytes(16))
    r[320:384] = reportdata
    r[384:416] = platform['report_keyid']
    key = derivekey(root, dict(keyname=KEYNAME_REPORT, ownerepoch=platform['ownerepoch'],
                               attributes=targetinfo[32:48], mrenclave=targetinfo[0:32],
                               keyid=platform['report_keyid'], seal_fuses=platform['seal_fuses'],
                               cpusvn=platform['cpusvn'], padding=HARDCODED_PKCS1_5_PADDING,
                               miscselect=struct.unpack_from('<I', targetinfo, 52)[0],
                               configid=targetinfo[64:128],
                               configsvn=struct.unpack_from('<H', targetinfo, 50)[0]))
    r[416:432] = cmac(key, bytes(r[0:384]))
    return bytes(r), key


def egetkey(root, platform, secs, kr):
    """EGETKEY (Vol3D p. 41-99..41-108, Table 41-62) for the KEYREQUEST bytes 'kr' (Table 38-24):
    (0, key) or (error code, None). Reserved-field (#GP) checks are not modelled here."""
    name, pol, isvsvn = struct.unpack_from('<HHH', kr, 0)
    cpusvn, amask, keyid = kr[8:24], kr[24:40], kr[40:72]
    miscmask, configsvn = struct.unpack_from('<IH', kr, 72)
    attr = secs['attributes']
    a0, a1 = struct.unpack('<QQ', attr)
    m0, m1 = struct.unpack('<QQ', amask)
    tmp_attr = struct.pack('<QQ', (m0 | 3) & a0, m1 & a1)
    tmp_misc = miscmask & secs['miscselect']
    beyond = any(cpusvn[i] > platform['cpusvn'][i] for i in range(16))
    kd = dict(keyname=name)
    if name == KEYNAME_REPORT:
        kd.update(ownerepoch=platform['ownerepoch'], attributes=attr, mrenclave=secs['mrenclave'],
                  keyid=keyid, seal_fuses=platform['seal_fuses'], cpusvn=platform['cpusvn'],
                  padding=HARDCODED_PKCS1_5_PADDING, miscselect=secs['miscselect'],
                  configid=secs.get('configid', bytes(64)), configsvn=secs.get('configsvn', 0))
        return 0, derivekey(root, kd)
    if name not in (KEYNAME_SEAL, KEYNAME_EINITTOKEN, KEYNAME_PROVISION, KEYNAME_PROVISION_SEAL):
        return SGX_INVALID_KEYNAME, None
    if name == KEYNAME_EINITTOKEN and not (a0 & ATTR_EINITTOKEN_KEY):
        return SGX_INVALID_ATTRIBUTE, None
    if name in (KEYNAME_PROVISION, KEYNAME_PROVISION_SEAL) and not (a0 & ATTR_PROVISIONKEY):
        return SGX_INVALID_ATTRIBUTE, None
    if beyond:
        return SGX_INVALID_CPUSVN, None
    if isvsvn > secs['isvsvn']:
        return SGX_INVALID_ISVSVN, None
    if name == KEYNAME_SEAL and configsvn > secs.get('configsvn', 0):
        return SGX_INVALID_ISVSVN, None
    kd.update(isvsvn=isvsvn, attributes=tmp_attr, cpusvn=cpusvn, padding=secs['padding'],
              miscselect=tmp_misc)
    if name in (KEYNAME_SEAL, KEYNAME_EINITTOKEN):
        kd.update(ownerepoch=platform['ownerepoch'], keyid=keyid)
    if name != KEYNAME_PROVISION:
        kd.update(seal_fuses=platform['seal_fuses'])
    if name != KEYNAME_EINITTOKEN:
        kd.update(attributesmask=amask, miscmask=~miscmask & 0xffffffff)
    if name in (KEYNAME_SEAL, KEYNAME_PROVISION_SEAL):
        if name == KEYNAME_SEAL and pol & 1:
            kd['mrenclave'] = secs['mrenclave']
        if name == KEYNAME_SEAL and pol & 2:
            kd['mrsigner'] = secs['mrsigner']
        if pol & 0x10:
            kd['isvfamilyid'] = secs.get('isvfamilyid', bytes(16))
        if not pol & 4:
            kd['isvprodid'] = secs['isvprodid']
        if pol & 8:
            kd.update(configid=secs.get('configid', bytes(64)), configsvn=configsvn)
        if pol & 0x20:
            kd['isvextprodid'] = secs.get('isvextprodid', bytes(16))
        kd['keypolicy'] = pol
    else:
        kd['isvprodid'] = secs['isvprodid']
    if name != KEYNAME_SEAL:
        kd['mrsigner'] = secs['mrsigner']
    return 0, derivekey(root, kd)


def ewb(base_pk, ver, page, linaddr, eid, flags, pcmd_eid):
    """EWB (Vol3D p. 41-69..41-73) with the model's MAC-header layout (SECINFO 0, EID 64, RSVD 72,
    LINADDR 112) and GCM IV = (TMP_VER << 32) as 12 little-endian bytes: (ciphertext, PCMD).
    'flags' = SECINFO.FLAGS bytes 0-1 (R W X PENDING MODIFIED PR, PT)."""
    hdr = bytearray(128)
    hdr[0:2] = flags
    struct.pack_into('<Q', hdr, 64, eid)
    struct.pack_into('<Q', hdr, 112, linaddr)
    iv = bytes(4) + struct.pack('<Q', ver)
    ct, tag = gcm(base_pk, iv, bytes(hdr), page)
    pcmd = bytearray(128)
    pcmd[0:2] = flags
    struct.pack_into('<Q', pcmd, 64, pcmd_eid)
    pcmd[112:128] = tag
    return ct, bytes(pcmd)


# ---------------------------------------------------------------------------------------------
# The test enclave of the unit tests (test_x86.c sgx_ block)
# ---------------------------------------------------------------------------------------------
ENC_BASE = 0x10000000
ENC_SIZE = 0x10000
SSAFRAMESIZE = 1
NSSA = 2
ATTR_MODE64 = 1 << 2
ATTR_DEBUG = 1 << 1
ATTR_EINITTOKEN_KEY = 1 << 5
XFRM = 3
MISCSELECT = 1          # EXINFO

# page offsets
OFF_TCS, OFF_SSA, OFF_CODE, OFF_DATA, OFF_DATA2 = 0x0000, 0x1000, 0x3000, 0x4000, 0x5000


class Asm:
    """a tiny 64-bit assembler: raw bytes, labels, rel8 / rel32 jumps"""
    def __init__(self):
        self.b = bytearray()
        self.labels = {}
        self.fix = []

    def h(self, hexs):
        self.b += bytes.fromhex(hexs.replace(' ', ''))

    def d32(self, v):
        self.b += struct.pack('<I', v & 0xffffffff)

    def label(self, name):
        self.labels[name] = len(self.b)

    def j8(self, op, name):         # op: '74' je, '75' jne, 'eb' jmp
        self.h(op)
        self.fix.append((len(self.b), name, 1))
        self.b += b'\x00'

    def jmp32(self, name):
        self.h('e9')
        self.fix.append((len(self.b), name, 4))
        self.b += bytes(4)

    def done(self):
        for at, name, n in self.fix:
            rel = self.labels[name] - (at + n)
            if n == 1:
                assert -128 <= rel < 128, name
                self.b[at] = rel & 0xff
            else:
                self.b[at:at + 4] = struct.pack('<i', rel)
        return bytes(self.b)


# enclave commands (RDI at EENTER); the result goes to R14, EEXIT returns to RCX of EENTER
CMD_EXIT, CMD_UD2, CMD_CPUID, CMD_INC, CMD_EREPORT, CMD_EGETKEY, CMD_READ, CMD_WRITE, \
    CMD_JMP, CMD_INT3, CMD_LOOP, CMD_POPF_TF, CMD_EACCEPT, CMD_EMODPE, CMD_EACCEPTCOPY, \
    CMD_FSREAD = range(16)


def enclave_code():
    """
    64-bit enclave code at OENTRY (BASE + 3000H). EENTER: RAX = CSSA, RCX = the address after
    EENTER. CSSA != 0: the exception handler - R14 = SSA[CSSA - 1].EXITINFO, SSA.RIP += 2 (skips a
    2-byte faulting instruction), EEXIT. Else RDI selects a command (CMD_*); every path ends in EEXIT
    to the saved RCX (R15) with the result in R14.
    """
    a = Asm()
    gpr0 = ENC_BASE + OFF_SSA + 4096 * SSAFRAMESIZE - 184     # SSA frame 0, GPRSGX
    a.h('49 89 cf')                     # mov r15, rcx
    a.h('85 c0')                        # test eax, eax
    a.j8('74', 'dispatch')
    a.h('44 8b 34 25'); a.d32(gpr0 + 160)       # mov r14d, [EXITINFO]
    a.h('48 83 04 25'); a.d32(gpr0 + 136); a.h('02')   # add qword [SSA.RIP], 2
    a.jmp32('exit')
    a.label('dispatch')
    bodies = [
        (CMD_EXIT, ''),
        (CMD_UD2, '0f 0b'),
        (CMD_CPUID, '0f a2'),
        (CMD_INC, '48 8b 04 25 %s 48 ff c0 48 89 04 25 %s 49 89 c6' %
         (struct.pack('<I', ENC_BASE + OFF_DATA).hex(), struct.pack('<I', ENC_BASE + OFF_DATA).hex())),
        (CMD_EREPORT, 'bb %s b9 %s ba %s 31 c0 0f 01 d7' %
         (struct.pack('<I', ENC_BASE + OFF_DATA2).hex(),
          struct.pack('<I', ENC_BASE + OFF_DATA2 + 0x200).hex(),
          struct.pack('<I', ENC_BASE + OFF_DATA2 + 0x400).hex())),
        (CMD_EGETKEY, 'bb %s b9 %s b8 01 00 00 00 0f 01 d7 49 89 c6' %
         (struct.pack('<I', ENC_BASE + OFF_DATA2 + 0x600).hex(),
          struct.pack('<I', ENC_BASE + OFF_DATA2 + 0x800).hex())),
        (CMD_READ, '4c 8b 36'),                         # mov r14, [rsi]
        (CMD_WRITE, '48 89 16'),                        # mov [rsi], rdx
        (CMD_JMP, 'ff e6'),                             # jmp rsi
        (CMD_INT3, 'cc'),
        (CMD_LOOP, 'eb fe'),
        (CMD_POPF_TF, '9c 48 81 0c 24 00 01 00 00 9d 9c 41 5e'),
        (CMD_EACCEPT, '48 89 f3 48 89 d1 b8 05 00 00 00 0f 01 d7 49 89 c6'),
        (CMD_EMODPE, '48 89 f3 48 89 d1 b8 06 00 00 00 0f 01 d7 49 89 c6'),
        (CMD_EACCEPTCOPY, '48 89 f3 48 89 d1 4c 89 c2 b8 07 00 00 00 0f 01 d7 49 89 c6'),
        (CMD_FSREAD, '64 4c 8b 34 25 00 00 00 00'),     # mov r14, fs:[0]
    ]
    for n, body in bodies:
        a.h('83 ff %02x' % n)               # cmp edi, n
        a.j8('75', 'next%d' % n)
        a.h(body)
        a.jmp32('exit')
        a.label('next%d' % n)
    a.label('exit')
    a.h('4c 89 fb')                     # mov rbx, r15
    a.h('b8 04 00 00 00')               # mov eax, 4
    a.h('0f 01 d7')                     # enclu (EEXIT)
    return a.done()


def enclave_pages():
    tcs = bytearray(4096)
    struct.pack_into('<QQQIIQQQQII', tcs, 0, 0, 0, OFF_SSA, 0, NSSA, OFF_CODE, 0,
                     OFF_DATA, OFF_DATA, 0xfff, 0xfff)
    code = enclave_code()
    code_page = code + b'\xcc' * (4096 - len(code))
    data = bytes(range(256)) * 16
    return [
        (OFF_TCS, secinfo(0, PT_TCS), bytes(tcs), True),
        (OFF_SSA, secinfo(3, PT_REG), bytes(4096), False),
        (OFF_SSA + 0x1000, secinfo(3, PT_REG), bytes(4096), False),
        (OFF_CODE, secinfo(5, PT_REG), code_page, True),
        (OFF_DATA, secinfo(3, PT_REG), data, True),
        (OFF_DATA2, secinfo(3, PT_REG), bytes(4096), False),
    ]


def enclave32_code():
    """
    32-bit enclave code at OENTRY (BASE + 3000H) for the compatibility-mode unit test (U1272).
    CSSA = 0: ESI := ECX (the return address), EDX := FS:[0] (the data page), UD2. CSSA != 0: the
    handler adds 2 to SSA frame 0's RIP. Both end in EEXIT to ESI.
    """
    gpr_rip = ENC_BASE + OFF_SSA + 4096 * SSAFRAMESIZE - 184 + 136
    c = bytes.fromhex('89ce' '85c0' '750c' '64a100000000' '89c2' '0f0b' 'eb07')
    c += bytes.fromhex('8305') + struct.pack('<I', gpr_rip) + b'\x02'
    c += bytes.fromhex('89f3' 'b804000000' '0f01d7')
    return c


def enclave32_pages():
    """the 64-bit enclave's pages with the 32-bit code page"""
    code = enclave32_code()
    pages = enclave_pages()
    return [(off, si, (code + b'\xcc' * (4096 - len(code))) if off == OFF_CODE else content, measured)
            for off, si, content, measured in pages]


def enclave_mrenclave(pages=None):
    m = Measure(SSAFRAMESIZE, ENC_SIZE)
    for off, si, content, measured in (pages or enclave_pages()):
        # EADD records SECINFO with R/W/X forced to 0 for a TCS (p. 41-16)
        si_m = secinfo(0, PT_TCS) if si[1] == PT_TCS else si
        m.eadd(off, si_m)
        if measured:
            for k in range(16):
                m.eextend(off + 256 * k, content[256 * k:256 * k + 256])
    return m.final()


def test_vectors():
    n, d = rsa3_keygen(0x5367)
    mre = enclave_mrenclave()
    attrs = struct.pack('<QQ', ATTR_MODE64, 0)
    amask = struct.pack('<QQ', 0xffffffffffffffff & ~ATTR_DEBUG, 0)
    ss = make_sigstruct(n, d, mre, attrs, amask, MISCSELECT, 0xffffffff, 0x1234, 7)
    mrsigner = hashlib.sha256(ss[128:512]).digest()
    # the test platform (test_x86.c sgx_platform): every key zero, IA32_SGXLEPUBKEYHASH reset
    # value 00 01 .. 1F; a launch token for the debug enclave (SECS.ATTRIBUTES = DEBUG | MODE64BIT,
    # XFRM = 3) from a Launch Enclave with ISVPRODID 20H, ISVSVN 1
    platform = dict(ownerepoch=bytes(16), seal_fuses=bytes(16), lepubkeyhash=bytes(range(32)))
    le = dict(cpusvn=bytes(16), isvprodid=0x20, isvsvn=1, maskedmiscselect=0,
              maskedattributes=struct.pack('<QQ', ATTR_MODE64, 0),
              keyid=bytes(range(0x40, 0x60)))
    tok, tokkey = einittoken(bytes(16), platform, le, mre, mrsigner,
                             struct.pack('<QQ', ATTR_MODE64 | ATTR_DEBUG, XFRM))
    # EREPORT / EGETKEY of the initialised debug enclave (ATTRIBUTES = INIT | DEBUG | MODE64BIT,
    # XFRM 3; ISVPRODID 1234H, ISVSVN 7) on the test platform (CR_REPORT_KEYID = 0, CR_CPUSVN = 0)
    pf = dict(platform, cpusvn=bytes(16), report_keyid=bytes(32))
    secs = dict(miscselect=MISCSELECT, attributes=struct.pack('<QQ', 7, XFRM), mrenclave=mre,
                mrsigner=mrsigner, isvprodid=0x1234, isvsvn=7, padding=HARDCODED_PKCS1_5_PADDING)
    ti = bytearray(512)
    ti[0:32] = mre
    ti[32:48] = secs['attributes']
    struct.pack_into('<I', ti, 52, MISCSELECT)
    report, _ = ereport(bytes(16), pf, secs, bytes(ti), bytes(range(64)))
    kr_seal = bytearray(512)
    struct.pack_into('<HHH', kr_seal, 0, KEYNAME_SEAL, 1, 7)      # KEYPOLICY.MRENCLAVE, ISVSVN 7
    kr_seal[24:40] = struct.pack('<QQ', 0xff, 0)
    kr_seal[40:72] = bytes(range(0xa0, 0xc0))
    struct.pack_into('<I', kr_seal, 72, 1)
    _, seal = egetkey(bytes(16), pf, secs, bytes(kr_seal))
    kr_rep = bytearray(512)
    struct.pack_into('<H', kr_rep, 0, KEYNAME_REPORT)
    _, repkey = egetkey(bytes(16), pf, secs, bytes(kr_rep))
    # EWB of the data page (offset 4000H, RW PT_REG) of a fresh test enclave: EID 1 (the first
    # ECREATE), TMP_VER 1 (the first EWB), CR_BASE_PK 0
    ct, pcmd = ewb(bytes(16), 1, bytes(range(256)) * 16, ENC_BASE + OFF_DATA, 1, bytes([3, 2]), 1)
    # the 32-bit enclave (U1272): ATTRIBUTES.MODE64BIT = 0, the same key (MRSIGNER)
    mre32 = enclave_mrenclave(enclave32_pages())
    ss32 = make_sigstruct(n, d, mre32, struct.pack('<QQ', 0, 0), amask, MISCSELECT, 0xffffffff,
                          0x1234, 7)
    return dict(n=n, d=d, mrenclave=mre, sigstruct=ss, mrsigner=mrsigner, token=tok,
                tokkey=tokkey, targetinfo=bytes(ti), report=report, kr_seal=bytes(kr_seal),
                seal_key=seal, report_key=repkey, ewb_data=ct, ewb_pcmd=pcmd, mrenclave32=mre32,
                sigstruct32=ss32)


# ---------------------------------------------------------------------------------------------
# Self test
# ---------------------------------------------------------------------------------------------
def selftest():
    ok = True

    def check(name, cond):
        nonlocal ok
        print(('PASS ' if cond else 'FAIL ') + name)
        ok = ok and cond

    k = bytes(range(16))
    check('FIPS 197 C.1 AES-128',
          aes128(k, bytes.fromhex('00112233445566778899aabbccddeeff')).hex() ==
          '69c4e0d86a7b0430d8cdb78070b4c55a')
    k = bytes.fromhex('2b7e151628aed2a6abf7158809cf4f3c')
    check('RFC 4493 CMAC len 0', cmac(k, b'').hex() == 'bb1d6929e95937287fa37d129b756746')
    check('RFC 4493 CMAC len 16',
          cmac(k, bytes.fromhex('6bc1bee22e409f96e93d7e117393172a')).hex() ==
          '070a16b46b4d4144f79bdd9dd04a287c')
    ct, tag = gcm(bytes(16), bytes(12), b'', bytes(16))
    check('GCM test case 2', ct.hex() == '0388dace60b6a392f328c2b971b2fe78' and
          tag.hex() == 'ab6e47d42cec13bdf53a67b21257bddf')
    pt, tag2 = gcm(bytes(16), bytes(12), b'', ct, decrypt=True)
    check('GCM decrypt round trip', pt == bytes(16) and tag2 == tag)
    check('PKCS#1 v1.5 padding prefix', HARDCODED_PKCS1_5_PADDING[:2] == b'\x00\x01' and
          HARDCODED_PKCS1_5_PADDING[2:332] == b'\xff' * 330 and
          HARDCODED_PKCS1_5_PADDING[332:] == b'\x00' + DER_SHA256)
    # the SDM's HARDCODED_PKCS1_5_PADDING (p. 41-46): [15:0] = 0100H, [2655:16] = 330 x FFH,
    # [2815:2656] = 2004000501020403650148866009060D30313000H (little-endian bit numbering)
    sdm = (0x0100).to_bytes(2, 'little') + b'\xff' * 330 + \
        (0x2004000501020403650148866009060D30313000).to_bytes(20, 'little')
    check('padding == SDM HARDCODED_PKCS1_5_PADDING', sdm == HARDCODED_PKCS1_5_PADDING)
    v = test_vectors()
    check('SIGSTRUCT verifies (q1, q2)', verify_sigstruct(v['sigstruct']))
    bad = bytearray(v['sigstruct'])
    bad[1040] ^= 1
    check('a wrong q1 fails', not verify_sigstruct(bytes(bad)))
    bad = bytearray(v['sigstruct'])
    bad[960] ^= 1
    check('a changed ENCLAVEHASH fails', not verify_sigstruct(bytes(bad)))
    m = Measure(1, 0x2000)
    check('ECREATE block is 64 bytes', len(m.stream) == 64)
    check('the REPORT MAC verifies with the EGETKEY report key (KEYID = REPORT.KEYID)',
          cmac(v['report_key'], v['report'][:384]) == v['report'][416:432])
    pf = dict(ownerepoch=bytes(16), seal_fuses=bytes(16), cpusvn=bytes(16))
    secs = dict(miscselect=1, attributes=struct.pack('<QQ', 7, 3), mrenclave=v['mrenclave'],
                mrsigner=v['mrsigner'], isvprodid=0x1234, isvsvn=7,
                padding=HARDCODED_PKCS1_5_PADDING)
    kr = bytearray(v['kr_seal'])
    struct.pack_into('<H', kr, 4, 8)
    check('EGETKEY ISVSVN beyond the enclave: SGX_INVALID_ISVSVN',
          egetkey(bytes(16), pf, secs, bytes(kr))[0] == SGX_INVALID_ISVSVN)
    kr = bytearray(512)
    struct.pack_into('<H', kr, 0, KEYNAME_PROVISION)
    check('EGETKEY PROVISION_KEY without ATTRIBUTES.PROVISIONKEY: SGX_INVALID_ATTRIBUTE',
          egetkey(bytes(16), pf, secs, bytes(kr))[0] == SGX_INVALID_ATTRIBUTE)
    struct.pack_into('<H', kr, 0, 5)
    pt, tag = gcm(bytes(16), bytes(4) + struct.pack('<Q', 1),
                  bytes([3, 2]) + bytes(62) + struct.pack('<Q', 1) + bytes(40) +
                  struct.pack('<Q', ENC_BASE + OFF_DATA) + bytes(8),
                  v['ewb_data'], decrypt=True)
    check('EWB vector decrypts (ELDU) to the data page with the PCMD MAC',
          pt == bytes(range(256)) * 16 and tag == v['ewb_pcmd'][112:128])
    check('32-bit enclave SIGSTRUCT verifies, its MRENCLAVE differs',
          verify_sigstruct(v['sigstruct32']) and v['mrenclave32'] != v['mrenclave'] and
          v['sigstruct32'][960:992] == v['mrenclave32'])
    check('EGETKEY KEYNAME 5: SGX_INVALID_KEYNAME',
          egetkey(bytes(16), pf, secs, bytes(kr))[0] == SGX_INVALID_KEYNAME)
    try:
        inc = open(INC, newline='').read().replace('\r\n', '\n')
    except OSError:
        inc = None
    check('x86_sgx_vectors.inc is the --cinc output', inc == cinc_text())
    print('selftest:', 'PASS' if ok else 'FAIL')
    return ok


def c_bytes(name, data):
    lines = ['static const uint8_t %s[%d] = {' % (name, len(data))]
    for i in range(0, len(data), 16):
        lines.append('    ' + ', '.join('0x%02x' % b for b in data[i:i + 16]) + ',')
    lines.append('};')
    return '\n'.join(lines)


def cinc_text():
    v = test_vectors()
    out = ['/* generated by Emulator/tools/isa/ref_sgx.py --cinc; do not edit */']
    out.append('#define SGXV_BASE 0x%xULL' % ENC_BASE)
    out.append('#define SGXV_SIZE 0x%xULL' % ENC_SIZE)
    out.append(c_bytes('sgxv_mrenclave', v['mrenclave']))
    out.append(c_bytes('sgxv_mrsigner', v['mrsigner']))
    out.append(c_bytes('sgxv_sigstruct', v['sigstruct']))
    out.append(c_bytes('sgxv_token', v['token']))
    out.append(c_bytes('sgxv_targetinfo', v['targetinfo']))
    out.append(c_bytes('sgxv_report', v['report']))
    out.append(c_bytes('sgxv_kr_seal', v['kr_seal']))
    out.append(c_bytes('sgxv_seal_key', v['seal_key']))
    out.append(c_bytes('sgxv_report_key', v['report_key']))
    out.append(c_bytes('sgxv_ewb_data', v['ewb_data']))
    out.append(c_bytes('sgxv_ewb_pcmd', v['ewb_pcmd']))
    out.append(c_bytes('sgxv32_mrenclave', v['mrenclave32']))
    out.append(c_bytes('sgxv32_sigstruct', v['sigstruct32']))
    out.append(c_bytes('sgxv32_page_3000', [c for o, si, c, m in enclave32_pages() if o == OFF_CODE][0]))
    for off, si, content, measured in enclave_pages():
        if any(content):
            out.append(c_bytes('sgxv_page_%04x' % off, content))
    return '\n'.join(out) + '\n'


def cinc():
    sys.stdout.write(cinc_text())


INC = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..', 'unicorn', 'tests',
                   'unit', 'x86_sgx_vectors.inc')


if __name__ == '__main__':
    if '--selftest' in sys.argv:
        sys.exit(0 if selftest() else 1)
    elif '--cinc' in sys.argv:
        cinc()
    else:
        print(__doc__)
