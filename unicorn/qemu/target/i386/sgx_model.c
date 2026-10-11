/*
 * Intel SGX (NoVmp, ledgers U1261-U1299, plan 1.J.11; design and requirement mapping: docs/sgx.md).
 *
 * Sources: SDM 092 Vol3D (332831-092) chapters 37-43 (compared with 093: identical for the parts
 * modelled here), Vol1 Table 21-47..21-49 (CPUID.12H), Vol3A 5.7 / 7.15 (PFEC.SGX), Vol4 Table 2-2
 * (3AH, 8CH-8FH, 500H). Everything here is specification-validated only: the i5-13600K has no SGX
 * and Intel SDE cannot run CPL0 enclave-management code.
 *
 * Opt-in: UC_CTL_X86_FEATURES / UC_X86_FEATURE_SGX (default off). The platform (EPC section, the
 * platform-specific keys and SVNs) comes from UC_CTL_X86_SGX_PLATFORM; every value the SDM leaves
 * to the platform is a documented placeholder (docs/sgx.md section 3), never an invented Intel
 * value.
 */
#include "qemu/osdep.h"
#if __Use_Original_Qemu != 1 /* ours (U1261): whole file */
#include "cpu.h"
#include "exec/exec-all.h"
#include "exec/cpu_ldst.h"
#include "exec/helper-proto.h"
#include "exec/memory.h"
#include "crypto/aes.h"
#include "uc_priv.h"
#include <unicorn/unicorn.h>

/* ---------------------------------------------------------------------------------------------
 * Enumeration and platform (U1261)
 * ------------------------------------------------------------------------------------------- */

/*
 * The model enumerates SGX: the opt-in created the package state and the CPU reports
 * CPUID.(07H,0):EBX.SGX as seen by software (a strict UC_CTL_X86_CPUID profile that hides it hides
 * SGX, as the translator hides instructions). CPUID.12H.0:EAX.SGX1 follows it (Table 37-3).
 */
bool x86_sgx_enumerated(CPUX86State *env)
{
    return env->sgx && (env->features[FEAT_7_0_EBX] & CPUID_7_0_EBX_SGX) &&
           (x86_cpuid_profile_mask(env, 7, 0, 1) & CPUID_7_0_EBX_SGX);
}

/*
 * NoVmp (ledger U1261): create the package state from UC_CTL_X86_SGX_PLATFORM (called from the
 * CPU realize path after the feature words are final). An EPC section beyond MAXPHYADDR is not
 * reachable by any paging mode (Vol3A 5.5: those PTE bits are reserved): the model then has no
 * EPC section (epc_size 0 in the platform read back too; CPUID.12H.2 invalid).
 */
void x86_sgx_init(CPUX86State *env)
{
    struct uc_struct *uc = env->uc;
    uc_x86_sgx_platform *cfg = &uc->x86_sgx_platform;
    X86CPU *cpu = env_archcpu(env);
    X86SgxPkg *p;

    if (env->sgx || !(uc->x86_features & UC_X86_FEATURE_SGX)) {
        return;
    }
    if (cfg->epc_size && cfg->epc_base + cfg->epc_size > (1ULL << cpu->phys_bits)) {
        cfg->epc_base = 0;
        cfg->epc_size = 0;
    }
    p = g_new0(X86SgxPkg, 1);
    p->epc_base = cfg->epc_size ? cfg->epc_base : 0;
    p->epc_size = cfg->epc_size;
    p->npages = cfg->epc_size >> 12;
    p->epcm = p->npages ? g_new0(X86SgxEpcm, p->npages) : NULL;
    memcpy(p->root_key, cfg->root_key, 16);
    memcpy(p->cpusvn, cfg->cpusvn, 16);
    memcpy(p->owner_epoch, cfg->owner_epoch, 16);
    memcpy(p->seal_fuses, cfg->seal_fuses, 16);
    memcpy(p->base_pk, cfg->base_pk, 16);
    memcpy(p->report_keyid, cfg->report_keyid, 32);
    memcpy(p->le_hash_reset, cfg->le_pubkey_hash, 32);
    p->svn_sinit = cfg->svn_sinit;
    p->next_eid = 1;            /* CR_NEXT_EID: our choice of start value */
    env->sgx = p;
    x86_sgx_reset(env);
}

/*
 * NoVmp (ledger U1261): the per-logical-processor state after reset (the reset area is already
 * zero: not in enclave mode, 42.9). IA32_SGXLEPUBKEYHASH0-3: "On reset, the default value is the
 * digest of Intel's signing key" (Vol4 8CH-8FH) - the platform's value (host-provided).
 */
void x86_sgx_reset(CPUX86State *env)
{
    int i;

    if (!env->sgx) {
        return;
    }
    for (i = 0; i < 4; i++) {
        env->sgx_lehash[i] = ldq_le_p(env->sgx->le_hash_reset + 8 * i);
    }
}

/*
 * CPUID.12H (Vol1 Table 21-47..21-49), valid with CPUID.(07H,0):EBX.SGX (the caller checks
 * MAX_LEAF). Sub-leaf 0: SGX1, SGX2; MISCSELECT EXINFO; the model's maximum enclave sizes.
 * Sub-leaf 1: the SECS.ATTRIBUTES bits ECREATE may set - DEBUG, MODE64BIT, PROVISIONKEY,
 * EINITTOKEN_KEY, and XFRM = the XCR0 bits the model supports (XFRM[63] never). Sub-leaf 2: the
 * EPC section (type 1; property 0001b "confidentiality, integrity and replay protection" - our
 * choice: the model protects it by access control only, docs/sgx.md 3); 3 and up: type 0.
 * Table 37-3 marks the leaf "Valid*" ("dependent on enablement") without giving values for an
 * unlocked / disabled IA32_FEATURE_CONTROL: the model reports the same values in every state
 * (our choice, docs/sgx.md 4).
 */
void x86_sgx_cpuid(CPUX86State *env, uint32_t count, uint32_t *eax, uint32_t *ebx,
                   uint32_t *ecx, uint32_t *edx)
{
    *eax = *ebx = *ecx = *edx = 0;
    if (!env->sgx || !(env->features[FEAT_7_0_EBX] & CPUID_7_0_EBX_SGX)) {
        return;
    }
    switch (count) {
    case 0:
        *eax = NOVMP_SGX_12_0_EAX;
        *ebx = NOVMP_SGX_12_0_EBX;
        *edx = NOVMP_SGX_12_0_EDX;
        break;
    case 1: {
        uint64_t xfrm = ((uint64_t)env->features[FEAT_XSAVE_XCR0_HI] << 32 |
                         env->features[FEAT_XSAVE_XCR0_LO]) & ~(1ULL << 63);

        *eax = (uint32_t)NOVMP_SGX_ATTR_LOW;
        *ebx = (uint32_t)(NOVMP_SGX_ATTR_LOW >> 32);
        *ecx = (uint32_t)(xfrm | 3);
        *edx = (uint32_t)(xfrm >> 32);
        break;
    }
    case 2:
        if (env->sgx->epc_size) {
            *eax = 1 | (uint32_t)(env->sgx->epc_base & 0xfffff000u);
            *ebx = (uint32_t)(env->sgx->epc_base >> 32) & 0xfffff;
            *ecx = 1 | (uint32_t)(env->sgx->epc_size & 0xfffff000u);
            *edx = (uint32_t)(env->sgx->epc_size >> 32) & 0xfffff;
        }
        break;
    default:
        break;
    }
}

/* ---------------------------------------------------------------------------------------------
 * MSRs (U1261): IA32_SGXLEPUBKEYHASH0-3 (8CH-8FH), IA32_SGX_SVN_STATUS (500H). IA32_FEATURE_CONTROL
 * bits 17 / 18 are in misc_helper.c (U906).
 * ------------------------------------------------------------------------------------------- */

static bool sgx_lc_enumerated(CPUX86State *env)
{
    return x86_sgx_enumerated(env) && (env->features[FEAT_7_0_ECX] & CPUID_7_0_ECX_SGX_LC) &&
           (x86_cpuid_profile_mask(env, 7, 0, 2) & CPUID_7_0_ECX_SGX_LC);
}

/*
 * Vol4 Table 2-2: 8CH-8FH "Read permitted If CPUID.12H.00H:EAX[0] = 1 && CPUID.07H.00H:ECX[30] = 1";
 * 500H "If CPUID.07H.00H:EBX[2] = 1". 90H / 91H (IA32_SGXLEPUBKEYHASH4/5, "bits 383:256 of the
 * SHA256 digest") are not modelled (a SHA-256 digest has 256 bits; Vol3D uses 256 bits;
 * docs/sgx.md 4): they stay unimplemented MSRs (#GP(0)).
 */
int x86_sgx_msr_present(CPUX86State *env, uint32_t msr)
{
    if (msr >= MSR_IA32_SGXLEPUBKEYHASH0 && msr <= MSR_IA32_SGXLEPUBKEYHASH3) {
        return sgx_lc_enumerated(env);
    }
    if (msr == MSR_IA32_SGX_SVN_STATUS) {
        return x86_sgx_enumerated(env);
    }
    return -1;
}

/*
 * Write permission (Vol4 8CH: "Write permitted if CPUID.12H.00H:EAX[0] = 1 &&
 * IA32_FEATURE_CONTROL[17] = 1 && IA32_FEATURE_CONTROL[0] = 1"; Vol3D 39.1.4 / 42.2.2: "read
 * only" otherwise - #GP(0) for WRMSR). IA32_SGX_SVN_STATUS is R/O.
 */
int x86_sgx_msr_write_ok(CPUX86State *env, uint32_t msr, uint64_t val)
{
    (void)val;
    if (msr >= MSR_IA32_SGXLEPUBKEYHASH0 && msr <= MSR_IA32_SGXLEPUBKEYHASH3) {
        return (env->msr_ia32_feature_control & (FEATURE_CONTROL_LOCKED | FEATURE_CONTROL_SGX_LC)) ==
               (FEATURE_CONTROL_LOCKED | FEATURE_CONTROL_SGX_LC);
    }
    if (msr == MSR_IA32_SGX_SVN_STATUS) {
        return 0;
    }
    return -1;
}

bool x86_sgx_rdmsr(CPUX86State *env, uint32_t msr, uint64_t *val)
{
    if (msr >= MSR_IA32_SGXLEPUBKEYHASH0 && msr <= MSR_IA32_SGXLEPUBKEYHASH3) {
        *val = env->sgx_lehash[msr - MSR_IA32_SGXLEPUBKEYHASH0];
        return true;
    }
    if (msr == MSR_IA32_SGX_SVN_STATUS) {
        /* Table 42-2: Lock 0, SGX_SVN_SINIT 23:16 with CPUID.01H:ECX.SMX (else reserved 0) */
        *val = env->sgx_svn_lock |
               ((env->features[FEAT_1_ECX] & CPUID_EXT_SMX) ? (uint64_t)env->sgx->svn_sinit << 16 : 0);
        return true;
    }
    return false;
}

bool x86_sgx_wrmsr(CPUX86State *env, uint32_t msr, uint64_t val)
{
    if (msr >= MSR_IA32_SGXLEPUBKEYHASH0 && msr <= MSR_IA32_SGXLEPUBKEYHASH3) {
        env->sgx_lehash[msr - MSR_IA32_SGXLEPUBKEYHASH0] = val;
        return true;
    }
    return false;
}

/*
 * "Once processor reserved memory protections are activated (see Section 39.5), any execution of
 * INVD will result in a #GP(0)" (Vol3D 39.6.5); 39.5 does not say when they are activated. Our
 * choice (docs/sgx.md 3): the model has an EPC section and software opted in to SGX
 * (IA32_FEATURE_CONTROL.LOCK = SGX_ENABLE = 1, 37.7.1).
 */
bool x86_sgx_prm_active(CPUX86State *env)
{
    return x86_sgx_enumerated(env) && env->sgx->epc_size &&
           (env->msr_ia32_feature_control & (FEATURE_CONTROL_LOCKED | FEATURE_CONTROL_SGX)) ==
               (FEATURE_CONTROL_LOCKED | FEATURE_CONTROL_SGX);
}

/* ---------------------------------------------------------------------------------------------
 * Cryptography (U1262): SHA-256 (FIPS 180-4) with the SECS-resident running state that
 * SHA256INITIALIZE / SHA256UPDATE / SHA256FINAL of the ECREATE / EADD / EEXTEND / EINIT pseudocode
 * use; AES-CMAC-128 (SP 800-38B); AES-128-GCM (SP 800-38D, 96-bit IV, 128-bit tag); RSA-3072 with
 * e = 3 checked through q1 / q2 (Vol3D 38.14). AES itself is QEMU's crypto/aes.c (as Key Locker).
 * ------------------------------------------------------------------------------------------- */

