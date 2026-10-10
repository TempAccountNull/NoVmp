/*
 * sde_rcp_probe.c (decision A9, U940-U959): run ONE of our own generated instruction encodings over
 * operand records and dump the destination register. Executes the instruction only when CPUID and
 * XCR0 report the required feature (on the i5-13600K the AVX-512 / FP16 / AVX10 forms refuse:
 * "XCR0 & E6h != E6h ... refusing to execute"; they run only under Intel SDE as a labelled
 * reference, driven by Emulator\tools\isa\sde_rcp.py). Never linked with or shipped beside SDE.
 * Build (x64 Native Tools prompt): cl /nologo /O2 sde_rcp_probe.c
 *
 *   rcprobe <feat> <hexinsn> <mxcsr> file  <in> <out>         records: zmm0,zmm1,zmm2 (192 B), k1 (8 B), pad -> 256 B; out: zmm0 (64 B)
 *   rcprobe <feat> <hexinsn> <mxcsr> sweep <esz> <start> <count> <out>
 *        zmm1 lanes = start, start+1, ... (esz bytes each); zmm0 = zmm2 = 0; out: zmm0 (64 B per call)
 * feat: sse | avx512f | avx512vl | fp16 | avx10.2
 * the instruction must use zmm0 (dest), zmm1 (r/m source), zmm2 (vvvv), k1.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <intrin.h>
#include <immintrin.h>
#include <windows.h>

static int has_feature(const char *f)
{
    int r[4], r7[4], r71[4], r24[4], maxleaf;
    unsigned long long xcr0 = 0;

    __cpuid(r, 0);
    maxleaf = r[0];
    __cpuid(r, 1);
    if (!strcmp(f, "sse")) {
        return (r[3] >> 25) & 1;                    /* CPUID.1:EDX.SSE[25] */
    }
    if (!((r[2] >> 27) & 1)) {                      /* CPUID.1:ECX.OSXSAVE[27] */
        fprintf(stderr, "OSXSAVE = 0\n");
        return 0;
    }
    xcr0 = _xgetbv(0);                              /* only after OSXSAVE */
    fprintf(stderr, "XCR0 = %llx\n", xcr0);
    if ((xcr0 & 0xe6) != 0xe6) {
        fprintf(stderr, "XCR0 & E6h != E6h: AVX-512 state not enabled by the OS\n");
        return 0;
    }
    if (maxleaf < 7) {
        return 0;
    }
    __cpuidex(r7, 7, 0);
    __cpuidex(r71, 7, 1);
    if (!strcmp(f, "avx512f")) {
        return (r7[1] >> 16) & 1;
    }
    if (!strcmp(f, "avx512vl")) {
        return ((r7[1] >> 16) & 1) && ((r7[1] >> 31) & 1);
    }
    if (!strcmp(f, "fp16")) {
        return ((r7[3] >> 23) & 1) && ((r7[1] >> 31) & 1);
    }
    if (!strcmp(f, "avx10.2")) {
        if (!((r71[3] >> 19) & 1) || maxleaf < 0x24) {
            return 0;
        }
        __cpuidex(r24, 0x24, 0);
        fprintf(stderr, "AVX10 version %d\n", r24[1] & 0xff);
        return (r24[1] & 0xff) >= 2;
    }
    return 0;
}

typedef void (*fn_t)(const uint8_t *in, uint8_t *out);

static fn_t build(const char *hex, int sse)
{
    static const uint8_t pro512[] = {
        0xC4, 0xE1, 0xF8, 0x90, 0x89, 0xC0, 0, 0, 0,    /* kmovq k1, [rcx+0C0h] */
        0x62, 0xF1, 0xFE, 0x48, 0x6F, 0x01,             /* vmovdqu64 zmm0, [rcx] */
        0x62, 0xF1, 0xFE, 0x48, 0x6F, 0x49, 0x01,       /* vmovdqu64 zmm1, [rcx+40h] */
        0x62, 0xF1, 0xFE, 0x48, 0x6F, 0x51, 0x02 };     /* vmovdqu64 zmm2, [rcx+80h] */
    static const uint8_t epi512[] = {
        0x62, 0xF1, 0xFE, 0x48, 0x7F, 0x02,             /* vmovdqu64 [rdx], zmm0 */
        0xC5, 0xF8, 0x77, 0xC3 };                       /* vzeroupper; ret */
    static const uint8_t prosse[] = {
        0xF3, 0x0F, 0x6F, 0x01,                         /* movdqu xmm0, [rcx] */
        0xF3, 0x0F, 0x6F, 0x49, 0x40,                   /* movdqu xmm1, [rcx+40h] */
        0xF3, 0x0F, 0x6F, 0x91, 0x80, 0, 0, 0 };        /* movdqu xmm2, [rcx+80h] */
    static const uint8_t episse[] = {
        0xF3, 0x0F, 0x7F, 0x02, 0xC3 };                 /* movdqu [rdx], xmm0; ret */
    uint8_t *p = VirtualAlloc(NULL, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE), *q;
    size_t n = strlen(hex) / 2, i;

    q = p;
    memcpy(q, sse ? prosse : pro512, sse ? sizeof(prosse) : sizeof(pro512));
    q += sse ? sizeof(prosse) : sizeof(pro512);
    for (i = 0; i < n; i++) {
        unsigned v;
        sscanf(hex + 2 * i, "%2x", &v);
        *q++ = (uint8_t)v;
    }
    memcpy(q, sse ? episse : epi512, sse ? sizeof(episse) : sizeof(epi512));
    return (fn_t)p;
}

int main(int argc, char **argv)
{
    unsigned mxcsr, old;
    fn_t fn;
    FILE *fo;

    if (argc < 6) {
        fprintf(stderr, "usage: see source\n");
        return 2;
    }
    if (!has_feature(argv[1])) {
        fprintf(stderr, "feature %s not present: refusing to execute\n", argv[1]);
        return 3;
    }
    fn = build(argv[2], !strcmp(argv[1], "sse"));
    mxcsr = (unsigned)strtoul(argv[3], NULL, 16);
    old = _mm_getcsr();
    if (!strcmp(argv[4], "file") && argc >= 7) {
        FILE *fi = fopen(argv[5], "rb");
        uint8_t in[256], out[64];
        long n = 0;

        fo = fopen(argv[6], "wb");
        if (!fi || !fo) {
            return 4;
        }
        while (fread(in, 1, 256, fi) == 256) {
            memset(out, 0, sizeof(out));
            _mm_setcsr(mxcsr);
            fn(in, out);
            _mm_setcsr(old);
            fwrite(out, 1, 64, fo);
            n++;
        }
        fclose(fi);
        fclose(fo);
        fprintf(stderr, "%ld records\n", n);
        return 0;
    }
    if (!strcmp(argv[4], "sweep") && argc >= 9) {
        int esz = atoi(argv[5]), lanes, j;
        unsigned long long start = _strtoui64(argv[6], NULL, 16), count = _strtoui64(argv[7], NULL, 0), k;
        static uint8_t buf[1 << 20];
        uint8_t in[256];
        size_t bp = 0;

        lanes = 64 / esz;
        memset(in, 0, sizeof(in));
        fo = fopen(argv[8], "wb");
        if (!fo) {
            return 4;
        }
        _mm_setcsr(mxcsr);
        for (k = 0; k < count; k += lanes) {
            for (j = 0; j < lanes; j++) {
                unsigned long long v = start + k + j;
                memcpy(in + 64 + j * esz, &v, esz);
            }
            fn(in, buf + bp);
            bp += 64;
            if (bp == sizeof(buf)) {
                fwrite(buf, 1, bp, fo);
                bp = 0;
            }
        }
        _mm_setcsr(old);
        fwrite(buf, 1, bp, fo);
        fclose(fo);
        return 0;
    }
    fprintf(stderr, "bad mode\n");
    return 2;
}