static const uint32_t sgx_sha_k[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

static const uint32_t sgx_sha_h0[8] = {
    0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
};

static uint32_t sgx_ror(uint32_t x, int n)
{
    return (x >> n) | (x << (32 - n));
}

/* one SHA-256 compression of a 64-byte block (FIPS 180-4 6.2.2) */
static void sgx_sha256_block(uint32_t h[8], const uint8_t *p)
{
    uint32_t w[64], a, b, c, d, e, f, g, k, t1, t2;
    int i;

    for (i = 0; i < 16; i++) {
        w[i] = ldl_be_p(p + 4 * i);
    }
    for (i = 16; i < 64; i++) {
        w[i] = w[i - 16] + (sgx_ror(w[i - 15], 7) ^ sgx_ror(w[i - 15], 18) ^ (w[i - 15] >> 3)) +
               w[i - 7] + (sgx_ror(w[i - 2], 17) ^ sgx_ror(w[i - 2], 19) ^ (w[i - 2] >> 10));
    }
    a = h[0]; b = h[1]; c = h[2]; d = h[3]; e = h[4]; f = h[5]; g = h[6]; k = h[7];
    for (i = 0; i < 64; i++) {
        t1 = k + (sgx_ror(e, 6) ^ sgx_ror(e, 11) ^ sgx_ror(e, 25)) + ((e & f) ^ (~e & g)) +
             sgx_sha_k[i] + w[i];
        t2 = (sgx_ror(a, 2) ^ sgx_ror(a, 13) ^ sgx_ror(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        k = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += k;
}

/* SHA-256 of a byte string (the FIPS 180-4 digest byte string) */
static void sgx_sha256(const uint8_t *p, size_t n, uint8_t out[32])
{
    uint32_t h[8];
    uint8_t blk[64];
    size_t i, r;

    memcpy(h, sgx_sha_h0, sizeof(h));
    for (i = 0; i + 64 <= n; i += 64) {
        sgx_sha256_block(h, p + i);
    }
    r = n - i;
    memset(blk, 0, 64);
    memcpy(blk, p + i, r);
    blk[r] = 0x80;
    if (r >= 56) {
        sgx_sha256_block(h, blk);
        memset(blk, 0, 64);
    }
    stq_be_p(blk + 56, (uint64_t)n * 8);
    sgx_sha256_block(h, blk);
    for (i = 0; i < 8; i++) {
        stl_be_p(out + 4 * i, h[i]);
    }
}

/* AES-CMAC with a 128-bit key (NIST SP 800-38B; RFC 4493) */
static void sgx_cmac(const uint8_t key[16], const uint8_t *msg, size_t n, uint8_t out[16])
{
    AES_KEY k;
    uint8_t l[16] = {0}, k1[16], k2[16], x[16] = {0}, last[16];
    size_t blocks = n ? (n + 15) / 16 : 1, i, j;
    int carry;

    AES_set_encrypt_key(key, 128, &k);
    AES_encrypt(l, l, &k);
    for (i = 0, carry = 0; i < 16; i++) {           /* K1 = dbl(L) */
        k1[15 - i] = (uint8_t)((l[15 - i] << 1) | carry);
        carry = l[15 - i] >> 7;
    }
    if (l[0] & 0x80) {
        k1[15] ^= 0x87;
    }
    for (i = 0, carry = 0; i < 16; i++) {           /* K2 = dbl(K1) */
        k2[15 - i] = (uint8_t)((k1[15 - i] << 1) | carry);
        carry = k1[15 - i] >> 7;
    }
    if (k1[0] & 0x80) {
        k2[15] ^= 0x87;
    }
    for (i = 0; i + 1 < blocks; i++) {
        for (j = 0; j < 16; j++) {
            x[j] ^= msg[16 * i + j];
        }
        AES_encrypt(x, x, &k);
    }
    if (n && n % 16 == 0) {
        for (j = 0; j < 16; j++) {
            last[j] = msg[16 * i + j] ^ k1[j];
        }
    } else {
        size_t r = n - 16 * i;

        memset(last, 0, 16);
        memcpy(last, msg + 16 * i, r);
        last[r] = 0x80;
        for (j = 0; j < 16; j++) {
            last[j] ^= k2[j];
        }
    }
    for (j = 0; j < 16; j++) {
        x[j] ^= last[j];
    }
    AES_encrypt(x, out, &k);
}

/* GF(2^128) multiplication (SP 800-38D 6.3), big-endian blocks */
static void sgx_gf_mul(uint8_t x[16], const uint8_t y[16])
{
    uint64_t zh = 0, zl = 0, vh = ldq_be_p(y), vl = ldq_be_p(y + 8);
    int i;

    for (i = 0; i < 128; i++) {
        if ((x[i >> 3] >> (7 - (i & 7))) & 1) {
            zh ^= vh;
            zl ^= vl;
        }
        if (vl & 1) {
            vl = (vl >> 1) | (vh << 63);
            vh = (vh >> 1) ^ 0xe100000000000000ULL;
        } else {
            vl = (vl >> 1) | (vh << 63);
            vh >>= 1;
        }
    }
    stq_be_p(x, zh);
    stq_be_p(x + 8, zl);
}

static void sgx_ghash(uint8_t x[16], const uint8_t h[16], const uint8_t *p, size_t n)
{
    size_t i, j;

    for (i = 0; i < n; i += 16) {
        for (j = 0; j < 16 && i + j < n; j++) {
            x[j] ^= p[i + j];
        }
        sgx_gf_mul(x, h);
    }
}

/*
 * AES-128-GCM (SP 800-38D 7.1 / 7.2), 96-bit IV: 'in' -> 'out' (encrypt, or decrypt with dec = 1);
 * the tag is computed over 'aad' and the ciphertext
 */
static void sgx_gcm(const uint8_t key[16], const uint8_t iv[12], const uint8_t *aad, size_t na,
                    const uint8_t *in, size_t n, uint8_t *out, uint8_t tag[16], bool dec)
{
    AES_KEY k;
    uint8_t h[16] = {0}, j0[16], cb[16], ks[16], x[16] = {0}, lens[16];
    size_t i, j;

    AES_set_encrypt_key(key, 128, &k);
    AES_encrypt(h, h, &k);
    memcpy(j0, iv, 12);
    stl_be_p(j0 + 12, 1);
    sgx_ghash(x, h, aad, na);
    if (dec) {
        sgx_ghash(x, h, in, n);
    }
    memcpy(cb, j0, 16);
    for (i = 0; i < n; i += 16) {
        stl_be_p(cb + 12, ldl_be_p(cb + 12) + 1);
        AES_encrypt(cb, ks, &k);
        for (j = 0; j < 16 && i + j < n; j++) {
            out[i + j] = in[i + j] ^ ks[j];
        }
    }
    if (!dec) {
        sgx_ghash(x, h, out, n);
    }
    stq_be_p(lens, (uint64_t)na * 8);
    stq_be_p(lens + 8, (uint64_t)n * 8);
    sgx_ghash(x, h, lens, 16);
    AES_encrypt(j0, ks, &k);
    for (i = 0; i < 16; i++) {
        tag[i] = ks[i] ^ x[i];
    }
}

/* little-endian 32-bit-limb integers for the RSA-3072 check: 96 limbs, products up to 192 */
#define SGX_BN 96

static void sgx_bn_load(uint32_t *a, const uint8_t *le384)
{
    int i;

    for (i = 0; i < SGX_BN; i++) {
        a[i] = ldl_le_p(le384 + 4 * i);
    }
}

/* out[0 .. na+nb-1] = a * b */
static void sgx_bn_mul(const uint32_t *a, int na, const uint32_t *b, int nb, uint32_t *out)
{
    int i, j;

    memset(out, 0, sizeof(uint32_t) * (na + nb));
    for (i = 0; i < na; i++) {
        uint64_t c = 0;

        for (j = 0; j < nb; j++) {
            c += (uint64_t)a[i] * b[j] + out[i + j];
            out[i + j] = (uint32_t)c;
            c >>= 32;
        }
        out[i + nb] = (uint32_t)c;
    }
}

/* compare n-limb numbers: -1, 0, 1 */
static int sgx_bn_cmp(const uint32_t *a, const uint32_t *b, int n)
{
    int i;

    for (i = n - 1; i >= 0; i--) {
        if (a[i] != b[i]) {
            return a[i] < b[i] ? -1 : 1;
        }
    }
    return 0;
}

/* a -= b (n limbs, a >= b) */
static void sgx_bn_sub(uint32_t *a, const uint32_t *b, int n)
{
    int64_t c = 0;
    int i;

    for (i = 0; i < n; i++) {
        c += (int64_t)a[i] - b[i];
        a[i] = (uint32_t)c;
        c >>= 32;
    }
}

/*
 * r = s * x - q * n with 0 <= r < n required (x < n, all 96 limbs); false when the provided
 * quotient q is not floor(s * x / n). Our reading of "Verify signature using embedded public key,
 * q1, and q2" (EINIT p. 41-44, 38.14): r1 = S^2 - q1 * N and r2 = S * r1 - q2 * N are S^2 mod N
 * and S^3 mod N exactly when q1, q2 are the quotients 38.14 defines.
 */
static bool sgx_bn_mulmod_q(const uint32_t *s, const uint32_t *x, const uint32_t *q,
                            const uint32_t *n, uint32_t *r)
{
    uint32_t p1[2 * SGX_BN], p2[2 * SGX_BN];
    int i;

    sgx_bn_mul(s, SGX_BN, x, SGX_BN, p1);
    sgx_bn_mul(q, SGX_BN, n, SGX_BN, p2);
    if (sgx_bn_cmp(p1, p2, 2 * SGX_BN) < 0) {
        return false;
    }
    sgx_bn_sub(p1, p2, 2 * SGX_BN);
    for (i = SGX_BN; i < 2 * SGX_BN; i++) {
        if (p1[i]) {
            return false;
        }
    }
    if (sgx_bn_cmp(p1, n, SGX_BN) >= 0) {
        return false;
    }
    memcpy(r, p1, sizeof(uint32_t) * SGX_BN);
    return true;
}

/* DER encoding of the SHA-256 DigestInfo (RFC 3447 9.2 note 1) */
static const uint8_t sgx_der_sha256[19] = {
    0x30, 0x31, 0x30, 0x0d, 0x06, 0x09, 0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x01,
    0x05, 0x00, 0x04, 0x20,
};

/*
 * HARDCODED_PKCS1_5_PADDING (EGETKEY p. 41-100, EINIT p. 41-46): [15:0] = 0100H, [2655:16] = 330
 * bytes of FFH, [2815:2656] = 2004000501020403650148866009060D30313000H - the first 352 bytes of
 * the big-endian EMSA-PKCS1-v1_5 encoding of a SHA-256 digest: 00 01 FF..FF 00 || DER prefix
 */
static void sgx_pkcs_padding(uint8_t pad[352])
{
    pad[0] = 0x00;
    pad[1] = 0x01;
    memset(pad + 2, 0xff, 330);
    pad[332] = 0x00;
    memcpy(pad + 333, sgx_der_sha256, 19);
}

/*
 * The SIGSTRUCT signature check of EINIT: SIGNATURE^3 mod MODULUS (via q1, q2) must be the
 * EMSA-PKCS1-v1_5 encoding (RFC 3447 9.2, DER DigestInfo, SHA-256) of the signed fields - bytes
 * 0-127 and 900-1027 of SIGSTRUCT (the "Y" rows of Table 38-20). Returns the 352-byte top of the
 * encoded message (TMP_SIG_PADDING, big-endian byte order like HARDCODED_PKCS1_5_PADDING).
 */
static bool sgx_rsa_verify(const uint8_t *sig, uint8_t padding[352])
{
    uint32_t n[SGX_BN], s[SGX_BN], q1[SGX_BN], q2[SGX_BN], r1[SGX_BN], r2[SGX_BN];
    uint8_t em[384], signed_part[256], digest[32], want[352];
    int i;

    sgx_bn_load(n, sig + 128);
    sgx_bn_load(s, sig + 516);
    sgx_bn_load(q1, sig + 1040);
    sgx_bn_load(q2, sig + 1424);
    if (sgx_bn_cmp(s, n, SGX_BN) >= 0 ||
        !sgx_bn_mulmod_q(s, s, q1, n, r1) || !sgx_bn_mulmod_q(s, r1, q2, n, r2)) {
        return false;
    }
    for (i = 0; i < SGX_BN; i++) {
        stl_be_p(em + 384 - 4 * (i + 1), r2[i]);
    }
    memcpy(signed_part, sig, 128);
    memcpy(signed_part + 128, sig + 900, 128);
    sgx_sha256(signed_part, 256, digest);
    sgx_pkcs_padding(want);
    if (memcmp(em, want, 352) || memcmp(em + 352, digest, 32)) {
        return false;
    }
    memcpy(padding, em, 352);
    return true;
}

/* ---------------------------------------------------------------------------------------------
 * Common machinery of the leaf functions (U1262)
 * ------------------------------------------------------------------------------------------- */

/* SECS layout (Table 38-2) and the model's implementation-specific fields in its reserved area */
#define SECS_SIZE           0
#define SECS_BASEADDR       8
#define SECS_SSAFRAMESIZE   16
#define SECS_MISCSELECT     20
#define SECS_CET_LEG        24
#define SECS_CET_ATTR       32
#define SECS_ATTRIBUTES     48
#define SECS_XFRM           56
#define SECS_MRENCLAVE      64
#define SECS_MRSIGNER       128
#define SECS_CONFIGID       192
#define SECS_ISVPRODID      256
#define SECS_ISVSVN         258
#define SECS_CONFIGSVN      260
/* ours (Table 38-2: "EID ... PAD ... location is implementation specific") */
#define SECS_EID            0x200   /* 8 bytes */
#define SECS_MR_COUNT       0x208   /* 8: the MRENCLAVE update counter (64-byte blocks) */
#define SECS_ISVFAMILYID    0x210   /* 16 (KSS only: 0) */
#define SECS_ISVEXTPRODID   0x220   /* 16 (KSS only: 0) */
#define SECS_EPOCH          0x230   /* 8: the ETRACK epoch (the tracking model) */
#define SECS_PADDING        0x260   /* 352: TMP_SIG_PADDING of EINIT */

/* TCS layout (Table 38-6) */
#define TCS_STATE           0
#define TCS_FLAGS           8
#define TCS_OSSA            16
#define TCS_CSSA            24
#define TCS_NSSA            28
#define TCS_OENTRY          32
#define TCS_AEP             40
#define TCS_OFSBASE         48
#define TCS_OGSBASE         56
#define TCS_FSLIMIT         64
#define TCS_GSLIMIT         68
#define TCS_OCETSSA         72
#define TCS_PREVSSP         80
#define TCS_RESERVED        88

/* error codes (Table 41-3) */
#define SGX_INVALID_SIG_STRUCT      1
#define SGX_INVALID_ATTRIBUTE       2
#define SGX_BLKSTATE                3
#define SGX_INVALID_MEASUREMENT     4
#define SGX_NOTBLOCKABLE            5
#define SGX_PG_INVLD                6
#define SGX_EPC_PAGE_CONFLICT       7
#define SGX_INVALID_SIGNATURE       8
#define SGX_MAC_COMPARE_FAIL        9
#define SGX_PAGE_NOT_BLOCKED        10
#define SGX_NOT_TRACKED             11
#define SGX_VA_SLOT_OCCUPIED        12
#define SGX_CHILD_PRESENT           13
#define SGX_ENCLAVE_ACT             14
#define SGX_ENTRYEPOCH_LOCKED       15
#define SGX_INVALID_EINITTOKEN      16
#define SGX_PREV_TRK_INCMPL         17
#define SGX_PG_IS_SECS              18
#define SGX_PAGE_ATTRIBUTES_MISMATCH 19
#define SGX_PAGE_NOT_MODIFIABLE     20
#define SGX_PAGE_NOT_DEBUGGABLE     21
#define SGX_INVALID_CPUSVN          32
#define SGX_INVALID_ISVSVN          64
#define SGX_UNMASKED_EVENT          128
#define SGX_INVALID_KEYNAME         256

#define SGX_PFEC_SGX        (1u << 15)

typedef struct SgxOp {
    CPUX86State *env;
    uintptr_t ra;
    bool m64;                   /* IA32_EFER.LMA = 1 && CS.L = 1 */
    uint32_t flags;             /* the arithmetic flags the leaf ends with (CC_SRC) */
    uint64_t next_eip;          /* ENCLU (U1263): the RIP of the next instruction */
    bool single_step;           /* ENCLU (U1263): a single-step #DB is pending after the leaf */
} SgxOp;

static AddressSpace *sgx_as(CPUX86State *env)
{
    return &env->uc->address_space_memory;
}

static void sgx_pread(CPUX86State *env, uint64_t pa, void *buf, size_t n)
{
    cpu_physical_memory_read(sgx_as(env), pa, buf, n);
}

static void sgx_pwrite(CPUX86State *env, uint64_t pa, const void *buf, size_t n)
{
    cpu_physical_memory_write(sgx_as(env), pa, buf, n);
}

static uint64_t sgx_pld64(CPUX86State *env, uint64_t pa)
{
    uint8_t b[8];

    sgx_pread(env, pa, b, 8);
    return ldq_le_p(b);
}

static uint32_t sgx_pld32(CPUX86State *env, uint64_t pa)
{
    uint8_t b[4];

    sgx_pread(env, pa, b, 4);
    return ldl_le_p(b);
}

static void sgx_pst64(CPUX86State *env, uint64_t pa, uint64_t v)
{
    uint8_t b[8];

    stq_le_p(b, v);
    sgx_pwrite(env, pa, b, 8);
}

static void sgx_pst32(CPUX86State *env, uint64_t pa, uint32_t v)
{
    uint8_t b[4];

    stl_le_p(b, v);
    sgx_pwrite(env, pa, b, 4);
}

/* the EPCM entry of the EPC page that contains 'pa', NULL outside the EPC */
static X86SgxEpcm *sgx_epcm(CPUX86State *env, uint64_t pa)
{
    X86SgxPkg *p = env->sgx;

    if (!p || pa < p->epc_base || pa - p->epc_base >= p->epc_size) {
        return NULL;
    }
    return &p->epcm[(pa - p->epc_base) >> 12];
}

static uint64_t sgx_epcm_pa(CPUX86State *env, const X86SgxEpcm *m)
{
    return env->sgx->epc_base + ((uint64_t)(m - env->sgx->epcm) << 12);
}

/* an implicit register operand as an effective address: 64 bits in 64-bit mode, else 32 bits */
static uint64_t sgx_reg(SgxOp *o, int reg)
{
    return o->m64 ? o->env->regs[reg] : (uint32_t)o->env->regs[reg];
}

static void QEMU_NORETURN sgx_gp(SgxOp *o)
{
    raise_exception_err_ra(o->env, EXCP0D_GPF, 0, o->ra);
}

/*
 * DS:ea -> linear address (Vol3D 41.1, 42.3.2 / 42.3.3: DS only, segment overrides and the
 * address-size prefix ignored). Outside 64-bit mode the DS limit and its write permission are
 * checked (#GP(0)); ENCLS / ENCLU already made sure DS is usable and expand-up. In 64-bit mode the
 * address is used as is; a non-canonical one faults in the page walk (#GP(0)).
 */
static uint64_t sgx_la(SgxOp *o, uint64_t ea, uint32_t size, bool write)
{
    SegmentCache *ds = &o->env->segs[R_DS];

    if (o->m64) {
        return ea;
    }
    if (ea + size - 1 > ds->limit || (write && !(ds->flags & DESC_W_MASK))) {
        sgx_gp(o);
    }
    return (uint32_t)(ds->base + ea);
}

/*
 * The physical address of a linear address through the current paging structures with the
 * access rights of 'write' at the current CPL - the page walk of an explicit access (38.5.3.1):
 * ordinary #PF / #GP faults are raised here; no TLB entry is made (excp_helper.c: env->sgx_xlate).
 */
static uint64_t sgx_xlate(SgxOp *o, uint64_t la, bool write)
{
    CPUX86State *env = o->env;

    env->sgx_xlate = 1;
    env->sgx_xlate_pa = 0;
    x86_cpu_tlb_fill(env_cpu(env), la, 1, write ? MMU_DATA_STORE : MMU_DATA_LOAD,
                     cpu_mmu_index(env, false), false, o->ra);
    env->sgx_xlate = 0;
    return env->sgx_xlate_pa;
}

/*
 * An SGX-induced page fault (Vol3A 5.7 / 7.15): P = 1, W/R of the access, U/S = CPL 3, SGX[15].
 * CR2 bits 11:0 are cleared for a #PF during enclave-mode execution (Vol3A 5.7 p. 5-37).
 */
static void QEMU_NORETURN sgx_pf(SgxOp *o, uint64_t la, bool write)
{
    CPUX86State *env = o->env;
    uint32_t ec = PG_ERROR_P_MASK | SGX_PFEC_SGX | (write ? PG_ERROR_W_MASK : 0) |
                  ((env->hflags & HF_CPL_MASK) == 3 ? PG_ERROR_U_MASK : 0);

    env->cr[2] = env->sgx_enclave_mode ? la & ~0xfffULL : la;
    raise_exception_err_ra(env, EXCP0E_PAGE, ec, o->ra);
}

/* "IF (DS:la does not resolve within an EPC) THEN #PF(DS:la)": the EPCM entry of DS:la */
static X86SgxEpcm *sgx_epc_operand(SgxOp *o, uint64_t la, bool write, uint64_t *pa)
{
    uint64_t p = sgx_xlate(o, la, write);
    X86SgxEpcm *m = sgx_epcm(o->env, p);

    if (!m) {
        sgx_pf(o, la, write);
    }
    *pa = p;
    return m;
}

/* explicit accesses to non-EPC operands (linear addresses, ordinary access rights and faults) */
static uint64_t sgx_ld64(SgxOp *o, uint64_t la)
{
    return cpu_ldq_data_ra(o->env, la, o->ra);
}

static void sgx_st64(SgxOp *o, uint64_t la, uint64_t v)
{
    cpu_stq_data_ra(o->env, la, v, o->ra);
}

static void sgx_ldbuf(SgxOp *o, uint64_t la, uint8_t *buf, size_t n)
{
    size_t i;

    for (i = 0; i < n; i += 8) {
        stq_le_p(buf + i, cpu_ldq_data_ra(o->env, la + i, o->ra));
    }
}

static void sgx_stbuf(SgxOp *o, uint64_t la, const uint8_t *buf, size_t n)
{
    size_t i;

    for (i = 0; i < n; i += 8) {
        cpu_stq_data_ra(o->env, la + i, ldq_le_p(buf + i), o->ra);
    }
}

static bool sgx_zero(const uint8_t *p, size_t n)
{
    size_t i;

    for (i = 0; i < n; i++) {
        if (p[i]) {
            return false;
        }
    }
    return true;
}

/* SECINFO (Table 38-16 / 38-17): reserved fields - FLAGS bits 7:6 and 63:16, bytes 8-63 */
static bool sgx_secinfo_rsvd(const uint8_t si[64])
{
    return (ldq_le_p(si) & ~0xff3fULL) || !sgx_zero(si + 8, 56);
}

#define SI_R(si)        ((si)[0] & 1)
#define SI_W(si)        (((si)[0] >> 1) & 1)
#define SI_X(si)        (((si)[0] >> 2) & 1)
#define SI_PENDING(si)  (((si)[0] >> 3) & 1)
#define SI_MODIFIED(si) (((si)[0] >> 4) & 1)
#define SI_PR(si)       (((si)[0] >> 5) & 1)
#define SI_PT(si)       ((si)[1])

/* the processor's canonical rule for SECS.BASEADDR (CPUID.80000008H:EAX[15:8]: 57 with LA57) */
static bool sgx_canonical(CPUX86State *env, uint64_t a)
{
    int shift = (env->features[FEAT_7_0_ECX] & CPUID_7_0_ECX_LA57) ? 64 - 57 : 64 - 48;

    return (uint64_t)((int64_t)(a << shift) >> shift) == a;
}

/* the 64-bit ATTRIBUTES[127:64] = XFRM legality: what XSETBV would accept in XCR0 (42.7.3) */
static bool sgx_xfrm_legal(CPUX86State *env, uint64_t xfrm)
{
    uint32_t lo, hi, d;
    const uint64_t avx512 = XSTATE_OPMASK_MASK | XSTATE_ZMM_Hi256_MASK | XSTATE_Hi16_ZMM_MASK;

    cpu_x86_cpuid(env, 0xd, 0, &lo, &d, &d, &hi);
    if ((xfrm & 3) != 3 || (xfrm >> 63) || (xfrm & ~(((uint64_t)hi << 32) | lo))) {
        return false;
    }
    if ((xfrm ^ (xfrm * (XSTATE_BNDCSR_MASK / XSTATE_BNDREGS_MASK))) & XSTATE_BNDCSR_MASK) {
        return false;
    }
    if ((xfrm & avx512) &&
        (xfrm & (avx512 | XSTATE_SSE_MASK | XSTATE_YMM_MASK)) !=
            (avx512 | XSTATE_SSE_MASK | XSTATE_YMM_MASK)) {
        return false;
    }
    if (((xfrm >> XSTATE_XTILE_CFG_BIT) ^ (xfrm >> XSTATE_XTILE_DATA_BIT)) & 1) {
        return false;
    }
    return true;
}

/* compute_xsave_size(XFRM) of 42.7.2.2 (CPUID.0DH.x as the CPU reports it) */
static uint64_t sgx_xsave_size(CPUX86State *env, uint64_t xfrm)
{
    uint64_t offset = 576, last = 0;
    uint32_t a, b, c, d;
    int x;

    for (x = 2; x < 64; x++) {
        if ((xfrm >> x) & 1) {
            cpu_x86_cpuid(env, 0xd, x, &a, &b, &c, &d);
            if (b >= offset + last) {
                offset = b;
                last = a;
            }
        }
    }
    return offset + last;
}

/* the MISC region size (38.9.2): from the highest MISCSELECT bit; EXINFO (bit 0) = 16 bytes */
static uint64_t sgx_misc_size(uint32_t miscselect)
{
    return (miscselect & 1) ? 16 : 0;
}

#define SGX_GPR_SIZE 184

/* ---- MRENCLAVE: the SHA-256 running state in the SECS (ECREATE / EADD / EEXTEND / EINIT) ---- */

static void sgx_mr_update(CPUX86State *env, uint64_t secs, const uint8_t blk[64])
{
    uint32_t h[8];
    uint8_t b[32];
    int i;

    sgx_pread(env, secs + SECS_MRENCLAVE, b, 32);
    for (i = 0; i < 8; i++) {
        h[i] = ldl_le_p(b + 4 * i);
    }
    sgx_sha256_block(h, blk);
    for (i = 0; i < 8; i++) {
        stl_le_p(b + 4 * i, h[i]);
    }
    sgx_pwrite(env, secs + SECS_MRENCLAVE, b, 32);
    sgx_pst64(env, secs + SECS_MR_COUNT, sgx_pld64(env, secs + SECS_MR_COUNT) + 1);
}

/* SHA256FINAL(MRENCLAVE, update count * 512): the FIPS 180-4 digest byte string */
static void sgx_mr_final(CPUX86State *env, uint64_t secs, uint8_t out[32])
{
    uint32_t h[8];
    uint8_t b[32], pad[64] = {0x80};
    int i;

    sgx_pread(env, secs + SECS_MRENCLAVE, b, 32);
    for (i = 0; i < 8; i++) {
        h[i] = ldl_le_p(b + 4 * i);
    }
    stq_be_p(pad + 56, sgx_pld64(env, secs + SECS_MR_COUNT) * 512);
    sgx_sha256_block(h, pad);
    for (i = 0; i < 8; i++) {
        stl_be_p(out + 4 * i, h[i]);
    }
}

/* the SECS page an EPC page belongs to (its EPCM SECS identifier) */
static uint64_t sgx_secs_of(const X86SgxEpcm *m)
{
    return m->secs;
}

static bool sgx_secs_initialized(CPUX86State *env, uint64_t secs)
{
    return sgx_pld64(env, secs + SECS_ATTRIBUTES) & SGX_ATTR_INIT;
}

/* "DS:RCX has an EPC page associated with it": any valid EPCM entry of that enclave but its SECS */
static bool sgx_child_present(CPUX86State *env, uint64_t secs)
{
    X86SgxPkg *p = env->sgx;
    uint64_t i;

    for (i = 0; i < p->npages; i++) {
        if (p->epcm[i].valid && p->epcm[i].pt != SGX_PT_SECS && p->epcm[i].secs == secs) {
            return true;
        }
    }
    return false;
}

/* ---------------------------------------------------------------------------------------------
 * ENCLS leaves (Vol3D 41.3)
 * ------------------------------------------------------------------------------------------- */

/* ECREATE (EAX = 0, p. 41-25..41-29) */
static void sgx_ecreate(SgxOp *o)
{
    CPUX86State *env = o->env;
    uint64_t pi = sgx_la(o, sgx_reg(o, R_EBX), 32, false);
    uint64_t rcx = sgx_la(o, sgx_reg(o, R_ECX), 4096, true);
    uint64_t srcpge, secinfo, pa, size, base, xfrm, attr, xsize;
    uint32_t ssafs, misc, a, b, c, d;
    uint8_t si[64], page[4096], blk[64];
    X86SgxEpcm *m;

    if (pi & 31) {
        sgx_gp(o);
    }
    if (rcx & 0xfff) {
        sgx_gp(o);
    }
    m = sgx_epc_operand(o, rcx, true, &pa);
    srcpge = sgx_ld64(o, pi + 8);
    secinfo = sgx_ld64(o, pi + 16);
    if (!o->m64) {
        srcpge = (uint32_t)srcpge;
        secinfo = (uint32_t)secinfo;
    }
    if ((srcpge & 0xfff) || (secinfo & 63)) {
        sgx_gp(o);
    }
    if (sgx_ld64(o, pi) != 0 || sgx_ld64(o, pi + 24) != 0) {    /* LINADDR, SECS */
        sgx_gp(o);
    }
    sgx_ldbuf(o, sgx_la(o, secinfo, 64, false), si, 64);
    if (sgx_secinfo_rsvd(si) || SI_PT(si) != SGX_PT_SECS) {
        sgx_gp(o);
    }
    if (m->valid) {
        sgx_pf(o, rcx, true);
    }
    /* copy 4 KB from the source page into the EPC page; the checks look at the copy */
    sgx_ldbuf(o, sgx_la(o, srcpge, 4096, false), page, 4096);
    sgx_pwrite(env, pa, page, 4096);
    size = ldq_le_p(page + SECS_SIZE);
    base = ldq_le_p(page + SECS_BASEADDR);
    ssafs = ldl_le_p(page + SECS_SSAFRAMESIZE);
    misc = ldl_le_p(page + SECS_MISCSELECT);
    attr = ldq_le_p(page + SECS_ATTRIBUTES);
    xfrm = ldq_le_p(page + SECS_XFRM);
    if ((xfrm & 3) != 3 || !sgx_xfrm_legal(env, xfrm)) {
        sgx_gp(o);
    }
    /* CET: ATTRIBUTES.CET is not settable (12H.1:EAX[6] = 0): CET_ATTRIBUTES and the legacy
       bitmap offset must be 0 (and are reserved fields then) */
    if (ldq_le_p(page + SECS_CET_LEG) || page[SECS_CET_ATTR]) {
        sgx_gp(o);
    }
    /* unsupported MISCSELECT bits (docs/sgx.md 4: the pseudocode's "!(EBX & MISCSELECT)") */
    if (misc & ~NOVMP_SGX_12_0_EBX) {
        sgx_gp(o);
    }
    xsize = sgx_xsave_size(env, xfrm) + SGX_GPR_SIZE + sgx_misc_size(misc);
    if ((uint64_t)ssafs * 4096 < xsize) {
        sgx_gp(o);
    }
    x86_sgx_cpuid(env, 0, &a, &b, &c, &d);
    if (attr & SGX_ATTR_MODE64BIT) {
        if (!sgx_canonical(env, base) || size >= (1ULL << ((d >> 8) & 0xff))) {
            sgx_gp(o);
        }
    } else if ((base >> 32) || size >= (1ULL << (d & 0xff))) {
        sgx_gp(o);
    }
    if (size < 8192 || (size & (size - 1))) {
        sgx_gp(o);
    }
    if (base & (size - 1)) {
        sgx_gp(o);
    }
    x86_sgx_cpuid(env, 1, &a, &b, &c, &d);
    if ((attr & ~(((uint64_t)b << 32) | a)) || (xfrm & ~(((uint64_t)d << 32) | c))) {
        sgx_gp(o);
    }
    /* reserved: 33-47 (CET_ATTRIBUTES is reserved without 12H.1:EAX[6]), 96-127, 160-191,
       262-4095 ("must be cleared prior to executing ECREATE") */
    if (!sgx_zero(page + 33, 15) || !sgx_zero(page + 96, 32) || !sgx_zero(page + 160, 32) ||
        !sgx_zero(page + 262, 4096 - 262)) {
        sgx_gp(o);
    }
    /* CONFIGID / CONFIGSVN need ATTRIBUTES.KSS, which is not settable */
    if (!sgx_zero(page + SECS_CONFIGID, 64) || lduw_le_p(page + SECS_CONFIGSVN)) {
        sgx_gp(o);
    }
    /* commit: the SECS, its MRENCLAVE log and the EPCM entry */
    stw_le_p(page + SECS_ISVSVN, 0);
    stw_le_p(page + SECS_ISVPRODID, 0);
    for (a = 0; a < 8; a++) {
        stl_le_p(page + SECS_MRENCLAVE + 4 * a, sgx_sha_h0[a]);
    }
    stq_le_p(page + SECS_MR_COUNT, 0);
    stq_le_p(page + SECS_EID, env->sgx->next_eid++);
    sgx_pwrite(env, pa, page, 4096);
    memset(blk, 0, 64);
    memcpy(blk, "ECREATE", 8);                  /* 0045544145524345H */
    stl_le_p(blk + 8, ssafs);
    stq_le_p(blk + 12, size);
    /* TMPUPDATEFIELD[223:160] = CET_LEG_BITMAP_OFFSET with CET_IBT: 0 here (checked above) */
    sgx_mr_update(env, pa, blk);
    memset(m, 0, sizeof(*m));
    m->pt = SGX_PT_SECS;
    m->secs = pa;
    m->valid = 1;
}

/* EADD (EAX = 1, p. 41-13..41-17) */
static void sgx_eadd(SgxOp *o)
{
    CPUX86State *env = o->env;
    uint64_t pi = sgx_la(o, sgx_reg(o, R_EBX), 32, false);
    uint64_t rcx = sgx_la(o, sgx_reg(o, R_ECX), 4096, true);
    uint64_t srcpge, secs_ea, secinfo, linaddr, pa, secs_la, secs, base, size;
    uint8_t si[64], page[4096], blk[64];
    X86SgxEpcm *m, *sm;
    int pt;

    if (pi & 31) {
        sgx_gp(o);
    }
    if (rcx & 0xfff) {
        sgx_gp(o);
    }
    m = sgx_epc_operand(o, rcx, true, &pa);
    srcpge = sgx_ld64(o, pi + 8);
    secs_ea = sgx_ld64(o, pi + 24);
    secinfo = sgx_ld64(o, pi + 16);
    linaddr = sgx_ld64(o, pi);
    if (!o->m64) {
        srcpge = (uint32_t)srcpge;
        secs_ea = (uint32_t)secs_ea;
        secinfo = (uint32_t)secinfo;
    }
    if ((srcpge & 0xfff) || (secs_ea & 0xfff) || (secinfo & 63) || (linaddr & 0xfff)) {
        sgx_gp(o);
    }
    secs_la = sgx_la(o, secs_ea, 4096, false);
    sm = sgx_epc_operand(o, secs_la, false, &secs);
    sgx_ldbuf(o, sgx_la(o, secinfo, 64, false), si, 64);
    pt = SI_PT(si);
    /* PT_SS_FIRST / PT_SS_REST need 12H.1:EAX[6] (CET), not enumerated */
    if (sgx_secinfo_rsvd(si) || (pt != SGX_PT_REG && pt != SGX_PT_TCS)) {
        sgx_gp(o);
    }
    if (m->valid) {
        sgx_pf(o, rcx, true);
    }
    if (!sm->valid || sm->pt != SGX_PT_SECS) {
        sgx_pf(o, secs_la, false);
    }
    /* copy 4 KB from the source page; the type-specific checks look at the copy */
    sgx_ldbuf(o, sgx_la(o, srcpge, 4096, false), page, 4096);
    sgx_pwrite(env, pa, page, 4096);
    if (pt == SGX_PT_TCS) {
        if (!sgx_zero(page + TCS_RESERVED, 4096 - TCS_RESERVED)) {
            sgx_gp(o);
        }
        if (!(sgx_pld64(env, secs + SECS_ATTRIBUTES) & SGX_ATTR_MODE64BIT) &&
            ((ldl_le_p(page + TCS_FSLIMIT) & 0xfff) != 0xfff ||
             (ldl_le_p(page + TCS_GSLIMIT) & 0xfff) != 0xfff)) {
            sgx_gp(o);
        }
        if ((env->features[FEAT_7_0_ECX] & CPUID_7_0_ECX_CET_SHSTK) && ldq_le_p(page + TCS_PREVSSP)) {
            sgx_gp(o);
        }
    } else if (SI_W(si) && !SI_R(si)) {
        sgx_gp(o);
    }
    base = sgx_pld64(env, secs + SECS_BASEADDR);
    size = sgx_pld64(env, secs + SECS_SIZE);
    if (linaddr < base || linaddr >= base + size) {
        sgx_gp(o);
    }
    if (sgx_secs_initialized(env, secs)) {
        sgx_gp(o);
    }
    if (pt == SGX_PT_TCS) {
        si[0] &= ~7;                                    /* EPCM.RWX := 0 for a TCS */
        sgx_pst64(env, pa + TCS_FLAGS, ldq_le_p(page + TCS_FLAGS) & ~1ULL);   /* DBGOPTIN := 0 */
        sgx_pst32(env, pa + TCS_CSSA, 0);
        sgx_pst64(env, pa + TCS_AEP, 0);
        sgx_pst64(env, pa + TCS_STATE, 0);
    }
    memset(blk, 0, 64);
    memcpy(blk, "EADD", 4);                         /* 0000000044444145H */
    stq_le_p(blk + 8, linaddr - base);
    memcpy(blk + 16, si, 48);                       /* SCRATCH_SECINFO[375:0] */
    sgx_mr_update(env, secs, blk);
    memset(m, 0, sizeof(*m));
    m->r = SI_R(si);
    m->w = SI_W(si);
    m->x = SI_X(si);
    m->pt = pt;
    m->addr = linaddr;
    m->secs = secs;
    m->valid = 1;
}

/* EEXTEND (EAX = 6, p. 41-38..41-40) */
static void sgx_eextend(SgxOp *o)
{
    CPUX86State *env = o->env;
    uint64_t rbx = sgx_la(o, sgx_reg(o, R_EBX), 4096, false);
    uint64_t rcx = sgx_la(o, sgx_reg(o, R_ECX), 256, false);
    uint64_t secs_pa, pa, secs, off;
    uint8_t blk[64], chunk[256];
    X86SgxEpcm *m;
    int i;

    if (rbx & 0xfff) {
        sgx_gp(o);
    }
    sgx_epc_operand(o, rbx, false, &secs_pa);
    if (rcx & 0xff) {
        sgx_gp(o);
    }
    m = sgx_epc_operand(o, rcx, false, &pa);
    if (!m->valid || (m->pt != SGX_PT_REG && m->pt != SGX_PT_TCS)) {
        sgx_pf(o, rcx, false);
    }
    secs = sgx_secs_of(m);
    if ((secs_pa & ~0xfffULL) != secs) {
        sgx_gp(o);
    }
    /* "If the enclave is already initialized" (exception list; the pseudocode's "checking or
       updating the initialized state" condition) */
    if (sgx_secs_initialized(env, secs)) {
        sgx_gp(o);
    }
    off = m->addr - sgx_pld64(env, secs + SECS_BASEADDR) + (pa & 0xfff);
    memset(blk, 0, 64);
    memcpy(blk, "EEXTEND", 8);                      /* 00444E4554584545H */
    stq_le_p(blk + 8, off);
    sgx_mr_update(env, secs, blk);
    sgx_pread(env, pa, chunk, 256);
    for (i = 0; i < 4; i++) {
        sgx_mr_update(env, secs, chunk + 64 * i);
    }
}

/* ---- key derivation (model placeholder, docs/sgx.md 3; ref_sgx.py keydep_bytes) ---- */

#define KEYNAME_EINITTOKEN      0
#define KEYNAME_PROVISION       1
#define KEYNAME_PROVISION_SEAL  2
#define KEYNAME_REPORT          3
#define KEYNAME_SEAL            4

typedef struct SgxKeyDep {
    uint16_t keyname, isvprodid, isvsvn, configsvn, keypolicy;
    uint8_t ownerepoch[16], attributes[16], attributesmask[16];
    uint8_t mrenclave[32], mrsigner[32], keyid[32];
    uint8_t seal_fuses[16], cpusvn[16];
    uint32_t miscselect, miscmask;
    uint8_t isvfamilyid[16], isvextprodid[16], configid[64];
    uint8_t padding[352];
} SgxKeyDep;

/*
 * derivekey(TMP_KEYDEPENDENCIES) (EINIT p. 41-46, EGETKEY p. 41-107, EREPORT p. 41-115): "the
 * derivation data and package specific value" - neither the algorithm nor a byte layout is in the
 * SDM. The model: AES-CMAC-128 keyed with the platform root key (UC_CTL_X86_SGX_PLATFORM) over this
 * 648-byte little-endian serialization (our layout, docs/sgx.md 3; ref_sgx.py keydep_bytes).
 */
static void sgx_derivekey(CPUX86State *env, const SgxKeyDep *k, uint8_t out[16])
{
    uint8_t b[648];

    memset(b, 0, sizeof(b));
    stw_le_p(b + 0, k->keyname);
    stw_le_p(b + 2, k->isvprodid);
    stw_le_p(b + 4, k->isvsvn);
    stw_le_p(b + 6, k->configsvn);
    memcpy(b + 8, k->ownerepoch, 16);
    memcpy(b + 24, k->attributes, 16);
    memcpy(b + 40, k->attributesmask, 16);
    memcpy(b + 56, k->mrenclave, 32);
    memcpy(b + 88, k->mrsigner, 32);
    memcpy(b + 120, k->keyid, 32);
    memcpy(b + 152, k->seal_fuses, 16);
    memcpy(b + 168, k->cpusvn, 16);
    stl_le_p(b + 184, k->miscselect);
    stl_le_p(b + 188, k->miscmask);
    stw_le_p(b + 192, k->keypolicy);
    memcpy(b + 196, k->isvfamilyid, 16);
    memcpy(b + 212, k->isvextprodid, 16);
    memcpy(b + 228, k->configid, 64);
    memcpy(b + 292, k->padding, 352);
    /* 644: CET_ATTRIBUTES, 645: CET_ATTRIBUTES_MASK (CET not enumerated: 0), 646-647: 0 */
    sgx_cmac(env->sgx->root_key, b, sizeof(b), out);
}

/* IA32_SGXLEPUBKEYHASH as a 32-byte digest string (byte 0 = bits 7:0 of 8CH, our reading) */
static void sgx_lehash(CPUX86State *env, uint8_t out[32])
{
    int i;

    for (i = 0; i < 4; i++) {
        stq_le_p(out + 8 * i, env->sgx_lehash[i]);
    }
}

/*
 * "CPUSVN must not be a configuration beyond CR_CPUSVN" (EINIT p. 41-46, EGETKEY p. 41-102).
 * CPUSVN "is not an integer and cannot be compared mathematically" (39.4.2.2); the elements it
 * reflects are not specified. The model compares byte by byte (every byte <= CR_CPUSVN's) - our
 * choice, docs/sgx.md 3 / 4.
 */
static bool sgx_cpusvn_beyond(CPUX86State *env, const uint8_t svn[16])
{
    int i;

    for (i = 0; i < 16; i++) {
        if (svn[i] > env->sgx->cpusvn[i]) {
            return true;
        }
    }
    return false;
}

/* an unmasked external event pending (INTR with RFLAGS.IF = 1 outside an interrupt shadow, or an
   NMI while NMIs are not blocked): EINIT's "event window" (p. 41-42, 41-44) */
static bool sgx_event_pending(CPUX86State *env)
{
    CPUState *cs = env_cpu(env);

    if ((cs->interrupt_request & CPU_INTERRUPT_NMI) && !(env->hflags2 & HF2_NMI_MASK)) {
        return true;
    }
    return (cs->interrupt_request & CPU_INTERRUPT_HARD) && (env->eflags & IF_MASK) &&
           !(env->hflags & HF_INHIBIT_IRQ_MASK);
}

/* EINIT (EAX = 2, p. 41-41..41-48) */
static void sgx_einit(SgxOp *o)
{
    CPUX86State *env = o->env;
    uint64_t rbx = sgx_la(o, sgx_reg(o, R_EBX), 1808, false);
    uint64_t rcx = sgx_la(o, sgx_reg(o, R_ECX), 4096, true);
    uint64_t rdx = sgx_la(o, sgx_reg(o, R_EDX), 304, false);
    static const uint8_t hdr[16] = {0x06, 0, 0, 0, 0xe1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0};
    static const uint8_t hdr2[16] = {1, 1, 0, 0, 0x60, 0, 0, 0, 0x60, 0, 0, 0, 1, 0, 0, 0};
    uint8_t sig[1808], tok[304], pad[352], mre[32], mrs[32], lehash[32], secs_attr[16];
    uint8_t key[16], mac[16];
    uint64_t pa;
    uint32_t vendor, misc;
    X86SgxEpcm *m;
    SgxKeyDep k;
    int i;

    if ((rbx & 0xfff) || (rcx & 0xfff)) {
        sgx_gp(o);
    }
    if (rdx & 0x1ff) {
        sgx_gp(o);
    }
    m = sgx_epc_operand(o, rcx, true, &pa);
    sgx_ldbuf(o, rbx, sig, 1808);
    sgx_ldbuf(o, rdx, tok, 304);
    o->flags = 0;                                   /* CF, PF, AF, OF, SF := 0; ZF below */
    vendor = ldl_le_p(sig + 16);
    /* "Reserved space": 44-127, 908-911 (CET_ATTRIBUTES / _MASK are reserved without
       12H.1:EAX[6]), 992-1007, 1028-1039 */
    if (memcmp(sig, hdr, 16) || (vendor != 0 && vendor != 0x8086) || memcmp(sig + 24, hdr2, 16) ||
        ldl_le_p(sig + 512) != 3 || !sgx_zero(sig + 44, 84) || !sgx_zero(sig + 908, 4) ||
        !sgx_zero(sig + 992, 16) || !sgx_zero(sig + 1028, 12)) {
        env->regs[R_EAX] = SGX_INVALID_SIG_STRUCT;
        goto fail;
    }
    if (sgx_event_pending(env)) {
        env->regs[R_EAX] = SGX_UNMASKED_EVENT;
        goto fail;
    }
    if (!sgx_rsa_verify(sig, pad)) {
        env->regs[R_EAX] = SGX_INVALID_SIGNATURE;
        goto fail;
    }
    if (!m->valid || m->pt != SGX_PT_SECS) {
        sgx_pf(o, rcx, true);
    }
    /* ISVFAMILYID needs ATTRIBUTES.KSS (not settable) */
    if (!sgx_zero(sig + 912, 16)) {
        env->regs[R_EAX] = SGX_INVALID_SIG_STRUCT;
        goto fail;
    }
    if (sgx_secs_initialized(env, pa)) {
        sgx_gp(o);                                  /* "If the enclave is already initialized" */
    }
    sgx_mr_final(env, pa, mre);
    if (memcmp(sig + 960, mre, 32)) {
        env->regs[R_EAX] = SGX_INVALID_MEASUREMENT;
        goto fail;
    }
    sgx_sha256(sig + 128, 384, mrs);
    sgx_lehash(env, lehash);
    sgx_pread(env, pa + SECS_ATTRIBUTES, secs_attr, 16);
    if ((secs_attr[0] & SGX_ATTR_EINITTOKEN_KEY) && memcmp(mrs, lehash, 32)) {
        env->regs[R_EAX] = SGX_INVALID_ATTRIBUTE;   /* CONTROLLED_ATTRIBUTES = 20H */
        goto fail;
    }
    for (i = 0; i < 16; i++) {
        if ((secs_attr[i] & sig[944 + i]) != (sig[928 + i] & sig[944 + i])) {
            env->regs[R_EAX] = SGX_INVALID_ATTRIBUTE;
            goto fail;
        }
    }
    misc = sgx_pld32(env, pa + SECS_MISCSELECT);
    if ((misc & ldl_le_p(sig + 904)) != (ldl_le_p(sig + 900) & ldl_le_p(sig + 904))) {
        env->regs[R_EAX] = SGX_INVALID_ATTRIBUTE;
        goto fail;
    }
    if (!(tok[0] & 1)) {
        if (memcmp(mrs, lehash, 32)) {
            env->regs[R_EAX] = SGX_INVALID_EINITTOKEN;
            goto fail;
        }
        goto commit;
    }
    if ((tok[240] & SGX_ATTR_DEBUG) && !(secs_attr[0] & SGX_ATTR_DEBUG)) {
        env->regs[R_EAX] = SGX_INVALID_EINITTOKEN;
        goto fail;
    }
    /* reserved: VALID[31:1], 4-47, 96-127, 160-191, 212-235 (212 = CET_MASKED_ATTRIBUTES_LE,
       reserved without 12H.1:EAX[6]) */
    if ((ldl_le_p(tok) & ~1u) || !sgx_zero(tok + 4, 44) || !sgx_zero(tok + 96, 32) ||
        !sgx_zero(tok + 160, 32) || !sgx_zero(tok + 212, 24)) {
        env->regs[R_EAX] = SGX_INVALID_EINITTOKEN;
        goto fail;
    }
    if (sgx_cpusvn_beyond(env, tok + 192)) {
        env->regs[R_EAX] = SGX_INVALID_CPUSVN;
        goto fail;
    }
    memset(&k, 0, sizeof(k));
    k.keyname = KEYNAME_EINITTOKEN;
    k.isvprodid = lduw_le_p(tok + 208);
    k.isvsvn = lduw_le_p(tok + 210);
    memcpy(k.ownerepoch, env->sgx->owner_epoch, 16);
    memcpy(k.attributes, tok + 240, 16);
    memcpy(k.mrsigner, lehash, 32);
    memcpy(k.keyid, tok + 256, 32);
    memcpy(k.seal_fuses, env->sgx->seal_fuses, 16);
    memcpy(k.cpusvn, tok + 192, 16);
    k.miscselect = ldl_le_p(tok + 236);
    sgx_pkcs_padding(k.padding);
    sgx_derivekey(env, &k, key);
    sgx_cmac(key, tok, 192, mac);
    if (memcmp(mac, tok + 288, 16)) {
        env->regs[R_EAX] = SGX_INVALID_EINITTOKEN;
        goto fail;
    }
    if (memcmp(tok + 64, mre, 32) || memcmp(tok + 128, mrs, 32)) {
        env->regs[R_EAX] = SGX_INVALID_MEASUREMENT;
        goto fail;
    }
    /* "SGX_INVALID_EINIT_ATTRIBUTE" is not in Table 41-3: SGX_INVALID_ATTRIBUTE (docs/sgx.md 4) */
    if (memcmp(tok + 48, secs_attr, 16)) {
        env->regs[R_EAX] = SGX_INVALID_ATTRIBUTE;
        goto fail;
    }
commit:
    sgx_pwrite(env, pa + SECS_MRENCLAVE, mre, 32);
    sgx_pwrite(env, pa + SECS_MRSIGNER, mrs, 32);
    sgx_pwrite(env, pa + SECS_ISVEXTPRODID, sig + 1008, 16);
    sgx_pwrite(env, pa + SECS_ISVPRODID, sig + 1024, 4);       /* ISVPRODID, ISVSVN */
    sgx_pwrite(env, pa + SECS_ISVFAMILYID, sig + 912, 16);
    sgx_pwrite(env, pa + SECS_PADDING, pad, 352);
    secs_attr[0] |= SGX_ATTR_INIT;
    sgx_pwrite(env, pa + SECS_ATTRIBUTES, secs_attr, 16);
    env->regs[R_EAX] = 0;
    return;
fail:
    o->flags = CC_Z;
}

/* EREMOVE (EAX = 3, p. 41-62..41-64) */
static void sgx_eremove(SgxOp *o)
{
    CPUX86State *env = o->env;
    uint64_t rcx = sgx_la(o, sgx_reg(o, R_ECX), 4096, true);
    uint64_t pa;
    X86SgxEpcm *m;

    if (rcx & 0xfff) {
        sgx_gp(o);
    }
    m = sgx_epc_operand(o, rcx, true, &pa);
    o->flags = 0;
    env->regs[R_EAX] = 0;
    /*
     * "IF (VALID = 0) or (PT = PT_TRIM AND MODIFIED = 0) THEN GOTO DONE" comes before "IF (PT =
     * PT_VA) OR (PT = PT_TRIM AND MODIFIED = 0) THEN VALID := 0": read literally the second rule
     * never removes a trimmed page (the first one leaves it valid). 39.5.9 step 6 says the OS
     * removes the trimmed page with EREMOVE - the model removes it (docs/sgx.md 4).
     */
    if (!m->valid) {
        return;
    }
    if (m->pt == SGX_PT_VA || (m->pt == SGX_PT_TRIM && !m->modified)) {
        m->valid = 0;
        return;
    }
    if (m->pt == SGX_PT_SECS) {
        if (sgx_child_present(env, pa)) {
            o->flags = CC_Z;
            env->regs[R_EAX] = SGX_CHILD_PRESENT;
            return;
        }
        m->valid = 0;
        return;
    }
    /* "Other threads active using SECS" (SGX_ENCLAVE_ACT): one logical processor, and EREMOVE
       runs outside enclave mode - never in the model */
    m->valid = 0;
}

/* EPA (EAX = 0AH, p. 41-60..41-61) */
static void sgx_epa(SgxOp *o)
{
    CPUX86State *env = o->env;
    uint64_t rcx = sgx_la(o, sgx_reg(o, R_ECX), 4096, true);
    static const uint8_t zero[4096];
    uint64_t pa;
    X86SgxEpcm *m;

    if (sgx_reg(o, R_EBX) != SGX_PT_VA || (rcx & 0xfff)) {
        sgx_gp(o);
    }
    m = sgx_epc_operand(o, rcx, true, &pa);
    if (m->valid) {
        sgx_pf(o, rcx, true);
    }
    sgx_pwrite(env, pa, zero, 4096);
    memset(m, 0, sizeof(*m));
    m->pt = SGX_PT_VA;
    m->valid = 1;
}

/* EBLOCK (EAX = 9, p. 41-22..41-24) */
static void sgx_eblock(SgxOp *o)
{
    CPUX86State *env = o->env;
    uint64_t rcx = sgx_la(o, sgx_reg(o, R_ECX), 4096, true);
    uint64_t pa;
    X86SgxEpcm *m;

    if (rcx & 0xfff) {
        sgx_gp(o);
    }
    m = sgx_epc_operand(o, rcx, true, &pa);
    o->flags = 0;
    env->regs[R_EAX] = 0;
    if (!m->valid) {
        o->flags = CC_Z;
        env->regs[R_EAX] = SGX_PG_INVLD;
        return;
    }
    if (m->pt != SGX_PT_REG && m->pt != SGX_PT_TCS && m->pt != SGX_PT_TRIM) {
        o->flags = CC_C;
        env->regs[R_EAX] = m->pt == SGX_PT_SECS ? SGX_PG_IS_SECS : SGX_NOTBLOCKABLE;
        return;
    }
    if (m->blocked) {
        o->flags = CC_C;
        env->regs[R_EAX] = SGX_BLKSTATE;
        return;
    }
    m->blocked = 1;
    m->bepoch = sgx_pld64(env, m->secs + SECS_EPOCH);   /* the tracking model */
}

/*
 * ETRACK (EAX = 0CH, p. 41-65..41-66). The tracking model (docs/sgx.md 3): ETRACK starts a new
 * epoch of the enclave, complete when every logical processor that was executing in the enclave
 * has left it. The model has one logical processor and ETRACK runs outside enclave mode: the
 * previous cycle is always complete (SGX_PREV_TRK_INCMPL never) and the new one completes at once;
 * pages blocked (EBLOCK) or modified (EMODPR / EMODT) before it count as tracked.
 */
static void sgx_etrack(SgxOp *o)
{
    CPUX86State *env = o->env;
    uint64_t rcx = sgx_la(o, sgx_reg(o, R_ECX), 4096, true);
    uint64_t pa;
    X86SgxEpcm *m;

    if (rcx & 0xfff) {
        sgx_gp(o);
    }
    m = sgx_epc_operand(o, rcx, true, &pa);
    if (!m->valid || m->pt != SGX_PT_SECS) {
        sgx_pf(o, rcx, true);
    }
    sgx_pst64(env, pa + SECS_EPOCH, sgx_pld64(env, pa + SECS_EPOCH) + 1);
    o->flags = 0;
    env->regs[R_EAX] = 0;
}

/*
 * EDBGRD (EAX = 4, p. 41-30..41-33) / EDBGWR (EAX = 5, p. 41-34..41-37). SGX_TCS_LIMIT ("the
 * architectural size of the TCS") is not given: the model uses 88 - the end of the TCS fields of
 * Table 38-6 (docs/sgx.md 4).
 */
#define SGX_TCS_LIMIT 88

static void sgx_edbg(SgxOp *o, bool wr)
{
    CPUX86State *env = o->env;
    int n = o->m64 ? 8 : 4;
    uint64_t rcx = sgx_la(o, sgx_reg(o, R_ECX), n, wr);
    uint64_t pa, v;
    X86SgxEpcm *m;

    if (rcx & (n - 1)) {
        sgx_gp(o);
    }
    m = sgx_epc_operand(o, rcx, wr, &pa);
    if (!m->valid) {
        sgx_pf(o, rcx, wr);
    }
    if (m->pt != SGX_PT_REG && m->pt != SGX_PT_TCS && (wr || m->pt != SGX_PT_VA)) {
        sgx_pf(o, rcx, wr);
    }
    o->flags = 0;
    if (m->pending || m->modified) {
        o->flags = CC_Z;
        env->regs[R_EAX] = SGX_PAGE_NOT_DEBUGGABLE;
        return;
    }
    if (m->pt == SGX_PT_TCS &&
        (wr ? (pa & 0xff8) != (TCS_FLAGS & 0xff8) : (pa & 0xfff) >= SGX_TCS_LIMIT)) {
        sgx_gp(o);
    }
    if (m->pt != SGX_PT_VA && !(sgx_pld64(env, m->secs + SECS_ATTRIBUTES) & SGX_ATTR_DEBUG)) {
        sgx_gp(o);
    }
    if (wr) {
        if (n == 8) {
            sgx_pst64(env, pa, env->regs[R_EBX]);
        } else {
            sgx_pst32(env, pa, (uint32_t)env->regs[R_EBX]);
        }
    } else if (m->pt == SGX_PT_VA) {
        v = sgx_pld64(env, pa & ~7ULL) & ~7ULL;
        env->regs[R_EBX] = v ? (n == 8 ? ~0ULL : 0xffffffffULL) : 0;
    } else {
        env->regs[R_EBX] = n == 8 ? sgx_pld64(env, pa) : sgx_pld32(env, pa);
    }
    env->regs[R_EAX] = 0;
}

/* ---------------------------------------------------------------------------------------------
 * ENCLS (Vol3D p. 41-8..41-9)
 * ------------------------------------------------------------------------------------------- */

/* the leaves the model enumerates: SGX1 (00H-0CH), SGX2 (0DH-0FH); EUPDATESVN (18H) is not */
static bool sgx_encls_leaf_valid(uint32_t leaf)
{
    return leaf <= 0x0c;
}

/*
 * NoVmp (ledger U1262): ENCLS. The translator raised #UD for LOCK / 66 / F2 / F3 / VEX (and routes
 * here only when the model enumerates SGX). Order of the Operation section: #UD for CR0.PE = 0,
 * virtual-8086 mode, SMM, CPUID.12H.0:EAX.SGX1 = 0 and CPL > 0; (VMX: "enable ENCLS exiting" -
 * pending, plan 1.J.12); #GP(0) for IA32_FEATURE_CONTROL.LOCK / SGX_ENABLE = 0, an unsupported
 * leaf, CR0.PG = 0, an expand-down DS outside 64-bit mode. A leaf that completes without a fault
 * sets IA32_SGX_SVN_STATUS.Lock (42.11.3: "the first successful execution of an Intel SGX
 * instruction that doesn't return an error code" - with RAX = 0 for the leaves that return one).
 */
void helper_encls(CPUX86State *env)
{
    SgxOp o;
    uint32_t leaf = (uint32_t)env->regs[R_EAX];

    o.env = env;
    o.ra = GETPC();
    o.m64 = (env->hflags & HF_CS64_MASK) != 0;      /* IA32_EFER.LMA = 1 && CS.L = 1 */
    o.flags = cpu_cc_compute_all(env, CC_OP) & (CC_O | CC_S | CC_Z | CC_A | CC_P | CC_C);
    o.next_eip = 0;
    o.single_step = false;
    if (!(env->cr[0] & CR0_PE_MASK) || (env->eflags & VM_MASK) || (env->hflags & HF_SMM_MASK) ||
        !x86_sgx_enumerated(env) || (env->hflags & HF_CPL_MASK)) {
        raise_exception_ra(env, EXCP06_ILLOP, o.ra);
    }
    if ((env->msr_ia32_feature_control & (FEATURE_CONTROL_LOCKED | FEATURE_CONTROL_SGX)) !=
            (FEATURE_CONTROL_LOCKED | FEATURE_CONTROL_SGX) ||
        !sgx_encls_leaf_valid(leaf) || !(env->cr[0] & CR0_PG_MASK)) {
        sgx_gp(&o);
    }
    if (!o.m64 && (env->segs[R_DS].flags & (DESC_S_MASK | DESC_CS_MASK | DESC_E_MASK)) ==
                      (DESC_S_MASK | DESC_E_MASK)) {
        sgx_gp(&o);                                 /* expand-down data segment */
    }
    switch (leaf) {
    case 0x00:
        sgx_ecreate(&o);
        break;
    case 0x01:
        sgx_eadd(&o);
        break;
    case 0x02:
        sgx_einit(&o);
        break;
    case 0x03:
        sgx_eremove(&o);
        break;
    case 0x04:
        sgx_edbg(&o, false);
        break;
    case 0x05:
        sgx_edbg(&o, true);
        break;
    case 0x06:
        sgx_eextend(&o);
        break;
    case 0x09:
        sgx_eblock(&o);
        break;
    case 0x0a:
        sgx_epa(&o);
        break;
    case 0x0c:
        sgx_etrack(&o);
        break;
    default:
        sgx_gp(&o);     /* 07H, 08H, 0BH (ELDB, ELDU, EWB) and SGX2: U1265 / U1266 */
        break;
    }
    if (!(o.flags & CC_Z)) {
        env->sgx_svn_lock = 1;
    }
    CC_SRC = o.flags;
}

/* ---------------------------------------------------------------------------------------------
 * Enclave entry and exit (U1263): EENTER, ERESUME, EEXIT (Vol3D 41.4), AEX (40.3, 40.4)
 * ------------------------------------------------------------------------------------------- */

/* GPRSGX (Table 38-9) */
#define GPR_RAX         0
#define GPR_RFLAGS      128
#define GPR_RIP         136
#define GPR_URSP        144
#define GPR_URBP        152
#define GPR_EXITINFO    160
#define GPR_FSBASE      168
#define GPR_GSBASE      176

/* TCS.STATE (Table 38-6): 0 = available, 1 = a logical processor executes in its context */
#define TCS_ACTIVE      1

static bool sgx_cet_enumerated(CPUX86State *env)
{
    return (env->features[FEAT_7_0_EDX] & CPUID_7_0_EDX_CET_IBT) ||
           (env->features[FEAT_7_0_ECX] & CPUID_7_0_ECX_CET_SHSTK);
}

/*
 * The EPCM checks of an SSA frame page and of the GPR area (EENTER p. 41-90, ERESUME p. 41-120):
 * "Check that DS:TMP_SSA_PAGE is read/write accessible" (a page walk for a write), it resolves to
 * an EPC page that is VALID, not BLOCKED / PENDING / MODIFIED, at that ENCLAVEADDRESS, PT_REG, of
 * the TCS's enclave, R = W = 1; else #PF. Returns the physical address of 'la'.
 */
static uint64_t sgx_ssa_page(SgxOp *o, uint64_t la, uint64_t secs)
{
    uint64_t pa;
    X86SgxEpcm *m = sgx_epc_operand(o, la, true, &pa);

    if (!m->valid || m->blocked || m->pending || m->modified ||
        m->addr != (la & ~0xfffULL) || m->pt != SGX_PT_REG || m->secs != secs || !m->r || !m->w) {
        sgx_pf(o, la, true);
    }
    return pa;
}

/* the FS / GS EENTER and ERESUME construct (p. 41-93 / 41-126) */
static void sgx_load_fsgs(CPUX86State *env, int seg, uint64_t base, uint32_t limit)
{
    uint32_t ds = env->segs[R_DS].flags;
    uint32_t flags = (1u << DESC_TYPE_SHIFT) |          /* type := 0001b */
                     (ds & DESC_W_MASK) |               /* W := DS[bit 9] */
                     DESC_S_MASK | (ds & DESC_DPL_MASK) | DESC_G_MASK | DESC_B_MASK |
                     DESC_P_MASK | (ds & DESC_AVL_MASK) | (ds & DESC_L_MASK);

    cpu_x86_load_seg_cache(env, seg, 0x0b, base, limit, flags);
}

static void sgx_restore_seg(CPUX86State *env, int seg, const SegmentCache *sc)
{
    cpu_x86_load_seg_cache(env, seg, sc->selector, sc->base, sc->limit, sc->flags);
}

static void sgx_set_xcr0(CPUX86State *env, uint64_t xcr0)
{
    env->xcr0 = xcr0;
    cpu_sync_bndcs_hflags(env);
    cpu_sync_avx_hflag(env);
}

/*
 * The CET part of EENTER / ERESUME (p. 41-92, 41-94): CPUID.12H.01H:EAX[6] (CET attributes of an
 * enclave) is 0 in the model, so TMP_IA32_U_CET = TMP_SSP = 0; with CET_IBT or CET_SS enumerated
 * the application's IA32_U_CET (and SSP with CET_SS) are saved and the enclave's loaded.
 */
static void sgx_cet_enter(CPUX86State *env)
{
    if (!sgx_cet_enumerated(env)) {
        return;
    }
    env->sgx_save_ucet = env->u_cet;
    if (env->features[FEAT_7_0_ECX] & CPUID_7_0_ECX_CET_SHSTK) {
        env->sgx_save_ssp = env->ssp;
        env->ssp = 0;
    }
    env->u_cet = 0;
    cpu_sync_cet_hflags(env);
}

/* EEXIT p. 41-98 / AEX p. 40-7: the application's CET state; its tracker waits for ENDBRANCH */
static void sgx_cet_exit(CPUX86State *env)
{
    if (!sgx_cet_enumerated(env)) {
        return;
    }
    env->u_cet = env->sgx_save_ucet;
    if (env->features[FEAT_7_0_ECX] & CPUID_7_0_ECX_CET_SHSTK) {
        env->ssp = env->sgx_save_ssp;
    }
    if ((env->cr[4] & CR4_CET_MASK) && (env->u_cet & CET_ENDBR_EN)) {
        env->u_cet = (env->u_cet | CET_TRACKER) & ~CET_SUPPRESS;
    }
    cpu_sync_cet_hflags(env);
}

/*
 * The checks EENTER and ERESUME share up to the CSSA check (p. 41-88..41-90, 41-118..41-120), in
 * the order of their Operation sections. 'write': ERESUME's TCS is a "read/write access", EENTER's
 * an "Enclave access" (their Memory Parameter Semantics tables). Returns the TCS physical address;
 * *secs = its SECS.
 */
static uint64_t sgx_entry_checks(SgxOp *o, uint64_t tcs_la, bool write, uint64_t *secs)
{
    CPUX86State *env = o->env;
    SegmentCache *sg = env->segs;
    uint64_t pa, flags, xfrm, attr;
    X86SgxEpcm *m;

    if (!o->m64) {
        /* DS usable and expand-up writable data (DS[11:9] = 001B); CS / DS / ES / SS bases 0 */
        if (!(sg[R_DS].flags & DESC_P_MASK) || ((sg[R_DS].flags >> 9) & 7) != 1) {
            sgx_gp(o);
        }
        if (sg[R_CS].base || sg[R_DS].base ||
            ((sg[R_ES].flags & DESC_P_MASK) && sg[R_ES].base) ||
            ((sg[R_SS].flags & DESC_P_MASK) && sg[R_SS].base) ||
            ((sg[R_SS].flags & DESC_P_MASK) && !(sg[R_SS].flags & DESC_B_MASK))) {
            sgx_gp(o);
        }
    }
    if (tcs_la & 0xfff) {
        sgx_gp(o);
    }
    m = sgx_epc_operand(o, tcs_la, write, &pa);
    if (o->m64 && !x86_ip_is_canonical(env, env->regs[R_ECX])) {
        sgx_gp(o);                                  /* the AEP */
    }
    if (!m->valid || m->blocked || m->addr != tcs_la || m->pt != SGX_PT_TCS || m->pending ||
        m->modified) {
        sgx_pf(o, tcs_la, write);
    }
    if ((sgx_pld64(env, pa + TCS_OSSA) & 0xfff) || (sgx_pld64(env, pa + TCS_OFSBASE) & 0xfff) ||
        (sgx_pld64(env, pa + TCS_OGSBASE) & 0xfff)) {
        sgx_gp(o);
    }
    *secs = m->secs;
    flags = sgx_pld64(env, pa + TCS_FLAGS);
    if (flags & ~3ULL) {
        sgx_gp(o);
    }
    if (!sgx_secs_initialized(env, *secs)) {
        sgx_gp(o);
    }
    attr = sgx_pld64(env, *secs + SECS_ATTRIBUTES);
    if (o->m64 != ((attr & SGX_ATTR_MODE64BIT) != 0)) {
        sgx_gp(o);
    }
    if (!(env->cr[4] & CR4_OSFXSR_MASK)) {
        sgx_gp(o);
    }
    xfrm = sgx_pld64(env, *secs + SECS_XFRM);
    if (!(env->cr[4] & CR4_OSXSAVE_MASK) ? xfrm != 3 : (xfrm & env->xcr0) != xfrm) {
        sgx_gp(o);
    }
    /* "(DS:RBX).CSSA.FLAGS.DBGOPTIN = 0 and (DS:RBX).CSSA.FLAGS.AEXNOTIFY !=
       TMP_SECS.ATTRIBUTES.AEXNOTIFY": TCS.FLAGS bits 0 / 1, ATTRIBUTES bit 10 (docs/quirks.md) */
    if (!(flags & 1) && ((flags >> 1) & 1) != ((attr >> 10) & 1)) {
        sgx_gp(o);
    }
    return pa;
}

/*
 * The pages of the SSA frame at 'ssa' (the XSAVE area of XFRM, "FOR EACH TMP_SSA_PAGE = TMP_SSA to
 * TMP_SSA + TMP_XSIZE" - the pages the area occupies, docs/quirks.md) and of its GPR area; outside
 * 64-bit mode the GPR area must lie in the DS segment. Caches CR_XSAVE_PAGE_n and CR_GPR_PA.
 */
static void sgx_ssa_checks(SgxOp *o, uint64_t ssa, uint64_t secs)
{
    CPUX86State *env = o->env;
    uint64_t xsize = sgx_xsave_size(env, sgx_pld64(env, secs + SECS_XFRM));
    uint64_t gpr = ssa + 4096ULL * sgx_pld32(env, secs + SECS_SSAFRAMESIZE) - SGX_GPR_SIZE;
    uint64_t p, gpa;
    int n = 0;

    if (!o->m64) {
        gpr = (uint32_t)gpr;
    }
    for (p = 0; p < xsize; p += 4096) {
        g_assert(n < SGX_XSAVE_PAGES);
        env->sgx_xsave_pa[n++] = sgx_ssa_page(o, o->m64 ? ssa + p : (uint32_t)(ssa + p), secs);
    }
    gpa = sgx_ssa_page(o, gpr, secs);
    if (!o->m64 && gpr + SGX_GPR_SIZE - 1 > env->segs[R_DS].limit) {
        sgx_gp(o);
    }
    env->sgx_xsave_n = n;
    env->sgx_gpr_pa = gpa;
    env->sgx_ssa_la = ssa;
}

/* TMP_TARGET: canonical in 64-bit mode, else within the CS limit */
static void sgx_target_check(SgxOp *o, uint64_t target)
{
    if (o->m64 ? !x86_ip_is_canonical(o->env, target) : target > o->env->segs[R_CS].limit) {
        sgx_gp(o);
    }
}

/* the proposed FS / GS (p. 41-91 / 41-122): canonical in 64-bit mode, else within DS */
static void sgx_fsgs_check(SgxOp *o, uint64_t base, uint32_t limit)
{
    CPUX86State *env = o->env;
    uint32_t b = (uint32_t)base, l = (uint32_t)(base + limit);

    if (o->m64) {
        if (!x86_ip_is_canonical(env, base)) {
            sgx_gp(o);
        }
    } else if (l < b ? env->segs[R_DS].limit < 0xffffffffu : l > env->segs[R_DS].limit) {
        sgx_gp(o);
    }
}

/* map the SSA frame's XSAVE area through CR_XSAVE_PAGE_n for an implicit access */
static void sgx_map_xsave(CPUX86State *env)
{
    CPUState *cs = env_cpu(env);
    int idx = cpu_mmu_index(env, false), i;

    for (i = 0; i < env->sgx_xsave_n; i++) {
        tlb_set_page_sgx_implicit(cs, env->sgx_ssa_la + 4096ULL * i, env->sgx_xsave_pa[i],
                                  cpu_get_mem_attrs(env), idx);
    }
}

/*
 * The commit EENTER and ERESUME share (p. 41-93 / 41-125): enclave mode, the cached SECS / ELRANGE /
 * TCS, the AEP, the saved FS / GS, the TCS busy (39.2.1 "Set the TCS as busy", Table 38-6 STATE =
 * 1), the new FS / GS, DBGOPTIN, breakpoint and TF suppression, CET, Flush_linear_context.
 */
static void sgx_enter(SgxOp *o, uint64_t tcs_pa, uint64_t tcs_la, uint64_t secs, uint64_t aep,
                      uint64_t fsbase, uint64_t gsbase)
{
    CPUX86State *env = o->env;

    env->sgx_enclave_mode = 1;
    env->sgx_secs = secs;
    env->sgx_elbase = sgx_pld64(env, secs + SECS_BASEADDR);
    env->sgx_elsize = sgx_pld64(env, secs + SECS_SIZE);
    env->sgx_tcs_pa = tcs_pa;
    env->sgx_tcs_la = tcs_la;
    sgx_pst64(env, tcs_pa + TCS_AEP, aep);
    sgx_pst64(env, tcs_pa + TCS_STATE, TCS_ACTIVE);
    env->sgx_save_fs = env->segs[R_FS];
    env->sgx_save_gs = env->segs[R_GS];
    sgx_load_fsgs(env, R_FS, fsbase, sgx_pld32(env, tcs_pa + TCS_FSLIMIT));
    sgx_load_fsgs(env, R_GS, gsbase, sgx_pld32(env, tcs_pa + TCS_GSLIMIT));
    env->sgx_dbgoptin = sgx_pld64(env, tcs_pa + TCS_FLAGS) & 1;
    if (!env->sgx_dbgoptin) {
        env->sgx_save_tf = (env->eflags & TF_MASK) != 0;
        env->eflags &= ~TF_MASK;
    }
    x86_sgx_bp_update(env);
    sgx_cet_enter(env);
    tlb_flush(env_cpu(env));
}

/* EENTER (ENCLU[2], p. 41-87..41-95) */
static void sgx_eenter(SgxOp *o)
{
    CPUX86State *env = o->env;
    uint64_t tcs_la = sgx_la(o, sgx_reg(o, R_EBX), 4096, false);
    uint64_t secs, tcs, base, ssa, target, fsbase, gsbase, xfrm, aep;
    uint32_t cssa;

    tcs = sgx_entry_checks(o, tcs_la, false, &secs);
    cssa = sgx_pld32(env, tcs + TCS_CSSA);
    if (cssa >= sgx_pld32(env, tcs + TCS_NSSA)) {
        sgx_gp(o);
    }
    base = sgx_pld64(env, secs + SECS_BASEADDR);
    ssa = sgx_pld64(env, tcs + TCS_OSSA) + base +
          4096ULL * sgx_pld32(env, secs + SECS_SSAFRAMESIZE) * cssa;
    target = sgx_pld64(env, tcs + TCS_OENTRY) + base;
    fsbase = sgx_pld64(env, tcs + TCS_OFSBASE) + base;
    gsbase = sgx_pld64(env, tcs + TCS_OGSBASE) + base;
    if (!o->m64) {
        ssa = (uint32_t)ssa;
        target = (uint32_t)target;
        fsbase = (uint32_t)fsbase;
        gsbase = (uint32_t)gsbase;
    }
    sgx_ssa_checks(o, ssa, secs);
    sgx_target_check(o, target);
    sgx_fsgs_check(o, fsbase, sgx_pld32(env, tcs + TCS_FSLIMIT));
    sgx_fsgs_check(o, gsbase, sgx_pld32(env, tcs + TCS_GSLIMIT));
    if (sgx_pld64(env, tcs + TCS_STATE) == TCS_ACTIVE) {
        sgx_gp(o);
    }
    /* commit: CR_TCS_LA.AEP := RCX precedes RCX := RIP */
    aep = sgx_reg(o, R_ECX);
    xfrm = sgx_pld64(env, secs + SECS_XFRM);
    if (env->cr[4] & CR4_OSXSAVE_MASK) {
        env->sgx_save_xcr0 = env->xcr0;
        sgx_set_xcr0(env, xfrm);
    }
    env->regs[R_ECX] = o->next_eip;                 /* RCX := RIP (of the next instruction) */
    env->eip = target;
    env->regs[R_EAX] = cssa;
    sgx_pst64(env, env->sgx_gpr_pa + GPR_URSP, sgx_reg(o, R_ESP));
    sgx_pst64(env, env->sgx_gpr_pa + GPR_URBP, sgx_reg(o, R_EBP));
    sgx_enter(o, tcs, tcs_la, secs, aep, fsbase, gsbase);
    if (env->sgx_dbgoptin && (env->eflags & TF_MASK)) {
        o->single_step = true;      /* "pend a single-step #DB at the end of EENTER" */
    }
}

/*
 * ERESUME (ENCLU[3], p. 41-117..41-128). AEX-Notify is not enumerated (CPUID.12H.01H:EAX[10] = 0):
 * TMP_NOTIFY is taken as 0 (docs/sgx.md). The XRSTOR of the SSA frame precedes the commit; its #GP
 * ends ERESUME with #GP(0) ("DS:RBX.STATE := INACTIVE" - the TCS is not busy yet in the model).
 */
static void sgx_eresume(SgxOp *o)
{
    CPUX86State *env = o->env;
    uint64_t tcs_la = sgx_la(o, sgx_reg(o, R_EBX), 4096, true);
    uint64_t secs, tcs, base, ssa, target, fsbase, gsbase, xfrm, gpr, hdr[3], f, aep;
    uint32_t cssa, ef, mask;
    int i, fault;

    tcs = sgx_entry_checks(o, tcs_la, true, &secs);
    cssa = sgx_pld32(env, tcs + TCS_CSSA);
    if (cssa == 0) {
        sgx_gp(o);
    }
    base = sgx_pld64(env, secs + SECS_BASEADDR);
    ssa = sgx_pld64(env, tcs + TCS_OSSA) + base +
          4096ULL * sgx_pld32(env, secs + SECS_SSAFRAMESIZE) * (cssa - 1);
    if (!o->m64) {
        ssa = (uint32_t)ssa;
    }
    sgx_ssa_checks(o, ssa, secs);
    gpr = env->sgx_gpr_pa;
    target = o->m64 ? sgx_pld64(env, gpr + GPR_RIP) : sgx_pld32(env, gpr + GPR_RIP);
    sgx_target_check(o, target);
    if (o->m64) {
        fsbase = sgx_pld64(env, gpr + GPR_FSBASE);
        gsbase = sgx_pld64(env, gpr + GPR_GSBASE);
    } else {
        fsbase = (uint32_t)(sgx_pld64(env, tcs + TCS_OFSBASE) + base);
        gsbase = (uint32_t)(sgx_pld64(env, tcs + TCS_OGSBASE) + base);
    }
    sgx_fsgs_check(o, fsbase, sgx_pld32(env, tcs + TCS_FSLIMIT));
    sgx_fsgs_check(o, gsbase, sgx_pld32(env, tcs + TCS_GSLIMIT));
    if (sgx_pld64(env, tcs + TCS_STATE) == TCS_ACTIVE) {
        sgx_gp(o);
    }
    /* XRSTOR(TMP_MODE64, SECS.ATTRIBUTES.XFRM, CR_XSAVE_PAGE_n) */
    xfrm = sgx_pld64(env, secs + SECS_XFRM);
    sgx_pread(env, env->sgx_xsave_pa[0] + 512, hdr, sizeof(hdr));
    fault = x86_sgx_xrstor_fault(env, xfrm, hdr[0], hdr[1], hdr[2],
                                 sgx_pld32(env, env->sgx_xsave_pa[0] + 24));
    if (fault == EXCP0D_GPF) {
        sgx_gp(o);
    } else if (fault >= 0) {
        raise_exception_ra(env, fault, o->ra);
    }
    env->sgx_implicit = 1;
    sgx_map_xsave(env);
    x86_sgx_xrstor(env, env->sgx_ssa_la, xfrm, o->m64);
    env->sgx_implicit = 0;
    /* commit (the AEP from RCX before the GPRs are restored) */
    aep = sgx_reg(o, R_ECX);
    for (i = 0; i < (o->m64 ? 16 : 8); i++) {
        env->regs[i] = o->m64 ? sgx_pld64(env, gpr + GPR_RAX + 8 * i)
                              : sgx_pld32(env, gpr + GPR_RAX + 8 * i);
    }
    /* RFLAGS: CF PF AF ZF SF DF OF NT AC ID RF from the SSA, VM := 0, IF from the SSA if IOPL = 3,
       TF := 0 for TCS.FLAGS.OPTIN (DBGOPTIN) = 0 */
    f = sgx_pld64(env, gpr + GPR_RFLAGS);
    mask = CC_C | CC_P | CC_A | CC_Z | CC_S | CC_O | DF_MASK | NT_MASK | AC_MASK | ID_MASK |
           RF_MASK;
    ef = ((uint32_t)env->eflags & ~(mask | VM_MASK)) | ((uint32_t)f & mask);
    if (((env->eflags & IOPL_MASK) >> IOPL_SHIFT) == 3) {
        ef = (ef & ~IF_MASK) | ((uint32_t)f & IF_MASK);
    }
    if (!(sgx_pld64(env, tcs + TCS_FLAGS) & 1)) {
        ef &= ~TF_MASK;
    }
    o->flags = ef & (CC_C | CC_P | CC_A | CC_Z | CC_S | CC_O);
    env->df = (ef & DF_MASK) ? -1 : 1;
    env->eflags = ef & ~(CC_C | CC_P | CC_A | CC_Z | CC_S | CC_O | DF_MASK);
    if (env->cr[4] & CR4_OSXSAVE_MASK) {
        env->sgx_save_xcr0 = env->xcr0;
        sgx_set_xcr0(env, xfrm);
    }
    sgx_pst32(env, tcs + TCS_CSSA, cssa - 1);
    env->eip = target;
    sgx_enter(o, tcs, tcs_la, secs, aep, fsbase, gsbase);
}

/*
 * The exit EEXIT and AEX share: FS / GS, XCR0, breakpoints, TF of an opt-out entry, CET, enclave
 * mode, the TCS not busy (EEXIT: "CR_TCS_PA.STATE := INACTIVE"; AEX: 39.2.1 "the TCS is considered
 * busy until the logical processors exits the enclave"), Flush_linear_context.
 */
static void sgx_leave(CPUX86State *env)
{
    sgx_restore_seg(env, R_FS, &env->sgx_save_fs);
    sgx_restore_seg(env, R_GS, &env->sgx_save_gs);
    if (env->cr[4] & CR4_OSXSAVE_MASK) {
        sgx_set_xcr0(env, env->sgx_save_xcr0);
    }
    if (!env->sgx_dbgoptin) {
        env->eflags = (env->eflags & ~TF_MASK) | (env->sgx_save_tf ? TF_MASK : 0);
    }
    sgx_cet_exit(env);
    env->sgx_enclave_mode = 0;
    x86_sgx_bp_update(env);
    sgx_pst64(env, env->sgx_tcs_pa + TCS_STATE, 0);
    tlb_flush(env_cpu(env));
}

/* EEXIT (ENCLU[4], p. 41-96..41-98) */
static void sgx_eexit(SgxOp *o)
{
    CPUX86State *env = o->env;
    uint64_t target = sgx_reg(o, R_EBX);
    uint64_t aep = sgx_pld64(env, env->sgx_tcs_pa + TCS_AEP);

    sgx_target_check(o, target);
    env->eip = target;
    env->regs[R_ECX] = o->m64 ? aep : (uint32_t)aep;
    sgx_leave(env);
    if (env->eflags & TF_MASK) {
        o->single_step = true;                      /* "Pend Single-Step #DB at the end of EEXIT" */
    }
}

/* the fault-class exception vectors (the RF the non-SGX delivery would save, 40.4 item 5) */
static bool sgx_fault_vector(int v)
{
    switch (v) {
    case 0: case 5: case 6: case 7: case 10: case 11: case 12: case 13: case 14: case 16:
    case 17: case 19: case 20: case 21:
        return true;
    default:
        return false;
    }
}

/*
 * NoVmp (ledger U1263): the asynchronous enclave exit (SDM 092 Vol3D 40.3, 40.4.1 p. 40-5..40-8),
 * "just before the normal interrupt or exception processing" of an event in enclave mode: 'vector'
 * (-1: an SMI), 'is_int' (INT n / INT3 / INTO: not possible inside an enclave, which makes those
 * #UD or fault-class), 'is_hw' (an external interrupt / NMI / SMI), 'next_eip' (the RIP after a
 * software interrupt). Saves the GPRs, RFLAGS (TF := 0, RF as the non-SGX delivery would push it:
 * 1 for fault-class exceptions incl. INT3 / INT1 inside an enclave, else unchanged - the REP
 * iteration RF of U482 is already in RFLAGS), RIP, FS / GS bases and the XSAVE state of XFRM into
 * the current SSA frame, EXITINFO / EXINFO, loads the synthetic state (Table 40-1), increments
 * CSSA and leaves the enclave; CR2 bits 11:0 are cleared for a #PF. Returns the RIP to deliver
 * (the AEP).
 */
target_ulong x86_sgx_aex(CPUX86State *env, int vector, int is_int, int is_hw, int error_code,
                         target_ulong next_eip)
{
    struct uc_x86_exception *rec = &env->uc->x86_exc;
    bool m64 = (env->hflags & HF_CS64_MASK) != 0;
    bool exc = !is_hw && !is_int && vector >= 0 && vector < 32;
    uint64_t gpr = env->sgx_gpr_pa, secs = env->sgx_secs, tcs = env->sgx_tcs_pa;
    uint64_t xfrm, aep, rip, f, bv;
    uint32_t misc, info = 0;
    int i;

    if (!env->sgx_enclave_mode) {
        return next_eip;
    }
    xfrm = sgx_pld64(env, secs + SECS_XFRM);
    misc = sgx_pld32(env, secs + SECS_MISCSELECT);
    aep = sgx_pld64(env, tcs + TCS_AEP);
    rip = is_int ? next_eip : env->eip;             /* TMP_RIP: the resume RIP */
    f = cpu_compute_eflags(env) & ~(uint64_t)TF_MASK;
    if (exc && (sgx_fault_vector(vector) || vector == EXCP03_INT3 ||
                (vector == EXCP01_DB && env->sgx_int_fault))) {
        f |= RF_MASK;
    }
    if (m64) {
        for (i = 0; i < 16; i++) {
            sgx_pst64(env, gpr + GPR_RAX + 8 * i, env->regs[i]);
        }
        sgx_pst64(env, gpr + GPR_RFLAGS, f);
        sgx_pst64(env, gpr + GPR_RIP, rip);
    } else {
        for (i = 0; i < 8; i++) {
            sgx_pst32(env, gpr + GPR_RAX + 8 * i, (uint32_t)env->regs[i]);
        }
        sgx_pst32(env, gpr + GPR_RFLAGS, (uint32_t)f);
        sgx_pst32(env, gpr + GPR_RIP, (uint32_t)rip);
    }
    sgx_pst64(env, gpr + GPR_FSBASE, env->segs[R_FS].base);
    sgx_pst64(env, gpr + GPR_GSBASE, env->segs[R_GS].base);
    /* SSA.XSAVE.i := XSAVE_STATE_i; XHEADER bytes 23:8 := 0; XHEADER_BV &= XFRM */
    sgx_map_xsave(env);
    x86_sgx_xsave(env, env->sgx_ssa_la, xfrm, m64);
    bv = sgx_pld64(env, env->sgx_xsave_pa[0] + 512) & xfrm;
    sgx_pst64(env, env->sgx_xsave_pa[0] + 512, bv);
    sgx_pst64(env, env->sgx_xsave_pa[0] + 520, 0);
    sgx_pst64(env, env->sgx_xsave_pa[0] + 528, 0);
    /* EXITINFO (Table 38-10) and the EXINFO of MISCSELECT[0] (Table 38-13) */
    if (exc && (vector == EXCP00_DIVZ || vector == EXCP01_DB || vector == EXCP03_INT3 ||
                vector == EXCP05_BOUND || vector == EXCP06_ILLOP || vector == EXCP10_COPR ||
                vector == EXCP11_ALGN || vector == EXCP13_XM)) {
        info = (1u << 31) | ((vector == EXCP03_INT3 ? 6u : 3u) << 8) | (uint32_t)vector;
    } else if (exc && (misc & 1) &&
               (vector == EXCP0D_GPF ||
                (vector == EXCP0E_PAGE && (error_code & PG_ERROR_U_MASK)))) {
        info = (1u << 31) | (3u << 8) | (uint32_t)vector;
        sgx_pst64(env, gpr - 16, vector == EXCP0E_PAGE ? env->cr[2] : 0);   /* MADDR */
        sgx_pst32(env, gpr - 8, (uint32_t)error_code);                     /* ERRCD */
        sgx_pst32(env, gpr - 4, 0);                                         /* RESERVED */
    } else if (misc & 1) {
        sgx_pst64(env, gpr - 16, 0);
        sgx_pst64(env, gpr - 8, 0);
    }
    sgx_pst32(env, gpr + GPR_EXITINFO, info);
    /* the synthetic state (Table 40-1, 40.3.2) */
    x86_sgx_xinit(env, xfrm, exc && vector == EXCP10_COPR, exc && vector == EXCP13_XM,
                  m64);
    env->regs[R_ESP] = sgx_pld64(env, gpr + GPR_URSP);
    env->regs[R_EBP] = sgx_pld64(env, gpr + GPR_URBP);
    env->regs[R_EAX] = 3;                           /* ENCLU[3] is ERESUME */
    env->regs[R_EBX] = env->sgx_tcs_la;
    env->regs[R_ECX] = aep;
    env->regs[R_EDX] = env->regs[R_ESI] = env->regs[R_EDI] = 0;
    if (m64) {
        for (i = 8; i < 16; i++) {
            env->regs[i] = 0;
        }
    } else {
        for (i = 0; i < 8; i++) {
            env->regs[i] = (uint32_t)env->regs[i];
        }
        aep = (uint32_t)aep;
    }
    CC_SRC = 0;                                     /* CF PF AF ZF SF OF := 0 */
    CC_OP = CC_OP_EFLAGS;
    env->eflags &= ~RF_MASK;
    env->eip = aep;
    sgx_pst32(env, tcs + TCS_CSSA, sgx_pld32(env, tcs + TCS_CSSA) + 1);
    sgx_leave(env);
    if (exc && vector == EXCP0E_PAGE) {
        env->cr[2] &= ~0xfffULL;
        if (rec->vector == EXCP0E_PAGE && rec->has_address) {
            rec->address = env->cr[2];
        }
    }
    env->sgx_enc_event = 1;
    env->sgx_int_fault = 0;
    return is_int ? env->eip : next_eip;
}

/*
 * NoVmp (ledger U1263): cpu-exec.c cpu_handle_exception, before an exception (or an external
 * interrupt for UC_HOOK_INTR, 'ext') is delivered through the IDT / FRED ('delivered') or handed
 * to the Unicorn hooks: the AEX in enclave mode; the FRED enclave bit is kept only for a delivery.
 */
void x86_sgx_aex_exception(CPUX86State *env, int vector, bool ext, bool delivered)
{
    if (env->sgx_enclave_mode) {
        int is_int = ext ? 0 : env->exception_is_int;
        target_ulong n = x86_sgx_aex(env, vector, is_int, ext, env->error_code,
                                     env->exception_next_eip);

        if (is_int) {
            env->exception_next_eip = n;
        }
    }
    if (!delivered) {
        env->sgx_enc_event = 0;
    }
}

/* ---------------------------------------------------------------------------------------------
 * Instructions inside an enclave (U1263)
 * ------------------------------------------------------------------------------------------- */

/*
 * NoVmp (ledger U1263): the translator calls this at CPL 3 with SGX enumerated before an
 * instruction of Table 39-1 (kind 0: #UD inside an enclave, 39.6.1), INT3 (kind 1) and INT1 (kind
 * 2). Inside an enclave INT1 / INT3 are fault-class (43.4.1): #UD after an opt-out entry, #DB / #BP
 * after an opt-in entry, the IDT DPL check of INT3 not enforced, RIP at the instruction.
 */
void helper_sgx_enclave_check(CPUX86State *env, uint32_t kind)
{
    if (!env->sgx_enclave_mode) {
        return;
    }
    if (kind == 0 || !env->sgx_dbgoptin) {
        raise_exception_ra(env, EXCP06_ILLOP, GETPC());
    }
    env->sgx_int_fault = 1;
    raise_exception_ra(env, kind == 1 ? EXCP03_INT3 : EXCP01_DB, GETPC());
}

/* NoVmp (ledger U1263): "The POPF instruction forces RFLAGS.TF to 0 if the enclave was entered
   through opt-out entry" (43.2.5) */
void helper_sgx_popf(CPUX86State *env)
{
    if (env->sgx_enclave_mode && !env->sgx_dbgoptin) {
        env->eflags &= ~TF_MASK;
    }
}

/* the ENCLU leaves the model enumerates: SGX1 (00H-04H), SGX2 (05H-07H); EVERIFYREPORT2 (08H) and
   EDECCSSA (09H) are not */
static bool sgx_enclu_leaf_valid(uint32_t leaf)
{
    return leaf <= 7;
}

/*
 * NoVmp (ledger U1263): ENCLU (Vol3D p. 41-10..41-12). The translator raised #UD for LOCK / 66 / F2
 * / F3 and routes here only with SGX enumerated; the CET tracker rule of the Operation is checked
 * at the TB start (helper_ibt_check kind 4). Order: #UD for CR0.PE = 0, virtual-8086 mode, SMM,
 * SGX1 = 0; #NM for CR0.TS = 1; #UD for CPL < 3; #GP(0) for IA32_FEATURE_CONTROL.LOCK /
 * SGX_ENABLE = 0, an unsupported leaf, CR0.PG = 0 or CR0.NE = 0, CS.D = 0 outside 64-bit mode,
 * EENTER / ERESUME inside or another leaf outside an enclave. 'len' = the instruction length. The
 * helper sets RIP; RF is cleared (the instruction completes) except after ERESUME, which loads it
 * from the SSA; a single-step #DB pended by EENTER (opt-in) or EEXIT is raised after the leaf.
 */
void helper_enclu(CPUX86State *env, uint32_t len)
{
    SgxOp o;
    uint32_t leaf = (uint32_t)env->regs[R_EAX];

    o.env = env;
    o.ra = GETPC();
    o.m64 = (env->hflags & HF_CS64_MASK) != 0;      /* IA32_EFER.LMA = 1 && CS.L = 1 */
    o.flags = cpu_cc_compute_all(env, CC_OP) & (CC_O | CC_S | CC_Z | CC_A | CC_P | CC_C);
    o.next_eip = o.m64 ? env->eip + len : (uint32_t)(env->eip + len);
    o.single_step = false;
    if (!(env->cr[0] & CR0_PE_MASK) || (env->eflags & VM_MASK) || (env->hflags & HF_SMM_MASK) ||
        !x86_sgx_enumerated(env)) {
        raise_exception_ra(env, EXCP06_ILLOP, o.ra);
    }
    if (env->cr[0] & CR0_TS_MASK) {
        raise_exception_ra(env, EXCP07_PREX, o.ra);
    }
    if ((env->hflags & HF_CPL_MASK) != 3) {
        raise_exception_ra(env, EXCP06_ILLOP, o.ra);
    }
    if ((env->msr_ia32_feature_control & (FEATURE_CONTROL_LOCKED | FEATURE_CONTROL_SGX)) !=
            (FEATURE_CONTROL_LOCKED | FEATURE_CONTROL_SGX) ||
        !sgx_enclu_leaf_valid(leaf) || !(env->cr[0] & CR0_PG_MASK) ||
        !(env->cr[0] & CR0_NE_MASK)) {
        sgx_gp(&o);
    }
    if (!o.m64 && !(env->segs[R_CS].flags & DESC_B_MASK)) {
        sgx_gp(&o);
    }
    if (env->sgx_enclave_mode ? (leaf == 2 || leaf == 3) : (leaf != 2 && leaf != 3)) {
        sgx_gp(&o);
    }
    switch (leaf) {
    case 2:
        sgx_eenter(&o);
        break;
    case 3:
        sgx_eresume(&o);
        break;
    case 4:
        sgx_eexit(&o);
        break;
    default:
        sgx_gp(&o);     /* EREPORT, EGETKEY: U1264; EACCEPT, EMODPE, EACCEPTCOPY: U1266 */
        break;
    }
    if (leaf != 3) {
        env->eflags &= ~RF_MASK;
    }
    env->hflags &= ~HF_INHIBIT_IRQ_MASK;
    env->sgx_svn_lock = 1;
    CC_SRC = o.flags;
    CC_OP = CC_OP_EFLAGS;
    if (o.single_step) {
        helper_single_step(env);
    }
}

/* ---------------------------------------------------------------------------------------------
 * Enclave-mode memory access control (U1263)
 * ------------------------------------------------------------------------------------------- */

/*
 * Accesses in enclave mode (Vol3D 38.3, 38.5.2): a linear address in ELRANGE must map to an EPC
 * page whose EPCM entry is VALID, of the current enclave, at that ENCLAVEADDRESS, PT_REG, not
 * BLOCKED / PENDING / MODIFIED, with R / W / X for the access; else a #PF with PFEC.SGX (P = 1,
 * W/R, U/S = 1, I/D for a fetch as Vol3A 5.7 sets it). An address of ELRANGE that maps outside the
 * EPC is that #PF too (38.1: "a fault is generated", docs/quirks.md). An instruction fetch outside
 * ELRANGE is #GP(0); data accesses outside ELRANGE are non-enclave accesses (abort page for EPC).
 */
static int sgx_enclave_access(CPUX86State *env, uint64_t la, uint64_t pa, int access, int *prot)
{
    CPUState *cs = env_cpu(env);
    X86SgxEpcm *m = sgx_epcm(env, pa);
    int allowed;

    if (la - env->sgx_elbase >= env->sgx_elsize) {
        if (access == MMU_INST_FETCH) {
            env->error_code = 0;
            cs->exception_index = EXCP0D_GPF;
            return -1;
        }
        return m ? 1 : 0;
    }
    allowed = m ? (m->r ? PAGE_READ : 0) | (m->w ? PAGE_WRITE : 0) | (m->x ? PAGE_EXEC : 0) : 0;
    if (!m || !m->valid || m->secs != env->sgx_secs || m->addr != (la & ~0xfffULL) ||
        m->pt != SGX_PT_REG || m->blocked || m->pending || m->modified ||
        !(allowed & (1 << access))) {
        uint32_t ec = PG_ERROR_P_MASK | PG_ERROR_U_MASK | SGX_PFEC_SGX;

        if (access == MMU_DATA_STORE) {
            ec |= PG_ERROR_W_MASK;
        }
        if (access == MMU_INST_FETCH &&
            (((env->efer & MSR_EFER_NXE) && (env->cr[4] & CR4_PAE_MASK)) ||
             (env->cr[4] & CR4_SMEP_MASK))) {
            ec |= PG_ERROR_I_D_MASK;
        }
        env->cr[2] = la;
        env->error_code = ec;
        cs->exception_index = EXCP0E_PAGE;
        return -1;
    }
    *prot &= allowed;
    return 0;
}

/* ---------------------------------------------------------------------------------------------
 * Memory access control (U1262: non-enclave accesses to the EPC)
 * ------------------------------------------------------------------------------------------- */

/*
 * The abort page: "Attempts to execute, read, or write to linear addresses mapped to EPC pages when
 * not inside an enclave will result in the processor altering the access ... As an example a read
 * of an enclave page may result in the return of all one's or return of cyphertext of the cache
 * line. Writing to an enclave page may result in a dropped write" (Vol3D 38.1 p. 38-1, 38.5.1;
 * EEXIT p. 41-96: a fetch "returns a fixed data pattern"). The model takes the first example: a RAM
 * page of all ones that the TLB maps for reads and instruction fetches (writes are discarded,
 * cputlb.c tlb_set_page_sgx_abort). Our choice among the SDM's examples (docs/sgx.md 3).
 */
void *x86_sgx_abort_page(CPUX86State *env, uint64_t *ram)
{
    X86SgxPkg *p = env->sgx;

    if (!p->abort_mr) {
        MemoryRegion *mr = g_new0(MemoryRegion, 1);

        memory_region_init_ram(env->uc, mr, TARGET_PAGE_SIZE, UC_PROT_ALL);
        p->abort_host = memory_region_get_ram_ptr(mr);
        memset(p->abort_host, 0xff, TARGET_PAGE_SIZE);
        p->abort_mr = mr;
    }
    *ram = memory_region_get_ram_addr((MemoryRegion *)p->abort_mr);
    return p->abort_host;
}

/*
 * NoVmp (ledger U1262): the SGX part of a TLB fill (excp_helper.c handle_mmu_fault, after the
 * paging checks): 0 = an ordinary mapping (*prot may be reduced), 1 = the abort page, -1 = a fault
 * set up in env (exception_index, error_code, CR2), 2 (U1263) = an implicit access to a cached
 * CR_XSAVE_PAGE_n during ERESUME's XRSTOR. An access that is not an enclave access (38.2: outside
 * enclave mode, or from enclave mode outside ELRANGE) to an EPC page gets the abort page; U1263:
 * enclave accesses (sgx_enclave_access).
 */
int x86_sgx_access(CPUX86State *env, uint64_t la, uint64_t pa, int access, int *prot)
{
    int i;

    if (!env->sgx) {
        return 0;
    }
    if (env->sgx_implicit) {
        for (i = 0; i < env->sgx_xsave_n; i++) {
            if ((pa & ~0xfffULL) == env->sgx_xsave_pa[i]) {
                return 2;
            }
        }
    }
    if (env->sgx_enclave_mode) {
        return sgx_enclave_access(env, la, pa, access, prot);
    }
    return sgx_epcm(env, pa) ? 1 : 0;
}

/* NoVmp (ledger U1262): free the package state (uc_close) */
void x86_sgx_release(CPUX86State *env)
{
    X86SgxPkg *p = env->sgx;

    if (!p) {
        return;
    }
    if (p->abort_mr) {
        MemoryRegion *mr = p->abort_mr;

        mr->destructor(mr);
        g_free(mr);
    }
    g_free(p->epcm);
    g_free(p);
    env->sgx = NULL;
}

#endif /* __Use_Original_Qemu (U1261) */
