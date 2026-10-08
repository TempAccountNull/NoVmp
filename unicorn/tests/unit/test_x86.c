#include "unicorn_test.h"

const uint64_t code_start = 0x1000;
const uint64_t code_len = 0x4000;

#define MEM_BASE 0x40000000
#define MEM_SIZE 1024 * 1024
#define MEM_STACK MEM_BASE + (MEM_SIZE / 2)
#define MEM_TEXT MEM_STACK + 4096
#define TEST_MSR_IA32_XFD 0x000001c4
#define TEST_MSR_IA32_XFD_ERR 0x000001c5
#define TEST_MSR_IA32_PKRS 0x000006e1
#define TEST_MSR_ARCH_LBR_CTL 0x000014ce
#define TEST_MSR_ARCH_LBR_DEPTH 0x000014cf
#define TEST_MSR_ARCH_LBR_FROM_0 0x00001500
#define TEST_MSR_ARCH_LBR_TO_0 0x00001600
#define TEST_MSR_ARCH_LBR_INFO_0 0x00001200
#define TEST_MSR_IA32_XSS 0x00000da0
#define TEST_X86_CPUID_7_0_EBX_AVX2 (1U << 5)
#define TEST_X86_CPUID_7_0_EBX_AVX512F (1U << 16)
#define TEST_X86_CPUID_7_0_EBX_AVX512DQ (1U << 17)
#define TEST_X86_CPUID_7_0_EBX_AVX512CD (1U << 28)
#define TEST_X86_CPUID_7_0_EBX_AVX512BW (1U << 30)
#define TEST_X86_CPUID_7_0_EBX_AVX512VL (1U << 31)
#define TEST_X86_CPUID_7_0_ECX_VAES (1U << 9)
#define TEST_X86_CPUID_7_0_ECX_VPCLMULQDQ (1U << 10)

static void uc_common_setup(uc_engine **uc, uc_arch arch, uc_mode mode,
                            const char *code, uint64_t size)
{
    OK(uc_open(arch, mode, uc));
    OK(uc_mem_map(*uc, code_start, code_len, UC_PROT_ALL));
    OK(uc_mem_write(*uc, code_start, code, size));
}

typedef struct RegInfo_t {
    const char *file;
    int line;
    const char *name;
    uc_x86_reg reg;
    uint64_t value;
} RegInfo;

typedef struct QuickTest_t {
    uc_mode mode;
    uint8_t *code_data;
    size_t code_size;
    size_t in_count;
    RegInfo in_regs[32];
    size_t out_count;
    RegInfo out_regs[32];
} QuickTest;

static void QuickTest_run(QuickTest *test)
{
    uc_engine *uc;

    // initialize emulator in X86-64bit mode
    OK(uc_open(UC_ARCH_X86, test->mode, &uc));

    // map 1MB of memory for this emulation
    OK(uc_mem_map(uc, MEM_BASE, MEM_SIZE, UC_PROT_ALL));
    OK(uc_mem_write(uc, MEM_TEXT, test->code_data, test->code_size));
    if (test->mode == UC_MODE_64) {
        uint64_t stack_top = MEM_STACK;
        OK(uc_reg_write(uc, UC_X86_REG_RSP, &stack_top));
    } else {
        uint32_t stack_top = MEM_STACK;
        OK(uc_reg_write(uc, UC_X86_REG_ESP, &stack_top));
    }
    for (size_t i = 0; i < test->in_count; i++) {
        if (test->mode == UC_MODE_64) {
            OK(uc_reg_write(uc, test->in_regs[i].reg, &test->in_regs[i].value));
        } else {
            uint32_t reg = test->in_regs[i].value & 0xFFFFFFFF;
            OK(uc_reg_write(uc, test->in_regs[i].reg, &reg));
        }
    }
    OK(uc_emu_start(uc, MEM_TEXT, MEM_TEXT + test->code_size, 0, 0));
    for (size_t i = 0; i < test->out_count; i++) {
        RegInfo *out = &test->out_regs[i];
        if (test->mode == UC_MODE_64) {
            uint64_t value = 0;
            OK(uc_reg_read(uc, out->reg, &value));
            acutest_check_(value == out->value, out->file, out->line,
                           "OUT_REG(%s, 0x%" PRIx64 ") = 0x%" PRIx64 "",
                           out->name, out->value, value);
        } else {
            uint32_t value = 0;
            OK(uc_reg_read(uc, out->reg, &value));
            acutest_check_(value == (uint32_t)out->value, out->file, out->line,
                           "OUT_REG(%s, 0x%X) = 0x%X", out->name,
                           (uint32_t)out->value, value);
        }
    }
    OK(uc_mem_unmap(uc, MEM_BASE, MEM_SIZE));
    OK(uc_close(uc));
}

#define TEST_CODE(MODE, CODE)                                                  \
    QuickTest t;                                                               \
    memset(&t, 0, sizeof(t));                                                  \
    t.mode = MODE;                                                             \
    t.code_data = CODE;                                                        \
    t.code_size = sizeof(CODE)

#define TEST_IN_REG(NAME, VALUE)                                               \
    t.in_regs[t.in_count].file = __FILE__;                                     \
    t.in_regs[t.in_count].line = __LINE__;                                     \
    t.in_regs[t.in_count].name = #NAME;                                        \
    t.in_regs[t.in_count].reg = UC_X86_REG_##NAME;                             \
    t.in_regs[t.in_count].value = VALUE;                                       \
    t.in_count++

#define TEST_OUT_REG(NAME, VALUE)                                              \
    t.out_regs[t.out_count].file = __FILE__;                                   \
    t.out_regs[t.out_count].line = __LINE__;                                   \
    t.out_regs[t.out_count].name = #NAME;                                      \
    t.out_regs[t.out_count].reg = UC_X86_REG_##NAME;                           \
    t.out_regs[t.out_count].value = VALUE;                                     \
    t.out_count++

#define TEST_RUN() QuickTest_run(&t)

typedef struct _INSN_IN_RESULT {
    uint32_t port;
    int size;
} INSN_IN_RESULT;

static void test_x86_in_callback(uc_engine *uc, uint32_t port, int size,
                                 void *user_data)
{
    INSN_IN_RESULT *result = (INSN_IN_RESULT *)user_data;
    uint32_t eip;

    result->port = port;
    result->size = size;

    OK(uc_reg_read(uc, UC_X86_REG_EIP, (void*)&eip));
    TEST_CHECK(eip == code_start);
}

static void test_x86_in(void)
{
    uc_engine *uc;
    uc_hook hook;
    char code[] = "\xe5\x10"; // IN eax, 0x10
    INSN_IN_RESULT result;

    uc_common_setup(&uc, UC_ARCH_X86, UC_MODE_32, code, sizeof(code) - 1);
    OK(uc_hook_add(uc, &hook, UC_HOOK_INSN, test_x86_in_callback, &result, 1, 0,
                   UC_X86_INS_IN));

    OK(uc_emu_start(uc, code_start, code_start + sizeof(code) - 1, 0, 0));
    TEST_CHECK(result.port == 0x10);
    TEST_CHECK(result.size == 4);

    OK(uc_hook_del(uc, hook));
    OK(uc_close(uc));
}

typedef struct _INSN_OUT_RESULT {
    uint32_t port;
    int size;
    uint32_t value;
} INSN_OUT_RESULT;

static void test_x86_out_callback(uc_engine *uc, uint32_t port, int size,
                                  uint32_t value, void *user_data)
{
    INSN_OUT_RESULT *result = (INSN_OUT_RESULT *)user_data;

    result->port = port;
    result->size = size;
    result->value = value;
}

static void test_x86_out(void)
{
    uc_engine *uc;
    uc_hook hook;
    char code[] = "\xb0\x32\xe6\x46"; // MOV al, 0x32; OUT  0x46, al;
    INSN_OUT_RESULT result;

    uc_common_setup(&uc, UC_ARCH_X86, UC_MODE_32, code, sizeof(code) - 1);
    OK(uc_hook_add(uc, &hook, UC_HOOK_INSN, test_x86_out_callback, &result, 1,
                   0, UC_X86_INS_OUT));

    OK(uc_emu_start(uc, code_start, code_start + sizeof(code) - 1, 0, 0));
    TEST_CHECK(result.port == 0x46);
    TEST_CHECK(result.size == 1);
    TEST_CHECK(result.value == 0x32);

    OK(uc_hook_del(uc, hook));
    OK(uc_close(uc));
}

typedef struct _MEM_HOOK_RESULT {
    uc_mem_type type;
    uint64_t address;
    int size;
    uint64_t value;
} MEM_HOOK_RESULT;

typedef struct _MEM_HOOK_RESULTS {
    uint64_t count;
    MEM_HOOK_RESULT results[16];
} MEM_HOOK_RESULTS;

static bool test_x86_mem_hook_all_callback(uc_engine *uc, uc_mem_type type,
                                           uint64_t address, int size,
                                           uint64_t value, void *user_data)
{
    MEM_HOOK_RESULTS *r = (MEM_HOOK_RESULTS *)user_data;
    uint64_t count = r->count;

    if (count >= 16) {
        TEST_ASSERT(false);
    }

    r->results[count].type = type;
    r->results[count].address = address;
    r->results[count].size = size;
    r->results[count].value = value;
    r->count++;

    if (type == UC_MEM_READ_UNMAPPED) {
        uc_mem_map(uc, address, 0x1000, UC_PROT_ALL);
    }

    return true;
}

static void test_x86_mem_hook_all(void)
{
    uc_engine *uc;
    uc_hook hook;
    // mov eax, 0xdeadbeef;
    // mov [0x8000], eax;
    // mov eax, [0x10000];
    char code[] =
        "\xb8\xef\xbe\xad\xde\xa3\x00\x80\x00\x00\xa1\x00\x00\x01\x00";
    MEM_HOOK_RESULTS r = {0};
    MEM_HOOK_RESULT expects[3] = {{UC_MEM_WRITE, 0x8000, 4, 0xdeadbeef},
                                  {UC_MEM_READ_UNMAPPED, 0x10000, 4, 0},
                                  {UC_MEM_READ, 0x10000, 4, 0}};

    uc_common_setup(&uc, UC_ARCH_X86, UC_MODE_32, code, sizeof(code) - 1);
    OK(uc_mem_map(uc, 0x8000, 0x1000, UC_PROT_ALL));
    OK(uc_hook_add(uc, &hook, UC_HOOK_MEM_VALID | UC_HOOK_MEM_INVALID,
                   test_x86_mem_hook_all_callback, &r, 1, 0));

    OK(uc_emu_start(uc, code_start, code_start + sizeof(code) - 1, 0, 0));
    TEST_CHECK(r.count == 3);
    for (int i = 0; i < r.count; i++) {
        TEST_CHECK(expects[i].type == r.results[i].type);
        TEST_CHECK(expects[i].address == r.results[i].address);
        TEST_CHECK(expects[i].size == r.results[i].size);
        TEST_CHECK(expects[i].value == r.results[i].value);
    }

    OK(uc_hook_del(uc, hook));
    OK(uc_close(uc));
}

static void test_x86_inc_dec_pxor(void)
{
    uc_engine *uc;
    char code[] =
        "\x41\x4a\x66\x0f\xef\xc1"; // INC ecx; DEC edx; PXOR xmm0, xmm1
    int r_ecx = 0x1234;
    int r_edx = 0x7890;
    uint64_t r_xmm0[2] = {0x08090a0b0c0d0e0f, 0x0001020304050607};
    uint64_t r_xmm1[2] = {0x8090a0b0c0d0e0f0, 0x0010203040506070};

    OK(uc_open(UC_ARCH_X86, UC_MODE_32, &uc));
    OK(uc_ctl_set_cpu_model(uc, UC_CPU_X86_HASWELL));
    OK(uc_mem_map(uc, code_start, code_len, UC_PROT_ALL));
    OK(uc_mem_write(uc, code_start, code, sizeof(code) - 1));

    OK(uc_reg_write(uc, UC_X86_REG_ECX, &r_ecx));
    OK(uc_reg_write(uc, UC_X86_REG_EDX, &r_edx));
    OK(uc_reg_write(uc, UC_X86_REG_XMM0, &r_xmm0));
    OK(uc_reg_write(uc, UC_X86_REG_XMM1, &r_xmm1));

    OK(uc_emu_start(uc, code_start, code_start + sizeof(code) - 1, 0, 0));

    OK(uc_reg_read(uc, UC_X86_REG_ECX, &r_ecx));
    OK(uc_reg_read(uc, UC_X86_REG_EDX, &r_edx));
    OK(uc_reg_read(uc, UC_X86_REG_XMM0, &r_xmm0));

    TEST_CHECK(r_ecx == 0x1235);
    TEST_CHECK(r_edx == 0x788f);
    TEST_CHECK(r_xmm0[0] == 0x8899aabbccddeeff);
    TEST_CHECK(r_xmm0[1] == 0x0011223344556677);

    OK(uc_close(uc));
}

static void test_x86_avx_vpxor_ymm(void)
{
    uc_engine *uc;
    char code[] = "\xc5\xfd\xef\xc1";
    uint64_t ymm0[4] = {0x08090a0b0c0d0e0fULL, 0x0001020304050607ULL,
                        0x8899aabbccddeeffULL, 0x0011223344556677ULL};
    uint64_t ymm1[4] = {0x8090a0b0c0d0e0f0ULL, 0x0010203040506070ULL,
                        0x1020304050607080ULL, 0xfedcba9876543210ULL};

    OK(uc_open(UC_ARCH_X86, UC_MODE_32, &uc));
    OK(uc_ctl_set_cpu_model(uc, UC_CPU_X86_HASWELL));
    OK(uc_mem_map(uc, code_start, code_len, UC_PROT_ALL));
    OK(uc_mem_write(uc, code_start, code, sizeof(code) - 1));

    OK(uc_reg_write(uc, UC_X86_REG_YMM0, &ymm0));
    OK(uc_reg_write(uc, UC_X86_REG_YMM1, &ymm1));

    OK(uc_emu_start(uc, code_start, code_start + sizeof(code) - 1, 0, 0));

    OK(uc_reg_read(uc, UC_X86_REG_YMM0, &ymm0));

    TEST_CHECK(ymm0[0] == 0x8899aabbccddeeffULL);
    TEST_CHECK(ymm0[1] == 0x0011223344556677ULL);
    TEST_CHECK(ymm0[2] == 0x98b99afb9cbd9e7fULL);
    TEST_CHECK(ymm0[3] == 0xfecd98ab32015467ULL);

    OK(uc_close(uc));
}

static void test_x86_avx_vex128_zero_upper_one(int cpu_model)
{
    uc_engine *uc;
    char code[] = "\xc5\xf1\xef\xc2";
    uint64_t ymm0[4] = {0xffffffffffffffffULL, 0xeeeeeeeeeeeeeeeeULL,
                        0xddddddddddddddddULL, 0xccccccccccccccccULL};
    uint64_t ymm1[4] = {0x0011223344556677ULL, 0x8899aabbccddeeffULL,
                        0x1020304050607080ULL, 0xfedcba9876543210ULL};
    uint64_t ymm2[4] = {0xff00ff00aa55aa55ULL, 0x123456789abcdef0ULL,
                        0x0f1e2d3c4b5a6978ULL, 0x8877665544332211ULL};
    uint64_t expected[4] = {0xff11dd33ee00cc22ULL, 0x9aadfcc35661300fULL,
                            0, 0};

    OK(uc_open(UC_ARCH_X86, UC_MODE_64, &uc));
    OK(uc_ctl_set_cpu_model(uc, cpu_model));
    OK(uc_mem_map(uc, code_start, code_len, UC_PROT_ALL));
    OK(uc_mem_write(uc, code_start, code, sizeof(code) - 1));

    OK(uc_reg_write(uc, UC_X86_REG_YMM0, &ymm0));
    OK(uc_reg_write(uc, UC_X86_REG_YMM1, &ymm1));
    OK(uc_reg_write(uc, UC_X86_REG_YMM2, &ymm2));

    OK(uc_emu_start(uc, code_start, code_start + sizeof(code) - 1, 0, 0));

    OK(uc_reg_read(uc, UC_X86_REG_YMM0, &ymm0));

    TEST_CHECK(memcmp(ymm0, expected, sizeof(ymm0)) == 0);

    OK(uc_close(uc));
}

static void test_x86_avx_vex128_zero_upper(void)
{
    test_x86_avx_vex128_zero_upper_one(UC_CPU_X86_HASWELL);
    test_x86_avx_vex128_zero_upper_one(UC_CPU_X86_ICELAKE_CLIENT);
}

static void test_x86_avx_scalar_zero_upper_ss_one(int cpu_model)
{
    uc_engine *uc;
    char code[] = "\xc5\xf2\x58\xc2";
    uint32_t ymm0[8] = {
        0xffffffff, 0xeeeeeeee, 0xdddddddd, 0xcccccccc,
        0xbbbbbbbb, 0xaaaaaaaa, 0x99999999, 0x88888888,
    };
    uint32_t ymm1[8] = {
        0x3fc00000, 0x11223344, 0x55667788, 0x99aabbcc,
        0x12345678, 0x23456789, 0x3456789a, 0x456789ab,
    };
    uint32_t ymm2[8] = {
        0x40100000, 0x80818283, 0x84858687, 0x88898a8b,
        0x8c8d8e8f, 0x90919293, 0x94959697, 0x98999a9b,
    };
    uint32_t expected[8] = {
        0x40700000, 0x11223344, 0x55667788, 0x99aabbcc,
        0, 0, 0, 0,
    };

    OK(uc_open(UC_ARCH_X86, UC_MODE_64, &uc));
    OK(uc_ctl_set_cpu_model(uc, cpu_model));
    OK(uc_mem_map(uc, code_start, code_len, UC_PROT_ALL));
    OK(uc_mem_write(uc, code_start, code, sizeof(code) - 1));

    OK(uc_reg_write(uc, UC_X86_REG_YMM0, &ymm0));
    OK(uc_reg_write(uc, UC_X86_REG_YMM1, &ymm1));
    OK(uc_reg_write(uc, UC_X86_REG_YMM2, &ymm2));

    OK(uc_emu_start(uc, code_start, code_start + sizeof(code) - 1, 0, 0));

    OK(uc_reg_read(uc, UC_X86_REG_YMM0, &ymm0));

    TEST_CHECK(memcmp(ymm0, expected, sizeof(ymm0)) == 0);

    OK(uc_close(uc));
}

static void test_x86_avx_scalar_zero_upper_sd_one(int cpu_model)
{
    uc_engine *uc;
    char code[] = "\xc5\xf3\x58\xc2";
    uint64_t ymm0[4] = {0xffffffffffffffffULL, 0xeeeeeeeeeeeeeeeeULL,
                        0xddddddddddddddddULL, 0xccccccccccccccccULL};
    uint64_t ymm1[4] = {0x3ff8000000000000ULL, 0x1122334455667788ULL,
                        0x123456789abcdef0ULL, 0x0f1e2d3c4b5a6978ULL};
    uint64_t ymm2[4] = {0x4002000000000000ULL, 0x8899aabbccddeeffULL,
                        0x1020304050607080ULL, 0xfedcba9876543210ULL};
    uint64_t expected[4] = {0x400e000000000000ULL, 0x1122334455667788ULL,
                            0, 0};

    OK(uc_open(UC_ARCH_X86, UC_MODE_64, &uc));
    OK(uc_ctl_set_cpu_model(uc, cpu_model));
    OK(uc_mem_map(uc, code_start, code_len, UC_PROT_ALL));
    OK(uc_mem_write(uc, code_start, code, sizeof(code) - 1));

    OK(uc_reg_write(uc, UC_X86_REG_YMM0, &ymm0));
    OK(uc_reg_write(uc, UC_X86_REG_YMM1, &ymm1));
    OK(uc_reg_write(uc, UC_X86_REG_YMM2, &ymm2));

    OK(uc_emu_start(uc, code_start, code_start + sizeof(code) - 1, 0, 0));

    OK(uc_reg_read(uc, UC_X86_REG_YMM0, &ymm0));

    TEST_CHECK(memcmp(ymm0, expected, sizeof(ymm0)) == 0);

    OK(uc_close(uc));
}

static void test_x86_avx_scalar_zero_upper(void)
{
    test_x86_avx_scalar_zero_upper_ss_one(UC_CPU_X86_HASWELL);
    test_x86_avx_scalar_zero_upper_sd_one(UC_CPU_X86_HASWELL);
    test_x86_avx_scalar_zero_upper_ss_one(UC_CPU_X86_ICELAKE_CLIENT);
    test_x86_avx_scalar_zero_upper_sd_one(UC_CPU_X86_ICELAKE_CLIENT);
}

static void test_x86_avx_fma_ps(void)
{
    uc_engine *uc;
    char code[] = "\xc4\xe2\x79\x98\xc1";
    uint32_t xmm0[4] = {
        0x40000000, 0x40400000, 0x40800000, 0x40a00000,
    };
    uint32_t xmm1[4] = {
        0x41200000, 0x41a00000, 0x41f00000, 0x42200000,
    };

    OK(uc_open(UC_ARCH_X86, UC_MODE_64, &uc));
    OK(uc_ctl_set_cpu_model(uc, UC_CPU_X86_HASWELL));
    OK(uc_mem_map(uc, code_start, code_len, UC_PROT_ALL));
    OK(uc_mem_write(uc, code_start, code, sizeof(code) - 1));

    OK(uc_reg_write(uc, UC_X86_REG_XMM0, &xmm0));
    OK(uc_reg_write(uc, UC_X86_REG_XMM1, &xmm1));

    OK(uc_emu_start(uc, code_start, code_start + sizeof(code) - 1, 0, 0));

    OK(uc_reg_read(uc, UC_X86_REG_XMM0, &xmm0));

    TEST_CHECK(xmm0[0] == 0x41b00000);
    TEST_CHECK(xmm0[1] == 0x427c0000);
    TEST_CHECK(xmm0[2] == 0x42f80000);
    TEST_CHECK(xmm0[3] == 0x434d0000);

    OK(uc_close(uc));
}

static void test_x86_fma_scalar_variants(void)
{
    uc_engine *uc;
    char code[] =
        "\xc4\xe2\x71\x99\xc2"
        "\xc4\xe2\xd9\xbf\xdd"
        "\xc4\xc2\x45\xa6\xf0";
    uint32_t xmm0[4] = {
        0x40000000, 0x41300000, 0x41400000, 0x41500000,
    };
    uint32_t xmm1[4] = {
        0x41200000, 0, 0, 0,
    };
    uint32_t xmm2[4] = {
        0x40400000, 0, 0, 0,
    };
    uint64_t xmm3[2] = {
        0x4014000000000000ULL, 0,
    };
    uint64_t xmm4[2] = {
        0x4000000000000000ULL, 0,
    };
    uint64_t xmm5[2] = {
        0x4008000000000000ULL, 0,
    };
    uint32_t ymm6[8] = {
        0x3f800000, 0x40000000, 0x40400000, 0x40800000,
        0x40a00000, 0x40c00000, 0x40e00000, 0x41000000,
    };
    uint32_t ymm7[8] = {
        0x40000000, 0x40000000, 0x40000000, 0x40000000,
        0x40000000, 0x40000000, 0x40000000, 0x40000000,
    };
    uint32_t ymm8[8] = {
        0x3f800000, 0x3f800000, 0x3f800000, 0x3f800000,
        0x3f800000, 0x3f800000, 0x3f800000, 0x3f800000,
    };
    uint32_t expected6[8] = {
        0x3f800000, 0x40a00000, 0x40a00000, 0x41100000,
        0x41100000, 0x41500000, 0x41500000, 0x41880000,
    };

    OK(uc_open(UC_ARCH_X86, UC_MODE_64, &uc));
    OK(uc_ctl_set_cpu_model(uc, UC_CPU_X86_HASWELL));
    OK(uc_mem_map(uc, code_start, code_len, UC_PROT_ALL));
    OK(uc_mem_write(uc, code_start, code, sizeof(code) - 1));

    OK(uc_reg_write(uc, UC_X86_REG_XMM0, &xmm0));
    OK(uc_reg_write(uc, UC_X86_REG_XMM1, &xmm1));
    OK(uc_reg_write(uc, UC_X86_REG_XMM2, &xmm2));
    OK(uc_reg_write(uc, UC_X86_REG_XMM3, &xmm3));
    OK(uc_reg_write(uc, UC_X86_REG_XMM4, &xmm4));
    OK(uc_reg_write(uc, UC_X86_REG_XMM5, &xmm5));
    OK(uc_reg_write(uc, UC_X86_REG_YMM6, &ymm6));
    OK(uc_reg_write(uc, UC_X86_REG_YMM7, &ymm7));
    OK(uc_reg_write(uc, UC_X86_REG_YMM8, &ymm8));

    OK(uc_emu_start(uc, code_start, code_start + sizeof(code) - 1, 0, 0));

    OK(uc_reg_read(uc, UC_X86_REG_XMM0, &xmm0));
    OK(uc_reg_read(uc, UC_X86_REG_XMM3, &xmm3));
    OK(uc_reg_read(uc, UC_X86_REG_YMM6, &ymm6));

    TEST_CHECK(xmm0[0] == 0x41800000);
    TEST_CHECK(xmm3[0] == 0xc026000000000000ULL);
    TEST_CHECK(memcmp(ymm6, expected6, sizeof(ymm6)) == 0);

    OK(uc_close(uc));
}

static void test_x86_avx2_broadcast_permute(void)
{
    uc_engine *uc;
    char code[] =
        "\xc4\xe2\x7d\x58\x00"
        "\xc4\xe2\x4d\x36\xef"
        "\xc4\xe2\x7d\x5a\x50\x20"
        "\xc4\xe3\x75\x46\xda\x21";
    uint64_t rax = code_start + 0x100;
    uint32_t value = 0x11223344;
    uint32_t block[4] = {
        0xa0a1a2a3, 0xb0b1b2b3, 0xc0c1c2c3, 0xd0d1d2d3,
    };
    uint32_t ymm0[8];
    uint32_t ymm1[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    uint32_t ymm2[8];
    uint32_t ymm3[8];
    uint32_t ymm5[8];
    uint32_t ymm6[8] = { 7, 0, 6, 1, 5, 2, 4, 3 };
    uint32_t ymm7[8] = { 10, 20, 30, 40, 50, 60, 70, 80 };
    uint32_t expected3[8] = {
        5, 6, 7, 8,
        0xa0a1a2a3, 0xb0b1b2b3, 0xc0c1c2c3, 0xd0d1d2d3,
    };

    OK(uc_open(UC_ARCH_X86, UC_MODE_64, &uc));
    OK(uc_ctl_set_cpu_model(uc, UC_CPU_X86_HASWELL));
    OK(uc_mem_map(uc, code_start, code_len, UC_PROT_ALL));
    OK(uc_mem_write(uc, code_start, code, sizeof(code) - 1));
    OK(uc_mem_write(uc, rax, &value, sizeof(value)));
    OK(uc_mem_write(uc, rax + 0x20, block, sizeof(block)));

    OK(uc_reg_write(uc, UC_X86_REG_RAX, &rax));
    OK(uc_reg_write(uc, UC_X86_REG_YMM1, &ymm1));
    OK(uc_reg_write(uc, UC_X86_REG_YMM6, &ymm6));
    OK(uc_reg_write(uc, UC_X86_REG_YMM7, &ymm7));

    OK(uc_emu_start(uc, code_start, code_start + sizeof(code) - 1, 0, 0));

    OK(uc_reg_read(uc, UC_X86_REG_YMM0, &ymm0));
    OK(uc_reg_read(uc, UC_X86_REG_YMM2, &ymm2));
    OK(uc_reg_read(uc, UC_X86_REG_YMM3, &ymm3));
    OK(uc_reg_read(uc, UC_X86_REG_YMM5, &ymm5));

    for (size_t i = 0; i < 8; i++) {
        TEST_CHECK(ymm0[i] == value);
    }
    TEST_CHECK(ymm5[0] == 80);
    TEST_CHECK(ymm5[1] == 10);
    TEST_CHECK(ymm5[2] == 70);
    TEST_CHECK(ymm5[3] == 20);
    TEST_CHECK(ymm5[4] == 60);
    TEST_CHECK(ymm5[5] == 30);
    TEST_CHECK(ymm5[6] == 50);
    TEST_CHECK(ymm5[7] == 40);
    TEST_CHECK(memcmp(ymm2, block, sizeof(block)) == 0);
    TEST_CHECK(memcmp(&ymm2[4], block, sizeof(block)) == 0);
    TEST_CHECK(memcmp(ymm3, expected3, sizeof(ymm3)) == 0);

    OK(uc_close(uc));
}

static void test_x86_avx2_variable_shifts(void)
{
    uc_engine *uc;
    char code[] =
        "\xc4\xe2\x75\x47\xc2"
        "\xc4\xe2\x5d\x45\xdd"
        "\xc4\xc2\x45\x46\xf0"
        "\xc4\x42\xad\x47\xcb"
        "\xc4\x42\x95\x45\xe6";
    uint32_t ymm1[8] = {
        1, 2, 0x80000000, 0xffffffff,
        0x12345678, 0x7fffffff, 0x89abcdef, 0x00010000,
    };
    uint32_t ymm2[8] = { 0, 1, 4, 31, 32, 33, 8, 16 };
    uint32_t ymm4[8] = {
        0xffffffff, 0x80000000, 0x7fffffff, 0x12345678,
        1, 0x80000001, 0xf0000000, 0x00ff00ff,
    };
    uint32_t ymm5[8] = { 0, 1, 4, 31, 32, 33, 8, 16 };
    uint32_t ymm7[8] = {
        0xffffffff, 0x80000000, 0x7fffffff, 0x80000000,
        1, 0x80000000, 0xf0000000, 0x00ff00ff,
    };
    uint32_t ymm8[8] = { 0, 1, 4, 31, 32, 33, 8, 16 };
    uint64_t ymm10[4] = {
        1, 0x8000000000000000ULL,
        0x0123456789abcdefULL, 0xffffffffffffffffULL,
    };
    uint64_t ymm11[4] = { 0, 1, 64, 8 };
    uint64_t ymm13[4] = {
        0xffffffffffffffffULL, 0x8000000000000000ULL,
        0x0123456789abcdefULL, 0x00ff00ff00ff00ffULL,
    };
    uint64_t ymm14[4] = { 0, 1, 64, 8 };
    uint32_t expected0[8] = {
        1, 4, 0, 0x80000000, 0, 0, 0xabcdef00, 0,
    };
    uint32_t expected3[8] = {
        0xffffffff, 0x40000000, 0x07ffffff, 0, 0, 0,
        0x00f00000, 0x000000ff,
    };
    uint32_t expected6[8] = {
        0xffffffff, 0xc0000000, 0x07ffffff, 0xffffffff,
        0, 0xffffffff, 0xfff00000, 0x000000ff,
    };
    uint64_t expected9[4] = {
        1, 0, 0, 0xffffffffffffff00ULL,
    };
    uint64_t expected12[4] = {
        0xffffffffffffffffULL, 0x4000000000000000ULL,
        0, 0x0000ff00ff00ff00ULL,
    };
    uint32_t ymm0[8];
    uint32_t ymm3[8];
    uint32_t ymm6[8];
    uint64_t ymm9[4];
    uint64_t ymm12[4];

    OK(uc_open(UC_ARCH_X86, UC_MODE_64, &uc));
    OK(uc_ctl_set_cpu_model(uc, UC_CPU_X86_HASWELL));
    OK(uc_mem_map(uc, code_start, code_len, UC_PROT_ALL));
    OK(uc_mem_write(uc, code_start, code, sizeof(code) - 1));

    OK(uc_reg_write(uc, UC_X86_REG_YMM1, &ymm1));
    OK(uc_reg_write(uc, UC_X86_REG_YMM2, &ymm2));
    OK(uc_reg_write(uc, UC_X86_REG_YMM4, &ymm4));
    OK(uc_reg_write(uc, UC_X86_REG_YMM5, &ymm5));
    OK(uc_reg_write(uc, UC_X86_REG_YMM7, &ymm7));
    OK(uc_reg_write(uc, UC_X86_REG_YMM8, &ymm8));
    OK(uc_reg_write(uc, UC_X86_REG_YMM10, &ymm10));
    OK(uc_reg_write(uc, UC_X86_REG_YMM11, &ymm11));
    OK(uc_reg_write(uc, UC_X86_REG_YMM13, &ymm13));
    OK(uc_reg_write(uc, UC_X86_REG_YMM14, &ymm14));

    OK(uc_emu_start(uc, code_start, code_start + sizeof(code) - 1, 0, 0));

    OK(uc_reg_read(uc, UC_X86_REG_YMM0, &ymm0));
    OK(uc_reg_read(uc, UC_X86_REG_YMM3, &ymm3));
    OK(uc_reg_read(uc, UC_X86_REG_YMM6, &ymm6));
    OK(uc_reg_read(uc, UC_X86_REG_YMM9, &ymm9));
    OK(uc_reg_read(uc, UC_X86_REG_YMM12, &ymm12));

    TEST_CHECK(memcmp(ymm0, expected0, sizeof(ymm0)) == 0);
    TEST_CHECK(memcmp(ymm3, expected3, sizeof(ymm3)) == 0);
    TEST_CHECK(memcmp(ymm6, expected6, sizeof(ymm6)) == 0);
    TEST_CHECK(memcmp(ymm9, expected9, sizeof(ymm9)) == 0);
    TEST_CHECK(memcmp(ymm12, expected12, sizeof(ymm12)) == 0);

    OK(uc_close(uc));
}

static void test_x86_avx2_mask_gather(void)
{
    uc_engine *uc;
    char code[] =
        "\xc4\xe2\x45\x8c\x30"
        "\xc4\xe2\x6d\x90\x04\x88";
    uint64_t rax = code_start + 0x100;
    uint32_t data[8] = {
        0x10001000, 0x20002000, 0x30003000, 0x40004000,
        0x50005000, 0x60006000, 0x70007000, 0x80008000,
    };
    uint32_t ymm0[8] = {
        0x11111111, 0x22222222, 0x33333333, 0x44444444,
        0x55555555, 0x66666666, 0x77777777, 0x88888888,
    };
    uint32_t ymm1[8] = { 7, 0, 5, 2, 1, 4, 3, 6 };
    uint32_t ymm2[8] = {
        0x80000000, 0, 0xffffffff, 0,
        0x80000000, 0x7fffffff, 0, 0xffffffff,
    };
    uint32_t ymm7[8] = {
        0x80000000, 0, 0xffffffff, 0x7fffffff,
        0x80000000, 0, 0xffffffff, 0,
    };
    uint32_t expected0[8] = {
        0x80008000, 0x22222222, 0x60006000, 0x44444444,
        0x20002000, 0x66666666, 0x77777777, 0x70007000,
    };
    uint32_t expected2[8] = { 0 };
    uint32_t expected6[8] = {
        0x10001000, 0, 0x30003000, 0,
        0x50005000, 0, 0x70007000, 0,
    };
    uint32_t ymm6[8] = { 0 };

    OK(uc_open(UC_ARCH_X86, UC_MODE_64, &uc));
    OK(uc_ctl_set_cpu_model(uc, UC_CPU_X86_HASWELL));
    OK(uc_mem_map(uc, code_start, code_len, UC_PROT_ALL));
    OK(uc_mem_write(uc, code_start, code, sizeof(code) - 1));
    OK(uc_mem_write(uc, rax, data, sizeof(data)));

    OK(uc_reg_write(uc, UC_X86_REG_RAX, &rax));
    OK(uc_reg_write(uc, UC_X86_REG_YMM0, &ymm0));
    OK(uc_reg_write(uc, UC_X86_REG_YMM1, &ymm1));
    OK(uc_reg_write(uc, UC_X86_REG_YMM2, &ymm2));
    OK(uc_reg_write(uc, UC_X86_REG_YMM7, &ymm7));

    OK(uc_emu_start(uc, code_start, code_start + sizeof(code) - 1, 0, 0));

    OK(uc_reg_read(uc, UC_X86_REG_YMM0, &ymm0));
    OK(uc_reg_read(uc, UC_X86_REG_YMM2, &ymm2));
    OK(uc_reg_read(uc, UC_X86_REG_YMM6, &ymm6));

    TEST_CHECK(memcmp(ymm0, expected0, sizeof(ymm0)) == 0);
    TEST_CHECK(memcmp(ymm2, expected2, sizeof(ymm2)) == 0);
    TEST_CHECK(memcmp(ymm6, expected6, sizeof(ymm6)) == 0);

    OK(uc_close(uc));
}

static void test_x86_avx_vzeroall(void)
{
    uc_engine *uc;
    char code[] = "\xc5\xfc\x77";
    uint64_t ymm0[4] = {
        0x1111111111111111ULL, 0x2222222222222222ULL,
        0x3333333333333333ULL, 0x4444444444444444ULL,
    };
    uint64_t ymm15[4] = {
        0x5555555555555555ULL, 0x6666666666666666ULL,
        0x7777777777777777ULL, 0x8888888888888888ULL,
    };

    OK(uc_open(UC_ARCH_X86, UC_MODE_64, &uc));
    OK(uc_ctl_set_cpu_model(uc, UC_CPU_X86_HASWELL));
    OK(uc_mem_map(uc, code_start, code_len, UC_PROT_ALL));
    OK(uc_mem_write(uc, code_start, code, sizeof(code) - 1));

    OK(uc_reg_write(uc, UC_X86_REG_YMM0, &ymm0));
    OK(uc_reg_write(uc, UC_X86_REG_YMM15, &ymm15));

    OK(uc_emu_start(uc, code_start, code_start + sizeof(code) - 1, 0, 0));

    OK(uc_reg_read(uc, UC_X86_REG_YMM0, &ymm0));
    OK(uc_reg_read(uc, UC_X86_REG_YMM15, &ymm15));

    for (size_t i = 0; i < 4; i++) {
        TEST_CHECK(ymm0[i] == 0);
        TEST_CHECK(ymm15[i] == 0);
    }

    OK(uc_close(uc));
}

static void test_x86_aes_pclmul(void)
{
    uc_engine *uc;
    char code[] =
        "\x66\x0f\x38\xdc\xc1"
        "\x66\x0f\x38\xde\xd3"
        "\x66\x0f\x38\xdb\xe5"
        "\x66\x0f\x3a\xdf\xf7\x1b"
        "\x66\x45\x0f\x3a\x44\xc1\x11";
    uint8_t xmm0[16] = {
        0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
        0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff,
    };
    uint8_t xmm1[16] = {
        0x0f, 0x0e, 0x0d, 0x0c, 0x0b, 0x0a, 0x09, 0x08,
        0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01, 0x00,
    };
    uint8_t xmm2[16] = {
        0x69, 0xc4, 0xe0, 0xd8, 0x6a, 0x7b, 0x04, 0x30,
        0xd8, 0xcd, 0xb7, 0x80, 0x70, 0xb4, 0xc5, 0x5a,
    };
    uint8_t xmm3[16] = {
        0x13, 0x11, 0x1d, 0x7f, 0xe3, 0x94, 0x4a, 0x17,
        0xf3, 0x07, 0xa7, 0x8b, 0x4d, 0x2b, 0x30, 0xc5,
    };
    uint8_t xmm5[16] = {
        0xac, 0x19, 0x28, 0x57, 0x77, 0xfa, 0xd1, 0x5c,
        0x66, 0xdc, 0x29, 0x00, 0xf3, 0x21, 0x41, 0x6a,
    };
    uint8_t xmm7[16] = {
        0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
        0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c,
    };
    uint8_t xmm8[16] = {
        0x10, 0x32, 0x54, 0x76, 0x98, 0xba, 0xdc, 0xfe,
        0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
    };
    uint8_t xmm9[16] = {
        0x55, 0xaa, 0x00, 0xff, 0x11, 0xee, 0x22, 0xdd,
        0x33, 0xcc, 0x44, 0xbb, 0x55, 0xaa, 0x66, 0x99,
    };
    const uint8_t expected_xmm0[16] = {
        0x6c, 0x77, 0xeb, 0xd5, 0xff, 0x6d, 0xf2, 0x7e,
        0xaa, 0x00, 0x39, 0xf0, 0xd1, 0xe9, 0x8b, 0xa3,
    };
    const uint8_t expected_xmm2[16] = {
        0xd4, 0x4f, 0x0a, 0xfb, 0xa3, 0x23, 0x94, 0xd3,
        0x52, 0x84, 0x00, 0xc6, 0x83, 0x41, 0x84, 0x98,
    };
    const uint8_t expected_xmm4[16] = {
        0x3b, 0x98, 0x30, 0x59, 0xd8, 0x02, 0x7e, 0xa4,
        0x49, 0x17, 0x3b, 0xf6, 0xc2, 0xa6, 0x99, 0x04,
    };
    const uint8_t expected_xmm6[16] = {
        0x34, 0xe4, 0xb5, 0x24, 0xff, 0xb5, 0x24, 0x34,
        0x01, 0x8a, 0x84, 0xeb, 0x91, 0x84, 0xeb, 0x01,
    };
    const uint8_t expected_xmm8[16] = {
        0x33, 0xf9, 0xa9, 0x26, 0x51, 0x89, 0xaf, 0x7e,
        0x13, 0xd9, 0xa9, 0x26, 0x71, 0xa9, 0xaf, 0x7e,
    };

    OK(uc_open(UC_ARCH_X86, UC_MODE_64, &uc));
    OK(uc_ctl_set_cpu_model(uc, UC_CPU_X86_HASWELL));
    OK(uc_mem_map(uc, code_start, code_len, UC_PROT_ALL));
    OK(uc_mem_write(uc, code_start, code, sizeof(code) - 1));

    OK(uc_reg_write(uc, UC_X86_REG_XMM0, &xmm0));
    OK(uc_reg_write(uc, UC_X86_REG_XMM1, &xmm1));
    OK(uc_reg_write(uc, UC_X86_REG_XMM2, &xmm2));
    OK(uc_reg_write(uc, UC_X86_REG_XMM3, &xmm3));
    OK(uc_reg_write(uc, UC_X86_REG_XMM5, &xmm5));
    OK(uc_reg_write(uc, UC_X86_REG_XMM7, &xmm7));
    OK(uc_reg_write(uc, UC_X86_REG_XMM8, &xmm8));
    OK(uc_reg_write(uc, UC_X86_REG_XMM9, &xmm9));

    OK(uc_emu_start(uc, code_start, code_start + sizeof(code) - 1, 0, 0));

    OK(uc_reg_read(uc, UC_X86_REG_XMM0, &xmm0));
    OK(uc_reg_read(uc, UC_X86_REG_XMM2, &xmm2));
    OK(uc_reg_read(uc, UC_X86_REG_XMM4, &xmm5));
    OK(uc_reg_read(uc, UC_X86_REG_XMM6, &xmm7));
    OK(uc_reg_read(uc, UC_X86_REG_XMM8, &xmm8));

    TEST_CHECK(memcmp(xmm0, expected_xmm0, sizeof(xmm0)) == 0);
    TEST_CHECK(memcmp(xmm2, expected_xmm2, sizeof(xmm2)) == 0);
    TEST_CHECK(memcmp(xmm5, expected_xmm4, sizeof(xmm5)) == 0);
    TEST_CHECK(memcmp(xmm7, expected_xmm6, sizeof(xmm7)) == 0);
    TEST_CHECK(memcmp(xmm8, expected_xmm8, sizeof(xmm8)) == 0);

    OK(uc_close(uc));
}

static uint32_t test_x86_cpuid_7_0_ecx(uc_cpu_x86 cpu_model)
{
    uc_engine *uc;
    char code[] = "\x0f\xa2";
    uint32_t eax = 7;
    uint32_t ecx = 0;

    OK(uc_open(UC_ARCH_X86, UC_MODE_64, &uc));
    OK(uc_ctl_set_cpu_model(uc, cpu_model));
    OK(uc_mem_map(uc, code_start, code_len, UC_PROT_ALL));
    OK(uc_mem_write(uc, code_start, code, sizeof(code) - 1));
    OK(uc_reg_write(uc, UC_X86_REG_EAX, &eax));
    OK(uc_reg_write(uc, UC_X86_REG_ECX, &ecx));

    OK(uc_emu_start(uc, code_start, code_start + sizeof(code) - 1, 0, 0));
    OK(uc_reg_read(uc, UC_X86_REG_ECX, &ecx));

    OK(uc_close(uc));
    return ecx;
}

static uint32_t test_x86_cpuid_7_0_ebx(uc_cpu_x86 cpu_model)
{
    uc_engine *uc;
    char code[] = "\x0f\xa2";
    uint32_t eax = 7;
    uint32_t ebx;
    uint32_t ecx = 0;

    OK(uc_open(UC_ARCH_X86, UC_MODE_64, &uc));
    OK(uc_ctl_set_cpu_model(uc, cpu_model));
    OK(uc_mem_map(uc, code_start, code_len, UC_PROT_ALL));
    OK(uc_mem_write(uc, code_start, code, sizeof(code) - 1));
    OK(uc_reg_write(uc, UC_X86_REG_EAX, &eax));
    OK(uc_reg_write(uc, UC_X86_REG_ECX, &ecx));

    OK(uc_emu_start(uc, code_start, code_start + sizeof(code) - 1, 0, 0));
    OK(uc_reg_read(uc, UC_X86_REG_EBX, &ebx));

    OK(uc_close(uc));
    return ebx;
}

static void test_x86_avx512_tcg_mask(void)
{
    const uint32_t avx512_mask = TEST_X86_CPUID_7_0_EBX_AVX512F |
                                 TEST_X86_CPUID_7_0_EBX_AVX512DQ |
                                 TEST_X86_CPUID_7_0_EBX_AVX512CD |
                                 TEST_X86_CPUID_7_0_EBX_AVX512BW |
                                 TEST_X86_CPUID_7_0_EBX_AVX512VL;
    uint32_t ebx;

    ebx = test_x86_cpuid_7_0_ebx(UC_CPU_X86_SKYLAKE_SERVER);
    TEST_CHECK((ebx & TEST_X86_CPUID_7_0_EBX_AVX2) != 0);
    TEST_CHECK((ebx & avx512_mask) == 0);

    ebx = test_x86_cpuid_7_0_ebx(UC_CPU_X86_ICELAKE_SERVER);
    TEST_CHECK((ebx & TEST_X86_CPUID_7_0_EBX_AVX2) != 0);
    TEST_CHECK((ebx & avx512_mask) == 0);
}

static void test_x86_vaes_vex_gating(void)
{
    uc_engine *uc;
    char vaesenc_xmm[] = "\xc4\xe2\x79\xdc\xc1";
    char vaesenc_ymm[] = "\xc4\xe2\x7d\xdc\xc1";
    char vaes_ymm[] =
        "\xc4\xe2\x7d\xdc\xc1"
        "\xc4\xe2\x6d\xde\xd3";
    uint8_t xmm0[16] = {
        0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
        0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff,
    };
    uint8_t xmm1[16] = {
        0x0f, 0x0e, 0x0d, 0x0c, 0x0b, 0x0a, 0x09, 0x08,
        0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01, 0x00,
    };
    uint8_t ymm0[32] = {
        0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
        0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff,
        0x10, 0x32, 0x54, 0x76, 0x98, 0xba, 0xdc, 0xfe,
        0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
    };
    uint8_t ymm1[32] = {
        0x0f, 0x0e, 0x0d, 0x0c, 0x0b, 0x0a, 0x09, 0x08,
        0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01, 0x00,
        0x55, 0xaa, 0x00, 0xff, 0x11, 0xee, 0x22, 0xdd,
        0x33, 0xcc, 0x44, 0xbb, 0x55, 0xaa, 0x66, 0x99,
    };
    uint8_t ymm2[32] = {
        0x69, 0xc4, 0xe0, 0xd8, 0x6a, 0x7b, 0x04, 0x30,
        0xd8, 0xcd, 0xb7, 0x80, 0x70, 0xb4, 0xc5, 0x5a,
        0x10, 0x32, 0x54, 0x76, 0x98, 0xba, 0xdc, 0xfe,
        0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
    };
    uint8_t ymm3[32] = {
        0x13, 0x11, 0x1d, 0x7f, 0xe3, 0x94, 0x4a, 0x17,
        0xf3, 0x07, 0xa7, 0x8b, 0x4d, 0x2b, 0x30, 0xc5,
        0x55, 0xaa, 0x00, 0xff, 0x11, 0xee, 0x22, 0xdd,
        0x33, 0xcc, 0x44, 0xbb, 0x55, 0xaa, 0x66, 0x99,
    };
    const uint8_t expected_xmm0[16] = {
        0x6c, 0x77, 0xeb, 0xd5, 0xff, 0x6d, 0xf2, 0x7e,
        0xaa, 0x00, 0x39, 0xf0, 0xd1, 0xe9, 0x8b, 0xa3,
    };
    const uint8_t expected_ymm0[32] = {
        0x6c, 0x77, 0xeb, 0xd5, 0xff, 0x6d, 0xf2, 0x7e,
        0xaa, 0x00, 0x39, 0xf0, 0xd1, 0xe9, 0x8b, 0xa3,
        0x6c, 0xfe, 0x98, 0x85, 0x72, 0x00, 0x6b, 0xfc,
        0xf6, 0xaf, 0xcc, 0x10, 0x66, 0x5f, 0x61, 0xdf,
    };
    const uint8_t expected_ymm2[32] = {
        0xd4, 0x4f, 0x0a, 0xfb, 0xa3, 0x23, 0x94, 0xd3,
        0x52, 0x84, 0x00, 0xc6, 0x83, 0x41, 0x84, 0x98,
        0x3b, 0xc6, 0x56, 0xbd, 0x3d, 0x4c, 0xe5, 0x5d,
        0x85, 0x0f, 0xac, 0x73, 0x29, 0xbf, 0xc3, 0x09,
    };

    TEST_CHECK((test_x86_cpuid_7_0_ecx(UC_CPU_X86_HASWELL) &
                TEST_X86_CPUID_7_0_ECX_VAES) == 0);
    TEST_CHECK((test_x86_cpuid_7_0_ecx(UC_CPU_X86_ICELAKE_CLIENT) &
                TEST_X86_CPUID_7_0_ECX_VAES) != 0);

    OK(uc_open(UC_ARCH_X86, UC_MODE_64, &uc));
    OK(uc_ctl_set_cpu_model(uc, UC_CPU_X86_HASWELL));
    OK(uc_mem_map(uc, code_start, code_len, UC_PROT_ALL));
    OK(uc_mem_write(uc, code_start, vaesenc_xmm, sizeof(vaesenc_xmm) - 1));
    OK(uc_reg_write(uc, UC_X86_REG_XMM0, &xmm0));
    OK(uc_reg_write(uc, UC_X86_REG_XMM1, &xmm1));
    OK(uc_emu_start(uc, code_start, code_start + sizeof(vaesenc_xmm) - 1,
                    0, 0));
    OK(uc_reg_read(uc, UC_X86_REG_XMM0, &xmm0));
    TEST_CHECK(memcmp(xmm0, expected_xmm0, sizeof(xmm0)) == 0);
    OK(uc_close(uc));

    OK(uc_open(UC_ARCH_X86, UC_MODE_64, &uc));
    OK(uc_ctl_set_cpu_model(uc, UC_CPU_X86_HASWELL));
    OK(uc_mem_map(uc, code_start, code_len, UC_PROT_ALL));
    OK(uc_mem_write(uc, code_start, vaesenc_ymm, sizeof(vaesenc_ymm) - 1));
    uc_assert_err(UC_ERR_INSN_INVALID,
                  uc_emu_start(uc, code_start,
                               code_start + sizeof(vaesenc_ymm) - 1, 0, 0));
    OK(uc_close(uc));

    OK(uc_open(UC_ARCH_X86, UC_MODE_64, &uc));
    OK(uc_ctl_set_cpu_model(uc, UC_CPU_X86_ICELAKE_CLIENT));
    OK(uc_mem_map(uc, code_start, code_len, UC_PROT_ALL));
    OK(uc_mem_write(uc, code_start, vaes_ymm, sizeof(vaes_ymm) - 1));
    OK(uc_reg_write(uc, UC_X86_REG_YMM0, &ymm0));
    OK(uc_reg_write(uc, UC_X86_REG_YMM1, &ymm1));
    OK(uc_reg_write(uc, UC_X86_REG_YMM2, &ymm2));
    OK(uc_reg_write(uc, UC_X86_REG_YMM3, &ymm3));
    OK(uc_emu_start(uc, code_start, code_start + sizeof(vaes_ymm) - 1, 0, 0));
    OK(uc_reg_read(uc, UC_X86_REG_YMM0, &ymm0));
    OK(uc_reg_read(uc, UC_X86_REG_YMM2, &ymm2));
    TEST_CHECK(memcmp(ymm0, expected_ymm0, sizeof(ymm0)) == 0);
    TEST_CHECK(memcmp(ymm2, expected_ymm2, sizeof(ymm2)) == 0);
    OK(uc_close(uc));
}

static void test_x86_vpclmulqdq_tcg_mask(void)
{
    uc_engine *uc;
    char pclmul_xmm[] = "\xc4\xe3\x79\x44\xc1\x11";
    char pclmul_ymm[] = "\xc4\xe3\x7d\x44\xc1\x11";
    uint8_t ymm0[32] = {
        0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
        0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff,
        0x10, 0x32, 0x54, 0x76, 0x98, 0xba, 0xdc, 0xfe,
        0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
    };
    uint8_t ymm1[32] = {
        0x0f, 0x0e, 0x0d, 0x0c, 0x0b, 0x0a, 0x09, 0x08,
        0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01, 0x00,
        0x55, 0xaa, 0x00, 0xff, 0x11, 0xee, 0x22, 0xdd,
        0x33, 0xcc, 0x44, 0xbb, 0x55, 0xaa, 0x66, 0x99,
    };
    const uint8_t expected_ymm0[32] = {
        0xb8, 0xfc, 0xa8, 0x02, 0x00, 0xff, 0x10, 0x01,
        0xa8, 0xfd, 0xb8, 0x03, 0x10, 0xfe, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };

    TEST_CHECK((test_x86_cpuid_7_0_ecx(UC_CPU_X86_HASWELL) &
                TEST_X86_CPUID_7_0_ECX_VPCLMULQDQ) == 0);
    /* NoVmp U69: TCG implements VPCLMULQDQ, so models that list it keep it */
    TEST_CHECK((test_x86_cpuid_7_0_ecx(UC_CPU_X86_ICELAKE_CLIENT) &
                TEST_X86_CPUID_7_0_ECX_VPCLMULQDQ) != 0);

    OK(uc_open(UC_ARCH_X86, UC_MODE_64, &uc));
    OK(uc_ctl_set_cpu_model(uc, UC_CPU_X86_HASWELL));
    OK(uc_mem_map(uc, code_start, code_len, UC_PROT_ALL));
    OK(uc_mem_write(uc, code_start, pclmul_xmm, sizeof(pclmul_xmm) - 1));
    OK(uc_reg_write(uc, UC_X86_REG_YMM0, &ymm0));
    OK(uc_reg_write(uc, UC_X86_REG_YMM1, &ymm1));
    OK(uc_emu_start(uc, code_start, code_start + sizeof(pclmul_xmm) - 1,
                    0, 0));
    OK(uc_reg_read(uc, UC_X86_REG_YMM0, &ymm0));
    TEST_CHECK(memcmp(ymm0, expected_ymm0, sizeof(ymm0)) == 0);
    OK(uc_close(uc));

    OK(uc_open(UC_ARCH_X86, UC_MODE_64, &uc));
    OK(uc_ctl_set_cpu_model(uc, UC_CPU_X86_ICELAKE_CLIENT));
    OK(uc_mem_map(uc, code_start, code_len, UC_PROT_ALL));
    OK(uc_mem_write(uc, code_start, pclmul_ymm, sizeof(pclmul_ymm) - 1));
    {
        /* both 128-bit lanes: clmul(high qwords) per lane (SDM VPCLMULQDQ, imm 11h) */
        static const uint8_t expected_ymm[32] = {
            0xb8, 0xfc, 0xa8, 0x02, 0x00, 0xff, 0x10, 0x01,
            0xa8, 0xfd, 0xb8, 0x03, 0x10, 0xfe, 0x00, 0x00,
            0x33, 0xf9, 0xa9, 0x26, 0x51, 0x89, 0xaf, 0x7e,
            0x13, 0xd9, 0xa9, 0x26, 0x71, 0xa9, 0xaf, 0x7e,
        };
        uint8_t y0[32], y1[32];
        memcpy(y0, (uint8_t[32]){
            0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
            0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff,
            0x10, 0x32, 0x54, 0x76, 0x98, 0xba, 0xdc, 0xfe,
            0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef}, 32);
        memcpy(y1, ymm1, 32);
        OK(uc_reg_write(uc, UC_X86_REG_YMM0, &y0));
        OK(uc_reg_write(uc, UC_X86_REG_YMM1, &y1));
        OK(uc_emu_start(uc, code_start, code_start + sizeof(pclmul_ymm) - 1, 0, 0));
        OK(uc_reg_read(uc, UC_X86_REG_YMM0, &y0));
        TEST_CHECK(memcmp(y0, expected_ymm, sizeof(y0)) == 0);
    }

    OK(uc_close(uc));
}

/*
 * NoVmp U85-U88: AVX-VNNI-INT8, AVX-VNNI-INT16, AVX-IFMA, AVX-NE-CONVERT.
 * Expected values come from the independent SDM model
 * Emulator/tools/isa/ref_vnni_ifma_ne.py (x86_vnni_ifma_ne_vectors.inc).
 */
#include "x86_vnni_ifma_ne_vectors.inc"

#define TEST_X86_VNNI_DATA 0x200000

static void test_x86_vnni_ifma_ne_cpuid(void)
{
    uc_engine *uc;
    char code[] = "\x0f\xa2";
    uint32_t eax = 7, ebx = 0, ecx = 1, edx = 0;

    OK(uc_open(UC_ARCH_X86, UC_MODE_64, &uc));
    OK(uc_ctl_set_cpu_model(uc, UC_CPU_X86_MAX));
    OK(uc_mem_map(uc, code_start, code_len, UC_PROT_ALL));
    OK(uc_mem_write(uc, code_start, code, sizeof(code) - 1));
    OK(uc_reg_write(uc, UC_X86_REG_EAX, &eax));
    OK(uc_reg_write(uc, UC_X86_REG_ECX, &ecx));
    OK(uc_emu_start(uc, code_start, code_start + sizeof(code) - 1, 0, 0));
    OK(uc_reg_read(uc, UC_X86_REG_EAX, &eax));
    OK(uc_reg_read(uc, UC_X86_REG_EBX, &ebx));
    OK(uc_reg_read(uc, UC_X86_REG_EDX, &edx));
    OK(uc_close(uc));

    /* CPUID.(7,1): EAX[4] AVX-VNNI (U71), EAX[23] AVX-IFMA (U87) */
    TEST_CHECK((eax & (1U << 4)) != 0);
    TEST_CHECK((eax & (1U << 23)) != 0);
    /* EDX[4] AVX-VNNI-INT8 (U85), EDX[5] AVX-NE-CONVERT (U88), EDX[10] AVX-VNNI-INT16 (U86) */
    TEST_CHECK((edx & (1U << 4)) != 0);
    TEST_CHECK((edx & (1U << 5)) != 0);
    TEST_CHECK((edx & (1U << 10)) != 0);
    TEST_CHECK(ebx == 0);
}

/* Run one vector on 'model'; returns the uc_emu_start result, ymm0 in 'out'. */
static uc_err test_x86_vnni_run(const struct x86_vnni_vec *t, uc_cpu_x86 model,
                                uint8_t out[32])
{
    uc_engine *uc;
    uc_err err;
    uint8_t y0[32], y1[32], y2[32];
    uint64_t rsi = TEST_X86_VNNI_DATA;

    memcpy(y0, t->ymm0, 32);
    memcpy(y1, t->ymm1, 32);
    memcpy(y2, t->ymm2, 32);
    OK(uc_open(UC_ARCH_X86, UC_MODE_64, &uc));
    OK(uc_ctl_set_cpu_model(uc, model));
    OK(uc_mem_map(uc, code_start, code_len, UC_PROT_ALL));
    OK(uc_mem_map(uc, TEST_X86_VNNI_DATA, 0x1000, UC_PROT_ALL));
    OK(uc_mem_write(uc, code_start, t->code, t->code_len));
    OK(uc_mem_write(uc, TEST_X86_VNNI_DATA, t->mem, sizeof(t->mem)));
    OK(uc_reg_write(uc, UC_X86_REG_YMM0, y0));
    OK(uc_reg_write(uc, UC_X86_REG_YMM1, y1));
    OK(uc_reg_write(uc, UC_X86_REG_YMM2, y2));
    OK(uc_reg_write(uc, UC_X86_REG_RSI, &rsi));
    err = uc_emu_start(uc, code_start, code_start + t->code_len, 0, 0);
    OK(uc_reg_read(uc, UC_X86_REG_YMM0, out));
    OK(uc_close(uc));
    return err;
}

static void test_x86_vnni_ifma_ne_vectors(void)
{
    size_t i;

    for (i = 0; i < sizeof(x86_vnni_vecs) / sizeof(x86_vnni_vecs[0]); i++) {
        const struct x86_vnni_vec *t = &x86_vnni_vecs[i];
        uint8_t y0[32];

        TEST_CHECK_(test_x86_vnni_run(t, UC_CPU_X86_MAX, y0) == UC_ERR_OK,
                    "%s executes", t->name);
        TEST_CHECK_(memcmp(y0, t->expect, 32) == 0, "%s ymm0 == SDM model",
                    t->name);
    }
}

static void test_x86_vnni_ifma_ne_ud(void)
{
    size_t i;

    /* reserved encodings (W, vvvv, register ModRM on memory-only forms, F2 D2/D3, legacy) */
    for (i = 0; i < sizeof(x86_vnni_uds) / sizeof(x86_vnni_uds[0]); i++) {
        const struct x86_vnni_ud *u = &x86_vnni_uds[i];
        struct x86_vnni_vec t;
        uint8_t y0[32];

        memset(&t, 0, sizeof(t));
        memcpy(t.code, u->code, sizeof(t.code));
        t.code_len = u->code_len;
        TEST_CHECK_(test_x86_vnni_run(&t, UC_CPU_X86_MAX, y0) ==
                        UC_ERR_INSN_INVALID,
                    "#UD: %s", u->name);
    }
    /* Haswell has none of the four extensions: every valid form #UDs */
    for (i = 0; i < sizeof(x86_vnni_vecs) / sizeof(x86_vnni_vecs[0]); i++) {
        uint8_t y0[32];

        TEST_CHECK_(test_x86_vnni_run(&x86_vnni_vecs[i], UC_CPU_X86_HASWELL,
                                      y0) == UC_ERR_INSN_INVALID,
                    "#UD on Haswell: %s", x86_vnni_vecs[i].name);
    }
}

static void test_x86_relative_jump(void)
{
    uc_engine *uc;
    char code[] = "\xeb\x02\x90\x90\x90\x90\x90\x90"; // jmp 4; nop; nop; nop;
                                                      // nop; nop; nop
    int r_eip;

    uc_common_setup(&uc, UC_ARCH_X86, UC_MODE_32, code, sizeof(code) - 1);

    OK(uc_emu_start(uc, code_start, code_start + 4, 0, 0));

    OK(uc_reg_read(uc, UC_X86_REG_EIP, &r_eip));

    TEST_CHECK(r_eip == code_start + 4);

    OK(uc_close(uc));
}

static void test_x86_loop(void)
{
    uc_engine *uc;
    char code[] = "\x41\x4a\xeb\xfe"; // inc ecx; dec edx; jmp $;
    int r_ecx = 0x1234;
    int r_edx = 0x7890;

    uc_common_setup(&uc, UC_ARCH_X86, UC_MODE_32, code, sizeof(code) - 1);
    OK(uc_reg_write(uc, UC_X86_REG_ECX, &r_ecx));
    OK(uc_reg_write(uc, UC_X86_REG_EDX, &r_edx));

    OK(uc_emu_start(uc, code_start, code_start + sizeof(code) - 1, 1 * 1000000,
                    0));

    OK(uc_reg_read(uc, UC_X86_REG_ECX, &r_ecx));
    OK(uc_reg_read(uc, UC_X86_REG_EDX, &r_edx));

    TEST_CHECK(r_ecx == 0x1235);
    TEST_CHECK(r_edx == 0x788f);

    OK(uc_close(uc));
}

static void test_x86_invalid_mem_read(void)
{
    uc_engine *uc;
    char code[] = "\x8b\x0d\xaa\xaa\xaa\xaa"; // mov  ecx, [0xAAAAAAAA]

    uc_common_setup(&uc, UC_ARCH_X86, UC_MODE_32, code, sizeof(code) - 1);

    uc_assert_err(
        UC_ERR_READ_UNMAPPED,
        uc_emu_start(uc, code_start, code_start + sizeof(code) - 1, 0, 0));

    OK(uc_close(uc));
}

static void test_x86_invalid_mem_write(void)
{
    uc_engine *uc;
    char code[] = "\x89\x0d\xaa\xaa\xaa\xaa"; // mov  ecx, [0xAAAAAAAA]

    uc_common_setup(&uc, UC_ARCH_X86, UC_MODE_32, code, sizeof(code) - 1);

    uc_assert_err(
        UC_ERR_WRITE_UNMAPPED,
        uc_emu_start(uc, code_start, code_start + sizeof(code) - 1, 0, 0));

    OK(uc_close(uc));
}

static void test_x86_invalid_jump(void)
{
    uc_engine *uc;
    char code[] = "\xe9\xe9\xee\xee\xee"; // jmp 0xEEEEEEEE

    uc_common_setup(&uc, UC_ARCH_X86, UC_MODE_32, code, sizeof(code) - 1);

    uc_assert_err(
        UC_ERR_FETCH_UNMAPPED,
        uc_emu_start(uc, code_start, code_start + sizeof(code) - 1, 0, 0));

    OK(uc_close(uc));
}

static void test_x86_64_syscall_callback(uc_engine *uc, void *user_data)
{
    uint64_t rax;

    OK(uc_reg_read(uc, UC_X86_REG_RAX, &rax));

    TEST_CHECK(rax == 0x100);
}

static void test_x86_64_syscall(void)
{
    uc_engine *uc;
    uc_hook hook;
    char code[] = "\x0f\x05"; // syscall
    uint64_t r_rax = 0x100;

    uc_common_setup(&uc, UC_ARCH_X86, UC_MODE_64, code, sizeof(code) - 1);
    OK(uc_reg_write(uc, UC_X86_REG_RAX, &r_rax));
    OK(uc_hook_add(uc, &hook, UC_HOOK_INSN, test_x86_64_syscall_callback, NULL,
                   1, 0, UC_X86_INS_SYSCALL));

    OK(uc_emu_start(uc, code_start, code_start + sizeof(code) - 1, 0, 0));

    OK(uc_hook_del(uc, hook));
    OK(uc_close(uc));
}

static void test_x86_16_add(void)
{
    uc_engine *uc;
    char code[] = "\x00\x00"; // add   byte ptr [bx + si], al
    uint16_t r_ax = 7;
    uint16_t r_bx = 5;
    uint16_t r_si = 6;
    uint8_t result;

    uc_common_setup(&uc, UC_ARCH_X86, UC_MODE_16, code, sizeof(code) - 1);
    OK(uc_mem_map(uc, 0, 0x1000, UC_PROT_ALL));
    OK(uc_reg_write(uc, UC_X86_REG_AX, &r_ax));
    OK(uc_reg_write(uc, UC_X86_REG_BX, &r_bx));
    OK(uc_reg_write(uc, UC_X86_REG_SI, &r_si));

    OK(uc_emu_start(uc, code_start, code_start + sizeof(code) - 1, 0, 0));

    OK(uc_mem_read(uc, r_bx + r_si, &result, 1));
    TEST_CHECK(result == 7);
    OK(uc_close(uc));
}

static void test_x86_reg_save(void)
{
    uc_engine *uc;
    uc_context *ctx;
    char code[] = "\x40"; // inc eax
    int r_eax = 1;

    uc_common_setup(&uc, UC_ARCH_X86, UC_MODE_32, code, sizeof(code) - 1);
    OK(uc_reg_write(uc, UC_X86_REG_EAX, &r_eax));

    OK(uc_context_alloc(uc, &ctx));
    OK(uc_context_save(uc, ctx));
    OK(uc_emu_start(uc, code_start, code_start + sizeof(code) - 1, 0, 0));

    OK(uc_reg_read(uc, UC_X86_REG_EAX, &r_eax));
    TEST_CHECK(r_eax == 2);

    OK(uc_context_restore(uc, ctx));

    OK(uc_reg_read(uc, UC_X86_REG_EAX, &r_eax));
    TEST_CHECK(r_eax == 1);

    OK(uc_context_free(ctx));
    OK(uc_close(uc));
}

static bool
test_x86_invalid_mem_read_stop_in_cb_callback(uc_engine *uc, uc_mem_type type,
                                              uint64_t address, int size,
                                              uint64_t value, void *user_data)
{
    // False indicates that we fail to handle this ERROR and let the emulation
    // stop.
    //
    // Note that the memory must be mapped properly if we return true! Check
    // test_x86_mem_hook_all for example.
    return false;
}

static void test_x86_invalid_mem_read_stop_in_cb(void)
{
    uc_engine *uc;
    uc_hook hook;
    char code[] = "\x40\x8b\x1d\x00\x00\x10\x00\x42"; // inc eax; mov ebx,
                                                      // [0x100000]; inc edx
    int r_eax = 0x1234;
    int r_edx = 0x5678;
    int r_eip = 0;

    uc_common_setup(&uc, UC_ARCH_X86, UC_MODE_32, code, sizeof(code) - 1);
    OK(uc_hook_add(uc, &hook, UC_HOOK_MEM_READ,
                   test_x86_invalid_mem_read_stop_in_cb_callback, NULL, 1, 0));
    OK(uc_reg_write(uc, UC_X86_REG_EAX, &r_eax));
    OK(uc_reg_write(uc, UC_X86_REG_EDX, &r_edx));

    uc_assert_err(
        UC_ERR_READ_UNMAPPED,
        uc_emu_start(uc, code_start, code_start + sizeof(code) - 1, 0, 0));

    // The state of Unicorn should be correct at this time.
    OK(uc_reg_read(uc, UC_X86_REG_EIP, &r_eip));
    OK(uc_reg_read(uc, UC_X86_REG_EAX, &r_eax));
    OK(uc_reg_read(uc, UC_X86_REG_EDX, &r_edx));

    TEST_CHECK(r_eip == code_start + 1);
    TEST_CHECK(r_eax == 0x1235);
    TEST_CHECK(r_edx == 0x5678);

    OK(uc_close(uc));
}

static void test_x86_x87_fnstenv_callback(uc_engine *uc, uint64_t address,
                                          uint32_t size, void *user_data)
{
    uint32_t r_eip;
    uint32_t r_eax;
    uint32_t fnstenv[7];

    if (address == code_start + 4) { // The first fnstenv executed
        // Save the address of the fld.
        OK(uc_reg_read(uc, UC_X86_REG_EIP, &r_eip));
        *((uint32_t *)user_data) = r_eip;

        OK(uc_reg_read(uc, UC_X86_REG_EAX, &r_eax));
        OK(uc_mem_read(uc, r_eax, fnstenv, sizeof(fnstenv)));
#if __Use_Original_Qemu == 1 /* original QEMU (U90) */
        // Don't update FCS:FIP for fnop.
        TEST_CHECK(fnstenv[3] == 0);
#else /* ours (U90) */
        /* NoVmp U90: FNOP is not in the SDM's x87 control list (Vol1 8.1.8),
           FIP = the FNOP like on the i5-13600K */
        TEST_CHECK(LEINT32(fnstenv[3]) == code_start);
#endif /* __Use_Original_Qemu (U90) */
    }
}

static void test_x86_x87_fnstenv(void)
{
    uc_engine *uc;
    uc_hook hook;
    char code[] =
        "\xd9\xd0\xd9\x30\xd9\x00\xd9\x30"; // fnop;fnstenv [eax];fld dword ptr
                                            // [eax];fnstenv [eax]
    uint32_t base = code_start + 3 * code_len;
    uint32_t last_eip;
    uint32_t fnstenv[7];

    uc_common_setup(&uc, UC_ARCH_X86, UC_MODE_32, code, sizeof(code) - 1);
    OK(uc_mem_map(uc, base, code_len, UC_PROT_ALL));
    OK(uc_reg_write(uc, UC_X86_REG_EAX, &base));

    OK(uc_hook_add(uc, &hook, UC_HOOK_CODE, test_x86_x87_fnstenv_callback,
                   &last_eip, 1, 0));

    OK(uc_emu_start(uc, code_start, code_start + sizeof(code) - 1, 0, 0));

    OK(uc_mem_read(uc, base, fnstenv, sizeof(fnstenv)));
    // But update FCS:FIP for fld.
    TEST_CHECK(LEINT32(fnstenv[3]) == last_eip);

    OK(uc_close(uc));
}

static uint64_t test_x86_mmio_read_callback(uc_engine *uc, uint64_t offset,
                                            unsigned size, void *user_data)
{
    TEST_CHECK(offset == 4);
    TEST_CHECK(size == 4);

    return 0x19260817;
}

static void test_x86_mmio_write_callback(uc_engine *uc, uint64_t offset,
                                         unsigned size, uint64_t value,
                                         void *user_data)
{
    TEST_CHECK(offset == 4);
    TEST_CHECK(size == 4);
    TEST_CHECK(value == 0xdeadbeef);

    return;
}

static void test_x86_mmio(void)
{
    uc_engine *uc;
    int r_ecx = 0xdeadbeef;
    char code[] =
        "\x89\x0d\x04\x00\x02\x00\x8b\x0d\x04\x00\x02\x00"; // mov [0x20004],
                                                            // ecx; mov ecx,
                                                            // [0x20004]

    uc_common_setup(&uc, UC_ARCH_X86, UC_MODE_32, code, sizeof(code) - 1);
    OK(uc_reg_write(uc, UC_X86_REG_ECX, &r_ecx));
    OK(uc_mmio_map(uc, 0x20000, 0x1000, test_x86_mmio_read_callback, NULL,
                   test_x86_mmio_write_callback, NULL));

    OK(uc_emu_start(uc, code_start, code_start + sizeof(code) - 1, 0, 0));

    OK(uc_reg_read(uc, UC_X86_REG_ECX, &r_ecx));

    TEST_CHECK(r_ecx == 0x19260817);

    OK(uc_close(uc));
}

static bool test_x86_missing_code_callback(uc_engine *uc, uc_mem_type type,
                                           uint64_t address, int size,
                                           uint64_t value, void *user_data)
{
    char code[] = "\x41\x4a"; // inc ecx; dec edx;
    uint64_t algined_address = address & 0xFFFFFFFFFFFFF000ULL;
    int aligned_size = ((int)(size / 0x1000) + 1) * 0x1000;

    OK(uc_mem_map(uc, algined_address, aligned_size, UC_PROT_ALL));

    OK(uc_mem_write(uc, algined_address, code, sizeof(code) - 1));

    return true;
}

static void test_x86_missing_code(void)
{
    uc_engine *uc;
    uc_hook hook;
    int r_ecx = 0x1234;
    int r_edx = 0x7890;

    // Don't write any code by design.
    OK(uc_open(UC_ARCH_X86, UC_MODE_32, &uc));
    OK(uc_reg_write(uc, UC_X86_REG_ECX, &r_ecx));
    OK(uc_reg_write(uc, UC_X86_REG_EDX, &r_edx));
    OK(uc_hook_add(uc, &hook, UC_HOOK_MEM_UNMAPPED,
                   test_x86_missing_code_callback, NULL, 1, 0));

    OK(uc_emu_start(uc, code_start, code_start + 2, 0, 0));

    OK(uc_reg_read(uc, UC_X86_REG_ECX, &r_ecx));
    OK(uc_reg_read(uc, UC_X86_REG_EDX, &r_edx));

    TEST_CHECK(r_ecx == 0x1235);
    TEST_CHECK(r_edx == 0x788f);

    OK(uc_close(uc));
}

static void test_x86_smc_xor(void)
{
    uc_engine *uc;
    /*
     * 0x1000 xor dword ptr [edi+0x3], eax ; edi=0x1000, eax=0xbc4177e6
     * 0x1003 dw 0x3ea98b13
     */
    char code[] = "\x31\x47\x03\x13\x8b\xa9\x3e";
    int r_edi = code_start;
    int r_eax = 0xbc4177e6;
    uint32_t result;

    uc_common_setup(&uc, UC_ARCH_X86, UC_MODE_32, code, sizeof(code) - 1);
    uc_reg_write(uc, UC_X86_REG_EDI, &r_edi);
    uc_reg_write(uc, UC_X86_REG_EAX, &r_eax);

    OK(uc_emu_start(uc, code_start, code_start + 3, 0, 0));

    OK(uc_mem_read(uc, code_start + 3, (void *)&result, 4));

    TEST_CHECK(LEINT32(result) == (0x3ea98b13 ^ 0xbc4177e6));

    OK(uc_close(uc));
}

static void test_x86_smc_add(void)
{
    uc_engine *uc;
    uint64_t stack_base = 0x20000;
    uint64_t r_rsp;
    /*
     * mov qword ptr [rip+0x10], rax
     * mov word ptr [rip], 0x0548
     * [orig] mov eax, dword ptr [rax + 0x12345678]; [after SMC] 480578563412
     * add rax, 0x12345678 hlt
     */
    char code[] = "\x48\x89\x05\x10\x00\x00\x00\x66\xc7\x05\x00\x00\x00\x00\x48"
                  "\x05\x8b\x80\x78\x56\x34\x12\xf4";
    uc_common_setup(&uc, UC_ARCH_X86, UC_MODE_64, code, sizeof(code) - 1);

    OK(uc_mem_map(uc, stack_base, 0x2000, UC_PROT_ALL));
    r_rsp = stack_base + 0x1800;
    OK(uc_reg_write(uc, UC_X86_REG_RSP, &r_rsp));
    OK(uc_emu_start(uc, code_start, -1, 0, 0));

    OK(uc_close(uc));
}

static void test_x86_smc_mem_hook_callback(uc_engine *uc, uc_mem_type t,
                                           uint64_t addr, int size,
                                           uint64_t value, void *user_data)
{
    uint64_t write_addresses[] = {0x1030, 0x1010, 0x1010, 0x1018,
                                  0x1018, 0x1029, 0x1029};
    unsigned int *i = user_data;

    TEST_CHECK(*i < (sizeof(write_addresses) / sizeof(write_addresses[0])));
    TEST_CHECK(write_addresses[*i] == addr);
    (*i)++;
}

static void test_x86_smc_mem_hook(void)
{
    uc_engine *uc;
    uc_hook hook;
    uint64_t stack_base = 0x20000;
    uint64_t r_rsp;
    unsigned int i = 0;
    /*
     * mov qword ptr [rip+0x29], rax
     * mov word ptr [rip], 0x0548
     * [orig] mov eax, dword ptr [rax + 0x12345678]; [after SMC] 480578563412
     * add rax, 0x12345678 nop nop nop mov qword ptr [rip-0x08], rax mov word
     * ptr [rip], 0x0548 [orig] mov eax, dword ptr [rax + 0x12345678]; [after
     * SMC] 480578563412 add rax, 0x12345678 hlt
     */
    char code[] =
        "\x48\x89\x05\x29\x00\x00\x00\x66\xC7\x05\x00\x00\x00\x00\x48\x05\x8B"
        "\x80\x78\x56\x34\x12\x90\x90\x90\x48\x89\x05\xF8\xFF\xFF\xFF\x66\xC7"
        "\x05\x00\x00\x00\x00\x48\x05\x8B\x80\x78\x56\x34\x12\xF4";
    uc_common_setup(&uc, UC_ARCH_X86, UC_MODE_64, code, sizeof(code) - 1);

    OK(uc_hook_add(uc, &hook, UC_HOOK_MEM_WRITE, test_x86_smc_mem_hook_callback,
                   &i, 1, 0));
    OK(uc_mem_map(uc, stack_base, 0x2000, UC_PROT_ALL));
    r_rsp = stack_base + 0x1800;
    OK(uc_reg_write(uc, UC_X86_REG_RSP, &r_rsp));
    OK(uc_emu_start(uc, code_start, -1, 0, 0));

    OK(uc_close(uc));
}

static uint64_t test_x86_mmio_uc_mem_rw_read_callback(uc_engine *uc,
                                                      uint64_t offset,
                                                      unsigned size,
                                                      void *user_data)
{
    TEST_CHECK(offset == 8);
    TEST_CHECK(size == 4);

    return 0x19260817;
}

static void test_x86_mmio_uc_mem_rw_write_callback(uc_engine *uc,
                                                   uint64_t offset,
                                                   unsigned size,
                                                   uint64_t value,
                                                   void *user_data)
{
    TEST_CHECK(offset == 4);
    TEST_CHECK(size == 4);
    TEST_CHECK(value == 0xdeadbeef);

    return;
}

static void test_x86_mmio_uc_mem_rw(void)
{
    uc_engine *uc;
    int data = LEINT32(0xdeadbeef);

    OK(uc_open(UC_ARCH_X86, UC_MODE_32, &uc));

    OK(uc_mmio_map(uc, 0x20000, 0x1000, test_x86_mmio_uc_mem_rw_read_callback,
                   NULL, test_x86_mmio_uc_mem_rw_write_callback, NULL));

    OK(uc_mem_write(uc, 0x20004, (void *)&data, 4));
    OK(uc_mem_read(uc, 0x20008, (void *)&data, 4));

    TEST_CHECK(LEINT32(data) == 0x19260817);

    OK(uc_close(uc));
}

static void test_x86_sysenter_hook(uc_engine *uc, void *user)
{
    *(int *)user = 1;
}

static void test_x86_sysenter(void)
{
    uc_engine *uc;
    char code[] = "\x0F\x34"; // sysenter
    uc_hook h;
    int called = 0;

    uc_common_setup(&uc, UC_ARCH_X86, UC_MODE_32, code, sizeof(code) - 1);

    OK(uc_hook_add(uc, &h, UC_HOOK_INSN, test_x86_sysenter_hook, &called, 1, 0,
                   UC_X86_INS_SYSENTER));

    OK(uc_emu_start(uc, code_start, code_start + sizeof(code) - 1, 0, 0));

    TEST_CHECK(called == 1);

    OK(uc_close(uc));
}

static int test_x86_hook_cpuid_callback(uc_engine *uc, void *data)
{
    uint32_t reg = 7;
    uint32_t eip;

    OK(uc_reg_read(uc, UC_X86_REG_EIP, (void*)&eip));
    OK(uc_reg_write(uc, UC_X86_REG_EAX, &reg));

    TEST_CHECK(eip == code_start + 1);
    // Overwrite the cpuid instruction.
    return 1;
}

static void test_x86_hook_cpuid(void)
{
    uc_engine *uc;
    char code[] = "\x40\x0F\xA2"; // INC EAX; CPUID
    uc_hook h;
    int reg;

    uc_common_setup(&uc, UC_ARCH_X86, UC_MODE_32, code, sizeof(code) - 1);

    OK(uc_hook_add(uc, &h, UC_HOOK_INSN, test_x86_hook_cpuid_callback, NULL, 1,
                   0, UC_X86_INS_CPUID));

    OK(uc_emu_start(uc, code_start, code_start + sizeof(code) - 1, 0, 0));

    OK(uc_reg_read(uc, UC_X86_REG_EAX, &reg));

    TEST_CHECK(reg == 7);

    OK(uc_close(uc));
}

static void test_x86_486_cpuid(void)
{
    uc_engine *uc;
    uint32_t eax;
    uint32_t ebx;

    char code[] = {0x31, 0xC0, 0x0F, 0xA2}; // XOR EAX EAX; CPUID

    OK(uc_open(UC_ARCH_X86, UC_MODE_32, &uc));
    OK(uc_ctl_set_cpu_model(uc, UC_CPU_X86_486));
    OK(uc_mem_map(uc, 0, 4 * 1024, UC_PROT_ALL));
    OK(uc_mem_write(uc, 0, code, sizeof(code) / sizeof(code[0])));
    OK(uc_emu_start(uc, 0, sizeof(code) / sizeof(code[0]), 0, 0));

    /* Read eax after emulation */
    OK(uc_reg_read(uc, UC_X86_REG_EAX, &eax));
    OK(uc_reg_read(uc, UC_X86_REG_EBX, &ebx));

    TEST_CHECK(eax != 0);
    TEST_CHECK(ebx == 0x756e6547); // magic string "Genu" for intel cpu

    OK(uc_close(uc));
}

static void test_x86_qemu72_xsave_cpuid(void)
{
    uc_engine *uc;
    char code[] = "\x0f\xa2";
    uint32_t eax = 0xd;
    uint32_t ebx;
    uint32_t ecx = 1;
    uint32_t edx;
    uint32_t eip = code_start;

    OK(uc_open(UC_ARCH_X86, UC_MODE_32, &uc));
    OK(uc_ctl_set_cpu_model(uc, UC_CPU_X86_HASWELL));
    OK(uc_mem_map(uc, code_start, code_len, UC_PROT_ALL));
    OK(uc_mem_write(uc, code_start, code, sizeof(code) - 1));

    OK(uc_reg_write(uc, UC_X86_REG_EAX, &eax));
    OK(uc_reg_write(uc, UC_X86_REG_ECX, &ecx));
    OK(uc_emu_start(uc, code_start, code_start + sizeof(code) - 1, 0, 0));

    OK(uc_reg_read(uc, UC_X86_REG_EAX, &eax));
    OK(uc_reg_read(uc, UC_X86_REG_EBX, &ebx));
    OK(uc_reg_read(uc, UC_X86_REG_ECX, &ecx));
    OK(uc_reg_read(uc, UC_X86_REG_EDX, &edx));

    TEST_CHECK(eax != 0);
    TEST_CHECK(ebx >= 512);
    TEST_CHECK((ecx & ~(1U << 15)) == 0);
    TEST_CHECK(edx == 0);

    eax = 0xd;
    ecx = 0;
    OK(uc_reg_write(uc, UC_X86_REG_EAX, &eax));
    OK(uc_reg_write(uc, UC_X86_REG_ECX, &ecx));
    OK(uc_reg_write(uc, UC_X86_REG_EIP, &eip));
    OK(uc_emu_start(uc, code_start, code_start + sizeof(code) - 1, 0, 0));

    OK(uc_reg_read(uc, UC_X86_REG_EAX, &eax));
    OK(uc_reg_read(uc, UC_X86_REG_EBX, &ebx));
    OK(uc_reg_read(uc, UC_X86_REG_ECX, &ecx));
    OK(uc_reg_read(uc, UC_X86_REG_EDX, &edx));

    TEST_CHECK((eax & 0x7) == 0x7);
    TEST_CHECK(ebx >= 512);
    TEST_CHECK(ecx >= ebx);
    TEST_CHECK(edx == 0);

    OK(uc_close(uc));
}

static void test_x86_opmask_registers(void)
{
    uc_engine *uc;

    OK(uc_open(UC_ARCH_X86, UC_MODE_64, &uc));

    for (int i = 0; i < 8; i++) {
        uint64_t in = 0x1122334455667700ULL + i;
        uint64_t out = 0;
        int reg = UC_X86_REG_K0 + i;

        OK(uc_reg_write(uc, reg, &in));
        OK(uc_reg_read(uc, reg, &out));
        TEST_CHECK(out == in);
    }

    OK(uc_close(uc));
}

static void test_x86_qemu72_msr_state(void)
{
    uc_engine *uc;
    uc_x86_msr msr;

    OK(uc_open(UC_ARCH_X86, UC_MODE_64, &uc));

    msr.rid = TEST_MSR_IA32_XFD;
    msr.value = 0x12345678abcdef00ULL;
    OK(uc_reg_write(uc, UC_X86_REG_MSR, &msr));
    msr.value = 0;
    OK(uc_reg_read(uc, UC_X86_REG_MSR, &msr));
    TEST_CHECK(msr.value == 0x12345678abcdef00ULL);

    msr.rid = TEST_MSR_IA32_XFD_ERR;
    msr.value = 0xfedcba9876543210ULL;
    OK(uc_reg_write(uc, UC_X86_REG_MSR, &msr));
    msr.value = 0;
    OK(uc_reg_read(uc, UC_X86_REG_MSR, &msr));
    TEST_CHECK(msr.value == 0xfedcba9876543210ULL);

    msr.rid = TEST_MSR_IA32_PKRS;
    msr.value = 0xa5a55a5aULL;
    OK(uc_reg_write(uc, UC_X86_REG_MSR, &msr));
    msr.value = 0;
    OK(uc_reg_read(uc, UC_X86_REG_MSR, &msr));
    TEST_CHECK(msr.value == 0xa5a55a5aULL);

    msr.rid = TEST_MSR_ARCH_LBR_CTL;
    msr.value = 0x19;
    OK(uc_reg_write(uc, UC_X86_REG_MSR, &msr));
    msr.value = 0;
    OK(uc_reg_read(uc, UC_X86_REG_MSR, &msr));
    TEST_CHECK(msr.value == 0x19);

    msr.rid = TEST_MSR_ARCH_LBR_DEPTH;
    msr.value = 32;
    OK(uc_reg_write(uc, UC_X86_REG_MSR, &msr));
    msr.value = 0;
    OK(uc_reg_read(uc, UC_X86_REG_MSR, &msr));
    TEST_CHECK(msr.value == 32);

    msr.rid = TEST_MSR_ARCH_LBR_FROM_0 + 3;
    msr.value = 0x1111222233334444ULL;
    OK(uc_reg_write(uc, UC_X86_REG_MSR, &msr));
    msr.value = 0;
    OK(uc_reg_read(uc, UC_X86_REG_MSR, &msr));
    TEST_CHECK(msr.value == 0x1111222233334444ULL);

    msr.rid = TEST_MSR_ARCH_LBR_TO_0 + 3;
    msr.value = 0x5555666677778888ULL;
    OK(uc_reg_write(uc, UC_X86_REG_MSR, &msr));
    msr.value = 0;
    OK(uc_reg_read(uc, UC_X86_REG_MSR, &msr));
    TEST_CHECK(msr.value == 0x5555666677778888ULL);

    msr.rid = TEST_MSR_ARCH_LBR_INFO_0 + 3;
    msr.value = 0x9999aaaabbbbccccULL;
    OK(uc_reg_write(uc, UC_X86_REG_MSR, &msr));
    msr.value = 0;
    OK(uc_reg_read(uc, UC_X86_REG_MSR, &msr));
    TEST_CHECK(msr.value == 0x9999aaaabbbbccccULL);

    msr.rid = TEST_MSR_IA32_XSS;
    msr.value = UINT64_MAX;
    OK(uc_reg_write(uc, UC_X86_REG_MSR, &msr));
    msr.value = UINT64_MAX;
    OK(uc_reg_read(uc, UC_X86_REG_MSR, &msr));
    TEST_CHECK((msr.value & ~(1ULL << 15)) == 0);

    OK(uc_close(uc));
}

// This is a regression bug.
static void test_x86_clear_tb_cache(void)
{
    uc_engine *uc;
    char code[] = "\x83\xc1\x01\x4a"; // ADD ecx, 1; DEC edx;
    int r_ecx = 0x1234;
    int r_edx = 0x7890;
    uint64_t code_start = 0x1240; // Choose this address by design
    uint64_t code_len = 0x1000;

    OK(uc_open(UC_ARCH_X86, UC_MODE_32, &uc));
    OK(uc_mem_map(uc, code_start & (1 << 12), code_len, UC_PROT_ALL));
    OK(uc_mem_write(uc, code_start, code, sizeof(code)));
    OK(uc_reg_write(uc, UC_X86_REG_ECX, &r_ecx));
    OK(uc_reg_write(uc, UC_X86_REG_EDX, &r_edx));

    // This emulation should take no effect at all.
    OK(uc_emu_start(uc, code_start, code_start, 0, 0));

    // Emulate ADD ecx, 1.
    OK(uc_emu_start(uc, code_start, code_start + 3, 0, 0));

    // If tb cache is not cleared, edx would be still 0x7890
    OK(uc_emu_start(uc, code_start, code_start + sizeof(code) - 1, 0, 0));

    OK(uc_reg_read(uc, UC_X86_REG_ECX, &r_ecx));
    OK(uc_reg_read(uc, UC_X86_REG_EDX, &r_edx));

    TEST_CHECK(r_ecx == 0x1236);
    TEST_CHECK(r_edx == 0x788f);

    OK(uc_close(uc));
}

static void test_x86_clear_count_cache(void)
{
    uc_engine *uc;
    // uc_emu_start will clear last TB when exiting so generating a tb at last
    // by design
    char code[] =
        "\x83\xc1\x01\x4a\xeb\x00\x83\xc3\x01"; // ADD ecx, 1; DEC edx;
                                                // jmp t;
                                                // t:
                                                // ADD ebx, 1
    int r_ecx = 0x1234;
    int r_edx = 0x7890;

    uc_common_setup(&uc, UC_ARCH_X86, UC_MODE_32, code, sizeof(code) - 1);
    OK(uc_reg_write(uc, UC_X86_REG_ECX, &r_ecx));
    OK(uc_reg_write(uc, UC_X86_REG_EDX, &r_edx));

    OK(uc_emu_start(uc, code_start, code_start + sizeof(code) - 1, 0, 2));
    OK(uc_emu_start(uc, code_start, code_start + sizeof(code) - 1, 0, 0));

    OK(uc_reg_read(uc, UC_X86_REG_ECX, &r_ecx));
    OK(uc_reg_read(uc, UC_X86_REG_EDX, &r_edx));

    TEST_CHECK(r_ecx == 0x1236);
    TEST_CHECK(r_edx == 0x788e);

    OK(uc_close(uc));
}

// This is a regression bug.
static void test_x86_clear_empty_tb(void)
{
    uc_engine *uc;
    // lb:
    //    add ecx, 1;
    //    cmp ecx, 0;
    //    jz lb;
    //    dec edx;
    char code[] = "\x83\xc1\x01\x83\xf9\x00\x74\xf8\x4a";
    int r_edx = 0x7890;
    uint64_t code_start = 0x1240; // Choose this address by design
    uint64_t code_len = 0x1000;

    OK(uc_open(UC_ARCH_X86, UC_MODE_32, &uc));
    OK(uc_mem_map(uc, code_start & (1 << 12), code_len, UC_PROT_ALL));
    OK(uc_mem_write(uc, code_start, code, sizeof(code)));
    OK(uc_reg_write(uc, UC_X86_REG_EDX, &r_edx));

    // Make sure we generate an empty tb at the exit address by stopping at dec
    // edx.
    OK(uc_emu_start(uc, code_start, code_start + 8, 0, 0));

    // If tb cache is not cleared, edx would be still 0x7890
    OK(uc_emu_start(uc, code_start, code_start + sizeof(code) - 1, 0, 0));

    OK(uc_reg_read(uc, UC_X86_REG_EDX, &r_edx));

    TEST_CHECK(r_edx == 0x788f);

    OK(uc_close(uc));
}

typedef struct TbLoopStop_t {
    unsigned int block_count;
    unsigned int stop_at;
} TbLoopStop;

static void test_x86_tb_loop_stop_cb(uc_engine *uc, uint64_t address,
                                     uint32_t size, void *user_data)
{
    TbLoopStop *stop = user_data;

    stop->block_count++;
    if (stop->block_count == stop->stop_at) {
        OK(uc_emu_stop(uc));
    }
}

static void test_x86_self_linked_tb_guest_smc(void)
{
    uc_engine *uc;
    uc_hook hook;
    char loop[] = "\xeb\xfe";
    char smc[] = "\xc6\x05\x00\x10\x00\x00\x40"
                 "\xc6\x05\x01\x10\x00\x00\x90";
    TbLoopStop stop = {0, 2};
    uint32_t eax = 0x1234;

    uc_common_setup(&uc, UC_ARCH_X86, UC_MODE_32, loop, sizeof(loop) - 1);
    OK(uc_mem_write(uc, code_start + 0x1000, smc, sizeof(smc) - 1));
    OK(uc_hook_add(uc, &hook, UC_HOOK_BLOCK, test_x86_tb_loop_stop_cb,
                   &stop, code_start, code_start));

    OK(uc_emu_start(uc, code_start, -1, 0, 0));
    TEST_CHECK(stop.block_count == 2);

    OK(uc_emu_start(uc, code_start + 0x1000,
                    code_start + 0x1000 + sizeof(smc) - 1, 0, 0));
    stop.stop_at = 4;
    OK(uc_reg_write(uc, UC_X86_REG_EAX, &eax));
    OK(uc_emu_start(uc, code_start, code_start + 1, 0, 0));
    OK(uc_reg_read(uc, UC_X86_REG_EAX, &eax));
    TEST_CHECK(eax == 0x1235);
    TEST_CHECK(stop.block_count == 3);

    OK(uc_close(uc));
}

static void test_x86_two_page_tb_invalidation(void)
{
    const uint64_t tb_start = 0x1ffe;
    uc_engine *uc;
    char code[] = "\xb8\x11\x11\x11\x11";
    char second_page_byte = '\x22';
    char first_page_byte = '\x33';
    uint32_t eax;

    OK(uc_open(UC_ARCH_X86, UC_MODE_32, &uc));
    OK(uc_mem_map(uc, 0x1000, 0x2000, UC_PROT_ALL));
    OK(uc_mem_write(uc, tb_start, code, sizeof(code) - 1));

    OK(uc_emu_start(uc, tb_start, tb_start + sizeof(code) - 1, 0, 0));
    OK(uc_reg_read(uc, UC_X86_REG_EAX, &eax));
    TEST_CHECK(eax == 0x11111111);

    OK(uc_mem_write(uc, tb_start + 2, &second_page_byte, 1));
    OK(uc_emu_start(uc, tb_start, tb_start + sizeof(code) - 1, 0, 0));
    OK(uc_reg_read(uc, UC_X86_REG_EAX, &eax));
    TEST_CHECK(eax == 0x11112211);

    OK(uc_mem_write(uc, tb_start + 1, &first_page_byte, 1));
    OK(uc_emu_start(uc, tb_start, tb_start + sizeof(code) - 1, 0, 0));
    OK(uc_reg_read(uc, UC_X86_REG_EAX, &eax));
    TEST_CHECK(eax == 0x11112233);

    OK(uc_close(uc));
}

static void test_x86_tb_cache_engine_isolation(void)
{
    uc_engine *uc1;
    uc_engine *uc2;
    char code1[] = "\xb8\x11\x11\x11\x11";
    char code2[] = "\xb8\x22\x22\x22\x22";
    char replacement[] = "\xb8\x33\x33\x33\x33";
    uint32_t eax;

    OK(uc_open(UC_ARCH_X86, UC_MODE_32, &uc1));
    OK(uc_open(UC_ARCH_X86, UC_MODE_32, &uc2));
    OK(uc_mem_map(uc1, code_start, 0x1000, UC_PROT_ALL));
    OK(uc_mem_map(uc2, code_start, 0x1000, UC_PROT_ALL));
    OK(uc_mem_write(uc1, code_start, code1, sizeof(code1) - 1));
    OK(uc_mem_write(uc2, code_start, code2, sizeof(code2) - 1));

    OK(uc_emu_start(uc1, code_start, code_start + sizeof(code1) - 1, 0, 0));
    OK(uc_emu_start(uc2, code_start, code_start + sizeof(code2) - 1, 0, 0));

    OK(uc_ctl_flush_tb(uc1));
    OK(uc_mem_write(uc1, code_start, replacement, sizeof(replacement) - 1));
    OK(uc_emu_start(uc1, code_start,
                    code_start + sizeof(replacement) - 1, 0, 0));
    OK(uc_reg_read(uc1, UC_X86_REG_EAX, &eax));
    TEST_CHECK(eax == 0x33333333);

    OK(uc_emu_start(uc2, code_start, code_start + sizeof(code2) - 1, 0, 0));
    OK(uc_reg_read(uc2, UC_X86_REG_EAX, &eax));
    TEST_CHECK(eax == 0x22222222);

    OK(uc_close(uc1));
    OK(uc_close(uc2));
}

typedef struct _HOOK_TCG_OP_RESULT {
    uint64_t address;
    uint64_t arg1;
    uint64_t arg2;
} HOOK_TCG_OP_RESULT;

typedef struct _HOOK_TCG_OP_RESULTS {
    HOOK_TCG_OP_RESULT results[128];
    uint64_t len;
} HOOK_TCG_OP_RESULTS;

static void test_x86_hook_tcg_op_cb(uc_engine *uc, uint64_t address,
                                    uint64_t arg1, uint64_t arg2, uint32_t size,
                                    void *data)
{
    HOOK_TCG_OP_RESULTS *results = (HOOK_TCG_OP_RESULTS *)data;
    HOOK_TCG_OP_RESULT *result = &results->results[results->len++];

    result->address = address;
    result->arg1 = arg1;
    result->arg2 = arg2;
}

static void test_x86_hook_tcg_op(void)
{
    uc_engine *uc;
    uc_hook h;
    int flag;
    HOOK_TCG_OP_RESULTS results;
    // sub esi, [0x1000];
    // sub eax, ebx;
    // sub eax, 1;
    // cmp eax, 0;
    // cmp ebx, edx;
    // cmp esi, [0x1000];
    char code[] = "\x2b\x35\x00\x10\x00\x00\x29\xd8\x83\xe8\x01\x83\xf8\x00\x39"
                  "\xd3\x3b\x35\x00\x10\x00\x00";
    int r_eax = 0x1234;
    int r_ebx = 2;

    uc_common_setup(&uc, UC_ARCH_X86, UC_MODE_32, code, sizeof(code) - 1);
    OK(uc_reg_write(uc, UC_X86_REG_EAX, &r_eax));
    OK(uc_reg_write(uc, UC_X86_REG_EBX, &r_ebx));

    memset(&results, 0, sizeof(HOOK_TCG_OP_RESULTS));
    flag = 0;
    OK(uc_hook_add(uc, &h, UC_HOOK_TCG_OPCODE, test_x86_hook_tcg_op_cb,
                   &results, 0, -1, UC_TCG_OP_SUB, flag));
    OK(uc_emu_start(uc, code_start, code_start + sizeof(code) - 1, 0, 0));
    OK(uc_hook_del(uc, h));

    TEST_CHECK(results.len == 6);

    memset(&results, 0, sizeof(HOOK_TCG_OP_RESULTS));
    flag = UC_TCG_OP_FLAG_DIRECT;
    OK(uc_hook_add(uc, &h, UC_HOOK_TCG_OPCODE, test_x86_hook_tcg_op_cb,
                   &results, 0, -1, UC_TCG_OP_SUB, flag));
    OK(uc_emu_start(uc, code_start, code_start + sizeof(code) - 1, 0, 0));
    OK(uc_hook_del(uc, h));

    TEST_CHECK(results.len == 3);

    memset(&results, 0, sizeof(HOOK_TCG_OP_RESULTS));
    flag = UC_TCG_OP_FLAG_CMP;
    OK(uc_hook_add(uc, &h, UC_HOOK_TCG_OPCODE, test_x86_hook_tcg_op_cb,
                   &results, 0, -1, UC_TCG_OP_SUB, flag));
    OK(uc_emu_start(uc, code_start, code_start + sizeof(code) - 1, 0, 0));
    OK(uc_hook_del(uc, h));

    TEST_CHECK(results.len == 3);

    OK(uc_close(uc));
}

static bool test_x86_cmpxchg_mem_hook(uc_engine *uc, uc_mem_type type,
                                      uint64_t address, int size, int64_t val,
                                      void *data)
{
    if (type == UC_MEM_READ) {
        *((int *)data) |= 1;
    } else {
        *((int *)data) |= 2;
    }

    return true;
}

static void test_x86_cmpxchg(void)
{
    uc_engine *uc;
    char code[] = "\x0F\xC7\x0D\xE0\xBE\xAD\xDE"; // cmpxchg8b [0xdeadbee0]
    int r_zero = 0;
    int r_aaaa = 0x41414141;
    uint64_t mem;
    uc_hook h;
    int result = 0;

    uc_common_setup(&uc, UC_ARCH_X86, UC_MODE_32, code, sizeof(code) - 1);
    OK(uc_mem_map(uc, 0xdeadb000, 0x1000, UC_PROT_ALL));
    OK(uc_hook_add(uc, &h, UC_HOOK_MEM_READ | UC_HOOK_MEM_WRITE,
                   test_x86_cmpxchg_mem_hook, &result, 1, 0));

    OK(uc_reg_write(uc, UC_X86_REG_EDX, &r_zero));
    OK(uc_reg_write(uc, UC_X86_REG_EAX, &r_zero));
    OK(uc_reg_write(uc, UC_X86_REG_ECX, &r_aaaa));
    OK(uc_reg_write(uc, UC_X86_REG_EBX, &r_aaaa));

    OK(uc_emu_start(uc, code_start, code_start + sizeof(code) - 1, 0, 0));

    OK(uc_mem_read(uc, 0xdeadbee0, &mem, 8));

    TEST_CHECK(mem == 0x4141414141414141);

    // Both read and write happened.
    TEST_CHECK(result == 3);

    OK(uc_close(uc));
}

static void test_x86_cmpxchg32_acc_case(uint64_t initial_rax,
                                        uint64_t initial_mem,
                                        uint64_t expected_rax,
                                        uint64_t expected_mem,
                                        bool expected_zf)
{
    uc_engine *uc;
    char code[] = "\x41\x0f\xb1\x18"; /* cmpxchg dword ptr [r8], ebx */
    uint64_t data_address = 0x2000000;
    uint64_t rax = initial_rax;
    uint64_t rbx = 0;
    uint64_t r8 = data_address;
    uint64_t rflags;
    uint64_t mem;

    uc_common_setup(&uc, UC_ARCH_X86, UC_MODE_64, code, sizeof(code) - 1);
    OK(uc_mem_map(uc, data_address, 0x1000, UC_PROT_ALL));
    OK(uc_mem_write(uc, data_address, &initial_mem, sizeof(initial_mem)));
    OK(uc_reg_write(uc, UC_X86_REG_R8, &r8));
    OK(uc_reg_write(uc, UC_X86_REG_RAX, &rax));
    OK(uc_reg_write(uc, UC_X86_REG_RBX, &rbx));

    OK(uc_emu_start(uc, code_start, code_start + sizeof(code) - 1, 0, 0));
    OK(uc_reg_read(uc, UC_X86_REG_RAX, &rax));
    OK(uc_reg_read(uc, UC_X86_REG_RFLAGS, &rflags));
    OK(uc_mem_read(uc, data_address, &mem, sizeof(mem)));

    TEST_CHECK(rax == expected_rax);
    TEST_CHECK(mem == expected_mem);
    TEST_CHECK((bool)(rflags & 0x40) == expected_zf);

    OK(uc_close(uc));
}

static void test_x86_cmpxchg32_accumulator(void)
{
    test_x86_cmpxchg32_acc_case(0xffffffffffffffffULL,
                                0xffffffffffffffffULL,
                                0xffffffffffffffffULL,
                                0xffffffff00000000ULL, true);
    test_x86_cmpxchg32_acc_case(0xffffffff00000000ULL,
                                0xffffffffffffffffULL,
                                0x00000000ffffffffULL,
                                0xffffffffffffffffULL, false);
}

static void test_x86_cmpxchg32_reg_case(uint64_t initial_rax,
                                        uint64_t initial_rcx,
                                        uint64_t initial_rbx,
                                        uint64_t expected_rax,
                                        uint64_t expected_rcx,
                                        bool expected_zf)
{
    uc_engine *uc;
    char code[] = "\x0f\xb1\xd9"; /* cmpxchg ecx, ebx */
    uint64_t rax = initial_rax;
    uint64_t rcx = initial_rcx;
    uint64_t rbx = initial_rbx;
    uint64_t rflags;

    uc_common_setup(&uc, UC_ARCH_X86, UC_MODE_64, code, sizeof(code) - 1);
    OK(uc_reg_write(uc, UC_X86_REG_RAX, &rax));
    OK(uc_reg_write(uc, UC_X86_REG_RCX, &rcx));
    OK(uc_reg_write(uc, UC_X86_REG_RBX, &rbx));

    OK(uc_emu_start(uc, code_start, code_start + sizeof(code) - 1, 0, 0));
    OK(uc_reg_read(uc, UC_X86_REG_RAX, &rax));
    OK(uc_reg_read(uc, UC_X86_REG_RCX, &rcx));
    OK(uc_reg_read(uc, UC_X86_REG_RFLAGS, &rflags));

    TEST_CHECK(rax == expected_rax);
    TEST_CHECK(rcx == expected_rcx);
    TEST_CHECK((bool)(rflags & 0x40) == expected_zf);

    OK(uc_close(uc));
}

static void test_x86_cmpxchg32_register(void)
{
    test_x86_cmpxchg32_reg_case(0xeeeeeeeeffffffffULL,
                                0xaaaaaaaaffffffffULL,
                                0x1111111122222222ULL,
                                0xeeeeeeeeffffffffULL,
                                0x0000000022222222ULL, true);
    test_x86_cmpxchg32_reg_case(0x1111111112345678ULL,
                                0xaaaaaaaaffffffffULL,
                                0x1111111122222222ULL,
                                0x00000000ffffffffULL,
                                0xaaaaaaaaffffffffULL, false);
}

static void test_x86_ret_imm16_unsigned(void)
{
    uc_engine *uc;
    char code[] = "\xc2\x00\xff"; /* ret 0xff00 */
    uint64_t stack_address = 0x2000000;
    uint64_t return_address = code_start + sizeof(code) - 1;
    uint64_t rsp = stack_address;

    uc_common_setup(&uc, UC_ARCH_X86, UC_MODE_64, code, sizeof(code) - 1);
    OK(uc_mem_map(uc, stack_address, 0x1000, UC_PROT_ALL));
    OK(uc_mem_write(uc, stack_address, &return_address,
                    sizeof(return_address)));
    OK(uc_reg_write(uc, UC_X86_REG_RSP, &rsp));

    OK(uc_emu_start(uc, code_start, return_address, 0, 1));
    OK(uc_reg_read(uc, UC_X86_REG_RSP, &rsp));

    TEST_CHECK(rsp == stack_address + 8 + 0xff00);

    OK(uc_close(uc));
}

static void test_x86_rorx_rip_relative_imm(void)
{
    uc_engine *uc;
    char code[] = "\xc4\xe3\x7b\xf0\x05\xf6\x14\x00\x00\x00";
    uint64_t expected_address = code_start + sizeof(code) - 1 + 0x14f6;
    uint8_t data[] = {0xaa, 0x11, 0x22, 0x33, 0x44};
    uint64_t rax;

    uc_common_setup(&uc, UC_ARCH_X86, UC_MODE_64, code, sizeof(code) - 1);
    OK(uc_mem_write(uc, expected_address - 1, data, sizeof(data)));

    OK(uc_emu_start(uc, code_start, code_start + sizeof(code) - 1, 0, 1));
    OK(uc_reg_read(uc, UC_X86_REG_RAX, &rax));

    TEST_CHECK(rax == 0x44332211);

    OK(uc_close(uc));
}

static void test_x86_shiftd_rip_relative_imm(const char *code,
                                             size_t code_size, uint64_t rbx,
                                             uint16_t expected_value)
{
    uc_engine *uc;
    uint64_t expected_address = code_start + code_size + 0x14f7;
    uint8_t data[] = {0xaa, 0x11, 0x22, 0x33, 0x44};
    uint8_t previous;
    uint16_t mem;

    uc_common_setup(&uc, UC_ARCH_X86, UC_MODE_64, code, code_size);
    OK(uc_mem_write(uc, expected_address - 1, data, sizeof(data)));
    OK(uc_reg_write(uc, UC_X86_REG_RBX, &rbx));

    OK(uc_emu_start(uc, code_start, code_start + code_size, 0, 1));
    OK(uc_mem_read(uc, expected_address - 1, &previous, sizeof(previous)));
    OK(uc_mem_read(uc, expected_address, &mem, sizeof(mem)));

    TEST_CHECK(previous == 0xaa);
    TEST_CHECK(mem == expected_value);

    OK(uc_close(uc));
}

static void test_x86_shld_rip_relative_imm(void)
{
    char code[] = "\x66\x0f\xa4\x1d\xf7\x14\x00\x00\x01";

    test_x86_shiftd_rip_relative_imm(code, sizeof(code) - 1, 0x8000, 0x4423);
}

static void test_x86_shrd_rip_relative_imm(void)
{
    char code[] = "\x66\x0f\xac\x1d\xf7\x14\x00\x00\x01";

    test_x86_shiftd_rip_relative_imm(code, sizeof(code) - 1, 1, 0x9108);
}

static void test_x86_pdep32_zero_extend(void)
{
    uc_engine *uc;
    char code[] = "\xc4\xe2\x63\xf5\xc1"; /* pdep eax, ebx, ecx */
    uint64_t rax = 0xffffffffffffffffULL;
    uint64_t rbx = 0xffffffffffffff00ULL;
    uint64_t rcx = 0xffffffffffffff00ULL;

    uc_common_setup(&uc, UC_ARCH_X86, UC_MODE_64, code, sizeof(code) - 1);
    OK(uc_reg_write(uc, UC_X86_REG_RAX, &rax));
    OK(uc_reg_write(uc, UC_X86_REG_RBX, &rbx));
    OK(uc_reg_write(uc, UC_X86_REG_RCX, &rcx));

    OK(uc_emu_start(uc, code_start, code_start + sizeof(code) - 1, 0, 1));
    OK(uc_reg_read(uc, UC_X86_REG_RAX, &rax));

    TEST_CHECK(rax == 0x00000000ffff0000ULL);

    OK(uc_close(uc));
}

static void test_x86_pext32_zero_extend(void)
{
    uc_engine *uc;
    char code[] = "\xc4\xe2\x62\xf5\xc1"; /* pext eax, ebx, ecx */
    uint64_t rax = 0xffffffffffffffffULL;
    uint64_t rbx = 0xffffffffabcdef00ULL;
    uint64_t rcx = 0xffffffff0000ff00ULL;

    uc_common_setup(&uc, UC_ARCH_X86, UC_MODE_64, code, sizeof(code) - 1);
    OK(uc_reg_write(uc, UC_X86_REG_RAX, &rax));
    OK(uc_reg_write(uc, UC_X86_REG_RBX, &rbx));
    OK(uc_reg_write(uc, UC_X86_REG_RCX, &rcx));

    OK(uc_emu_start(uc, code_start, code_start + sizeof(code) - 1, 0, 1));
    OK(uc_reg_read(uc, UC_X86_REG_RAX, &rax));

    TEST_CHECK(rax == 0xef);

    OK(uc_close(uc));
}

static void test_x86_nested_emu_start_cb(uc_engine *uc, uint64_t addr,
                                         size_t size, void *data)
{
    OK(uc_emu_start(uc, code_start + 1, code_start + 2, 0, 0));
}

static void test_x86_nested_emu_start(void)
{
    uc_engine *uc;
    char code[] = "\x41\x4a"; // INC ecx; DEC edx;
    int r_ecx = 0x1234;
    int r_edx = 0x7890;
    uc_hook h;

    uc_common_setup(&uc, UC_ARCH_X86, UC_MODE_32, code, sizeof(code) - 1);
    OK(uc_reg_write(uc, UC_X86_REG_ECX, &r_ecx));
    OK(uc_reg_write(uc, UC_X86_REG_EDX, &r_edx));
    // Emulate DEC in the nested hook.
    OK(uc_hook_add(uc, &h, UC_HOOK_CODE, test_x86_nested_emu_start_cb, NULL,
                   code_start, code_start));

    // Emulate INC
    OK(uc_emu_start(uc, code_start, code_start + 1, 0, 0));

    OK(uc_reg_read(uc, UC_X86_REG_ECX, &r_ecx));
    OK(uc_reg_read(uc, UC_X86_REG_EDX, &r_edx));

    TEST_CHECK(r_ecx == 0x1235);
    TEST_CHECK(r_edx == 0x788f);

    OK(uc_close(uc));
}

static void test_x86_nested_emu_stop_cb(uc_engine *uc, uint64_t addr,
                                        size_t size, void *data)
{
    OK(uc_emu_start(uc, code_start + 1, code_start + 2, 0, 0));
    // ecx shouldn't be changed!
    OK(uc_emu_stop(uc));
}

static void test_x86_nested_emu_stop(void)
{
    uc_engine *uc;
    // INC ecx; DEC edx; DEC edx;
    char code[] = "\x41\x4a\x4a";
    int r_ecx = 0x1234;
    int r_edx = 0x7890;
    uc_hook h;

    uc_common_setup(&uc, UC_ARCH_X86, UC_MODE_32, code, sizeof(code) - 1);
    OK(uc_reg_write(uc, UC_X86_REG_ECX, &r_ecx));
    OK(uc_reg_write(uc, UC_X86_REG_EDX, &r_edx));
    // Emulate DEC in the nested hook.
    OK(uc_hook_add(uc, &h, UC_HOOK_CODE, test_x86_nested_emu_stop_cb, NULL,
                   code_start, code_start));

    OK(uc_emu_start(uc, code_start, code_start + 3, 0, 0));

    OK(uc_reg_read(uc, UC_X86_REG_ECX, &r_ecx));
    OK(uc_reg_read(uc, UC_X86_REG_EDX, &r_edx));

    TEST_CHECK(r_ecx == 0x1234);
    TEST_CHECK(r_edx == 0x788f);

    OK(uc_close(uc));
}

static void test_x86_nested_emu_start_error_cb(uc_engine *uc, uint64_t addr,
                                               size_t size, void *data)
{
    uc_assert_err(UC_ERR_READ_UNMAPPED,
                  uc_emu_start(uc, code_start + 2, 0, 0, 0));
}

static void test_x86_64_nested_emu_start_error(void)
{
    uc_engine *uc;
    // "nop;nop;mov rax, [0x10000]"
    char code[] = "\x90\x90\x48\xa1\x00\x00\x01\x00\x00\x00\x00\x00";
    uc_hook hk;

    uc_common_setup(&uc, UC_ARCH_X86, UC_MODE_64, code, sizeof(code) - 1);
    OK(uc_hook_add(uc, &hk, UC_HOOK_CODE, test_x86_nested_emu_start_error_cb,
                   NULL, code_start, code_start));

    // This call shouldn't fail!
    OK(uc_emu_start(uc, code_start, code_start + 2, 0, 0));

    OK(uc_close(uc));
}

static void test_x86_eflags_reserved_bit(void)
{
    uc_engine *uc;
    uint32_t r_eflags;

    OK(uc_open(UC_ARCH_X86, UC_MODE_32, &uc));

    OK(uc_reg_read(uc, UC_X86_REG_EFLAGS, &r_eflags));

    TEST_CHECK((r_eflags & 2) != 0);

    OK(uc_reg_write(uc, UC_X86_REG_EFLAGS, &r_eflags));

    OK(uc_reg_read(uc, UC_X86_REG_EFLAGS, &r_eflags));

    TEST_CHECK((r_eflags & 2) != 0);

    OK(uc_close(uc));
}

static void test_x86_blsi_cf_case(uint64_t src, uint64_t expected_dst,
                                  bool expected_cf, bool expected_zf)
{
    uc_engine *uc;
    char code[] = "\xc4\xe2\xf8\xf3\xdb"; /* blsi rax, rbx */
    uint64_t rax;
    uint64_t rflags;

    uc_common_setup(&uc, UC_ARCH_X86, UC_MODE_64, code, sizeof(code) - 1);
    OK(uc_reg_write(uc, UC_X86_REG_RBX, &src));

    OK(uc_emu_start(uc, code_start, code_start + sizeof(code) - 1, 0, 0));
    OK(uc_reg_read(uc, UC_X86_REG_RAX, &rax));
    OK(uc_reg_read(uc, UC_X86_REG_RFLAGS, &rflags));

    TEST_CHECK(rax == expected_dst);
    TEST_CHECK((bool)(rflags & 1) == expected_cf);
    TEST_CHECK((bool)(rflags & 0x40) == expected_zf);

    OK(uc_close(uc));
}

static void test_x86_blsi_cf(void)
{
    test_x86_blsi_cf_case(1, 1, true, false);
    test_x86_blsi_cf_case(0, 0, false, true);
}

static void test_x86_blsr_flags_case(uint64_t src, uint64_t expected_dst,
                                     bool expected_cf, bool expected_zf)
{
    uc_engine *uc;
    char code[] = "\xc4\xe2\xf8\xf3\xcb"; /* blsr rax, rbx */
    uint64_t rax;
    uint64_t rflags;

    uc_common_setup(&uc, UC_ARCH_X86, UC_MODE_64, code, sizeof(code) - 1);
    OK(uc_reg_write(uc, UC_X86_REG_RBX, &src));

    OK(uc_emu_start(uc, code_start, code_start + sizeof(code) - 1, 0, 0));
    OK(uc_reg_read(uc, UC_X86_REG_RAX, &rax));
    OK(uc_reg_read(uc, UC_X86_REG_RFLAGS, &rflags));

    TEST_CHECK(rax == expected_dst);
    TEST_CHECK((bool)(rflags & 1) == expected_cf);
    TEST_CHECK((bool)(rflags & 0x40) == expected_zf);

    OK(uc_close(uc));
}

static void test_x86_blsr_flags(void)
{
    test_x86_blsr_flags_case(0x28, 0x20, false, false);
    test_x86_blsr_flags_case(1, 0, false, true);
    test_x86_blsr_flags_case(0, 0, true, true);
}

static void test_x86_blsmsk_flags_case(uint64_t src, uint64_t expected_dst,
                                       bool expected_cf, bool expected_zf,
                                       bool expected_sf)
{
    uc_engine *uc;
    char code[] = "\xc4\xe2\xf8\xf3\xd3"; /* blsmsk rax, rbx */
    uint64_t rax;
    uint64_t rflags;

    uc_common_setup(&uc, UC_ARCH_X86, UC_MODE_64, code, sizeof(code) - 1);
    OK(uc_reg_write(uc, UC_X86_REG_RBX, &src));

    OK(uc_emu_start(uc, code_start, code_start + sizeof(code) - 1, 0, 0));
    OK(uc_reg_read(uc, UC_X86_REG_RAX, &rax));
    OK(uc_reg_read(uc, UC_X86_REG_RFLAGS, &rflags));

    TEST_CHECK(rax == expected_dst);
    TEST_CHECK((bool)(rflags & 1) == expected_cf);
    TEST_CHECK((bool)(rflags & 0x40) == expected_zf);
    TEST_CHECK((bool)(rflags & 0x80) == expected_sf);

    OK(uc_close(uc));
}

static void test_x86_blsmsk_flags(void)
{
    test_x86_blsmsk_flags_case(0x28, 0x0f, false, false, false);
    test_x86_blsmsk_flags_case(0, UINT64_MAX, true, false, true);
}

static void test_x86_bzhi_index_case(uint64_t index, uint64_t expected_dst,
                                     bool expected_cf, bool expected_sf)
{
    uc_engine *uc;
    char code[] = "\xc4\xe2\xf0\xf5\xc3"; /* bzhi rax, rbx, rcx */
    uint64_t rax;
    uint64_t rflags;
    uint64_t src = 0xffffffffffffffffULL;

    uc_common_setup(&uc, UC_ARCH_X86, UC_MODE_64, code, sizeof(code) - 1);
    OK(uc_reg_write(uc, UC_X86_REG_RBX, &src));
    OK(uc_reg_write(uc, UC_X86_REG_RCX, &index));

    OK(uc_emu_start(uc, code_start, code_start + sizeof(code) - 1, 0, 0));
    OK(uc_reg_read(uc, UC_X86_REG_RAX, &rax));
    OK(uc_reg_read(uc, UC_X86_REG_RFLAGS, &rflags));

    TEST_CHECK(rax == expected_dst);
    TEST_CHECK((bool)(rflags & 1) == expected_cf);
    TEST_CHECK((bool)(rflags & 0x80) == expected_sf);

    OK(uc_close(uc));
}

static void test_x86_bzhi_index_boundary(void)
{
    test_x86_bzhi_index_case(63, 0x7fffffffffffffffULL, false, false);
    test_x86_bzhi_index_case(255, 0xffffffffffffffffULL, true, true);
}

static void test_x86_nested_uc_emu_start_exits_cb(uc_engine *uc, uint64_t addr,
                                                  size_t size, void *data)
{
    OK(uc_emu_start(uc, code_start + 5, code_start + 6, 0, 0));
}

static void test_x86_nested_uc_emu_start_exits(void)
{
    uc_engine *uc;
    //  cmp eax, 0
    //  jnz t
    //  nop <-- nested emu_start
    // t:mov dword ptr [eax], 0
    char code[] = "\x83\xf8\x00\x75\x01\x90\xc7\x00\x00\x00\x00\x00";
    uc_hook hk;
    uint32_t r_pc;

    uc_common_setup(&uc, UC_ARCH_X86, UC_MODE_32, code, sizeof(code) - 1);

    OK(uc_hook_add(uc, &hk, UC_HOOK_CODE, test_x86_nested_uc_emu_start_exits_cb,
                   NULL, code_start, code_start));
    OK(uc_emu_start(uc, code_start, code_start + 5, 0, 0));
    OK(uc_reg_read(uc, UC_X86_REG_EIP, &r_pc));

    TEST_CHECK(r_pc == code_start + 5);

    OK(uc_close(uc));
}

static bool test_x86_correct_address_in_small_jump_hook_callback(
    uc_engine *uc, int type, uint64_t address, int size, int64_t value,
    void *user_data)
{
    // Check registers
    uint64_t r_rax = 0x0;
    uint64_t r_rip = 0x0;
    OK(uc_reg_read(uc, UC_X86_REG_RAX, &r_rax));
    OK(uc_reg_read(uc, UC_X86_REG_RIP, &r_rip));
    TEST_CHECK(r_rax == 0x7F00);
    TEST_CHECK(r_rip == 0x7F00);

    // Check address
    // printf("%lx\n", address);
    TEST_CHECK(address == 0x7F00);

    return false;
}

static void test_x86_correct_address_in_small_jump_hook(void)
{
    uc_engine *uc;
    // movabs $0x7F00, %rax
    // jmp  *%rax
    char code[] = "\x48\xb8\x00\x7F\x00\x00\x00\x00\x00\x00\xff\xe0";

    uint64_t r_rax = 0x0;
    uint64_t r_rip = 0x0;
    uc_hook hook;

    uc_common_setup(&uc, UC_ARCH_X86, UC_MODE_64, code, sizeof(code) - 1);
    OK(uc_hook_add(uc, &hook, UC_HOOK_MEM_UNMAPPED,
                   test_x86_correct_address_in_small_jump_hook_callback, NULL,
                   1, 0));

    uc_assert_err(
        UC_ERR_FETCH_UNMAPPED,
        uc_emu_start(uc, code_start, code_start + sizeof(code) - 1, 0, 0));

    OK(uc_reg_read(uc, UC_X86_REG_RAX, &r_rax));
    OK(uc_reg_read(uc, UC_X86_REG_RIP, &r_rip));
    TEST_CHECK(r_rax == 0x7F00);
    TEST_CHECK(r_rip == 0x7F00);

    OK(uc_close(uc));
}

static bool test_x86_correct_address_in_long_jump_hook_callback(
    uc_engine *uc, int type, uint64_t address, int size, int64_t value,
    void *user_data)
{
    // Check registers
    uint64_t r_rax = 0x0;
    uint64_t r_rip = 0x0;
    OK(uc_reg_read(uc, UC_X86_REG_RAX, &r_rax));
    OK(uc_reg_read(uc, UC_X86_REG_RIP, &r_rip));
    TEST_CHECK(r_rax == 0x7FFFFFFFFFFFFF00);
    TEST_CHECK(r_rip == 0x7FFFFFFFFFFFFF00);

    // Check address
    // printf("%lx\n", address);
    TEST_CHECK(address == 0x7FFFFFFFFFFFFF00);

    return false;
}

static void test_x86_correct_address_in_long_jump_hook(void)
{
    uc_engine *uc;
    // movabs $0x7FFFFFFFFFFFFF00, %rax
    // jmp  *%rax
    char code[] = "\x48\xb8\x00\xff\xff\xff\xff\xff\xff\x7f\xff\xe0";

    uint64_t r_rax = 0x0;
    uint64_t r_rip = 0x0;
    uc_hook hook;

    uc_common_setup(&uc, UC_ARCH_X86, UC_MODE_64, code, sizeof(code) - 1);
    OK(uc_ctl_tlb_mode(uc, UC_TLB_VIRTUAL));
    OK(uc_hook_add(uc, &hook, UC_HOOK_MEM_UNMAPPED,
                   test_x86_correct_address_in_long_jump_hook_callback, NULL, 1,
                   0));

    uc_assert_err(
        UC_ERR_FETCH_UNMAPPED,
        uc_emu_start(uc, code_start, code_start + sizeof(code) - 1, 0, 0));

    OK(uc_reg_read(uc, UC_X86_REG_RAX, &r_rax));
    OK(uc_reg_read(uc, UC_X86_REG_RIP, &r_rip));
    TEST_CHECK(r_rax == 0x7FFFFFFFFFFFFF00);
    TEST_CHECK(r_rip == 0x7FFFFFFFFFFFFF00);

    OK(uc_close(uc));
}

static void test_x86_invalid_vex_l(void)
{
    uc_engine *uc;

    /* andn eax, eax, eax with reserved VEX.L set */
    char code[] = {'\xC4', '\xE2', '\x7F', '\xF2', '\xC0'};

    /* initialize memory and run emulation  */
    OK(uc_open(UC_ARCH_X86, UC_MODE_64, &uc));
    OK(uc_ctl_set_cpu_model(uc, UC_CPU_X86_HASWELL));
    OK(uc_mem_map(uc, 0, 2 * 1024 * 1024, UC_PROT_ALL));

    OK(uc_mem_write(uc, 0, code, sizeof(code) / sizeof(code[0])));

    uc_assert_err(UC_ERR_INSN_INVALID,
                  uc_emu_start(uc, 0, sizeof(code) / sizeof(code[0]), 0, 0));
    OK(uc_close(uc));
}

typedef struct {
    uint32_t count;
    uint32_t intno;
} X86IntrCapture;

static void test_x86_intr_capture_cb(uc_engine *uc, uint32_t intno, void *data)
{
    X86IntrCapture *capture = (X86IntrCapture *)data;

    capture->count++;
    capture->intno = intno;
    uc_emu_stop(uc);
}

static void test_x86_sse_aligned_access(void)
{
    const uint64_t data_addr = 0x200008;
    const uint8_t code[] = {
        0x0f, 0x11, 0x00, /* movups [rax], xmm0 */
        0x0f, 0x29, 0x00, /* movaps [rax], xmm0 */
    };
    const uint8_t xmm0[16] = {
        0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
        0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff,
    };
    const uint8_t sentinel[16] = {
        0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5,
        0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5,
    };
    uint8_t memory[16];
    X86IntrCapture capture = { 0 };
    uc_engine *uc;
    uc_hook hook;
    uint64_t rax = data_addr;

    OK(uc_open(UC_ARCH_X86, UC_MODE_64, &uc));
    OK(uc_ctl_set_cpu_model(uc, UC_CPU_X86_HASWELL));
    OK(uc_mem_map(uc, code_start, code_len, UC_PROT_ALL));
    OK(uc_mem_write(uc, code_start, code, sizeof(code)));
    OK(uc_mem_map(uc, 0x200000, 0x1000, UC_PROT_ALL));
    OK(uc_hook_add(uc, &hook, UC_HOOK_INTR, test_x86_intr_capture_cb,
                   &capture, 1, 0));
    OK(uc_reg_write(uc, UC_X86_REG_RAX, &rax));
    OK(uc_reg_write(uc, UC_X86_REG_XMM0, &xmm0));

    OK(uc_emu_start(uc, code_start, code_start + 3, 0, 1));
    OK(uc_mem_read(uc, data_addr, memory, sizeof(memory)));
    TEST_CHECK(capture.count == 0);
    TEST_CHECK(memcmp(memory, xmm0, sizeof(memory)) == 0);

    OK(uc_mem_write(uc, data_addr, sentinel, sizeof(sentinel)));
    OK(uc_emu_start(uc, code_start + 3, code_start + sizeof(code), 0, 1));
    OK(uc_mem_read(uc, data_addr, memory, sizeof(memory)));
    TEST_CHECK(capture.count == 1);
    TEST_CHECK(capture.intno == 13);
    TEST_CHECK(memcmp(memory, sentinel, sizeof(memory)) == 0);

    OK(uc_close(uc));
}

/*
 * NoVmp U80: PTWRITE is #UD by default (SDM, CPUID.14.0:EBX[4] = 0) and, with
 * UC_X86_QUIRK_PTWRITE_NOP, reads its operand and does nothing else (i5-13600K).
 * The quirk is checked at run time: toggling it over the same translated block
 * must change the behaviour.
 */
static void test_x86_ptwrite_quirk(void)
{
    /* ptwrite qword [rax]; inc rbx */
    static const char code[] = "\xf3\x48\x0f\xae\x20\x48\xff\xc3";
    X86IntrCapture capture = { 0 };
    uc_engine *uc;
    uc_hook hook;
    uint64_t rax = 0x200000, rbx = 0;
    uint32_t quirks;

    OK(uc_open(UC_ARCH_X86, UC_MODE_64, &uc));
    OK(uc_mem_map(uc, code_start, code_len, UC_PROT_ALL));
    OK(uc_mem_write(uc, code_start, code, sizeof(code) - 1));
    OK(uc_mem_map(uc, 0x200000, 0x1000, UC_PROT_ALL));
    OK(uc_hook_add(uc, &hook, UC_HOOK_INTR, test_x86_intr_capture_cb,
                   &capture, 1, 0));

    /* default: #UD (raw Unicorn reports it as UC_ERR_INSN_INVALID), RBX untouched */
    OK(uc_reg_write(uc, UC_X86_REG_RAX, &rax));
    OK(uc_reg_write(uc, UC_X86_REG_RBX, &rbx));
    uc_assert_err(UC_ERR_INSN_INVALID,
                  uc_emu_start(uc, code_start, code_start + sizeof(code) - 1, 0, 0));
    OK(uc_reg_read(uc, UC_X86_REG_RBX, &rbx));
    TEST_CHECK(rbx == 0);

    /* quirk on, same code: runs, RBX incremented */
    OK(uc_ctl_set_x86_hw_quirks(uc, UC_X86_QUIRK_PTWRITE_NOP));
    OK(uc_ctl_get_x86_hw_quirks(uc, &quirks));
    TEST_CHECK(quirks == UC_X86_QUIRK_PTWRITE_NOP);
    capture.count = 0;
    OK(uc_emu_start(uc, code_start, code_start + sizeof(code) - 1, 0, 0));
    OK(uc_reg_read(uc, UC_X86_REG_RBX, &rbx));
    TEST_CHECK(capture.count == 0);
    TEST_CHECK(rbx == 1);

    /* quirk on: the operand is read, an unmapped one faults */
    rax = 0x300000;
    OK(uc_reg_write(uc, UC_X86_REG_RAX, &rax));
    uc_assert_err(UC_ERR_READ_UNMAPPED,
                  uc_emu_start(uc, code_start, code_start + sizeof(code) - 1, 0, 0));
    OK(uc_reg_read(uc, UC_X86_REG_RBX, &rbx));
    TEST_CHECK(rbx == 1);

    OK(uc_close(uc));
}

/*
 * NoVmp U82-U84: VEX SHA512 / SM3 / SM4 (CPUID.(EAX=7,ECX=1):EAX bits 0-2). The
 * i5-13600K implements none of them, so the expected values come from
 * Emulator/tools/isa/ref_sha512_sm.py: a literal transcription of the SDM Vol2
 * pseudocode, proven there against the FIPS 180-4 SHA-512, GB/T 32905 SM3 and
 * GB/T 32907 SM4 standard vectors (whole hashes / cipher built from the
 * instructions alone). Hex strings are register / memory images, lowest byte
 * first; expected values are the whole YMM destination (VEX.128 zeroes 255:128).
 */
typedef struct X86VecCase {
    const char *name;
    const char *code;   /* instruction bytes */
    int reg[3];         /* YMM inputs, -1 = unused */
    const char *val[3]; /* their 32-byte values */
    const char *mem;    /* 32 bytes at [rsi], or NULL */
    int dest;
    const char *expect; /* YMM[dest] afterwards */
} X86VecCase;

typedef struct X86UdCase {
    const char *name;
    const char *code;
} X86UdCase;

static size_t test_x86_hex(const char *hex, uint8_t *out, size_t max)
{
    size_t n = 0;

    while (hex[0] && hex[1] && n < max) {
        int hi = hex[0] <= '9' ? hex[0] - '0' : (hex[0] | 0x20) - 'a' + 10;
        int lo = hex[1] <= '9' ? hex[1] - '0' : (hex[1] | 0x20) - 'a' + 10;
        out[n++] = (uint8_t)(hi << 4 | lo);
        hex += 2;
    }
    return n;
}

/* Runs one case on a fresh engine; returns the uc_emu_start result. */
static uc_err test_x86_vec_run(const X86VecCase *c, int model,
                               const uc_x86_cpuid *profile, size_t nprofile,
                               uint8_t out[32])
{
    const uint64_t data = 0x200000;
    uint8_t code[16], v[32];
    uint64_t rsi = data;
    size_t len = test_x86_hex(c->code, code, sizeof(code));
    uc_engine *uc;
    uc_err err;
    int k;

    OK(uc_open(UC_ARCH_X86, UC_MODE_64, &uc));
    OK(uc_ctl_set_cpu_model(uc, model));
    if (profile) {
        OK(uc_ctl_set_x86_cpuid(uc, profile, nprofile));
        OK(uc_ctl_set_x86_cpuid_strict(uc, 1));
    }
    OK(uc_mem_map(uc, code_start, code_len, UC_PROT_ALL));
    OK(uc_mem_write(uc, code_start, code, len));
    OK(uc_mem_map(uc, data, 0x1000, UC_PROT_ALL));
    OK(uc_reg_write(uc, UC_X86_REG_RSI, &rsi));
    for (k = 0; k < 3; k++) {
        if (c->reg[k] >= 0) {
            memset(v, 0, sizeof(v));
            test_x86_hex(c->val[k], v, sizeof(v));
            OK(uc_reg_write(uc, UC_X86_REG_YMM0 + c->reg[k], v));
        }
    }
    if (c->mem) {
        test_x86_hex(c->mem, v, sizeof(v));
        OK(uc_mem_write(uc, data, v, sizeof(v)));
    }
    err = uc_emu_start(uc, code_start, code_start + len, 0, 0);
    if (err == UC_ERR_OK) {
        OK(uc_reg_read(uc, UC_X86_REG_YMM0 + c->dest, out));
        /* sources that are not the destination are left alone */
        for (k = 0; k < 3; k++) {
            uint8_t want[32] = { 0 };
            if (c->reg[k] < 0 || c->reg[k] == c->dest) {
                continue;
            }
            test_x86_hex(c->val[k], want, sizeof(want));
            OK(uc_reg_read(uc, UC_X86_REG_YMM0 + c->reg[k], v));
            TEST_CHECK_(memcmp(v, want, 32) == 0, "%s: source ymm%d unchanged",
                        c->name, c->reg[k]);
        }
    }
    OK(uc_close(uc));
    return err;
}

static void test_x86_vec_cases(const X86VecCase *cases, size_t n)
{
    size_t i;

    for (i = 0; i < n; i++) {
        uint8_t out[32], expect[32];
        uc_err err = test_x86_vec_run(&cases[i], UC_CPU_X86_MAX, NULL, 0, out);

        TEST_CHECK_(err == UC_ERR_OK, "%s: runs on UC_CPU_X86_MAX (%s)",
                    cases[i].name, uc_strerror(err));
        if (err != UC_ERR_OK) {
            continue;
        }
        test_x86_hex(cases[i].expect, expect, sizeof(expect));
        if (!TEST_CHECK_(memcmp(out, expect, 32) == 0, "%s: result", cases[i].name)) {
            char got[65];
            int k;
            for (k = 0; k < 32; k++) {
                snprintf(got + 2 * k, 3, "%02X", out[k]);
            }
            TEST_MSG("expected %s", cases[i].expect);
            TEST_MSG("got      %s", got);
        }
        /* the CPU model without the feature (Haswell) raises #UD */
        err = test_x86_vec_run(&cases[i], UC_CPU_X86_HASWELL, NULL, 0, out);
        TEST_CHECK_(err == UC_ERR_INSN_INVALID, "%s: #UD without the CPUID feature (%s)",
                    cases[i].name, uc_strerror(err));
    }
}

static void test_x86_ud_cases(const X86UdCase *cases, size_t n)
{
    size_t i;

    for (i = 0; i < n; i++) {
        X86VecCase c = { cases[i].name, cases[i].code, { -1, -1, -1 },
                         { NULL, NULL, NULL }, NULL, 0, NULL };
        uint8_t out[32];
        uc_err err = test_x86_vec_run(&c, UC_CPU_X86_MAX, NULL, 0, out);

        TEST_CHECK_(err == UC_ERR_INSN_INVALID, "%s: #UD on UC_CPU_X86_MAX (%s)",
                    cases[i].name, uc_strerror(err));
    }
}

static void test_x86_cpuid_regs(int model, uint32_t leaf, uint32_t sub, uint32_t r[4])
{
    uc_engine *uc;
    const char code[] = "\x0f\xa2";

    OK(uc_open(UC_ARCH_X86, UC_MODE_64, &uc));
    OK(uc_ctl_set_cpu_model(uc, model));
    OK(uc_mem_map(uc, code_start, code_len, UC_PROT_ALL));
    OK(uc_mem_write(uc, code_start, code, sizeof(code) - 1));
    OK(uc_reg_write(uc, UC_X86_REG_EAX, &leaf));
    OK(uc_reg_write(uc, UC_X86_REG_ECX, &sub));
    OK(uc_emu_start(uc, code_start, code_start + sizeof(code) - 1, 0, 0));
    OK(uc_reg_read(uc, UC_X86_REG_EAX, &r[0]));
    OK(uc_reg_read(uc, UC_X86_REG_EBX, &r[1]));
    OK(uc_reg_read(uc, UC_X86_REG_ECX, &r[2]));
    OK(uc_reg_read(uc, UC_X86_REG_EDX, &r[3]));
    OK(uc_close(uc));
}

/*
 * UC_CTL_X86_CPUID_STRICT: the UC_CPU_X86_MAX leaves 0/1/7.0 replayed as a CPUID
 * profile whose leaf 7.1 EAX is reduced to 'keep'. The case must run when its
 * feature bit is kept and #UD when the profile hides it.
 */
static void test_x86_vec_strict(const X86VecCase *c, uint32_t feature, uint32_t keep)
{
    uc_x86_cpuid p[6];
    uint32_t r[4];
    uint8_t out[32], expect[32];
    uc_err err;
    int i;

    /* leaf 0DH too: since U120 the reset XCR0 stays within the profile's 0DH.0 */
    static const uint32_t leaves[6][2] = { { 0, 0 }, { 1, 0 }, { 7, 0 }, { 7, 1 },
                                           { 0xd, 0 }, { 0xd, 1 } };
    for (i = 0; i < 6; i++) {
        test_x86_cpuid_regs(UC_CPU_X86_MAX, leaves[i][0], leaves[i][1], r);
        p[i].leaf = leaves[i][0];
        p[i].subleaf = leaves[i][1];
        p[i].eax = r[0];
        p[i].ebx = r[1];
        p[i].ecx = r[2];
        p[i].edx = r[3];
    }
    TEST_CHECK((p[3].eax & feature) != 0); /* MAX advertises it (TCG feature) */
    p[3].eax = keep;

    err = test_x86_vec_run(c, UC_CPU_X86_MAX, p, 6, out);
    if ((keep & feature) != 0) {
        TEST_CHECK_(err == UC_ERR_OK, "%s: strict profile with the bit (%s)", c->name,
                    uc_strerror(err));
        test_x86_hex(c->expect, expect, sizeof(expect));
        TEST_CHECK_(err != UC_ERR_OK || memcmp(out, expect, 32) == 0, "%s: strict result",
                    c->name);
    } else {
        TEST_CHECK_(err == UC_ERR_INSN_INVALID, "%s: strict profile without the bit (%s)",
                    c->name, uc_strerror(err));
    }
}

#define TEST_X86_CPUID_7_1_EAX_SHA512 (1U << 0)
#define TEST_X86_CPUID_7_1_EAX_SM3 (1U << 1)
#define TEST_X86_CPUID_7_1_EAX_SM4 (1U << 2)

/* U82: VSHA512MSG1 / VSHA512MSG2 / VSHA512RNDS2 (VEX.256.F2.0F38.W0 CC/CD/CB, registers only) */
/* generated by Emulator/tools/isa/ref_sha512_sm.py --vectors sha512 */
static const X86VecCase test_x86_sha512_cases[] = {
    { "vsha512msg1 ymm0, xmm1 #0", "c4e27fccc1",
      { 0, 1, -1 },
      "E66FDBB975C99BA8BD465BE8C2810B7F88B38B5F60E51BB337882EEF59E1E004",
      "FABB2BB4261A1CC24192DAF5E273E79EBC7536096B17ABD25B77CEF0931C05CC",
      "",
      NULL,
      0, "7BBEF0ECD8A5A0AACD8B80776260644F0BEB317DEF3699686829F820970A299F" },
    { "vsha512msg2 ymm0, ymm1 #0", "c4e27fcdc1",
      { 0, 1, -1 },
      "E66FDBB975C99BA8BD465BE8C2810B7F88B38B5F60E51BB337882EEF59E1E004",
      "FABB2BB4261A1CC24192DAF5E273E79EBC7536096B17ABD25B77CEF0931C05CC",
      "",
      NULL,
      0, "FC845541C6564101D7437E52300BFF2C616DFD98A118AC6D30760CA38B2112B4" },
    { "vsha512rnds2 ymm0, ymm1, xmm2 #0", "c4e277cbc2",
      { 0, 1, 2 },
      "E66FDBB975C99BA8BD465BE8C2810B7F88B38B5F60E51BB337882EEF59E1E004",
      "FABB2BB4261A1CC24192DAF5E273E79EBC7536096B17ABD25B77CEF0931C05CC",
      "4EA1DF3490108A31058E223ED5CBDE0530F3D72CF540E217BF10F2C5293DF744",
      NULL,
      0, "5DB50E3EF6423B8DF4E17B3FCEDA305B36FB794C76E7F57572B69447444F0EE3" },
    { "vsha512msg1 ymm0, xmm1 #1", "c4e27fccc1",
      { 0, 1, -1 },
      "E2336E4C7905D95209BE0D5D01598113E45F556BE506B98F6378AFAF4E7EE735",
      "B6C7975BC1963D854DE616A28BD9FBB7D82F7FB8AF018016471237771C61B5BA",
      "",
      NULL,
      0, "A8FDCFF9E434CDD31B0E25F77AA1CDB59DAD7CEF8A9D6689F6D3D69D2A0CF92A" },
    { "vsha512msg2 ymm0, ymm1 #1", "c4e27fcdc1",
      { 0, 1, -1 },
      "E2336E4C7905D95209BE0D5D01598113E45F556BE506B98F6378AFAF4E7EE735",
      "B6C7975BC1963D854DE616A28BD9FBB7D82F7FB8AF018016471237771C61B5BA",
      "",
      NULL,
      0, "52A99BC924157AA49B7E55C132838A48E71CB508E31C24E46D717841DAD88C20" },
    { "vsha512rnds2 ymm0, ymm1, xmm2 #1", "c4e277cbc2",
      { 0, 1, 2 },
      "E2336E4C7905D95209BE0D5D01598113E45F556BE506B98F6378AFAF4E7EE735",
      "B6C7975BC1963D854DE616A28BD9FBB7D82F7FB8AF018016471237771C61B5BA",
      "CAF079F5994B9B7CD10ADDB94D1529BB0C1745A3F7B085736B82F7EA2F78D331",
      NULL,
      0, "C6FFF4C2DCA1DBC96FFE12EE8E731DB967FD6CE1D48A1AC74100C37FBC271CA1" },
    { "vsha512msg1 ymm0, xmm1 #2", "c4e27fccc1",
      { 0, 1, -1 },
      "1E83E3BA1187212E956FE7E7F1FA2D3280092AB915EA6A75CFAC01E6F5538012",
      "B292E8427D4522B99998A954E9444F4A343BF8386BA0BF3F73B5AC05CDB11FC4",
      "",
      NULL,
      0, "9821BF9C8912623A6FEA45CA26C558EC12DE54A62493E2BBBD1DB50C6798DA01" },
    { "vsha512msg2 ymm0, ymm1 #2", "c4e27fcdc1",
      { 0, 1, -1 },
      "1E83E3BA1187212E956FE7E7F1FA2D3280092AB915EA6A75CFAC01E6F5538012",
      "B292E8427D4522B99998A954E9444F4A343BF8386BA0BF3F73B5AC05CDB11FC4",
      "",
      NULL,
      0, "70E1125A41018728932932D71E8371E7C67401EF34FD1E8EAE0BB0D80AF6A38F" },
    { "vsha512rnds2 ymm0, ymm1, xmm2 #2", "c4e277cbc2",
      { 0, 1, 2 },
      "1E83E3BA1187212E956FE7E7F1FA2D3280092AB915EA6A75CFAC01E6F5538012",
      "B292E8427D4522B99998A954E9444F4A343BF8386BA0BF3F73B5AC05CDB11FC4",
      "8673B664F486802EDD49C733764149552820B588BA8DE6FD57007A711A0D288B",
      NULL,
      0, "E617ED4AF825197B6EACFC6F74D9674A9D9BE00DDA10F99D8B76B81B539411BF" },
    { "vsha512msg1 ymm9, xmm12", "c4427fcccc",
      { 9, 12, -1 },
      "9AB9A776D726F020618798C606763FF65C6CD5223AB62A057B3139598EB903F3",
      "EE389856618F33092595ED7EE66AE6C1D013B1EE0A8304C9DF2C6C0D572BDFAB",
      "",
      NULL,
      9, "D3A3AFDE7021F6BBFB50A625CE9AD9544A404DE1478EBF88B9E5F061AF4C86DD" },
    { "vsha512msg2 ymm12, ymm9", "c4427fcde1",
      { 12, 9, -1 },
      "9AB9A776D726F020618798C606763FF65C6CD5223AB62A057B3139598EB903F3",
      "EE389856618F33092595ED7EE66AE6C1D013B1EE0A8304C9DF2C6C0D572BDFAB",
      "",
      NULL,
      12, "B9C0FA44533A6A8A44BFD69875490DD0ADF2645078019A0E3C453113179EC647" },
    { "vsha512rnds2 ymm9, ymm14, xmm3", "c4620fcbcb",
      { 9, 14, 3 },
      "9AB9A776D726F020618798C606763FF65C6CD5223AB62A057B3139598EB903F3",
      "EE389856618F33092595ED7EE66AE6C1D013B1EE0A8304C9DF2C6C0D572BDFAB",
      "8205794FF0A5BE6329F712C616EDF8DC844A373862E5555B8316EBD5279347F2",
      NULL,
      9, "14D54E638DA2F5032C9D58C5D1F4C677865C0576F6B555A5F82189CB8C74513F" },
    { "vsha512msg1 ymm0, xmm0", "c4e27fccc0",
      { 0, -1, -1 },
      "9AB9A776D726F020618798C606763FF65C6CD5223AB62A057B3139598EB903F3",
      "",
      "",
      NULL,
      0, "D3A3AFDE7021F6BBFB50A625CE9AD9544A404DE1478EBF888265011C8FBC1C7D" },
    { "vsha512msg2 ymm0, ymm0", "c4e27fcdc0",
      { 0, -1, -1 },
      "9AB9A776D726F020618798C606763FF65C6CD5223AB62A057B3139598EB903F3",
      "",
      "",
      NULL,
      0, "A52C8F1F84C2BAA59E2B354EFB323EB4CA462DD7D701FFCF181308B4247356F9" },
    { "vsha512rnds2 ymm0, ymm1, xmm0", "c4e277cbc0",
      { 0, 1, -1 },
      "9AB9A776D726F020618798C606763FF65C6CD5223AB62A057B3139598EB903F3",
      "EE389856618F33092595ED7EE66AE6C1D013B1EE0A8304C9DF2C6C0D572BDFAB",
      "",
      NULL,
      0, "2C897D8A742327C11B8BC718135E76A59E10349DDD3687628118DAD8C42D30AD" },
    { "vsha512rnds2 ymm0, ymm0, xmm0", "c4e27fcbc0",
      { 0, -1, -1 },
      "9AB9A776D726F020618798C606763FF65C6CD5223AB62A057B3139598EB903F3",
      "",
      "",
      NULL,
      0, "0F15070C211378A6E23CAA2C67503688234C887DF83FC0ADE8E732BD3D185371" },
};
static const X86UdCase test_x86_sha512_ud[] = {
    { "vsha512msg1 VEX.L0", "c4e27bccc1" },
    { "vsha512msg1 VEX.W1", "c4e2ffccc1" },
    { "vsha512msg1 [rsi] (register-only form)", "c4e27fcc06" },
    { "vsha512msg1 VEX.66", "c4e27dccc1" },
    { "vsha512msg1 VEX.NP (SHA-NI opcode, VEX)", "c4e27cccc1" },
    { "vsha512msg1 legacy F2 0F 38", "f20f38ccc1" },
    { "vsha512msg2 VEX.L0", "c4e27bcdc1" },
    { "vsha512msg2 VEX.W1", "c4e2ffcdc1" },
    { "vsha512msg2 [rsi] (register-only form)", "c4e27fcd06" },
    { "vsha512msg2 VEX.66", "c4e27dcdc1" },
    { "vsha512msg2 VEX.NP (SHA-NI opcode, VEX)", "c4e27ccdc1" },
    { "vsha512msg2 legacy F2 0F 38", "f20f38cdc1" },
    { "vsha512rnds2 VEX.L0", "c4e273cbc1" },
    { "vsha512rnds2 VEX.W1", "c4e2f7cbc1" },
    { "vsha512rnds2 [rsi] (register-only form)", "c4e277cb06" },
    { "vsha512rnds2 VEX.66", "c4e275cbc1" },
    { "vsha512rnds2 VEX.NP (SHA-NI opcode, VEX)", "c4e274cbc1" },
    { "vsha512rnds2 legacy F2 0F 38", "f20f38cbc1" },
    { "vsha512msg1 vvvv=0010b", "c4e26fccc1" },
    { "vsha512msg2 vvvv=1101b", "c4e217cdc1" },
};

static void test_x86_vsha512(void)
{
    test_x86_vec_cases(test_x86_sha512_cases,
                       sizeof(test_x86_sha512_cases) / sizeof(test_x86_sha512_cases[0]));
    test_x86_ud_cases(test_x86_sha512_ud,
                      sizeof(test_x86_sha512_ud) / sizeof(test_x86_sha512_ud[0]));
    test_x86_vec_strict(&test_x86_sha512_cases[2], TEST_X86_CPUID_7_1_EAX_SHA512,
                        TEST_X86_CPUID_7_1_EAX_SHA512);
    test_x86_vec_strict(&test_x86_sha512_cases[2], TEST_X86_CPUID_7_1_EAX_SHA512,
                        TEST_X86_CPUID_7_1_EAX_SM3 | TEST_X86_CPUID_7_1_EAX_SM4);
}


/* U83: VSM3MSG1 / VSM3MSG2 (VEX.128.NP/66.0F38.W0 DA) and VSM3RNDS2 (VEX.128.66.0F3A.W0 DE ib) */
/* generated by Emulator/tools/isa/ref_sha512_sm.py --vectors sm3 */
static const X86VecCase test_x86_sm3_cases[] = {
    { "vsm3msg1 xmm0, xmm1, xmm2 #0", "c4e270dac2",
      { 0, 1, 2 },
      "C7FF34D0D34B62F64A306FCEC770149E51B4F97C169C20F28C62E5CE8DD8C334",
      "EB47190D702D62F59EBA4D6F10D0459015B19B9D0477A50F000DD83560CA6BE7",
      "4FCAD42DBA8CB715324227CAC1494B2C19F23E59233177C2B4761F36E3AB245D",
      NULL,
      0, "D796B805B071FBC7FFAA8494B54F85AD00000000000000000000000000000000" },
    { "vsm3msg2 xmm0, xmm1, xmm2 #0", "c4e271dac2",
      { 0, 1, 2 },
      "C7FF34D0D34B62F64A306FCEC770149E51B4F97C169C20F28C62E5CE8DD8C334",
      "EB47190D702D62F59EBA4D6F10D0459015B19B9D0477A50F000DD83560CA6BE7",
      "4FCAD42DBA8CB715324227CAC1494B2C19F23E59233177C2B4761F36E3AB245D",
      NULL,
      0, "0EC04371137FC3524F3D95A270FA10BC00000000000000000000000000000000" },
    { "vsm3rnds2 xmm0, xmm1, xmm2, 0x00 #0", "c4e371dec200",
      { 0, 1, 2 },
      "C7FF34D0D34B62F64A306FCEC770149E51B4F97C169C20F28C62E5CE8DD8C334",
      "EB47190D702D62F59EBA4D6F10D0459015B19B9D0477A50F000DD83560CA6BE7",
      "4FCAD42DBA8CB715324227CAC1494B2C19F23E59233177C2B4761F36E3AB245D",
      NULL,
      0, "DBF2A659631BFE8DADD3FAE80CA5BFD600000000000000000000000000000000" },
    { "vsm3rnds2 xmm0, xmm1, xmm2, 0x0e #0", "c4e371dec20e",
      { 0, 1, 2 },
      "C7FF34D0D34B62F64A306FCEC770149E51B4F97C169C20F28C62E5CE8DD8C334",
      "EB47190D702D62F59EBA4D6F10D0459015B19B9D0477A50F000DD83560CA6BE7",
      "4FCAD42DBA8CB715324227CAC1494B2C19F23E59233177C2B4761F36E3AB245D",
      NULL,
      0, "FAB79110A8A5196B807E0928EA99EB9A00000000000000000000000000000000" },
    { "vsm3rnds2 xmm0, xmm1, xmm2, 0x10 #0", "c4e371dec210",
      { 0, 1, 2 },
      "C7FF34D0D34B62F64A306FCEC770149E51B4F97C169C20F28C62E5CE8DD8C334",
      "EB47190D702D62F59EBA4D6F10D0459015B19B9D0477A50F000DD83560CA6BE7",
      "4FCAD42DBA8CB715324227CAC1494B2C19F23E59233177C2B4761F36E3AB245D",
      NULL,
      0, "389AD5E21E72DEB7282D739A5E8FB26900000000000000000000000000000000" },
    { "vsm3rnds2 xmm0, xmm1, xmm2, 0x3e #0", "c4e371dec23e",
      { 0, 1, 2 },
      "C7FF34D0D34B62F64A306FCEC770149E51B4F97C169C20F28C62E5CE8DD8C334",
      "EB47190D702D62F59EBA4D6F10D0459015B19B9D0477A50F000DD83560CA6BE7",
      "4FCAD42DBA8CB715324227CAC1494B2C19F23E59233177C2B4761F36E3AB245D",
      NULL,
      0, "D313035C96D507CAB19A29CE428F98DE00000000000000000000000000000000" },
    { "vsm3rnds2 xmm0, xmm1, xmm2, 0xc1 #0", "c4e371dec2c1",
      { 0, 1, 2 },
      "C7FF34D0D34B62F64A306FCEC770149E51B4F97C169C20F28C62E5CE8DD8C334",
      "EB47190D702D62F59EBA4D6F10D0459015B19B9D0477A50F000DD83560CA6BE7",
      "4FCAD42DBA8CB715324227CAC1494B2C19F23E59233177C2B4761F36E3AB245D",
      NULL,
      0, "DBF2A659631BFE8DADD3FAE80CA5BFD600000000000000000000000000000000" },
    { "vsm3rnds2 xmm0, xmm1, xmm2, 0x11 #0", "c4e371dec211",
      { 0, 1, 2 },
      "C7FF34D0D34B62F64A306FCEC770149E51B4F97C169C20F28C62E5CE8DD8C334",
      "EB47190D702D62F59EBA4D6F10D0459015B19B9D0477A50F000DD83560CA6BE7",
      "4FCAD42DBA8CB715324227CAC1494B2C19F23E59233177C2B4761F36E3AB245D",
      NULL,
      0, "389AD5E21E72DEB7282D739A5E8FB26900000000000000000000000000000000" },
    { "vsm3rnds2 xmm0, xmm1, xmm2, 0xff #0", "c4e371dec2ff",
      { 0, 1, 2 },
      "C7FF34D0D34B62F64A306FCEC770149E51B4F97C169C20F28C62E5CE8DD8C334",
      "EB47190D702D62F59EBA4D6F10D0459015B19B9D0477A50F000DD83560CA6BE7",
      "4FCAD42DBA8CB715324227CAC1494B2C19F23E59233177C2B4761F36E3AB245D",
      NULL,
      0, "D313035C96D507CAB19A29CE428F98DE00000000000000000000000000000000" },
    { "vsm3msg1 xmm0, xmm1, xmm2 #1", "c4e270dac2",
      { 0, 1, 2 },
      "F3AA969D7AAFAC13061B210D6344E3AA5D3B1F35C494EB8BA81379F57C3CB33C",
      "D74D387F1381507D1AD90D65B5357085E190ACAD98C88DE7DC9791397EB87E63",
      "FB566103BDA452D96E50C1FDFE98B872A5364FF1EEF08FB050F778788A35C69D",
      NULL,
      0, "BB118A538E4B05E7644194575E1DF97800000000000000000000000000000000" },
    { "vsm3msg2 xmm0, xmm1, xmm2 #1", "c4e271dac2",
      { 0, 1, 2 },
      "F3AA969D7AAFAC13061B210D6344E3AA5D3B1F35C494EB8BA81379F57C3CB33C",
      "D74D387F1381507D1AD90D65B5357085E190ACAD98C88DE7DC9791397EB87E63",
      "FB566103BDA452D96E50C1FDFE98B872A5364FF1EEF08FB050F778788A35C69D",
      NULL,
      0, "B717D1027982BE625AC60C761A2F6B9F00000000000000000000000000000000" },
    { "vsm3rnds2 xmm0, xmm1, xmm2, 0x00 #1", "c4e371dec200",
      { 0, 1, 2 },
      "F3AA969D7AAFAC13061B210D6344E3AA5D3B1F35C494EB8BA81379F57C3CB33C",
      "D74D387F1381507D1AD90D65B5357085E190ACAD98C88DE7DC9791397EB87E63",
      "FB566103BDA452D96E50C1FDFE98B872A5364FF1EEF08FB050F778788A35C69D",
      NULL,
      0, "E46AF73543D0348453581FA7C256632B00000000000000000000000000000000" },
    { "vsm3rnds2 xmm0, xmm1, xmm2, 0x0e #1", "c4e371dec20e",
      { 0, 1, 2 },
      "F3AA969D7AAFAC13061B210D6344E3AA5D3B1F35C494EB8BA81379F57C3CB33C",
      "D74D387F1381507D1AD90D65B5357085E190ACAD98C88DE7DC9791397EB87E63",
      "FB566103BDA452D96E50C1FDFE98B872A5364FF1EEF08FB050F778788A35C69D",
      NULL,
      0, "272CE960590574CC48F50B625008A04900000000000000000000000000000000" },
    { "vsm3rnds2 xmm0, xmm1, xmm2, 0x10 #1", "c4e371dec210",
      { 0, 1, 2 },
      "F3AA969D7AAFAC13061B210D6344E3AA5D3B1F35C494EB8BA81379F57C3CB33C",
      "D74D387F1381507D1AD90D65B5357085E190ACAD98C88DE7DC9791397EB87E63",
      "FB566103BDA452D96E50C1FDFE98B872A5364FF1EEF08FB050F778788A35C69D",
      NULL,
      0, "2DDF66368C087BB29C88511E540926AC00000000000000000000000000000000" },
    { "vsm3rnds2 xmm0, xmm1, xmm2, 0x3e #1", "c4e371dec23e",
      { 0, 1, 2 },
      "F3AA969D7AAFAC13061B210D6344E3AA5D3B1F35C494EB8BA81379F57C3CB33C",
      "D74D387F1381507D1AD90D65B5357085E190ACAD98C88DE7DC9791397EB87E63",
      "FB566103BDA452D96E50C1FDFE98B872A5364FF1EEF08FB050F778788A35C69D",
      NULL,
      0, "E9A88DD2C56E87891C269AAB9431E6B900000000000000000000000000000000" },
    { "vsm3rnds2 xmm0, xmm1, xmm2, 0xc1 #1", "c4e371dec2c1",
      { 0, 1, 2 },
      "F3AA969D7AAFAC13061B210D6344E3AA5D3B1F35C494EB8BA81379F57C3CB33C",
      "D74D387F1381507D1AD90D65B5357085E190ACAD98C88DE7DC9791397EB87E63",
      "FB566103BDA452D96E50C1FDFE98B872A5364FF1EEF08FB050F778788A35C69D",
      NULL,
      0, "E46AF73543D0348453581FA7C256632B00000000000000000000000000000000" },
    { "vsm3rnds2 xmm0, xmm1, xmm2, 0x11 #1", "c4e371dec211",
      { 0, 1, 2 },
      "F3AA969D7AAFAC13061B210D6344E3AA5D3B1F35C494EB8BA81379F57C3CB33C",
      "D74D387F1381507D1AD90D65B5357085E190ACAD98C88DE7DC9791397EB87E63",
      "FB566103BDA452D96E50C1FDFE98B872A5364FF1EEF08FB050F778788A35C69D",
      NULL,
      0, "2DDF66368C087BB29C88511E540926AC00000000000000000000000000000000" },
    { "vsm3rnds2 xmm0, xmm1, xmm2, 0xff #1", "c4e371dec2ff",
      { 0, 1, 2 },
      "F3AA969D7AAFAC13061B210D6344E3AA5D3B1F35C494EB8BA81379F57C3CB33C",
      "D74D387F1381507D1AD90D65B5357085E190ACAD98C88DE7DC9791397EB87E63",
      "FB566103BDA452D96E50C1FDFE98B872A5364FF1EEF08FB050F778788A35C69D",
      NULL,
      0, "E9A88DD2C56E87891C269AAB9431E6B900000000000000000000000000000000" },
    { "vsm3msg1 xmm0, xmm1, [rsi]", "c4e270da06",
      { 0, 1, -1 },
      "5FAAEBD87D5714A30295A45FAC01846EA9B06BBF9FEA5CA7046656085F7D56DB",
      "036C87DB9CE9E430D6FA897E632F53A4EDC2A62F27D8AB08F8575CE29C5081C2",
      "",
      "E7FF9E64D37BCDCDEA15C10DB10A6DA100000000000000000000000000000000",
      0, "1D07CC6B4D3274EE746F8F266C1880C400000000000000000000000000000000" },
    { "vsm3msg2 xmm0, xmm1, [rsi]", "c4e271da06",
      { 0, 1, -1 },
      "5FAAEBD87D5714A30295A45FAC01846EA9B06BBF9FEA5CA7046656085F7D56DB",
      "036C87DB9CE9E430D6FA897E632F53A4EDC2A62F27D8AB08F8575CE29C5081C2",
      "",
      "E7FF9E64D37BCDCDEA15C10DB10A6DA100000000000000000000000000000000",
      0, "5554C37FB6E2AD1CD7EB981664C571E300000000000000000000000000000000" },
    { "vsm3rnds2 xmm0, xmm1, [rsi], 0x22", "c4e371de0622",
      { 0, 1, -1 },
      "5FAAEBD87D5714A30295A45FAC01846EA9B06BBF9FEA5CA7046656085F7D56DB",
      "036C87DB9CE9E430D6FA897E632F53A4EDC2A62F27D8AB08F8575CE29C5081C2",
      "",
      "E7FF9E64D37BCDCDEA15C10DB10A6DA100000000000000000000000000000000",
      0, "E3C369360177F70E06A9C8AC2C63932A00000000000000000000000000000000" },
    { "vsm3msg1 xmm10, xmm13, xmm8", "c44210dad0",
      { 10, 13, 8 },
      "5FAAEBD87D5714A30295A45FAC01846EA9B06BBF9FEA5CA7046656085F7D56DB",
      "036C87DB9CE9E430D6FA897E632F53A4EDC2A62F27D8AB08F8575CE29C5081C2",
      "E7FF9E64D37BCDCDEA15C10DB10A6DA17171696576687E282C81FCAC878D7EEB",
      NULL,
      10, "1D07CC6B4D3274EE746F8F266C1880C400000000000000000000000000000000" },
    { "vsm3msg2 xmm8, xmm2, xmm15", "c44269dac7",
      { 8, 2, 15 },
      "5FAAEBD87D5714A30295A45FAC01846EA9B06BBF9FEA5CA7046656085F7D56DB",
      "036C87DB9CE9E430D6FA897E632F53A4EDC2A62F27D8AB08F8575CE29C5081C2",
      "E7FF9E64D37BCDCDEA15C10DB10A6DA17171696576687E282C81FCAC878D7EEB",
      NULL,
      8, "5554C37FB6E2AD1CD7EB981664C571E300000000000000000000000000000000" },
    { "vsm3rnds2 xmm15, xmm9, xmm11, 0x30", "c44331defb30",
      { 15, 9, 11 },
      "5FAAEBD87D5714A30295A45FAC01846EA9B06BBF9FEA5CA7046656085F7D56DB",
      "036C87DB9CE9E430D6FA897E632F53A4EDC2A62F27D8AB08F8575CE29C5081C2",
      "E7FF9E64D37BCDCDEA15C10DB10A6DA17171696576687E282C81FCAC878D7EEB",
      NULL,
      15, "99B4D017970636E1E877C65ED949E4E000000000000000000000000000000000" },
    { "vsm3msg1 xmm0, xmm0, xmm0", "c4e278dac0",
      { 0, -1, -1 },
      "5FAAEBD87D5714A30295A45FAC01846EA9B06BBF9FEA5CA7046656085F7D56DB",
      "",
      "",
      NULL,
      0, "1491FF193D1B6E86054A4D340000000000000000000000000000000000000000" },
    { "vsm3msg2 xmm0, xmm0, xmm0", "c4e279dac0",
      { 0, -1, -1 },
      "5FAAEBD87D5714A30295A45FAC01846EA9B06BBF9FEA5CA7046656085F7D56DB",
      "",
      "",
      NULL,
      0, "EC2FD575D1BE2B8A2F814AD23B5CC8BD00000000000000000000000000000000" },
    { "vsm3rnds2 xmm0, xmm0, xmm0, 0x14", "c4e379dec014",
      { 0, -1, -1 },
      "5FAAEBD87D5714A30295A45FAC01846EA9B06BBF9FEA5CA7046656085F7D56DB",
      "",
      "",
      NULL,
      0, "F57A100A2154A8EB5DFF76CA9D50DBE400000000000000000000000000000000" },
};
static const X86UdCase test_x86_sm3_ud[] = {
    { "vsm3msg1 VEX.L1", "c4e274dac2" },
    { "vsm3msg2 VEX.L1", "c4e275dac2" },
    { "vsm3rnds2 VEX.L1", "c4e375dec200" },
    { "vsm3msg1 VEX.W1", "c4e2f0dac2" },
    { "vsm3msg2 VEX.W1", "c4e2f1dac2" },
    { "vsm3rnds2 VEX.W1", "c4e3f1dec200" },
    { "vsm3rnds2 VEX.F2", "c4e373dec200" },
    { "legacy NP 0F 38 DA", "0f38dac2" },
    { "legacy 66 0F 38 DA", "660f38dac2" },
    { "legacy 66 0F 3A DE", "660f3adec200" },
};

static void test_x86_vsm3(void)
{
    test_x86_vec_cases(test_x86_sm3_cases,
                       sizeof(test_x86_sm3_cases) / sizeof(test_x86_sm3_cases[0]));
    test_x86_ud_cases(test_x86_sm3_ud, sizeof(test_x86_sm3_ud) / sizeof(test_x86_sm3_ud[0]));
    test_x86_vec_strict(&test_x86_sm3_cases[2], TEST_X86_CPUID_7_1_EAX_SM3,
                        TEST_X86_CPUID_7_1_EAX_SM3);
    test_x86_vec_strict(&test_x86_sm3_cases[2], TEST_X86_CPUID_7_1_EAX_SM3,
                        TEST_X86_CPUID_7_1_EAX_SHA512 | TEST_X86_CPUID_7_1_EAX_SM4);
}


/* U84: VSM4KEY4 / VSM4RNDS4 (VEX.128/256.F3/F2.0F38.W0 DA, independent 128-bit lanes) */
/* generated by Emulator/tools/isa/ref_sha512_sm.py --vectors sm4 */
static const X86VecCase test_x86_sm4_cases[] = {
    { "vsm4key4 xmm0, xmm1, xmm2 #0", "c4e272dac2",
      { 0, 1, 2 },
      "C7FF34D0D378E18B4A306FCEC759C22D51B4F97C169149178C62E5CE8DE981A7",
      "EB47190D702A3A509EBA4D6F1049C0EF15B19B9D04BC3307000DD83560EBA828",
      "4FCAD42DBA59D515324227CAC1523B4B19F23E5923C61F84B4761F36E3DCB1C2",
      NULL,
      0, "60E779654BAD5AA09CB1445D2DA950F100000000000000000000000000000000" },
    { "vsm4rnds4 xmm0, xmm1, xmm2 #0", "c4e273dac2",
      { 0, 1, 2 },
      "C7FF34D0D378E18B4A306FCEC759C22D51B4F97C169149178C62E5CE8DE981A7",
      "EB47190D702A3A509EBA4D6F1049C0EF15B19B9D04BC3307000DD83560EBA828",
      "4FCAD42DBA59D515324227CAC1523B4B19F23E5923C61F84B4761F36E3DCB1C2",
      NULL,
      0, "7DF64192F3647FB3D4175C011CC48A5100000000000000000000000000000000" },
    { "vsm4key4 ymm0, ymm1, ymm2 #0", "c4e276dac2",
      { 0, 1, 2 },
      "C7FF34D0D378E18B4A306FCEC759C22D51B4F97C169149178C62E5CE8DE981A7",
      "EB47190D702A3A509EBA4D6F1049C0EF15B19B9D04BC3307000DD83560EBA828",
      "4FCAD42DBA59D515324227CAC1523B4B19F23E5923C61F84B4761F36E3DCB1C2",
      NULL,
      0, "60E779654BAD5AA09CB1445D2DA950F1298C399B45C13211EBC2380EABFEE558" },
    { "vsm4rnds4 ymm0, ymm1, ymm2 #0", "c4e277dac2",
      { 0, 1, 2 },
      "C7FF34D0D378E18B4A306FCEC759C22D51B4F97C169149178C62E5CE8DE981A7",
      "EB47190D702A3A509EBA4D6F1049C0EF15B19B9D04BC3307000DD83560EBA828",
      "4FCAD42DBA59D515324227CAC1523B4B19F23E5923C61F84B4761F36E3DCB1C2",
      NULL,
      0, "7DF64192F3647FB3D4175C011CC48A51E256A046ACC57E7DE1C4E66CDCA217CD" },
    { "vsm4key4 xmm0, xmm1, xmm2 #1", "c4e272dac2",
      { 0, 1, 2 },
      "F3AA969D7A4CCD7D061B210D63DD81E15D3B1F35C479B3E0A81379F57C7D7197",
      "D74D387F13EE00A81AD90D65B55E86FDE190ACAD98FDC9DEDC9791397E095F90",
      "FB566103BDE1EF296E50C1FDFE519F20A5364FF1EE75E56450F778788A96C926",
      NULL,
      0, "80464ABF9581B139A171574BE3F9DFA200000000000000000000000000000000" },
    { "vsm4rnds4 xmm0, xmm1, xmm2 #1", "c4e273dac2",
      { 0, 1, 2 },
      "F3AA969D7A4CCD7D061B210D63DD81E15D3B1F35C479B3E0A81379F57C7D7197",
      "D74D387F13EE00A81AD90D65B55E86FDE190ACAD98FDC9DEDC9791397E095F90",
      "FB566103BDE1EF296E50C1FDFE519F20A5364FF1EE75E56450F778788A96C926",
      NULL,
      0, "40CDD69781BAD9A1DE59FDE884D8C5B600000000000000000000000000000000" },
    { "vsm4key4 ymm0, ymm1, ymm2 #1", "c4e276dac2",
      { 0, 1, 2 },
      "F3AA969D7A4CCD7D061B210D63DD81E15D3B1F35C479B3E0A81379F57C7D7197",
      "D74D387F13EE00A81AD90D65B55E86FDE190ACAD98FDC9DEDC9791397E095F90",
      "FB566103BDE1EF296E50C1FDFE519F20A5364FF1EE75E56450F778788A96C926",
      NULL,
      0, "80464ABF9581B139A171574BE3F9DFA2A0D22F884B6A44D4D64863CBDAAE8CA6" },
    { "vsm4rnds4 ymm0, ymm1, ymm2 #1", "c4e277dac2",
      { 0, 1, 2 },
      "F3AA969D7A4CCD7D061B210D63DD81E15D3B1F35C479B3E0A81379F57C7D7197",
      "D74D387F13EE00A81AD90D65B55E86FDE190ACAD98FDC9DEDC9791397E095F90",
      "FB566103BDE1EF296E50C1FDFE519F20A5364FF1EE75E56450F778788A96C926",
      NULL,
      0, "40CDD69781BAD9A1DE59FDE884D8C5B6058A050ACE7F5E2726D23E8D58D9BBB3" },
    { "vsm4key4 xmm0, xmm1, xmm2 (GB/T 32907 rk0-3)", "c4e272dac2",
      { 1, 2, -1 },
      "A1FF92A2BFFE01DF0F2BA199CC1024C400000000000000000000000000000000",
      "150E0700312A231C4D463F3869625B5400000000000000000000000000000000",
      "",
      NULL,
      0, "F98621F1612B66419AB16A5A7720A97B00000000000000000000000000000000" },
    { "vsm4key4 xmm0, xmm1, [rsi]", "c4e272da06",
      { 0, 1, -1 },
      "5FAAEBD87D64CBDA0295A45FAC4A249AA9B06BBF9FBFC053046656085FEE8DA8",
      "036C87DB9CC6B289D6FA897E63082602EDC2A62F27FD626DF8575CE29CD10DDB",
      "",
      "E7FF9E64D3287FE7EA15C10DB1737BFB7171696576DD1D402C81FCAC871E9146",
      0, "A31129903C48866D9B4754FF53E1045600000000000000000000000000000000" },
    { "vsm4rnds4 xmm0, xmm1, [rsi]", "c4e273da06",
      { 0, 1, -1 },
      "5FAAEBD87D64CBDA0295A45FAC4A249AA9B06BBF9FBFC053046656085FEE8DA8",
      "036C87DB9CC6B289D6FA897E63082602EDC2A62F27FD626DF8575CE29CD10DDB",
      "",
      "E7FF9E64D3287FE7EA15C10DB1737BFB7171696576DD1D402C81FCAC871E9146",
      0, "802BDDD8D82044DF6C7A60C146ECB1F800000000000000000000000000000000" },
    { "vsm4key4 xmm11, xmm14, xmm9", "c4420adad9",
      { 11, 14, 9 },
      "5FAAEBD87D64CBDA0295A45FAC4A249AA9B06BBF9FBFC053046656085FEE8DA8",
      "036C87DB9CC6B289D6FA897E63082602EDC2A62F27FD626DF8575CE29CD10DDB",
      "E7FF9E64D3287FE7EA15C10DB1737BFB7171696576DD1D402C81FCAC871E9146",
      NULL,
      11, "A31129903C48866D9B4754FF53E1045600000000000000000000000000000000" },
    { "vsm4rnds4 xmm9, xmm3, xmm12", "c44263dacc",
      { 9, 3, 12 },
      "5FAAEBD87D64CBDA0295A45FAC4A249AA9B06BBF9FBFC053046656085FEE8DA8",
      "036C87DB9CC6B289D6FA897E63082602EDC2A62F27FD626DF8575CE29CD10DDB",
      "E7FF9E64D3287FE7EA15C10DB1737BFB7171696576DD1D402C81FCAC871E9146",
      NULL,
      9, "802BDDD8D82044DF6C7A60C146ECB1F800000000000000000000000000000000" },
    { "vsm4key4 xmm0, xmm0, xmm0", "c4e27adac0",
      { 0, -1, -1 },
      "5FAAEBD87D64CBDA0295A45FAC4A249AA9B06BBF9FBFC053046656085FEE8DA8",
      "",
      "",
      NULL,
      0, "F66CB6F7F5D00F3F855B4C66368060FC00000000000000000000000000000000" },
    { "vsm4rnds4 xmm0, xmm0, xmm0", "c4e27bdac0",
      { 0, -1, -1 },
      "5FAAEBD87D64CBDA0295A45FAC4A249AA9B06BBF9FBFC053046656085FEE8DA8",
      "",
      "",
      NULL,
      0, "AD56864894AF884F73937F2E1D0B3B5E00000000000000000000000000000000" },
    { "vsm4key4 ymm0, ymm1, [rsi]", "c4e276da06",
      { 0, 1, -1 },
      "5FAAEBD87D64CBDA0295A45FAC4A249AA9B06BBF9FBFC053046656085FEE8DA8",
      "036C87DB9CC6B289D6FA897E63082602EDC2A62F27FD626DF8575CE29CD10DDB",
      "",
      "E7FF9E64D3287FE7EA15C10DB1737BFB7171696576DD1D402C81FCAC871E9146",
      0, "A31129903C48866D9B4754FF53E104561AC773709F8D3D8E93F217B395CB2877" },
    { "vsm4rnds4 ymm0, ymm1, [rsi]", "c4e277da06",
      { 0, 1, -1 },
      "5FAAEBD87D64CBDA0295A45FAC4A249AA9B06BBF9FBFC053046656085FEE8DA8",
      "036C87DB9CC6B289D6FA897E63082602EDC2A62F27FD626DF8575CE29CD10DDB",
      "",
      "E7FF9E64D3287FE7EA15C10DB1737BFB7171696576DD1D402C81FCAC871E9146",
      0, "802BDDD8D82044DF6C7A60C146ECB1F86737042018994EF0311B00F4F89B90D1" },
    { "vsm4key4 ymm11, ymm14, ymm9", "c4420edad9",
      { 11, 14, 9 },
      "5FAAEBD87D64CBDA0295A45FAC4A249AA9B06BBF9FBFC053046656085FEE8DA8",
      "036C87DB9CC6B289D6FA897E63082602EDC2A62F27FD626DF8575CE29CD10DDB",
      "E7FF9E64D3287FE7EA15C10DB1737BFB7171696576DD1D402C81FCAC871E9146",
      NULL,
      11, "A31129903C48866D9B4754FF53E104561AC773709F8D3D8E93F217B395CB2877" },
    { "vsm4rnds4 ymm9, ymm3, ymm12", "c44267dacc",
      { 9, 3, 12 },
      "5FAAEBD87D64CBDA0295A45FAC4A249AA9B06BBF9FBFC053046656085FEE8DA8",
      "036C87DB9CC6B289D6FA897E63082602EDC2A62F27FD626DF8575CE29CD10DDB",
      "E7FF9E64D3287FE7EA15C10DB1737BFB7171696576DD1D402C81FCAC871E9146",
      NULL,
      9, "802BDDD8D82044DF6C7A60C146ECB1F86737042018994EF0311B00F4F89B90D1" },
    { "vsm4key4 ymm0, ymm0, ymm0", "c4e27edac0",
      { 0, -1, -1 },
      "5FAAEBD87D64CBDA0295A45FAC4A249AA9B06BBF9FBFC053046656085FEE8DA8",
      "",
      "",
      NULL,
      0, "F66CB6F7F5D00F3F855B4C66368060FC0853E85F0CCBC95DEFDE9E9522F4D9F9" },
    { "vsm4rnds4 ymm0, ymm0, ymm0", "c4e27fdac0",
      { 0, -1, -1 },
      "5FAAEBD87D64CBDA0295A45FAC4A249AA9B06BBF9FBFC053046656085FEE8DA8",
      "",
      "",
      NULL,
      0, "AD56864894AF884F73937F2E1D0B3B5E731958653988D84F5C2E4C68D38CAC74" },
};
static const X86UdCase test_x86_sm4_ud[] = {
    { "vsm4key4 VEX.128.W1", "c4e2f2dac2" },
    { "vsm4key4 VEX.256.W1", "c4e2f6dac2" },
    { "vsm4rnds4 VEX.128.W1", "c4e2f3dac2" },
    { "vsm4rnds4 VEX.256.W1", "c4e2f7dac2" },
    { "legacy F3 0F 38 DA", "f30f38dac2" },
    { "legacy F2 0F 38 DA", "f20f38dac2" },
};

static void test_x86_vsm4(void)
{
    test_x86_vec_cases(test_x86_sm4_cases,
                       sizeof(test_x86_sm4_cases) / sizeof(test_x86_sm4_cases[0]));
    test_x86_ud_cases(test_x86_sm4_ud, sizeof(test_x86_sm4_ud) / sizeof(test_x86_sm4_ud[0]));
    /* [3] = vsm4rnds4 ymm0, ymm1, ymm2 #0 */
    test_x86_vec_strict(&test_x86_sm4_cases[3], TEST_X86_CPUID_7_1_EAX_SM4,
                        TEST_X86_CPUID_7_1_EAX_SM4);
    test_x86_vec_strict(&test_x86_sm4_cases[3], TEST_X86_CPUID_7_1_EAX_SM4,
                        TEST_X86_CPUID_7_1_EAX_SHA512 | TEST_X86_CPUID_7_1_EAX_SM3);
}

static void test_x86_data_watchpoint(void)
{
    const uint64_t data_addr = 0x200000;
    const uint8_t code[] = {
        0xc7, 0x00, 0x44, 0x33, 0x22, 0x11, /* mov dword ptr [rax], 0x11223344 */
    };
    const uint64_t dr7_write_len4 = 1 | (1U << 16) | (3U << 18);
    X86IntrCapture capture = { 0 };
    uint32_t memory = 0;
    uint64_t dr6 = 0;
    uint64_t rax = data_addr;
    uc_engine *uc;
    uc_hook hook;

    uc_common_setup(&uc, UC_ARCH_X86, UC_MODE_64, (const char *)code,
                    sizeof(code));
    OK(uc_mem_map(uc, data_addr, 0x1000, UC_PROT_ALL));
    OK(uc_hook_add(uc, &hook, UC_HOOK_INTR, test_x86_intr_capture_cb,
                   &capture, 1, 0));
    OK(uc_reg_write(uc, UC_X86_REG_RAX, &rax));
    OK(uc_reg_write(uc, UC_X86_REG_DR0, &rax));
    OK(uc_reg_write(uc, UC_X86_REG_DR7, &dr7_write_len4));

    OK(uc_emu_start(uc, code_start, code_start + sizeof(code), 0, 0));
    OK(uc_mem_read(uc, data_addr, &memory, sizeof(memory)));
    OK(uc_reg_read(uc, UC_X86_REG_DR6, &dr6));
    TEST_CHECK(capture.count == 1);
    TEST_CHECK(capture.intno == 1);
    TEST_CHECK(memory == 0x11223344);
    TEST_CHECK((dr6 & 1) != 0);

    OK(uc_close(uc));
}

// AARCH64 inline the read while s390x won't split the access. Though not tested
// on other hosts but we restrict a bit more.
#if !defined(TARGET_READ_INLINED) && defined(BOOST_LITTLE_ENDIAN)

struct writelog_t {
    uint32_t addr, size;
};

static void test_x86_unaligned_access_callback(uc_engine *uc, uc_mem_type type,
                                               uint64_t address, int size,
                                               int64_t value, void *user_data)
{
    TEST_CHECK(size != 0);
    struct writelog_t *write_log = (struct writelog_t *)user_data;

    for (int i = 0; i < 10; i++) {
        if (write_log[i].size == 0) {
            write_log[i].addr = (uint32_t)address;
            write_log[i].size = (uint32_t)size;
            return;
        }
    }
    TEST_ASSERT(false);
}

static void test_x86_unaligned_access(void)
{
    uc_engine *uc;
    uc_hook hook;
    // mov dword ptr [0x200001], eax; mov eax, dword ptr [0x200001]
    char code[] = "\xa3\x01\x00\x20\x00\xa1\x01\x00\x20\x00";
    uint32_t r_eax = LEINT32(0x41424344);
    struct writelog_t write_log[10];
    struct writelog_t read_log[10];
    memset(write_log, 0, sizeof(write_log));
    memset(read_log, 0, sizeof(read_log));

    uc_common_setup(&uc, UC_ARCH_X86, UC_MODE_32, code, sizeof(code) - 1);
    OK(uc_mem_map(uc, 0x200000, 0x1000, UC_PROT_ALL));
    OK(uc_hook_add(uc, &hook, UC_HOOK_MEM_WRITE,
                   test_x86_unaligned_access_callback, write_log, 1, 0));
    OK(uc_hook_add(uc, &hook, UC_HOOK_MEM_READ,
                   test_x86_unaligned_access_callback, read_log, 1, 0));

    OK(uc_reg_write(uc, UC_X86_REG_EAX, &r_eax));
    OK(uc_emu_start(uc, code_start, code_start + sizeof(code) - 1, 0, 0));

    TEST_CHECK(write_log[0].addr == 0x200001);
    TEST_CHECK(write_log[0].size == 4);
    TEST_CHECK(write_log[1].size == 0);

    TEST_CHECK(read_log[0].addr == 0x200001);
    TEST_CHECK(read_log[0].size == 4);
    TEST_CHECK(read_log[1].size == 0);

    char b;
    OK(uc_mem_read(uc, 0x200001, &b, 1));
    TEST_CHECK(b == 0x44);
    OK(uc_mem_read(uc, 0x200002, &b, 1));
    TEST_CHECK(b == 0x43);
    OK(uc_mem_read(uc, 0x200003, &b, 1));
    TEST_CHECK(b == 0x42);
    OK(uc_mem_read(uc, 0x200004, &b, 1));
    TEST_CHECK(b == 0x41);

    OK(uc_close(uc));
}

static void test_x86_64_unaligned_access(void)
{
    uc_engine *uc;
    uc_hook hook;
    char code[] = {"\x48\x89\x01" //   mov         qword ptr [rcx],rax
                   "\x48\x8b\x00" //  mov         rax,qword ptr [rax]
                   "\xcc"};
    uint64_t r_rax = LEINT64(0x2fffff);
    uint64_t r_rcx = LEINT64(0x2fffff);
    struct writelog_t write_log[10];
    struct writelog_t read_log[10];
    memset(write_log, 0, sizeof(write_log));
    memset(read_log, 0, sizeof(read_log));
    uc_common_setup(&uc, UC_ARCH_X86, UC_MODE_64, code, sizeof(code) - 1);
    OK(uc_mem_map(uc, 0x200000, 0x200000, UC_PROT_ALL));
    OK(uc_hook_add(uc, &hook, UC_HOOK_MEM_WRITE,
                   test_x86_unaligned_access_callback, write_log, 1, 0));
    OK(uc_hook_add(uc, &hook, UC_HOOK_MEM_READ,
                   test_x86_unaligned_access_callback, read_log, 1, 0));

    OK(uc_reg_write(uc, UC_X86_REG_RAX, &r_rax));
    OK(uc_reg_write(uc, UC_X86_REG_RCX, &r_rcx));

    OK(uc_emu_start(uc, code_start, code_start + sizeof(code) - 1, 0, 2));

    TEST_CHECK(write_log[0].addr == 0x2fffff);
    TEST_CHECK(write_log[0].size == 8);
    TEST_CHECK(write_log[1].size == 0);

    TEST_CHECK(read_log[0].addr == 0x2fffff);
    TEST_CHECK(read_log[0].size == 8);
    TEST_CHECK(read_log[1].size == 0);

    uint64_t b;
    OK(uc_mem_read(uc, 0x2fffff, &b, 8));
    TEST_CHECK(b == 0x2fffff);

    OK(uc_close(uc));
}
#endif

static bool test_x86_lazy_mapping_mem_callback(uc_engine *uc, uc_mem_type type,
                                               uint64_t address, int size,
                                               int64_t value, void *user_data)
{
    OK(uc_mem_map(uc, 0x1000, 0x1000, UC_PROT_ALL));
    OK(uc_mem_write(uc, 0x1000, "\x90\x90", 2)); // nop; nop

    // Handled!
    return true;
}

static void test_x86_lazy_mapping_block_callback(uc_engine *uc,
                                                 uint64_t address,
                                                 uint32_t size, void *user_data)
{
    int *block_count = (int *)user_data;
    (*block_count)++;
}

static void test_x86_lazy_mapping(void)
{
    uc_engine *uc;
    uc_hook mem_hook, block_hook;
    int block_count = 0;

    OK(uc_open(UC_ARCH_X86, UC_MODE_32, &uc));
    OK(uc_hook_add(uc, &mem_hook, UC_HOOK_MEM_FETCH_UNMAPPED,
                   test_x86_lazy_mapping_mem_callback, NULL, 1, 0));
    OK(uc_hook_add(uc, &block_hook, UC_HOOK_BLOCK,
                   test_x86_lazy_mapping_block_callback, &block_count, 1, 0));

    OK(uc_emu_start(uc, 0x1000, 0x1002, 0, 0));
    TEST_CHECK(block_count == 1);
    OK(uc_close(uc));
}

static void test_x86_16_incorrect_ip_cb(uc_engine *uc, uint64_t address,
                                        uint32_t size, void *data)
{
    uint16_t cs, ip;

    OK(uc_reg_read(uc, UC_X86_REG_CS, &cs));
    OK(uc_reg_read(uc, UC_X86_REG_IP, &ip));

    TEST_CHECK(cs == 0x20);
    TEST_CHECK(address == ((cs << 4) + ip));
}

static void test_x86_16_incorrect_ip(void)
{
    uc_engine *uc;
    uc_hook hk1, hk2;
    uint16_t cs = 0x20;
    char code[] = "\x41"; // INC cx;

    uc_common_setup(&uc, UC_ARCH_X86, UC_MODE_16, code, sizeof(code) - 1);

    OK(uc_hook_add(uc, &hk1, UC_HOOK_BLOCK, test_x86_16_incorrect_ip_cb, NULL,
                   1, 0));
    OK(uc_hook_add(uc, &hk2, UC_HOOK_CODE, test_x86_16_incorrect_ip_cb, NULL, 1,
                   0));

    OK(uc_reg_write(uc, UC_X86_REG_CS, &cs));

    OK(uc_emu_start(uc, code_start, code_start + sizeof(code) - 1, 0, 0));

    OK(uc_close(uc));
}

// Porting to BE: Only uc_mem_read/write needs endian fixing
static void test_x86_mmu_prepare_tlb(uc_engine *uc, uint64_t vaddr,
                                     uint64_t tlb_base)
{
    uint64_t cr0;
    uint64_t cr4;
    uc_x86_msr msr = {.rid = 0x0c0000080, .value = 0};
    uint64_t pml4o = ((vaddr & 0x00ff8000000000) >> 39) * 8;
    uint64_t pdpo = ((vaddr & 0x00007fc0000000) >> 30) * 8;
    uint64_t pdo = ((vaddr & 0x0000003fe00000) >> 21) * 8;
    uint64_t pml4e = (tlb_base + 0x1000) | 1 | (1 << 2);
    uint64_t pdpe = (tlb_base + 0x2000) | 1 | (1 << 2);
    uint64_t pde = (tlb_base + 0x3000) | 1 | (1 << 2);
    uint64_t pml4e_mem = LEINT64(pml4e);
    uint64_t pde_mem = LEINT64(pde);
    uint64_t pdpe_mem = LEINT64(pdpe);
    OK(uc_mem_write(uc, tlb_base + pml4o, &pml4e_mem, sizeof(pml4o)));
    OK(uc_mem_write(uc, tlb_base + 0x1000 + pdpo, &pdpe_mem, sizeof(pdpe)));
    OK(uc_mem_write(uc, tlb_base + 0x2000 + pdo, &pde_mem, sizeof(pde)));
    OK(uc_reg_write(uc, UC_X86_REG_CR3, &tlb_base));
    OK(uc_reg_read(uc, UC_X86_REG_CR0, &cr0));
    OK(uc_reg_read(uc, UC_X86_REG_CR4, &cr4));
    OK(uc_reg_read(uc, UC_X86_REG_MSR, &msr));
    cr0 |= 1;
    cr0 |= 1l << 31;
    cr4 |= 1l << 5;
    msr.value |= 1l << 8;
    OK(uc_reg_write(uc, UC_X86_REG_CR0, &cr0));
    OK(uc_reg_write(uc, UC_X86_REG_CR4, &cr4));
    OK(uc_reg_write(uc, UC_X86_REG_MSR, &msr));
}

static void test_x86_mmu_pt_set(uc_engine *uc, uint64_t vaddr, uint64_t paddr,
                                uint64_t tlb_base, bool readwrite)
{
    uint64_t pto = ((vaddr & 0x000000001ff000) >> 12) * 8;
    uint32_t pte;
    if (readwrite)
        pte = (paddr) | 1 | (1 << 2);
    else
        pte = (paddr) | 1;
    pte = LEINT32(pte);

    uc_mem_write(uc, tlb_base + 0x3000 + pto, &pte, sizeof(pte));
}

static void test_x86_mmu_callback(uc_engine *uc, void *userdata)
{
    bool *parrent_done = userdata;
    uint64_t rax;
    OK(uc_reg_read(uc, UC_X86_REG_RAX, &rax));
    switch (rax) {
    case 57:
        /* fork */
        break;
    case 60:
        /* exit */
        uc_emu_stop(uc);
        return;
    default:
        TEST_CHECK(false);
    }

    if (!(*parrent_done)) {
        *parrent_done = true;
        rax = 27;
        OK(uc_reg_write(uc, UC_X86_REG_RAX, &rax));
        uc_emu_stop(uc);
    }
}

static void test_x86_mmu(void)
{
    bool parrent_done = false;
    uint64_t tlb_base = 0x3000;
    uint64_t parrent, child;
    uint64_t rax, rip;
    uc_context *context;
    uc_engine *uc;
    uc_hook h1;

    /*
     * mov rax, 57
     * syscall
     * test rax, rax
     * jz child
     * xor rax, rax
     * mov rax, 60
     * mov [0x4000], rax
     * syscall
     *
     * child:
     * xor rcx, rcx
     * mov rcx, 42
     * mov [0x4000], rcx
     * mov rax, 60
     * syscall
     */
    char code[] =
        "\xB8\x39\x00\x00\x00\x0F\x05\x48\x85\xC0\x74\x0F\xB8\x3C\x00\x00\x00"
        "\x48\x89\x04\x25\x00\x40\x00\x00\x0F\x05\xB9\x2A\x00\x00\x00\x48\x89"
        "\x0C\x25\x00\x40\x00\x00\xB8\x3C\x00\x00\x00\x0F\x05";

    OK(uc_open(UC_ARCH_X86, UC_MODE_64, &uc));
    OK(uc_ctl_tlb_mode(uc, UC_TLB_CPU));
    OK(uc_hook_add(uc, &h1, UC_HOOK_INSN, &test_x86_mmu_callback, &parrent_done,
                   1, 0, UC_X86_INS_SYSCALL));
    OK(uc_context_alloc(uc, &context));

    OK(uc_mem_map(uc, 0x0, 0x1000, UC_PROT_ALL)); // Code
    OK(uc_mem_write(uc, 0x0, code, sizeof(code) - 1));
    OK(uc_mem_map(uc, 0x1000, 0x1000, UC_PROT_ALL));   // Parrent
    OK(uc_mem_map(uc, 0x2000, 0x1000, UC_PROT_ALL));   // Child
    OK(uc_mem_map(uc, tlb_base, 0x4000, UC_PROT_ALL)); // TLB

    test_x86_mmu_prepare_tlb(uc, 0x0, tlb_base);
    test_x86_mmu_pt_set(uc, 0x2000, 0x0, tlb_base, true);
    test_x86_mmu_pt_set(uc, 0x4000, 0x1000, tlb_base, true);

    OK(uc_ctl_flush_tlb(uc));
    OK(uc_emu_start(uc, 0x2000, 0x0, 0, 0));

    OK(uc_context_save(uc, context));
    OK(uc_reg_read(uc, UC_X86_REG_RIP, &rip));

    OK(uc_emu_start(uc, rip, 0x0, 0, 0));

    /* restore for child */
    OK(uc_context_restore(uc, context));
    test_x86_mmu_prepare_tlb(uc, 0x0, tlb_base);
    test_x86_mmu_pt_set(uc, 0x4000, 0x2000, tlb_base, true);
    rax = 0;
    OK(uc_reg_write(uc, UC_X86_REG_RAX, &rax));
    OK(uc_ctl_flush_tlb(uc));

    OK(uc_emu_start(uc, rip, 0x0, 0, 0));
    OK(uc_mem_read(uc, 0x1000, &parrent, sizeof(parrent)));
    OK(uc_mem_read(uc, 0x2000, &child, sizeof(child)));
    TEST_CHECK(LEINT64(parrent) == 60);
    TEST_CHECK(LEINT64(child) == 42);
    OK(uc_context_free(context));
    OK(uc_close(uc));
}

static void test_x86_read_virtual(void)
{
    bool parrent_done = false;
    uint64_t tlb_base = 0x3000;
    uint64_t parrent, child, tmp;
    uint64_t rax, rip;
    uc_context *context;
    uc_engine *uc;
    uc_hook h1;

    /*
     * mov rax, 57
     * syscall
     * test rax, rax
     * jz child
     * xor rax, rax
     * mov rax, 60
     * mov [0x4000], rax
     * syscall
     *
     * child:
     * xor rcx, rcx
     * mov rcx, 42
     * mov [0x4000], rcx
     * mov rax, 60
     * syscall
     */
    char code[] =
        "\xB8\x39\x00\x00\x00\x0F\x05\x48\x85\xC0\x74\x0F\xB8\x3C\x00\x00\x00"
        "\x48\x89\x04\x25\x00\x40\x00\x00\x0F\x05\xB9\x2A\x00\x00\x00\x48\x89"
        "\x0C\x25\x00\x40\x00\x00\xB8\x3C\x00\x00\x00\x0F\x05";

    OK(uc_open(UC_ARCH_X86, UC_MODE_64, &uc));
    OK(uc_ctl_tlb_mode(uc, UC_TLB_CPU));
    OK(uc_hook_add(uc, &h1, UC_HOOK_INSN, &test_x86_mmu_callback, &parrent_done,
                   1, 0, UC_X86_INS_SYSCALL));
    OK(uc_context_alloc(uc, &context));

    OK(uc_mem_map(uc, 0x0, 0x1000, UC_PROT_ALL)); // Code
    OK(uc_mem_write(uc, 0x0, code, sizeof(code) - 1));
    OK(uc_mem_map(uc, 0x1000, 0x1000, UC_PROT_ALL));   // Parrent
    OK(uc_mem_map(uc, 0x2000, 0x1000, UC_PROT_ALL));   // Child
    OK(uc_mem_map(uc, tlb_base, 0x4000, UC_PROT_ALL)); // TLB

    test_x86_mmu_prepare_tlb(uc, 0x0, tlb_base);
    test_x86_mmu_pt_set(uc, 0x2000, 0x0, tlb_base, false);
    test_x86_mmu_pt_set(uc, 0x4000, 0x1000, tlb_base, true);

    OK(uc_ctl_flush_tlb(uc));
    OK(uc_emu_start(uc, 0x2000, 0x0, 0, 0));

    OK(uc_context_save(uc, context));
    OK(uc_reg_read(uc, UC_X86_REG_RIP, &rip));

    OK(uc_emu_start(uc, rip, 0x0, 0, 0));
    OK(uc_vmem_read(uc, 0x4000, UC_PROT_READ, &parrent,
                           sizeof(parrent)));

    /* restore for child */
    OK(uc_context_restore(uc, context));
    test_x86_mmu_prepare_tlb(uc, 0x0, tlb_base);
    test_x86_mmu_pt_set(uc, 0x4000, 0x2000, tlb_base, true);
    rax = 0;
    OK(uc_reg_write(uc, UC_X86_REG_RAX, &rax));
    OK(uc_ctl_flush_tlb(uc));

    OK(uc_emu_start(uc, rip, 0x0, 0, 0));
    OK(uc_vmem_read(uc, 0x4000, UC_PROT_READ, &child, sizeof(child)));
    uc_assert_err(
        UC_ERR_READ_PROT,
        uc_vmem_read(uc, 0x1000, UC_PROT_WRITE, &tmp, sizeof(tmp)));
    TEST_CHECK(parrent == 60);
    TEST_CHECK(child == 42);
}

static bool test_x86_vtlb_callback(uc_engine *uc, uint64_t addr,
                                   uc_mem_type type, uc_tlb_entry *result,
                                   void *user_data)
{
    result->paddr = addr;
    result->perms = UC_PROT_ALL;
    return true;
}

static void test_x86_vtlb(void)
{
    uc_engine *uc;
    uc_hook hook;
    char code[] = "\xeb\x02\x90\x90\x90\x90\x90\x90"; // jmp 4; nop; nop; nop;
                                                      // nop; nop; nop
    uint32_t r_eip = 0;

    uc_common_setup(&uc, UC_ARCH_X86, UC_MODE_32, code, sizeof(code) - 1);

    OK(uc_ctl_tlb_mode(uc, UC_TLB_VIRTUAL));
    OK(uc_hook_add(uc, &hook, UC_HOOK_TLB_FILL, test_x86_vtlb_callback, NULL, 1,
                   0));

    OK(uc_emu_start(uc, code_start, code_start + 4, 0, 0));

    OK(uc_reg_read(uc, UC_X86_REG_EIP, &r_eip));

    TEST_CHECK(r_eip == code_start + 4);

    OK(uc_close(uc));
}

static void test_x86_segmentation(void)
{
    uc_engine *uc;
    uint16_t fs = 0x53;
    uc_x86_mmr gdtr = {0, 0xfffff8076d962000, 0x57, 0};

    OK(uc_open(UC_ARCH_X86, UC_MODE_64, &uc));
    OK(uc_reg_write(uc, UC_X86_REG_GDTR, &gdtr));
    uc_assert_err(UC_ERR_EXCEPTION, uc_reg_write(uc, UC_X86_REG_FS, &fs));
    OK(uc_close(uc));
}

static void test_x86_0xff_lcall_callback(uc_engine *uc, uint64_t address,
                                         uint32_t size, void *user_data)
{
    // do nothing
    return;
}

// This aborts prior to a7a5d187e77f7853755eff4768658daf8095c3b7
static void test_x86_0xff_lcall(void)
{
    uc_engine *uc;
    uc_hook hk;
    const char code[] =
        "\xB8\x01\x00\x00\x00\xBB\x01\x00\x00\x00\xB9\x01\x00\x00\x00\xFF\xDD"
        "\xBA\x01\x00\x00\x00\xB8\x02\x00\x00\x00\xBB\x02\x00\x00\x00";
    // Taken from #1842
    // 0:  b8 01 00 00 00          mov    eax,0x1
    // 5:  bb 01 00 00 00          mov    ebx,0x1
    // a:  b9 01 00 00 00          mov    ecx,0x1
    // f:  ff                      (bad)
    // 10: dd ba 01 00 00 00       fnstsw WORD PTR [edx+0x1]
    // 16: b8 02 00 00 00          mov    eax,0x2
    // 1b: bb 02 00 00 00          mov    ebx,0x2

    uc_common_setup(&uc, UC_ARCH_X86, UC_MODE_32, code, sizeof(code) - 1);

    OK(uc_hook_add(uc, &hk, UC_HOOK_CODE, test_x86_0xff_lcall_callback, NULL, 1,
                   0));

    uc_assert_err(
        UC_ERR_INSN_INVALID,
        uc_emu_start(uc, code_start, code_start + sizeof(code) - 1, 0, 0));

    OK(uc_close(uc));
}

static bool test_x86_64_not_overwriting_tmp0_for_pc_update_cb(
    uc_engine *uc, uc_mem_type type, uint64_t address, int size, uint64_t value,
    void *user_data)
{
    return true;
}

// https://github.com/unicorn-engine/unicorn/issues/1717
// https://github.com/unicorn-engine/unicorn/issues/1862
static void test_x86_64_not_overwriting_tmp0_for_pc_update(void)
{
    uc_engine *uc;
    uc_hook hk;
    const char code[] = "\x48\xb9\xff\xff\xff\xff\xff\xff\xff\xff\x48\x89\x0c"
                        "\x24\x48\xd3\x24\x24\x73\x0a";
    uint64_t rsp, pc;
    uint32_t eflags;

    // 0x1000: movabs  rcx, 0xffffffffffffffff
    // 0x100a: mov     qword ptr [rsp], rcx
    // 0x100e: shl     qword ptr [rsp], cl ; (Shift to CF=1)
    // 0x1012: jae     0xd ; this jump should not be taken! (CF=1 but jae
    // expects CF=0)
    uc_common_setup(&uc, UC_ARCH_X86, UC_MODE_64, code, sizeof(code) - 1);
    OK(uc_hook_add(uc, &hk, UC_HOOK_MEM_READ | UC_HOOK_MEM_WRITE,
                   test_x86_64_not_overwriting_tmp0_for_pc_update_cb, NULL, 1,
                   0));

    rsp = 0x2000;
    OK(uc_reg_write(uc, UC_X86_REG_RSP, (void *)&rsp));
    OK(uc_emu_start(uc, code_start, code_start + sizeof(code) - 1, 0, 4));
    OK(uc_reg_read(uc, UC_X86_REG_RIP, &pc));
    OK(uc_reg_read(uc, UC_X86_REG_EFLAGS, &eflags));

    TEST_CHECK(pc == 0x1014);
    TEST_CHECK((eflags & 0x1) == 1);

    OK(uc_close(uc));
}

static void test_fxsave_fpip_x86(void)
{
    // note: fxsave was introduced in Pentium II
    uint8_t code_x86[] = {
        // help testing through NOP offset      [disassembly in at&t syntax]
        0x90, 0x90, 0x90, 0x90, // nop nop nop nop
        // run a floating point instruction
        0xdb, 0xc9, // fcmovne %st(1), %st
        // fxsave needs 512 bytes of storage space
        0x81, 0xec, 0x00, 0x02, 0x00, 0x00, // subl $512, %esp
        // fxsave needs a 16-byte aligned address for storage
        0x83, 0xe4, 0xf0, // andl $0xfffffff0, %esp
        // store fxsave data on the stack
        0x0f, 0xae, 0x04, 0x24, // fxsave (%esp)
        // fxsave stores FPIP at an 8-byte offset, move FPIP to eax register
        0x8b, 0x44, 0x24, 0x08 // movl 0x8(%esp), %eax
    };
    uint32_t X86_NOP_OFFSET = 4;
    uint32_t stack_top = (uint32_t)MEM_STACK;
    uint32_t value;
    uc_engine *uc;

    // initialize emulator in X86-32bit mode
    OK(uc_open(UC_ARCH_X86, UC_MODE_32, &uc));

    // map 1MB of memory for this emulation
    OK(uc_mem_map(uc, MEM_BASE, MEM_SIZE, UC_PROT_ALL));
    OK(uc_mem_write(uc, MEM_TEXT, code_x86, sizeof(code_x86)));
    OK(uc_reg_write(uc, UC_X86_REG_ESP, &stack_top));
    OK(uc_emu_start(uc, MEM_TEXT, MEM_TEXT + sizeof(code_x86), 0, 0));
    OK(uc_reg_read(uc, UC_X86_REG_EAX, &value));
    TEST_CHECK(value == ((uint32_t)MEM_TEXT + X86_NOP_OFFSET));
    OK(uc_mem_unmap(uc, MEM_BASE, MEM_SIZE));
    OK(uc_close(uc));
}

static void test_fxsave_fpip_x64(void)
{
    uint8_t code_x64[] = {
        // help testing through NOP offset     [disassembly in at&t]
        0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, // nops
        // run a floating point instruction
        0xdb, 0xc9, // fcmovne %st(1), %st
        // fxsave64 needs 512 bytes of storage space
        0x48, 0x81, 0xec, 0x00, 0x02, 0x00, 0x00, // subq $512, %rsp
        // fxsave needs a 16-byte aligned address for storage
        0x48, 0x83, 0xe4, 0xf0, // andq 0xfffffffffffffff0, %rsp
        // store fxsave64 data on the stack
        0x48, 0x0f, 0xae, 0x04, 0x24, // fxsave64 (%rsp)
        // fxsave64 stores FPIP at an 8-byte offset, move FPIP to rax register
        0x48, 0x8b, 0x44, 0x24, 0x08, // movq 0x8(%rsp), %rax
    };

    uint64_t stack_top = (uint64_t)MEM_STACK;
    uint64_t X64_NOP_OFFSET = 8;
    uint64_t value;
    uc_engine *uc;

    // initialize emulator in X86-32bit mode
    OK(uc_open(UC_ARCH_X86, UC_MODE_64, &uc));

    // map 1MB of memory for this emulation
    OK(uc_mem_map(uc, MEM_BASE, MEM_SIZE, UC_PROT_ALL));
    OK(uc_mem_write(uc, MEM_TEXT, code_x64, sizeof(code_x64)));
    OK(uc_reg_write(uc, UC_X86_REG_RSP, &stack_top));
    OK(uc_emu_start(uc, MEM_TEXT, MEM_TEXT + sizeof(code_x64), 0, 0));
    OK(uc_reg_read(uc, UC_X86_REG_RAX, &value));
    TEST_CHECK(value == ((uint64_t)MEM_TEXT + X64_NOP_OFFSET));
    OK(uc_mem_unmap(uc, MEM_BASE, MEM_SIZE));
    OK(uc_close(uc));
}

static void test_bswap_ax(void)
{
    // References:
    // - https://gynvael.coldwind.pl/?id=268
    // - https://github.com/JonathanSalwan/Triton/issues/1131
    {
        uint8_t code[] = {
            // bswap ax
            0x66,
            0x0F,
            0xC8,
        };
        TEST_CODE(UC_MODE_32, code);
        TEST_IN_REG(EAX, 0x44332211);
        TEST_OUT_REG(EAX, 0x44330000);
        TEST_RUN();
    }
    {
        uint8_t code[] = {
            // bswap ax
            0x66,
            0x0F,
            0xC8,
        };
        TEST_CODE(UC_MODE_64, code);
        TEST_IN_REG(RAX, 0x8877665544332211);
        TEST_OUT_REG(RAX, 0x8877665544330000);
        TEST_RUN();
    }
    {
        uint8_t code[] = {
            // bswap rax (66h ignored)
            0x66,
            0x48,
            0x0F,
            0xC8,
        };
        TEST_CODE(UC_MODE_64, code);
        TEST_IN_REG(RAX, 0x8877665544332211);
        TEST_OUT_REG(RAX, 0x1122334455667788);
        TEST_RUN();
    }
    {
        uint8_t code[] = {
            // bswap ax (rex ignored)
            0x48,
            0x66,
            0x0F,
            0xC8,
        };
        TEST_CODE(UC_MODE_64, code);
        TEST_IN_REG(RAX, 0x8877665544332211);
        TEST_OUT_REG(RAX, 0x8877665544330000);
        TEST_RUN();
    }
    {
        uint8_t code[] = {
            // bswap eax
            0x0F,
            0xC8,
        };
        TEST_CODE(UC_MODE_32, code);
        TEST_IN_REG(EAX, 0x44332211);
        TEST_OUT_REG(EAX, 0x11223344);
        TEST_RUN();
    }
    {
        uint8_t code[] = {
            // bswap eax
            0x0F,
            0xC8,
        };
        TEST_CODE(UC_MODE_64, code);
        TEST_IN_REG(RAX, 0x8877665544332211);
        TEST_OUT_REG(RAX, 0x0000000011223344);
        TEST_RUN();
    }
}

static void test_rex_x64(void)
{
    {
        uint8_t code[] = {
            // mov ax, bx (rex.w ignored)
            0x48,
            0x66,
            0x89,
            0xD8,
        };
        TEST_CODE(UC_MODE_64, code);
        TEST_IN_REG(RAX, 0x8877665544332211);
        TEST_IN_REG(RBX, 0x1122334455667788);
        TEST_OUT_REG(RAX, 0x8877665544337788);
        TEST_RUN();
    }
    {
        uint8_t code[] = {
            // mov rax, rbx (66h ignored)
            0x66,
            0x48,
            0x89,
            0xD8,
        };
        TEST_CODE(UC_MODE_64, code);
        TEST_IN_REG(RAX, 0x8877665544332211);
        TEST_IN_REG(RBX, 0x1122334455667788);
        TEST_OUT_REG(RAX, 0x1122334455667788);
        TEST_RUN();
    }
    {
        uint8_t code[] = {
            // mov ax, bx (expected encoding)
            0x66,
            0x89,
            0xD8,
        };
        TEST_CODE(UC_MODE_64, code);
        TEST_IN_REG(RAX, 0x8877665544332211);
        TEST_IN_REG(RBX, 0x1122334455667788);
        TEST_OUT_REG(RAX, 0x8877665544337788);
        TEST_RUN();
    }
}

static bool test_x86_ro_segfault_cb(uc_engine *uc, uc_mem_type type,
                                    uint64_t address, int size, uint64_t value,
                                    void *user_data)
{
    const char code[] = "\xA1\x00\x10\x00\x00\xA1\x00\x10\x00\x00";
    OK(uc_mem_write(uc, address, code, sizeof(code) - 1));
    return true;
}

static void test_x86_ro_segfault(void)
{
    uc_engine *uc;
    // mov eax, [0x1000]
    // mov eax, [0x1000]
    const char code[] = "\xA1\x00\x10\x00\x00\xA1\x00\x10\x00\x00";
    uint32_t out;
    uc_hook hh;

    OK(uc_open(UC_ARCH_X86, UC_MODE_32, &uc));
    OK(uc_mem_map(uc, 0, 0x1000, UC_PROT_ALL));
    OK(uc_mem_write(uc, 0, code, sizeof(code) - 1));
    OK(uc_mem_map(uc, 0x1000, 0x1000, UC_PROT_READ));

    OK(uc_hook_add(uc, &hh, UC_HOOK_MEM_READ, test_x86_ro_segfault_cb, NULL, 1,
                   0));
    OK(uc_emu_start(uc, 0, sizeof(code) - 1, 0, 0));

    OK(uc_reg_read(uc, UC_X86_REG_EAX, (void *)&out));
    TEST_CHECK(out == 0x001000a1);
    OK(uc_close(uc));
}

static void test_x86_vpermilps_null_ptr_call(void)
{
    char code_modrm_00[] = {
        0x0f, 0x38, 0x0c, 0x00
    };

    char code_modrm_ff[] = {
        0x0f, 0x38, 0x0c, 0xff
    };

    uc_engine *uc = NULL;
    uc_common_setup(&uc, UC_ARCH_X86, UC_MODE_64, code_modrm_00, sizeof(code_modrm_00));

    uc_assert_err(UC_ERR_INSN_INVALID,
            uc_emu_start(uc, code_start, code_start + sizeof(code_modrm_00), 0, 0));

    OK(uc_close(uc));

    uc = NULL;
    uc_common_setup(&uc, UC_ARCH_X86, UC_MODE_64, code_modrm_ff, sizeof(code_modrm_ff));

    uc_assert_err(UC_ERR_INSN_INVALID,
            uc_emu_start(uc, code_start, code_start + sizeof(code_modrm_ff), 0, 0));

    OK(uc_close(uc));
}

static bool test_x86_hook_insn_rdtsc_cb(uc_engine *uc, void *user_data)
{
    uint64_t h = 0x00000000FEDCBA98;
    OK(uc_reg_write(uc, UC_X86_REG_RDX, &h));

    uint64_t l = 0x0000000076543210;
    OK(uc_reg_write(uc, UC_X86_REG_RAX, &l));

    return true;
}

static void test_x86_hook_insn_rdtsc(void)
{
    char code[] = "\x0F\x31"; // RDTSC

    uc_engine *uc;
    uc_common_setup(&uc, UC_ARCH_X86, UC_MODE_64, code, sizeof code - 1);

    uc_hook hook;
    OK(uc_hook_add(uc, &hook, UC_HOOK_INSN, test_x86_hook_insn_rdtsc_cb, NULL,
                   1, 0, UC_X86_INS_RDTSC));

    OK(uc_emu_start(uc, code_start, code_start + sizeof code - 1, 0, 0));

    OK(uc_hook_del(uc, hook));

    uint64_t h = 0;
    OK(uc_reg_read(uc, UC_X86_REG_RDX, &h));
    TEST_CHECK(h == 0x00000000FEDCBA98);

    uint64_t l = 0;
    OK(uc_reg_read(uc, UC_X86_REG_RAX, &l));
    TEST_CHECK(l == 0x0000000076543210);

    OK(uc_close(uc));
}

static bool test_x86_hook_insn_rdtscp_cb(uc_engine *uc, void *user_data)
{
    uint64_t h = 0x0000000001234567;
    OK(uc_reg_write(uc, UC_X86_REG_RDX, &h));

    uint64_t l = 0x0000000089ABCDEF;
    OK(uc_reg_write(uc, UC_X86_REG_RAX, &l));

    uint64_t i = 0x00000000DEADBEEF;
    OK(uc_reg_write(uc, UC_X86_REG_RCX, &i));

    return true;
}

static void test_x86_hook_insn_rdtscp(void)
{
    uc_engine *uc;
    OK(uc_open(UC_ARCH_X86, UC_MODE_64, &uc));

    OK(uc_ctl_set_cpu_model(uc, UC_CPU_X86_HASWELL));

    OK(uc_mem_map(uc, code_start, code_len, UC_PROT_ALL));

    char code[] = "\x0F\x01\xF9"; // RDTSCP
    OK(uc_mem_write(uc, code_start, code, sizeof code - 1));

    uc_hook hook;
    OK(uc_hook_add(uc, &hook, UC_HOOK_INSN, test_x86_hook_insn_rdtscp_cb, NULL,
                   1, 0, UC_X86_INS_RDTSCP));

    OK(uc_emu_start(uc, code_start, code_start + sizeof code - 1, 0, 0));

    OK(uc_hook_del(uc, hook));

    uint64_t h = 0;
    OK(uc_reg_read(uc, UC_X86_REG_RDX, &h));
    TEST_CHECK(h == 0x0000000001234567);

    uint64_t l = 0;
    OK(uc_reg_read(uc, UC_X86_REG_RAX, &l));
    TEST_CHECK(l == 0x0000000089ABCDEF);

    uint64_t i = 0;
    OK(uc_reg_read(uc, UC_X86_REG_RCX, &i));
    TEST_CHECK(i == 0x00000000DEADBEEF);

    OK(uc_close(uc));
}

static int test_x86_hook_insn_wrmsr_cb(uc_engine *uc, void *user_data)
{
    *(int *)user_data = 1;
    return 0;
}

static void test_x86_hook_insn_wrmsr(void)
{
    /* WRMSR (0f 30) */
    char code[] = "\x0F\x30";
    int fired = 0;

    uc_engine *uc;
    uc_common_setup(&uc, UC_ARCH_X86, UC_MODE_64, code, sizeof code - 1);

    uint64_t rcx = 0x174; /* MSR_IA32_SYSENTER_CS */
    uint64_t rax = 0x10;
    uint64_t rdx = 0;
    OK(uc_reg_write(uc, UC_X86_REG_RCX, &rcx));
    OK(uc_reg_write(uc, UC_X86_REG_RAX, &rax));
    OK(uc_reg_write(uc, UC_X86_REG_RDX, &rdx));

    uc_hook hook;
    OK(uc_hook_add(uc, &hook, UC_HOOK_INSN, test_x86_hook_insn_wrmsr_cb,
                   &fired, 1, 0, UC_X86_INS_WRMSR));

    OK(uc_emu_start(uc, code_start, code_start + sizeof code - 1, 0, 0));

    OK(uc_hook_del(uc, hook));
    TEST_CHECK(fired == 1);
    OK(uc_close(uc));
}

static int test_x86_hook_insn_rdmsr_cb(uc_engine *uc, void *user_data)
{
    uint64_t eax = 0xDEAD;
    uint64_t edx = 0xBEEF;
    OK(uc_reg_write(uc, UC_X86_REG_RAX, &eax));
    OK(uc_reg_write(uc, UC_X86_REG_RDX, &edx));
    *(int *)user_data = 1;
    return 1; /* skip underlying rdmsr */
}

static void test_x86_hook_insn_rdmsr(void)
{
    /* RDMSR (0f 32) */
    char code[] = "\x0F\x32";
    int fired = 0;

    uc_engine *uc;
    uc_common_setup(&uc, UC_ARCH_X86, UC_MODE_64, code, sizeof code - 1);

    uint64_t rcx = 0x174; /* MSR_IA32_SYSENTER_CS */
    OK(uc_reg_write(uc, UC_X86_REG_RCX, &rcx));

    uc_hook hook;
    OK(uc_hook_add(uc, &hook, UC_HOOK_INSN, test_x86_hook_insn_rdmsr_cb,
                   &fired, 1, 0, UC_X86_INS_RDMSR));

    OK(uc_emu_start(uc, code_start, code_start + sizeof code - 1, 0, 0));

    OK(uc_hook_del(uc, hook));
    TEST_CHECK(fired == 1);

    uint64_t eax = 0;
    uint64_t edx = 0;
    OK(uc_reg_read(uc, UC_X86_REG_RAX, &eax));
    OK(uc_reg_read(uc, UC_X86_REG_RDX, &edx));
    TEST_CHECK(eax == 0xDEAD);
    TEST_CHECK(edx == 0xBEEF);

    OK(uc_close(uc));
}

static void test_x86_dr7(void)
{
    uc_engine *uc;
    char code[] =
        "\x48\xC7\xC0\x05\x00\x01\x00\x0F\x23\xF8"; // mov rax, 0x10005
                                                    // mov dr7, rax
    uc_common_setup(&uc, UC_ARCH_X86, UC_MODE_64, code, sizeof(code) - 1);
    OK(uc_emu_start(uc, code_start, code_start + sizeof(code) - 1, 0, 0));

    OK(uc_close(uc));
}

static void test_x86_hook_block_cb(uc_engine *uc, uint64_t address,
                                   uint32_t size, void *user_data)
{
    uint32_t pc;

    OK(uc_reg_read(uc, UC_X86_REG_EIP, (void *)&pc));

    TEST_CHECK(pc == address);
    *((uint64_t *)user_data) += 1;
}

static void test_x86_hook_block(void)
{
    uc_engine *uc;
    char code[] = "\xeb\x02\x90\x90\x90\x90\x90\x90"; // jmp 4; nop; nop; nop;
                                                      // nop; nop; nop
    uint64_t cnt = 0;
    uc_hook hk;

    uc_common_setup(&uc, UC_ARCH_X86, UC_MODE_32, code, sizeof(code) - 1);

    OK(uc_hook_add(uc, &hk, UC_HOOK_BLOCK, test_x86_hook_block_cb, (void *)&cnt,
                   1, 0));
    OK(uc_emu_start(uc, code_start, code_start + sizeof(code) - 1, 0, 0));

    TEST_CHECK(cnt == 2);
    OK(uc_close(uc));
}

static bool test_x86_mem_hooks_pc_guarante_mem(uc_engine *uc, uc_mem_type type,
                                               uint64_t addr, int size,
                                               int64_t val, void *data)
{
    if (addr >= code_start + code_len) {
        uint32_t eip;
        OK(uc_reg_read(uc, UC_X86_REG_EIP, (void*)&eip));
        TEST_CHECK(eip == code_start + 1);
    }
    return true;
}

static void test_x86_mem_hooks_pc_guarantee(void)
{
    uc_engine *uc;
    // bs, _ = ks.asm("inc edx; t: mov eax, [ebx]; inc ebx; cmp ebx, ecx; jnz t;")
    char code[] = "\x42\x8b\x03\x43\x39\xcb\x75\xf9";
    uint32_t ebx=code_start + code_len, ecx = code_start + code_len + 0x10;
    uc_hook hk;

    uc_common_setup(&uc, UC_ARCH_X86, UC_MODE_32, code, sizeof(code) - 1);

    OK(uc_mem_map(uc, code_start + code_len, 0x1000, UC_PROT_ALL));
    OK(uc_hook_add(uc, &hk, UC_HOOK_MEM_READ, test_x86_mem_hooks_pc_guarante_mem, NULL,
                   1, 0));
    OK(uc_reg_write(uc, UC_X86_REG_EBX, (void*)&ebx));
    OK(uc_reg_write(uc, UC_X86_REG_ECX, (void*)&ecx));
    OK(uc_emu_start(uc, code_start, code_start + sizeof(code) - 1, 0, 0));

    OK(uc_close(uc));
}

// Test that AAA sets PF, ZF, SF based on the result AL value.
// Bug: AAA previously left these flags stale from the prior instruction.
static void test_x86_aaa_flags(void)
{
    uc_engine *uc;

    // INC ECX sets PF from ECX result (to create a conflicting PF state).
    // AAA then adjusts AL and must set PF = parity(AL), ZF, SF independently.
    //
    // Case: EAX = 0x030A  (AH=3, AL=0x0A)
    //   AL low nibble (0x0A & 0x0F = 0x0A) > 9, so adjustment fires:
    //     AL = (0x0A + 6) & 0x0F = 0x00
    //     AH = 3 + 1 = 4
    //     CF = 1, AF = 1
    //   Result AL = 0x00: PF=1 (even parity), ZF=1, SF=0
    //
    // INC ECX (0x41) ; AAA (0x37)
    char code[] = "\x41\x37";

    uint32_t r_eax = 0x030A;
    uint32_t r_ecx = 0x0000; // INC 0 -> 1, PF=0 (odd parity of 1)
    uint32_t r_eflags;

    OK(uc_open(UC_ARCH_X86, UC_MODE_32, &uc));
    OK(uc_mem_map(uc, code_start, code_len, UC_PROT_ALL));
    OK(uc_mem_write(uc, code_start, code, sizeof(code) - 1));

    OK(uc_reg_write(uc, UC_X86_REG_EAX, &r_eax));
    OK(uc_reg_write(uc, UC_X86_REG_ECX, &r_ecx));

    OK(uc_emu_start(uc, code_start, code_start + sizeof(code) - 1, 0, 0));

    OK(uc_reg_read(uc, UC_X86_REG_EAX, &r_eax));
    OK(uc_reg_read(uc, UC_X86_REG_EFLAGS, &r_eflags));

    // EAX should be 0x0400 (AH=4, AL=0)
    TEST_CHECK(r_eax == 0x0400);
    // PF (bit 2) = 1 (AL=0x00 has even parity)
    TEST_CHECK((r_eflags & 0x04) != 0);
    // ZF (bit 6) = 1 (AL == 0)
    TEST_CHECK((r_eflags & 0x40) != 0);
    // SF (bit 7) = 0 (AL bit 7 clear)
    TEST_CHECK((r_eflags & 0x80) == 0);
    // CF (bit 0) = 1 (adjustment fired)
    TEST_CHECK((r_eflags & 0x01) != 0);

    OK(uc_close(uc));
}

// Test that AAS sets PF, ZF, SF based on the result AL value.
static void test_x86_aas_flags(void)
{
    uc_engine *uc;

    // Case: EAX = 0x030A  (AH=3, AL=0x0A)
    //   AL low nibble > 9, so adjustment fires:
    //     AL = (0x0A - 6) & 0x0F = 0x04
    //     AH = 3 - 1 = 2
    //     CF = 1, AF = 1
    //   Result AL = 0x04: PF=0 (odd parity of 0x04), ZF=0, SF=0
    //
    // INC ECX (0x41) ; AAS (0x3F)
    char code[] = "\x41\x3f";

    uint32_t r_eax = 0x030A;
    uint32_t r_ecx = 0x0002; // INC 2 -> 3, PF=1 (even parity of 3)
    uint32_t r_eflags;

    OK(uc_open(UC_ARCH_X86, UC_MODE_32, &uc));
    OK(uc_mem_map(uc, code_start, code_len, UC_PROT_ALL));
    OK(uc_mem_write(uc, code_start, code, sizeof(code) - 1));

    OK(uc_reg_write(uc, UC_X86_REG_EAX, &r_eax));
    OK(uc_reg_write(uc, UC_X86_REG_ECX, &r_ecx));

    OK(uc_emu_start(uc, code_start, code_start + sizeof(code) - 1, 0, 0));

    OK(uc_reg_read(uc, UC_X86_REG_EAX, &r_eax));
    OK(uc_reg_read(uc, UC_X86_REG_EFLAGS, &r_eflags));

    // EAX should be 0x0204 (AH=2, AL=4)
    TEST_CHECK(r_eax == 0x0204);
    // PF (bit 2) = 0 (AL=0x04 has odd parity: one bit set)
    TEST_CHECK((r_eflags & 0x04) == 0);
    // ZF (bit 6) = 0 (AL != 0)
    TEST_CHECK((r_eflags & 0x40) == 0);
    // SF (bit 7) = 0 (AL bit 7 clear)
    TEST_CHECK((r_eflags & 0x80) == 0);
    // CF (bit 0) = 1 (adjustment fired)
    TEST_CHECK((r_eflags & 0x01) != 0);

    OK(uc_close(uc));
}

static void test_x86_group_1a(void)
{
    uc_engine *uc;

    char code[] = {
        // pop rax (ModRM.reg == 0)
        0x8f, 0xc0,
        // reserved group 1a opcode (ModRM.reg == 7)
        0x8f, 0xc0 | (7 << 3)
    };

    uc_common_setup(&uc, UC_ARCH_X86, UC_MODE_64, code, sizeof(code));

    OK(uc_mem_map(uc, code_start + code_len, 0x1000, UC_PROT_ALL));

    uint8_t stack_data[] = {0x12, 0x34, 0x56, 0x78, 0x90, 0xab, 0xcd, 0xef};

    uint64_t rsp = code_start + code_len + 0x800;
    OK(uc_reg_write(uc, UC_X86_REG_RSP, &rsp));
    OK(uc_mem_write(uc, rsp, stack_data, sizeof(stack_data)));

    uc_assert_err(UC_ERR_INSN_INVALID,
            uc_emu_start(uc, code_start, code_start + sizeof(code), 0, 0));

    uint64_t rip = 0;
    OK(uc_reg_read(uc, UC_X86_REG_RIP, &rip));
    TEST_CHECK(rip == code_start + 2);

    OK(uc_reg_read(uc, UC_X86_REG_RSP, &rsp));
    TEST_CHECK(rsp == code_start + code_len + 0x800 + 8);

    uint64_t rax = 0;
    OK(uc_reg_read(uc, UC_X86_REG_RAX, &rax));
    TEST_CHECK(rax == 0xefcdab9078563412);

    OK(uc_close(uc));
}

static void test_x86_lock_bt_mem(void)
{
    uc_engine *uc;

    // lock bt [rax], eax
    char code[] = "\xf0\x0f\xa3\x00";

    uc_common_setup(&uc, UC_ARCH_X86, UC_MODE_64, code, sizeof(code) - 1);

    OK(uc_mem_map(uc, code_start + code_len, 0x1000, UC_PROT_ALL));
    uc_assert_err(UC_ERR_INSN_INVALID, uc_emu_start(uc, code_start, code_start + sizeof(code) - 1, 0, 0));

    OK(uc_close(uc));
}

static void test_x86_lock_bt_reg(void)
{
    uc_engine *uc;

    // lock bt eax, eax
    char code[] = "\xf0\x0f\xa3\xc0";

    uc_common_setup(&uc, UC_ARCH_X86, UC_MODE_64, code, sizeof(code) - 1);

    OK(uc_mem_map(uc, code_start + code_len, 0x1000, UC_PROT_ALL));
    uc_assert_err(UC_ERR_INSN_INVALID, uc_emu_start(uc, code_start, code_start + sizeof(code) - 1, 0, 0));

    OK(uc_close(uc));
}

static void test_x86_lock_btc_mem(void)
{
    uc_engine *uc;

    // lock btc [rax], ebx
    char code[] = "\xf0\x0f\xbb\x18";

    uc_common_setup(&uc, UC_ARCH_X86, UC_MODE_64, code, sizeof(code) - 1);

    OK(uc_mem_map(uc, code_start + code_len, 0x1000, UC_PROT_ALL));

    char data[1] = "\x80";
    OK(uc_mem_write(uc, code_start + code_len + 0x800, data, sizeof(data)));

    uint64_t rax = code_start + code_len;
    uint64_t rbx = 8*0x800+7;
    OK(uc_reg_write(uc, UC_X86_REG_RAX, &rax));
    OK(uc_reg_write(uc, UC_X86_REG_RBX, &rbx));

    OK(uc_emu_start(uc, code_start, code_start + sizeof(code) - 1, 0, 0));

    uint64_t rflags = 0;
    OK(uc_reg_read(uc, UC_X86_REG_RFLAGS, &rflags));
    TEST_CHECK((rflags & 1) != 0); // RFLAGS.CF must be set

    OK(uc_mem_read(uc, code_start + code_len + 0x800, data, sizeof(data)));
    TEST_CHECK(data[0] == 0);

    OK(uc_close(uc));
}

static void test_x86_lock_btc_reg(void)
{
    uc_engine *uc;

    // lock btc eax, eax
    char code[] = "\xf0\x0f\xbb\xc0";

    uc_common_setup(&uc, UC_ARCH_X86, UC_MODE_64, code, sizeof(code) - 1);

    OK(uc_mem_map(uc, code_start + code_len, 0x1000, UC_PROT_ALL));
    uc_assert_err(UC_ERR_INSN_INVALID, uc_emu_start(uc, code_start, code_start + sizeof(code) - 1, 0, 0));

    OK(uc_close(uc));
}

/*
 * ---- NoVmp U120-U126: AVX-512 state (EVEX milestone M0) ----
 * Each snippet runs from a fresh code address (no stale translation); m0_run returns
 * the exception vector (6 for #UD, 13 for #GP, 19 for #XM) or -1.
 */
#define M0_DATA 0x200000
#define M0_XCR0_AVX512 0xe7ULL

typedef struct M0 {
    uc_engine *uc;
    uc_mode mode;
    X86IntrCapture cap;
    uc_hook hook;
    uint64_t pc;
} M0;

static void m0_open(M0 *m, uc_mode mode, int avx512, const uc_x86_cpuid *prof,
                    size_t nprof)
{
    memset(m, 0, sizeof(*m));
    m->mode = mode;
    m->pc = code_start;
    OK(uc_open(UC_ARCH_X86, mode, &m->uc));
    OK(uc_ctl_set_cpu_model(m->uc, UC_CPU_X86_MAX));   /* as NoVmp and emu-alltest */
    if (avx512) {
        OK(uc_ctl_set_x86_avx512(m->uc, 1));
    }
    if (nprof) {
        OK(uc_ctl_set_x86_cpuid(m->uc, prof, nprof));
    }
    OK(uc_mem_map(m->uc, code_start, code_len, UC_PROT_ALL));
    OK(uc_mem_map(m->uc, M0_DATA, 0x4000, UC_PROT_ALL));
    OK(uc_hook_add(m->uc, &m->hook, UC_HOOK_INTR, test_x86_intr_capture_cb,
                   &m->cap, 1, 0));
}

static void m0_close(M0 *m)
{
    OK(uc_close(m->uc));
}

static void m0_set(M0 *m, int reg, uint64_t v)
{
    if (m->mode == UC_MODE_64) {
        OK(uc_reg_write(m->uc, reg, &v));
    } else {
        uint32_t v32 = (uint32_t)v;
        OK(uc_reg_write(m->uc, reg, &v32));
    }
}

static uint64_t m0_get(M0 *m, int reg)
{
    uint64_t v = 0;
    if (m->mode == UC_MODE_64) {
        OK(uc_reg_read(m->uc, reg, &v));
    } else {
        uint32_t v32 = 0;
        OK(uc_reg_read(m->uc, reg, &v32));
        v = v32;
    }
    return v;
}

static int m0_run(M0 *m, const char *code, size_t len)
{
    uint64_t pc = m->pc;
    uc_err err;

    m->pc += 0x40;
    TEST_CHECK(len <= 0x40 && m->pc <= code_start + code_len);
    m->cap.count = 0;
    OK(uc_mem_write(m->uc, pc, code, len));
    err = uc_emu_start(m->uc, pc, pc + len, 0, 0);
    if (err == UC_ERR_INSN_INVALID) {
        return 6;
    }
    OK(err);
    return m->cap.count ? (int)m->cap.intno : -1;
}

static void m0_cpuid(M0 *m, uint32_t leaf, uint32_t sub, uint32_t r[4])
{
    m0_set(m, UC_X86_REG_EAX, leaf);
    m0_set(m, UC_X86_REG_ECX, sub);
    TEST_CHECK(m0_run(m, "\x0f\xa2", 2) == -1);
    r[0] = (uint32_t)m0_get(m, UC_X86_REG_EAX);
    r[1] = (uint32_t)m0_get(m, UC_X86_REG_EBX);
    r[2] = (uint32_t)m0_get(m, UC_X86_REG_ECX);
    r[3] = (uint32_t)m0_get(m, UC_X86_REG_EDX);
}

static uint64_t m0_xcr0(M0 *m)
{
    uint64_t xcr0 = 0;
    OK(uc_reg_read(m->uc, UC_X86_REG_XCR0, &xcr0));
    return xcr0;
}

/* NoVmp U120: UC_CTL_X86_AVX512 opt-in; reset XCR0 within the CPUID profile's leaf 0DH */
static void test_x86_avx512_optin(void)
{
    static const uc_x86_cpuid prof7[] = {
        {0x0, 0, 0xd, 0x756e6547, 0x6c65746e, 0x49656e69},
        {0xd, 0, 0x7, 0x340, 0x340, 0},
    };
    static const uc_x86_cpuid prof2e7[] = {
        {0x0, 0, 0xd, 0x756e6547, 0x6c65746e, 0x49656e69},
        {0xd, 0, 0x2e7, 0xa88, 0xa88, 0},
    };
    M0 m;
    uint32_t r[4];
    uint64_t xcr0;
    int on = -1;

    /* default: off; CPUID.7.0:EBX.AVX512F = 0, 0DH.0:EAX[7:5] = 0, XCR0[7:5] = 0 */
    m0_open(&m, UC_MODE_64, 0, NULL, 0);
    OK(uc_ctl_get_x86_avx512(m.uc, &on));
    TEST_CHECK(on == 0);
    m0_cpuid(&m, 7, 0, r);
    TEST_CHECK((r[1] & TEST_X86_CPUID_7_0_EBX_AVX512F) == 0);
    m0_cpuid(&m, 0xd, 0, r);
    TEST_CHECK((r[0] & 0xe0) == 0);
    TEST_CHECK((m0_xcr0(&m) & 0xe0) == 0);
    /* the CPU exists now: the switch is fixed */
    uc_assert_err(UC_ERR_ARG, uc_ctl_set_x86_avx512(m.uc, 1));
    m0_close(&m);

    /* on: AVX512F only (no DQ/CD/BW/VL yet), components 5-7, reset XCR0[7:5] = 111b */
    m0_open(&m, UC_MODE_64, 1, NULL, 0);
    OK(uc_ctl_get_x86_avx512(m.uc, &on));
    TEST_CHECK(on == 1);
    m0_cpuid(&m, 7, 0, r);
    TEST_CHECK((r[1] & TEST_X86_CPUID_7_0_EBX_AVX512F) != 0);
    TEST_CHECK((r[1] & (TEST_X86_CPUID_7_0_EBX_AVX512DQ | TEST_X86_CPUID_7_0_EBX_AVX512CD |
                        TEST_X86_CPUID_7_0_EBX_AVX512BW | TEST_X86_CPUID_7_0_EBX_AVX512VL)) == 0);
    m0_cpuid(&m, 0xd, 0, r);
    TEST_CHECK((r[0] & 0xe7) == 0xe7);
    xcr0 = m0_xcr0(&m);
    TEST_CHECK((xcr0 & 0xe7) == 0xe7);
    TEST_MSG("xcr0=%llx", (unsigned long long)xcr0);
    /* a profile set later narrows XCR0 to its leaf 0DH ... */
    OK(uc_ctl_set_x86_cpuid(m.uc, prof7, 2));
    TEST_CHECK(m0_xcr0(&m) == 7);
    m0_close(&m);

    /* ... a profile that has the components keeps them */
    m0_open(&m, UC_MODE_64, 1, NULL, 0);
    OK(uc_ctl_set_x86_cpuid(m.uc, prof2e7, 2));
    TEST_CHECK(m0_xcr0(&m) == (xcr0 & 0x2e7));
    m0_close(&m);

    /* a profile set before init: reset XCR0 within its leaf 0DH */
    m0_open(&m, UC_MODE_64, 1, prof7, 2);
    TEST_CHECK(m0_xcr0(&m) == 7);
    m0_close(&m);

    /* without AVX-512 in the model, a profile listing 7:5 does not add them */
    m0_open(&m, UC_MODE_64, 0, prof2e7, 2);
    TEST_CHECK((m0_xcr0(&m) & 0xe0) == 0);
    TEST_CHECK((m0_xcr0(&m) & 7) == 7);
    m0_close(&m);
}

/* XSETBV XCR0 (ECX = 0) = v; returns the exception vector or -1 */
static int m0_xsetbv(M0 *m, uint32_t ecx, uint64_t v)
{
    m0_set(m, UC_X86_REG_ECX, ecx);
    m0_set(m, UC_X86_REG_EAX, (uint32_t)v);
    m0_set(m, UC_X86_REG_EDX, (uint32_t)(v >> 32));
    return m0_run(m, "\x0f\x01\xd1", 3);
}

static uint64_t m0_xgetbv(M0 *m, uint32_t ecx)
{
    m0_set(m, UC_X86_REG_ECX, ecx);
    TEST_CHECK(m0_run(m, "\x0f\x01\xd0", 3) == -1);
    return (m0_get(m, UC_X86_REG_EAX) & 0xffffffffULL) |
           (m0_get(m, UC_X86_REG_EDX) << 32);
}

/* NoVmp U122: XSETBV rules for XCR0[7:5] (SDM Vol1 13.3) and the supported mask */
static void test_x86_avx512_xsetbv(void)
{
    static const struct {
        uint64_t xcr0;
        int ok;
    } avx512_on[] = {
        {0xe7, 1}, {0x07, 1}, {0x03, 1}, {0x01, 1},
        {0x27, 0}, {0x47, 0}, {0x87, 0}, {0x67, 0}, {0xa7, 0}, {0xc7, 0}, /* 7:5 partial */
        {0xe3, 0}, {0xe5, 0}, {0xe1, 0},          /* 7:5 without 2:1 = 11b */
        {0xe6, 0}, {0x00, 0}, {0x05, 0},          /* bit 0 clear, YMM without SSE */
        {0x1e7, 0},                               /* bit 8 (PT) is not an XCR0 bit */
        {0xe7, 1}, {0x07, 1},                     /* still fine after the faults */
    };
    static const uc_x86_cpuid prof207[] = {
        {0x0, 0, 0xd, 0x756e6547, 0x6c65746e, 0x49656e69},
        {0xd, 0, 0x207, 0x340, 0xa88, 0},
    };
    static const uc_x86_cpuid prof2e7[] = {
        {0x0, 0, 0xd, 0x756e6547, 0x6c65746e, 0x49656e69},
        {0xd, 0, 0x2e7, 0xa88, 0xa88, 0},
    };
    M0 m;
    size_t i;
    int v;

    for (i = 0; i < 2; i++) {
        size_t j;
        m0_open(&m, i ? UC_MODE_32 : UC_MODE_64, 1, NULL, 0);
        for (j = 0; j < sizeof(avx512_on) / sizeof(avx512_on[0]); j++) {
            uint64_t before = m0_xcr0(&m);
            v = m0_xsetbv(&m, 0, avx512_on[j].xcr0);
            TEST_CHECK(v == (avx512_on[j].ok ? -1 : 13));
            TEST_MSG("mode %d xcr0 %llx -> %d", (int)i, (unsigned long long)avx512_on[j].xcr0, v);
            TEST_CHECK(m0_xgetbv(&m, 0) == (avx512_on[j].ok ? avx512_on[j].xcr0 : before));
        }
        /* only XCR0 exists */
        TEST_CHECK(m0_xsetbv(&m, 1, 0xe7) == 13);
        m0_close(&m);
    }

    /* AVX-512 off: 7:5 are unsupported */
    m0_open(&m, UC_MODE_64, 0, NULL, 0);
    TEST_CHECK(m0_xsetbv(&m, 0, 0xe7) == 13);
    TEST_CHECK(m0_xsetbv(&m, 0, 0x07) == -1);
    m0_close(&m);

    /* model with AVX-512, profile without (i5-13600K leaf 0DH): #GP */
    m0_open(&m, UC_MODE_64, 1, prof207, 2);
    TEST_CHECK(m0_xsetbv(&m, 0, 0xe7) == 13);
    TEST_CHECK(m0_xsetbv(&m, 0, 0x207) == -1);
    m0_close(&m);

    /* the supported mask is the profile's leaf 0DH: 7:5 with 2:1 accepted, partial not */
    m0_open(&m, UC_MODE_64, 1, prof2e7, 2);
    TEST_CHECK(m0_xsetbv(&m, 0, 0x2e7) == -1);
    TEST_CHECK(m0_xgetbv(&m, 0) == 0x2e7);
    TEST_CHECK(m0_xsetbv(&m, 0, 0x267) == 13);
    TEST_CHECK(m0_xgetbv(&m, 0) == 0x2e7);
    m0_close(&m);
}

static void m0_zmm_pattern(uint64_t z[8], unsigned n, unsigned salt)
{
    unsigned i;
    for (i = 0; i < 8; i++) {
        z[i] = 0x0101010101010101ULL * (n + 1) ^ ((uint64_t)(i + 1) << 56) ^
               ((uint64_t)salt << 40) ^ (0x1000 + i);
    }
}

/* NoVmp U123: UC_X86_REG_ZMM0..7 in 32-bit (and 16-bit) mode */
static void test_x86_zmm_api_32(void)
{
    static const uc_mode modes[] = {UC_MODE_16, UC_MODE_32, UC_MODE_64};
    size_t k;

    for (k = 0; k < 3; k++) {
        uc_engine *uc;
        unsigned n;

        OK(uc_open(UC_ARCH_X86, modes[k], &uc));
        for (n = 0; n < 8; n++) {
            uint64_t z[8], out[8], y[4], x[2];
            m0_zmm_pattern(z, n, 0x5a);
            OK(uc_reg_write(uc, UC_X86_REG_ZMM0 + n, z));
            memset(out, 0, sizeof(out));
            OK(uc_reg_read(uc, UC_X86_REG_ZMM0 + n, out));
            TEST_CHECK(memcmp(out, z, sizeof(z)) == 0);
            OK(uc_reg_read(uc, UC_X86_REG_YMM0 + n, y));
            TEST_CHECK(memcmp(y, z, sizeof(y)) == 0);
            OK(uc_reg_read(uc, UC_X86_REG_XMM0 + n, x));
            TEST_CHECK(memcmp(x, z, sizeof(x)) == 0);
            /* a YMM write keeps bits 511:256 */
            y[0] = ~y[0];
            y[3] = ~y[3];
            OK(uc_reg_write(uc, UC_X86_REG_YMM0 + n, y));
            OK(uc_reg_read(uc, UC_X86_REG_ZMM0 + n, out));
            TEST_CHECK(out[0] == ~z[0] && out[3] == ~z[3] && out[4] == z[4] && out[7] == z[7]);
        }
        OK(uc_close(uc));
    }
}

static uint64_t m0_kpat(unsigned i, unsigned salt)
{
    return 0x0123456789abcdefULL * (i + 1) ^ ((uint64_t)salt << 8);
}

/* ZMM0..nzmm-1 and k0-7 from the patterns */
static void m0_put_state(M0 *m, unsigned salt, unsigned nzmm)
{
    unsigned n;
    for (n = 0; n < nzmm; n++) {
        uint64_t z[8];
        m0_zmm_pattern(z, n, salt);
        OK(uc_reg_write(m->uc, UC_X86_REG_ZMM0 + n, z));
    }
    for (n = 0; n < 8; n++) {
        uint64_t k = m0_kpat(n, salt);
        OK(uc_reg_write(m->uc, UC_X86_REG_K0 + n, &k));
    }
}

static void m0_zmm(M0 *m, unsigned n, uint64_t z[8])
{
    OK(uc_reg_read(m->uc, UC_X86_REG_ZMM0 + n, z));
}

static uint64_t m0_k(M0 *m, unsigned n)
{
    uint64_t k;
    OK(uc_reg_read(m->uc, UC_X86_REG_K0 + n, &k));
    return k;
}

/* quadwords [lo, hi) of ZMMn equal the pattern (salt), the others equal `other` */
static int m0_zmm_is(M0 *m, unsigned n, unsigned salt, int lo, int hi, uint64_t other)
{
    uint64_t z[8], p[8];
    int i;
    m0_zmm(m, n, z);
    m0_zmm_pattern(p, n, salt);
    for (i = 0; i < 8; i++) {
        if (z[i] != ((i >= lo && i < hi) ? p[i] : other)) {
            return 0;
        }
    }
    return 1;
}

static uint64_t m0_rd64(const uint8_t *a, size_t o)
{
    uint64_t v;
    memcpy(&v, a + o, 8);
    return v;
}

static int m0_all_bytes(const uint8_t *a, size_t o, size_t len, uint8_t b)
{
    size_t i;
    for (i = 0; i < len; i++) {
        if (a[o + i] != b) {
            return 0;
        }
    }
    return 1;
}

/* XSAVE-area image at M0_DATA: 0xCC everywhere except a zero header */
static void m0_area_reset(M0 *m)
{
    uint8_t buf[0xb00];
    memset(buf, 0xcc, sizeof(buf));
    memset(buf + 512, 0, 64);
    OK(uc_mem_write(m->uc, M0_DATA, buf, sizeof(buf)));
}

/* run an XSAVE-family instruction on [rSI] = M0_DATA with EDX:EAX = rfbm */
static int m0_xop(M0 *m, const char *code, size_t len, uint64_t rfbm)
{
    m0_set(m, UC_X86_REG_ESI, M0_DATA);
    if (m->mode == UC_MODE_64) {
        m0_set(m, UC_X86_REG_RSI, M0_DATA);
    }
    m0_set(m, UC_X86_REG_EAX, (uint32_t)rfbm);
    m0_set(m, UC_X86_REG_EDX, (uint32_t)(rfbm >> 32));
    return m0_run(m, code, len);
}

#define M0_XSAVE "\x0f\xae\x26"
#define M0_XRSTOR "\x0f\xae\x2e"
#define M0_XSAVEOPT "\x0f\xae\x36"
#define M0_XSAVEC "\x0f\xc7\x26"

/* the standard-form sections of components 5-7 hold the pattern (64-bit mode) */
static void m0_check_std_sections(const uint8_t *a, unsigned salt, unsigned nhi256, int hi16)
{
    unsigned i, j;
    uint64_t p[8];
    for (i = 0; i < 8; i++) {
        TEST_CHECK(m0_rd64(a, 0x440 + 8 * i) == m0_kpat(i, salt));
    }
    for (i = 0; i < nhi256; i++) {
        m0_zmm_pattern(p, i, salt);
        for (j = 0; j < 4; j++) {
            TEST_CHECK(m0_rd64(a, 0x480 + 32 * i + 8 * j) == p[4 + j]);
        }
    }
    for (i = 16; hi16 && i < 32; i++) {
        m0_zmm_pattern(p, i, salt);
        for (j = 0; j < 8; j++) {
            TEST_CHECK(m0_rd64(a, 0x680 + 64 * (i - 16) + 8 * j) == p[j]);
        }
    }
}

/* NoVmp U124: XSAVE / XSAVEOPT / XRSTOR (standard form), components 5-7, 64-bit */
static void test_x86_avx512_xsave(void)
{
    uint8_t a[0xb00];
    uint64_t z[8];
    M0 m;
    unsigned i;

    m0_open(&m, UC_MODE_64, 1, NULL, 0);
    TEST_CHECK(m0_xsetbv(&m, 0, M0_XCR0_AVX512) == -1);
    m0_put_state(&m, 0x5a, 32);

    /* XSAVE: XSTATE_BV = E7h, XCOMP_BV = 0, sections at 440h/480h/680h */
    m0_area_reset(&m);
    TEST_CHECK(m0_xop(&m, M0_XSAVE, 3, ~0ULL) == -1);
    OK(uc_mem_read(m.uc, M0_DATA, a, sizeof(a)));
    TEST_CHECK(m0_rd64(a, 512) == 0xe7);
    TEST_CHECK(m0_rd64(a, 520) == 0);
    m0_check_std_sections(a, 0x5a, 16, 1);
    for (i = 0; i < 16; i++) {          /* low halves in the legacy / AVX sections */
        uint64_t p[8];
        m0_zmm_pattern(p, i, 0x5a);
        TEST_CHECK(m0_rd64(a, 160 + 16 * i) == p[0] && m0_rd64(a, 0x240 + 16 * i + 8) == p[3]);
    }
    TEST_CHECK(m0_all_bytes(a, 0xa80, 0x80, 0xcc));   /* nothing past Hi16_ZMM */

    /* scribble, XRSTOR: everything back */
    m0_put_state(&m, 0xa5, 32);
    TEST_CHECK(m0_xop(&m, M0_XRSTOR, 3, ~0ULL) == -1);
    for (i = 0; i < 32; i++) {
        TEST_CHECK(m0_zmm_is(&m, i, 0x5a, 0, 8, 0));
        TEST_MSG("zmm%u", i);
    }
    for (i = 0; i < 8; i++) {
        TEST_CHECK(m0_k(&m, i) == m0_kpat(i, 0x5a));
    }

    /* XSTATE_BV[7:5] = 0: k0-7, ZMM0-15 bits 511:256 and ZMM16-31 initialised */
    {
        uint64_t bv = 0x07;
        OK(uc_mem_write(m.uc, M0_DATA + 512, &bv, 8));
    }
    m0_put_state(&m, 0xa5, 32);
    TEST_CHECK(m0_xop(&m, M0_XRSTOR, 3, ~0ULL) == -1);
    for (i = 0; i < 8; i++) {
        TEST_CHECK(m0_k(&m, i) == 0);
    }
    for (i = 0; i < 16; i++) {
        TEST_CHECK(m0_zmm_is(&m, i, 0x5a, 0, 4, 0));
    }
    for (i = 16; i < 32; i++) {
        TEST_CHECK(m0_zmm_is(&m, i, 0, 0, 0, 0));
    }

    /* RFBM = opmask only: only k0-7 reloaded */
    {
        uint64_t bv = 0xe7;
        OK(uc_mem_write(m.uc, M0_DATA + 512, &bv, 8));
    }
    m0_put_state(&m, 0xa5, 32);
    TEST_CHECK(m0_xop(&m, M0_XRSTOR, 3, 0x20) == -1);
    for (i = 0; i < 8; i++) {
        TEST_CHECK(m0_k(&m, i) == m0_kpat(i, 0x5a));
    }
    for (i = 0; i < 32; i++) {
        TEST_CHECK(m0_zmm_is(&m, i, 0xa5, 0, 8, 0));
    }

    /* XINUSE (value-based for 5-7): all AVX-512 state zero -> XSTATE_BV[7:5] = 0 */
    memset(z, 0, sizeof(z));
    for (i = 0; i < 32; i++) {
        OK(uc_reg_write(m.uc, UC_X86_REG_ZMM0 + i, z));
    }
    for (i = 0; i < 8; i++) {
        OK(uc_reg_write(m.uc, UC_X86_REG_K0 + i, &z[0]));
    }
    m0_area_reset(&m);
    TEST_CHECK(m0_xop(&m, M0_XSAVE, 3, ~0ULL) == -1);
    OK(uc_mem_read(m.uc, M0_DATA, a, sizeof(a)));
    TEST_CHECK(m0_rd64(a, 512) == 0x07);
    TEST_CHECK(m0_all_bytes(a, 0x440, 0x640, 0));  /* XSAVE still writes the sections */
    TEST_CHECK(m0_xgetbv(&m, 1) == 0x07);           /* XINUSE & XCR0 */
    /* XSAVEOPT leaves sections of components not in use alone */
    m0_area_reset(&m);
    TEST_CHECK(m0_xop(&m, M0_XSAVEOPT, 3, ~0ULL) == -1);
    OK(uc_mem_read(m.uc, M0_DATA, a, sizeof(a)));
    TEST_CHECK(m0_rd64(a, 512) == 0x07);
    TEST_CHECK(m0_all_bytes(a, 0x440, 0x640, 0xcc));
    /* only k3 != 0 -> opmask in use; one ZMM_H quadword -> ZMM_Hi256; ZMM31 -> Hi16_ZMM */
    z[0] = 1;
    OK(uc_reg_write(m.uc, UC_X86_REG_K3, &z[0]));
    TEST_CHECK(m0_xgetbv(&m, 1) == 0x27);
    z[0] = 0;
    z[7] = 0x8000000000000000ULL;
    OK(uc_reg_write(m.uc, UC_X86_REG_ZMM15, z));
    TEST_CHECK(m0_xgetbv(&m, 1) == 0x67);
    OK(uc_reg_write(m.uc, UC_X86_REG_ZMM31, z));
    TEST_CHECK(m0_xgetbv(&m, 1) == 0xe7);
    m0_close(&m);
}

/* NoVmp U124: XSAVEC and the compacted XRSTOR with components 5-7, 64-bit */
static void test_x86_avx512_xsavec(void)
{
    uint8_t a[0xb00];
    uint64_t p[8];
    M0 m;
    unsigned i, j;

    m0_open(&m, UC_MODE_64, 1, NULL, 0);
    TEST_CHECK(m0_xsetbv(&m, 0, M0_XCR0_AVX512) == -1);
    m0_put_state(&m, 0x5a, 32);

    /* RFBM = all: AVX 240h, opmask 340h, ZMM_Hi256 380h, Hi16_ZMM 580h..980h */
    m0_area_reset(&m);
    TEST_CHECK(m0_xop(&m, M0_XSAVEC, 3, ~0ULL) == -1);
    OK(uc_mem_read(m.uc, M0_DATA, a, sizeof(a)));
    TEST_CHECK(m0_rd64(a, 512) == 0xe7);
    TEST_CHECK(m0_rd64(a, 520) == 0x80000000000000e7ULL);
    for (i = 0; i < 8; i++) {
        TEST_CHECK(m0_rd64(a, 0x340 + 8 * i) == m0_kpat(i, 0x5a));
    }
    for (i = 0; i < 16; i++) {
        m0_zmm_pattern(p, i, 0x5a);
        TEST_CHECK(m0_rd64(a, 0x240 + 16 * i) == p[2]);
        for (j = 0; j < 4; j++) {
            TEST_CHECK(m0_rd64(a, 0x380 + 32 * i + 8 * j) == p[4 + j]);
        }
    }
    for (i = 16; i < 32; i++) {
        m0_zmm_pattern(p, i, 0x5a);
        for (j = 0; j < 8; j++) {
            TEST_CHECK(m0_rd64(a, 0x580 + 64 * (i - 16) + 8 * j) == p[j]);
        }
    }
    TEST_CHECK(m0_all_bytes(a, 0x980, 0x180, 0xcc));
    m0_put_state(&m, 0xa5, 32);
    TEST_CHECK(m0_xop(&m, M0_XRSTOR, 3, ~0ULL) == -1);
    for (i = 0; i < 32; i++) {
        TEST_CHECK(m0_zmm_is(&m, i, 0x5a, 0, 8, 0));
    }
    for (i = 0; i < 8; i++) {
        TEST_CHECK(m0_k(&m, i) == m0_kpat(i, 0x5a));
    }

    /* RFBM = A1h (x87, opmask, Hi16_ZMM): opmask at 240h, Hi16_ZMM at 280h */
    m0_area_reset(&m);
    TEST_CHECK(m0_xop(&m, M0_XSAVEC, 3, 0xa1) == -1);
    OK(uc_mem_read(m.uc, M0_DATA, a, sizeof(a)));
    TEST_CHECK(m0_rd64(a, 512) == 0xa1);
    TEST_CHECK(m0_rd64(a, 520) == 0x80000000000000a1ULL);
    TEST_CHECK(m0_rd64(a, 0x240) == m0_kpat(0, 0x5a) && m0_rd64(a, 0x278) == m0_kpat(7, 0x5a));
    m0_zmm_pattern(p, 16, 0x5a);
    TEST_CHECK(m0_rd64(a, 0x280) == p[0]);
    m0_zmm_pattern(p, 31, 0x5a);
    TEST_CHECK(m0_rd64(a, 0x280 + 0x3c0 + 56) == p[7]);
    TEST_CHECK(m0_all_bytes(a, 0x680, 0x480, 0xcc));
    /* XRSTOR with RFBM = all: components outside XCOMP_BV are initialised */
    m0_put_state(&m, 0xa5, 32);
    TEST_CHECK(m0_xop(&m, M0_XRSTOR, 3, ~0ULL) == -1);
    for (i = 0; i < 16; i++) {
        TEST_CHECK(m0_zmm_is(&m, i, 0, 0, 0, 0));    /* SSE, AVX, ZMM_Hi256 init */
    }
    for (i = 16; i < 32; i++) {
        TEST_CHECK(m0_zmm_is(&m, i, 0x5a, 0, 8, 0));
    }
    for (i = 0; i < 8; i++) {
        TEST_CHECK(m0_k(&m, i) == m0_kpat(i, 0x5a));
    }

    /* compacted, XSTATE_BV[7:5] = 0 with XCOMP_BV[7:5] = 1: init */
    m0_put_state(&m, 0x5a, 32);
    m0_area_reset(&m);
    TEST_CHECK(m0_xop(&m, M0_XSAVEC, 3, ~0ULL) == -1);
    {
        uint64_t bv = 0x07;
        OK(uc_mem_write(m.uc, M0_DATA + 512, &bv, 8));
    }
    m0_put_state(&m, 0xa5, 32);
    TEST_CHECK(m0_xop(&m, M0_XRSTOR, 3, ~0ULL) == -1);
    for (i = 0; i < 8; i++) {
        TEST_CHECK(m0_k(&m, i) == 0);
    }
    for (i = 0; i < 16; i++) {
        TEST_CHECK(m0_zmm_is(&m, i, 0x5a, 0, 4, 0));
    }
    for (i = 16; i < 32; i++) {
        TEST_CHECK(m0_zmm_is(&m, i, 0, 0, 0, 0));
    }

    /* XSAVEC with AVX-512 state in its initial configuration: XSTATE_BV[7:5] = 0,
       the sections are not written but still occupy their compacted space */
    m0_area_reset(&m);
    TEST_CHECK(m0_xop(&m, M0_XSAVEC, 3, ~0ULL) == -1);
    OK(uc_mem_read(m.uc, M0_DATA, a, sizeof(a)));
    TEST_CHECK(m0_rd64(a, 512) == 0x07);
    TEST_CHECK(m0_rd64(a, 520) == 0x80000000000000e7ULL);
    TEST_CHECK(m0_all_bytes(a, 0x340, 0x640, 0xcc));
    m0_close(&m);
}

/* NoVmp U124: outside 64-bit mode only ZMM0_H-ZMM7_H; Hi16_ZMM neither saved nor loaded */
static void test_x86_avx512_xsave_32(void)
{
    uint8_t a[0xb00];
    M0 m;
    unsigned i;

    m0_open(&m, UC_MODE_32, 1, NULL, 0);
    TEST_CHECK(m0_xsetbv(&m, 0, M0_XCR0_AVX512) == -1);
    m0_put_state(&m, 0x5a, 8);

    m0_area_reset(&m);
    TEST_CHECK(m0_xop(&m, M0_XSAVE, 3, ~0ULL) == -1);
    OK(uc_mem_read(m.uc, M0_DATA, a, sizeof(a)));
    TEST_CHECK(m0_rd64(a, 512) == 0x67);       /* Hi16_ZMM always initial here */
    m0_check_std_sections(a, 0x5a, 8, 0);
    TEST_CHECK(m0_all_bytes(a, 0x480 + 256, 256, 0xcc));
    TEST_CHECK(m0_all_bytes(a, 0x680, 0x400, 0xcc));
    TEST_CHECK(m0_all_bytes(a, 160 + 128, 128, 0xcc));   /* XMM8-15 slots */

    m0_put_state(&m, 0xa5, 8);
    TEST_CHECK(m0_xop(&m, M0_XRSTOR, 3, ~0ULL) == -1);
    for (i = 0; i < 8; i++) {
        TEST_CHECK(m0_zmm_is(&m, i, 0x5a, 0, 8, 0));
        TEST_CHECK(m0_k(&m, i) == m0_kpat(i, 0x5a));
    }

    /* compacted: same offsets as in 64-bit mode, upper half of ZMM_Hi256 untouched */
    m0_area_reset(&m);
    TEST_CHECK(m0_xop(&m, M0_XSAVEC, 3, ~0ULL) == -1);
    OK(uc_mem_read(m.uc, M0_DATA, a, sizeof(a)));
    TEST_CHECK(m0_rd64(a, 512) == 0x67);
    TEST_CHECK(m0_rd64(a, 520) == 0x80000000000000e7ULL);
    TEST_CHECK(m0_rd64(a, 0x340) == m0_kpat(0, 0x5a));
    {
        uint64_t p[8];
        m0_zmm_pattern(p, 7, 0x5a);
        TEST_CHECK(m0_rd64(a, 0x380 + 32 * 7 + 24) == p[7]);
    }
    TEST_CHECK(m0_all_bytes(a, 0x380 + 256, 256, 0xcc));
    TEST_CHECK(m0_all_bytes(a, 0x580, 0x400, 0xcc));
    m0_put_state(&m, 0xa5, 8);
    TEST_CHECK(m0_xop(&m, M0_XRSTOR, 3, ~0ULL) == -1);
    for (i = 0; i < 8; i++) {
        TEST_CHECK(m0_zmm_is(&m, i, 0x5a, 0, 8, 0));
    }

    /* init: XSTATE_BV = 7 -> k and ZMM0-7_H zero */
    {
        uint64_t bv = 0x07;
        OK(uc_mem_write(m.uc, M0_DATA + 512, &bv, 8));
    }
    TEST_CHECK(m0_xop(&m, M0_XRSTOR, 3, ~0ULL) == -1);
    for (i = 0; i < 8; i++) {
        TEST_CHECK(m0_zmm_is(&m, i, 0x5a, 0, 4, 0));
        TEST_CHECK(m0_k(&m, i) == 0);
    }
    m0_close(&m);
}

/* NoVmp U125: CPUID leaf 0DH with AVX-512 on / off; EBX follows XCR0 (model, profile) */
static void test_x86_avx512_cpuid(void)
{
    /* the i5-13600K rows of leaf 0DH (Emulator\data\cpuid_i5-13600k.txt) */
    static const uc_x86_cpuid prof13600k[] = {
        {0x0, 0, 0x20, 0x756e6547, 0x6c65746e, 0x49656e69},
        {0xd, 0, 0x207, 0x340, 0xa88, 0},
        {0xd, 1, 0xf, 0x350, 0x1800, 0},
        {0xd, 2, 0x100, 0x240, 0, 0},
        {0xd, 9, 0x8, 0xa80, 0, 0},
    };
    /* the same with AVX-512 state (Sapphire Rapids-like offsets) */
    static const uc_x86_cpuid profavx512[] = {
        {0x0, 0, 0x20, 0x756e6547, 0x6c65746e, 0x49656e69},
        {0xd, 0, 0x2e7, 0xa88, 0xa88, 0},
        {0xd, 1, 0xf, 0xa88, 0x1800, 0},
        {0xd, 2, 0x100, 0x240, 0, 0},
        {0xd, 5, 0x40, 0x440, 0, 0},
        {0xd, 6, 0x200, 0x480, 0, 0},
        {0xd, 7, 0x400, 0x680, 0, 0},
        {0xd, 9, 0x8, 0xa80, 0, 0},
    };
    static const uint32_t sub[3][2] = {{0x40, 0x440}, {0x200, 0x480}, {0x400, 0x680}};
    M0 m;
    uint32_t r[4];
    uint64_t x;
    unsigned i;

    /* model, AVX-512 on */
    m0_open(&m, UC_MODE_64, 1, NULL, 0);
    TEST_CHECK(m0_xsetbv(&m, 0, M0_XCR0_AVX512) == -1);
    m0_cpuid(&m, 0xd, 0, r);
    TEST_CHECK((r[0] & 0xe7) == 0xe7 && r[3] == 0);
    TEST_CHECK(r[1] == 0xa80);                     /* XCR0 E7h: end of Hi16_ZMM */
    TEST_CHECK(r[2] >= 0xa80);                     /* all supported */
    m0_cpuid(&m, 0xd, 1, r);
    TEST_CHECK(r[1] == 0x980);                     /* XSAVEC size for XCR0 E7h */
    for (i = 0; i < 3; i++) {
        m0_cpuid(&m, 0xd, 5 + i, r);
        TEST_CHECK(r[0] == sub[i][0] && r[1] == sub[i][1] && r[2] == 0 && r[3] == 0);
        TEST_MSG("0DH.%u: %x %x %x %x", 5 + i, r[0], r[1], r[2], r[3]);
    }
    TEST_CHECK(m0_xsetbv(&m, 0, 7) == -1);
    m0_cpuid(&m, 0xd, 0, r);
    TEST_CHECK(r[1] == 0x340);
    m0_cpuid(&m, 0xd, 1, r);
    TEST_CHECK(r[1] == 0x340);
    m0_close(&m);

    /* model, AVX-512 off: no components 5-7 */
    m0_open(&m, UC_MODE_64, 0, NULL, 0);
    m0_cpuid(&m, 0xd, 0, r);
    TEST_CHECK((r[0] & 0xe0) == 0);
    for (i = 5; i < 8; i++) {
        m0_cpuid(&m, 0xd, i, r);
        TEST_CHECK(r[0] == 0 && r[1] == 0 && r[2] == 0 && r[3] == 0);
    }
    m0_close(&m);

    /* i5-13600K profile: EBX from XCR0 and the profile's offsets */
    m0_open(&m, UC_MODE_64, 0, prof13600k, sizeof(prof13600k) / sizeof(prof13600k[0]));
    for (i = 0; i < 3; i++) {
        static const uint64_t xs[3] = {7, 0x207, 3};
        static const uint32_t ebx[3] = {0x340, 0xa88, 0x240};
        x = xs[i];
        OK(uc_reg_write(m.uc, UC_X86_REG_XCR0, &x));
        m0_cpuid(&m, 0xd, 0, r);
        TEST_CHECK(r[0] == 0x207 && r[1] == ebx[i] && r[2] == 0xa88 && r[3] == 0);
        TEST_MSG("xcr0 %llx: ebx %x", (unsigned long long)x, r[1]);
    }
    m0_cpuid(&m, 0xd, 1, r);
    TEST_CHECK(r[1] == 0x350);                     /* profile value (IA32_XSS unknown) */
    m0_close(&m);

    /* AVX-512 profile + model */
    m0_open(&m, UC_MODE_64, 1, profavx512, sizeof(profavx512) / sizeof(profavx512[0]));
    TEST_CHECK(m0_xsetbv(&m, 0, M0_XCR0_AVX512) == -1);
    m0_cpuid(&m, 0xd, 0, r);
    TEST_CHECK(r[0] == 0x2e7 && r[1] == 0xa80 && r[2] == 0xa88);
    TEST_CHECK(m0_xsetbv(&m, 0, 0x2e7) == -1);
    m0_cpuid(&m, 0xd, 0, r);
    TEST_CHECK(r[1] == 0xa88);
    m0_cpuid(&m, 0xd, 6, r);
    TEST_CHECK(r[0] == 0x200 && r[1] == 0x480 && r[2] == 0);
    m0_close(&m);
}

/* NoVmp U126: VEX.128/256 register writes zero bits 511:VL; legacy SSE keeps 511:128 */
static void test_x86_vex_zero_maxvl(void)
{
    int on;

    for (on = 0; on < 2; on++) {
        M0 m;
        uint64_t p2[8], p3[8], z[8];
        unsigned i;

        m0_open(&m, UC_MODE_64, on, NULL, 0);

        /* vpxor xmm1, xmm2, xmm3 (VEX.128): 127:0 result, 511:128 zero */
        m0_put_state(&m, 0x11, 32);
        m0_zmm_pattern(p2, 2, 0x11);
        m0_zmm_pattern(p3, 3, 0x11);
        TEST_CHECK(m0_run(&m, "\xc5\xe9\xef\xcb", 4) == -1);
        m0_zmm(&m, 1, z);
        TEST_CHECK(z[0] == (p2[0] ^ p3[0]) && z[1] == (p2[1] ^ p3[1]));
        for (i = 2; i < 8; i++) {
            TEST_CHECK(z[i] == 0);
        }

        /* vpxor ymm1, ymm2, ymm3 (VEX.256): 255:0 result, 511:256 zero */
        m0_put_state(&m, 0x11, 32);
        TEST_CHECK(m0_run(&m, "\xc5\xed\xef\xcb", 4) == -1);
        m0_zmm(&m, 1, z);
        for (i = 0; i < 4; i++) {
            TEST_CHECK(z[i] == (p2[i] ^ p3[i]));
        }
        for (i = 4; i < 8; i++) {
            TEST_CHECK(z[i] == 0);
        }

        /* pxor xmm1, xmm3 (legacy SSE): bits 511:128 kept */
        m0_put_state(&m, 0x11, 32);
        TEST_CHECK(m0_run(&m, "\x66\x0f\xef\xcb", 4) == -1);
        {
            uint64_t p1[8];
            m0_zmm_pattern(p1, 1, 0x11);
            m0_zmm(&m, 1, z);
            TEST_CHECK(z[0] == (p1[0] ^ p3[0]) && z[1] == (p1[1] ^ p3[1]));
            for (i = 2; i < 8; i++) {
                TEST_CHECK(z[i] == p1[i]);
            }
        }

        /* vaddss xmm1, xmm2, xmm3 (VEX scalar): 127:32 from xmm2, 511:128 zero */
        m0_put_state(&m, 0x11, 32);
        TEST_CHECK(m0_run(&m, "\xc5\xea\x58\xcb", 4) == -1);
        m0_zmm(&m, 1, z);
        TEST_CHECK(z[1] == p2[1] && (z[0] >> 32) == (p2[0] >> 32));
        for (i = 2; i < 8; i++) {
            TEST_CHECK(z[i] == 0);
        }

        /* vmovups ymm4, ymm5 (VEX.256 store form, register destination) */
        m0_put_state(&m, 0x11, 32);
        TEST_CHECK(m0_run(&m, "\xc5\xfc\x11\xec", 4) == -1);
        {
            uint64_t p5[8];
            m0_zmm_pattern(p5, 5, 0x11);
            m0_zmm(&m, 4, z);
            TEST_CHECK(z[0] == p5[0] && z[3] == p5[3] && z[4] == 0 && z[7] == 0);
        }

        /* vzeroupper: ZMM0-15 bits 511:128 zero, ZMM16-31 untouched */
        m0_put_state(&m, 0x22, 32);
        TEST_CHECK(m0_run(&m, "\xc5\xf8\x77", 3) == -1);
        for (i = 0; i < 16; i++) {
            TEST_CHECK(m0_zmm_is(&m, i, 0x22, 0, 2, 0));
        }
        for (i = 16; i < 32; i++) {
            TEST_CHECK(m0_zmm_is(&m, i, 0x22, 0, 8, 0));
        }

        /* vzeroall: ZMM0-15 zero, ZMM16-31 untouched */
        m0_put_state(&m, 0x22, 32);
        TEST_CHECK(m0_run(&m, "\xc5\xfc\x77", 3) == -1);
        for (i = 0; i < 16; i++) {
            TEST_CHECK(m0_zmm_is(&m, i, 0, 0, 0, 0));
        }
        for (i = 16; i < 32; i++) {
            TEST_CHECK(m0_zmm_is(&m, i, 0x22, 0, 8, 0));
        }

        /* vpgatherdd xmm1, [rax + xmm2*4], xmm3 and the ymm form: destination and
           mask both have bits 511:VL zeroed */
        for (i = 0; i < 2; i++) {
            static const char g128[] = "\xc4\xe2\x61\x90\x0c\x90";
            static const char g256[] = "\xc4\xe2\x65\x90\x0c\x90";
            uint64_t idx[8] = {0x0000000100000000ULL, 0x0000000300000002ULL,
                               0x0000000500000004ULL, 0x0000000700000006ULL, 0, 0, 0, 0};
            uint32_t mem[8] = {0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7};
            unsigned vlq = i ? 4 : 2, j;

            m0_put_state(&m, 0x33, 32);
            m0_zmm_pattern(z, 3, 0x33);
            for (j = 0; j < vlq; j++) {
                z[j] = ~0ULL;                       /* gather every element */
            }
            OK(uc_reg_write(m.uc, UC_X86_REG_ZMM3, z));
            m0_zmm_pattern(z, 2, 0x33);
            memcpy(z, idx, 32);
            OK(uc_reg_write(m.uc, UC_X86_REG_ZMM2, z));
            OK(uc_mem_write(m.uc, M0_DATA, mem, sizeof(mem)));
            m0_set(&m, UC_X86_REG_RAX, M0_DATA);
            TEST_CHECK(m0_run(&m, i ? g256 : g128, 6) == -1);
            m0_zmm(&m, 1, z);
            TEST_CHECK(z[0] == 0x000000a1000000a0ULL && z[vlq - 1] != 0);
            for (j = vlq; j < 8; j++) {
                TEST_CHECK(z[j] == 0);
            }
            m0_zmm(&m, 3, z);
            for (j = 0; j < 8; j++) {
                TEST_CHECK(z[j] == 0);
            }
        }

        /* vpcmpistrm xmm1, xmm2, 0 (VEX.128): XMM0 bits 511:128 zero */
        m0_put_state(&m, 0x44, 32);
        TEST_CHECK(m0_run(&m, "\xc4\xe3\x79\x62\xca\x00", 6) == -1);
        m0_zmm(&m, 0, z);
        for (i = 2; i < 8; i++) {
            TEST_CHECK(z[i] == 0);
        }

        /* vdivps ymm1, ymm2, ymm3 with ZM unmasked: #XM, ZMM1 entirely unchanged */
        {
            uint64_t ones[8], zero[8] = {0};
            uint32_t mxcsr = 0x1f80 & ~0x200u;
            for (i = 0; i < 8; i++) {
                ones[i] = 0x3f8000003f800000ULL;
            }
            m0_put_state(&m, 0x55, 32);
            OK(uc_reg_write(m.uc, UC_X86_REG_ZMM2, ones));
            OK(uc_reg_write(m.uc, UC_X86_REG_ZMM3, zero));
            OK(uc_reg_write(m.uc, UC_X86_REG_MXCSR, &mxcsr));
            TEST_CHECK(m0_run(&m, "\xc5\xec\x5e\xcb", 4) == 19);
            TEST_CHECK(m0_zmm_is(&m, 1, 0x55, 0, 8, 0));
            mxcsr = 0x1f80;
            OK(uc_reg_write(m.uc, UC_X86_REG_MXCSR, &mxcsr));
        }
        m0_close(&m);
    }

    /* 32-bit mode: VEX.128 and VZEROUPPER zero ZMM0-7 up to bit 511 */
    {
        M0 m;
        unsigned i;
        uint64_t z[8];

        m0_open(&m, UC_MODE_32, 1, NULL, 0);
        m0_put_state(&m, 0x66, 8);
        TEST_CHECK(m0_run(&m, "\xc5\xe9\xef\xcb", 4) == -1);
        m0_zmm(&m, 1, z);
        for (i = 2; i < 8; i++) {
            TEST_CHECK(z[i] == 0);
        }
        m0_put_state(&m, 0x66, 8);
        TEST_CHECK(m0_run(&m, "\xc5\xf8\x77", 3) == -1);
        for (i = 0; i < 8; i++) {
            TEST_CHECK(m0_zmm_is(&m, i, 0x66, 0, 2, 0));
        }
        m0_close(&m);
    }
/* ---- NoVmp U100-U104 tests ---- */
/*
 * NoVmp (ledger U100-U104): Key Locker, RAO-INT, MOVRS, USER_MSR and UINTR on
 * UC_CPU_X86_MAX. Expected values: SDM / ISE / Key Locker spec pseudocode,
 * FIPS-197 Appendix C, the Key Locker spec zero-IWKey vector (page 45) and
 * Emulator/tools/isa/ref_keylocker_misc.py (independent reference).
 */
#define NV_DATA 0x50000000ULL     /* GDT at +0, handles at +0x4000, stack at +0x8000 */
#define NV_DATA_SIZE 0x10000
#define NV_HANDLE (NV_DATA + 0x4000)
#define NV_STACK (NV_DATA + 0x8000)

typedef struct {
    int count;
    uint32_t intno;
} nv_intr_t;

static void nv_hook_intr(uc_engine *uc, uint32_t intno, void *user_data)
{
    nv_intr_t *r = (nv_intr_t *)user_data;

    if (r->count++ == 0) {
        r->intno = intno;
    }
    uc_emu_stop(uc);
}

static uc_engine *nv_open(const char *code, size_t len, nv_intr_t *intr)
{
    uc_engine *uc;
    uc_hook h;
    uint64_t rsp = NV_STACK, rsi = NV_HANDLE;

    OK(uc_open(UC_ARCH_X86, UC_MODE_64, &uc));
    OK(uc_ctl_set_cpu_model(uc, UC_CPU_X86_MAX));
    OK(uc_mem_map(uc, code_start, code_len, UC_PROT_ALL));
    OK(uc_mem_write(uc, code_start, code, len));
    OK(uc_mem_map(uc, NV_DATA, NV_DATA_SIZE, UC_PROT_ALL));
    OK(uc_reg_write(uc, UC_X86_REG_RSP, &rsp));
    OK(uc_reg_write(uc, UC_X86_REG_RSI, &rsi));
    memset(intr, 0, sizeof(*intr));
    OK(uc_hook_add(uc, &h, UC_HOOK_INTR, nv_hook_intr, intr, 1, 0));
    return uc;
}

static uc_err nv_run(uc_engine *uc, size_t len)
{
    return uc_emu_start(uc, code_start, code_start + len, 0, 0);
}

static void nv_setxmm(uc_engine *uc, int i, uint64_t lo, uint64_t hi)
{
    uint64_t v[2] = {lo, hi};

    OK(uc_reg_write(uc, UC_X86_REG_XMM0 + i, v));
}

static void nv_chkxmm(uc_engine *uc, int i, uint64_t lo, uint64_t hi)
{
    uint64_t v[2];

    OK(uc_reg_read(uc, UC_X86_REG_XMM0 + i, v));
    TEST_CHECK(v[0] == lo && v[1] == hi);
    TEST_MSG("xmm%d = %016" PRIx64 "%016" PRIx64 ", expected %016" PRIx64 "%016" PRIx64,
             i, v[1], v[0], hi, lo);
}

static void nv_setreg(uc_engine *uc, int reg, uint64_t v)
{
    OK(uc_reg_write(uc, reg, &v));
}

static uint64_t nv_reg(uc_engine *uc, int reg)
{
    uint64_t v = 0;

    OK(uc_reg_read(uc, reg, &v));
    return v;
}

/*
 * CPL3 for the code after a leading IRETQ (48 CF): GDT with a DPL3 data
 * (0x10) and DPL3 64-bit code (0x18) descriptor; the IRETQ frame returns to
 * code_start + 2 with CS = 0x1B, SS = 0x13, RSP = NV_STACK.
 */
static void nv_setup_cpl3(uc_engine *uc)
{
    uint64_t gdt[4] = {0, 0, 0x00CFF2000000FFFFULL, 0x00AFFA000000FFFFULL};
    uint64_t frame[5] = {code_start + 2, 0x1B, 0x202, NV_STACK, 0x13};
    uc_x86_mmr gdtr = {0, NV_DATA, sizeof(gdt) - 1, 0};
    uint64_t rsp = NV_STACK - 0x100;

    OK(uc_mem_write(uc, NV_DATA, gdt, sizeof(gdt)));
    OK(uc_reg_write(uc, UC_X86_REG_GDTR, &gdtr));
    OK(uc_mem_write(uc, rsp, frame, sizeof(frame)));
    OK(uc_reg_write(uc, UC_X86_REG_RSP, &rsp));
}

/* LOADIWKEY inputs used by the tests (also in ref_keylocker_misc.py) */
static void kl_set_iwkey_inputs(uc_engine *uc)
{
    nv_setxmm(uc, 0, 0x0706050403020100ULL, 0x0f0e0d0c0b0a0908ULL);    /* integrity key */
    nv_setxmm(uc, 1, 0x2726252423222120ULL, 0x2f2e2d2c2b2a2928ULL);    /* EncryptionKey[255:128] */
    nv_setxmm(uc, 2, 0x1716151413121110ULL, 0x1f1e1d1c1b1a1918ULL);    /* EncryptionKey[127:0] */
}

#define KL_LOADIWKEY_X1_X2 "\xf3\x0f\x38\xdc\xca"
#define KL_ENCODEKEY128_EAX_ECX "\xf3\x0f\x38\xfa\xc1"
#define KL_ENCODEKEY256_EAX_ECX "\xf3\x0f\x38\xfb\xc1"
#define KL_STORE_HANDLE3 "\xf3\x0f\x7f\x06\xf3\x0f\x7f\x4e\x10\xf3\x0f\x7f\x56\x20"
#define KL_STORE_HANDLE4 KL_STORE_HANDLE3 "\xf3\x0f\x7f\x5e\x30"
#define NV_SETZ_R8B "\x41\x0f\x94\xc0"
#define NV_SETZ_R9B "\x41\x0f\x94\xc1"
/* FIPS-197 C.1 / C.3: plaintext, AES-128 and AES-256 ciphertexts */
#define FIPS_PT_LO 0x7766554433221100ULL
#define FIPS_PT_HI 0xffeeddccbbaa9988ULL
#define FIPS128_CT_LO 0x30047b6ad8e0c469ULL
#define FIPS128_CT_HI 0x5ac5b47080b7cdd8ULL
#define FIPS256_CT_LO 0xbf456751cab7a28eULL
#define FIPS256_CT_HI 0x8960494b9049fceaULL

/* U100: CPUID.(07H,0):ECX.KL, CPUID.19H, CR4.KL at reset, #UD with CR4.KL = 0 */
static void test_x86_keylocker_cpuid(void)
{
    /* mov eax,7; xor ecx,ecx; cpuid; mov r8d,ecx; mov eax,0x19; xor ecx,ecx; cpuid */
    const char code[] = "\xb8\x07\x00\x00\x00\x31\xc9\x0f\xa2\x41\x89\xc8"
                        "\xb8\x19\x00\x00\x00\x31\xc9\x0f\xa2";
    const char enc[] = KL_ENCODEKEY128_EAX_ECX;
    nv_intr_t intr;
    uc_engine *uc = nv_open(code, sizeof(code) - 1, &intr);
    uint64_t cr4;

    cr4 = nv_reg(uc, UC_X86_REG_CR4);
    TEST_CHECK(cr4 & (1ULL << 19));
    OK(nv_run(uc, sizeof(code) - 1));
    TEST_CHECK(nv_reg(uc, UC_X86_REG_R8) & (1u << 23));
    TEST_CHECK(nv_reg(uc, UC_X86_REG_RAX) == 7);
    TEST_CHECK(nv_reg(uc, UC_X86_REG_RBX) == 5);
    TEST_CHECK(nv_reg(uc, UC_X86_REG_RCX) == 1);
    TEST_CHECK(nv_reg(uc, UC_X86_REG_RDX) == 0);
    /* CR4.KL = 0: AESKLE reads 0 and every Key Locker instruction #UDs */
    nv_setreg(uc, UC_X86_REG_CR4, cr4 & ~(1ULL << 19));
    OK(nv_run(uc, sizeof(code) - 1));
    TEST_CHECK(nv_reg(uc, UC_X86_REG_RBX) == 4);
    OK(uc_mem_write(uc, code_start, enc, sizeof(enc) - 1));
    uc_assert_err(UC_ERR_INSN_INVALID, nv_run(uc, sizeof(enc) - 1));
    OK(uc_close(uc));
}

/* U100: Key Locker spec page 45 footnote: IWKey = 0 (reset), ENCODEKEY128 of key 0 */
static void test_x86_keylocker_zero_iwkey_vector(void)
{
    const char code[] = "\xf3\x0f\x38\xfa\xc0"; /* encodekey128 eax, eax */
    nv_intr_t intr;
    uc_engine *uc = nv_open(code, sizeof(code) - 1, &intr);
    int i;

    nv_setreg(uc, UC_X86_REG_RAX, 0xffffffff00000000ULL);
    nv_setreg(uc, UC_X86_REG_EFLAGS, 0x8d7);
    nv_setxmm(uc, 3, 0x33, 0x33);
    for (i = 4; i <= 6; i++) {
        nv_setxmm(uc, i, ~0ULL, ~0ULL);
    }
    OK(nv_run(uc, sizeof(code) - 1));
    nv_chkxmm(uc, 0, 0, 0);                                         /* AAD */
    nv_chkxmm(uc, 1, 0x898940a278c095dcULL, 0x8720849214a248adULL); /* tag */
    nv_chkxmm(uc, 2, 0x3382228c8474c308ULL, 0xd3e9d22b334fb3c2ULL); /* ciphertext */
    nv_chkxmm(uc, 3, 0x33, 0x33);
    for (i = 4; i <= 6; i++) {
        nv_chkxmm(uc, i, 0, 0);
    }
    TEST_CHECK(nv_reg(uc, UC_X86_REG_RAX) == 0); /* NoBackup 0, KeySource 0, r32 zero-extended */
    TEST_CHECK((nv_reg(uc, UC_X86_REG_EFLAGS) & 0x8d5) == 0);
    OK(uc_close(uc));
}

/* U100: LOADIWKEY + ENCODEKEY128 handle (reference) + AESENC128KL/AESDEC128KL == FIPS-197 C.1 */
static void test_x86_keylocker_aes128(void)
{
    const char code[] = KL_LOADIWKEY_X1_X2 "\x66\x0f\x6f\xc3" KL_ENCODEKEY128_EAX_ECX
                        KL_STORE_HANDLE3
                        "\xf3\x0f\x38\xdc\x3e"     /* aesenc128kl xmm7, [rsi] */
                        NV_SETZ_R8B
                        "\x66\x0f\x6f\xef"         /* movdqa xmm5, xmm7 */
                        "\xf3\x0f\x38\xdd\x3e"     /* aesdec128kl xmm7, [rsi] */
                        NV_SETZ_R9B;
    nv_intr_t intr;
    uc_engine *uc = nv_open(code, sizeof(code) - 1, &intr);

    kl_set_iwkey_inputs(uc);
    nv_setxmm(uc, 3, 0x0706050403020100ULL, 0x0f0e0d0c0b0a0908ULL); /* FIPS-197 C.1 key */
    nv_setxmm(uc, 7, FIPS_PT_LO, FIPS_PT_HI);
    nv_setreg(uc, UC_X86_REG_RAX, 0);
    nv_setreg(uc, UC_X86_REG_RCX, 0);
    OK(nv_run(uc, sizeof(code) - 1));
    TEST_CHECK(intr.count == 0);
    nv_chkxmm(uc, 0, 0, 0);
    nv_chkxmm(uc, 1, 0x634224a78ec0fc82ULL, 0xf011afe7aa419640ULL);
    nv_chkxmm(uc, 2, 0x07d7a1ff305028f3ULL, 0xfcd0b2449592ab3fULL);
    nv_chkxmm(uc, 5, FIPS128_CT_LO, FIPS128_CT_HI);
    nv_chkxmm(uc, 7, FIPS_PT_LO, FIPS_PT_HI);
    TEST_CHECK(nv_reg(uc, UC_X86_REG_R8) == 0 && nv_reg(uc, UC_X86_REG_R9) == 0);
    OK(uc_close(uc));
}

/* U100: ENCODEKEY256 (restrictions 5) handle, AESENCWIDE256KL / AESDECWIDE256KL == FIPS-197 C.3 */
static void test_x86_keylocker_wide256(void)
{
    char code[256];
    size_t n = 0;
    const char head[] = KL_LOADIWKEY_X1_X2 "\x66\x0f\x6f\xc3" "\x66\x0f\x6f\xcc"
                        KL_ENCODEKEY256_EAX_ECX KL_STORE_HANDLE4;
    const char wide_enc[] = "\xf3\x0f\x38\xd8\x16" NV_SETZ_R8B;   /* aesencwide256kl [rsi] */
    const char wide_dec[] = "\xf3\x0f\x38\xd8\x1e" NV_SETZ_R9B;   /* aesdecwide256kl [rsi] */
    uint64_t pt[2] = {FIPS_PT_LO, FIPS_PT_HI}, hnd[8];
    nv_intr_t intr;
    uc_engine *uc;
    int i;

    memcpy(code, head, sizeof(head) - 1);
    n = sizeof(head) - 1;
    for (i = 0; i < 8; i++) { /* movdqu xmm<i>, [rsi + 0x100] */
        memcpy(code + n, "\xf3\x0f\x6f", 3);
        code[n + 3] = (char)(0x86 | (i << 3));
        memcpy(code + n + 4, "\x00\x01\x00\x00", 4);
        n += 8;
    }
    memcpy(code + n, wide_enc, sizeof(wide_enc) - 1);
    n += sizeof(wide_enc) - 1;
    uc = nv_open(code, n, &intr);
    kl_set_iwkey_inputs(uc);
    nv_setxmm(uc, 3, 0x0706050403020100ULL, 0x0f0e0d0c0b0a0908ULL); /* FIPS-197 C.3 key */
    nv_setxmm(uc, 4, 0x1716151413121110ULL, 0x1f1e1d1c1b1a1918ULL);
    nv_setreg(uc, UC_X86_REG_RCX, 5); /* CPL0-only + no-decrypt (CPL 0 here) */
    OK(uc_mem_write(uc, NV_HANDLE + 0x100, pt, sizeof(pt)));
    OK(nv_run(uc, n));
    TEST_CHECK(intr.count == 0);
    OK(uc_mem_read(uc, NV_HANDLE, hnd, sizeof(hnd)));
    TEST_CHECK(hnd[0] == 0x0000000001000005ULL && hnd[1] == 0);
    TEST_CHECK(hnd[2] == 0x01e20cd8b6763808ULL && hnd[3] == 0x7813f6924c30cb0eULL);
    TEST_CHECK(hnd[4] == 0x2ff383ae0a96b9afULL && hnd[5] == 0x43b14021c4e562ecULL);
    TEST_CHECK(hnd[6] == 0xddea96a39ab8e43fULL && hnd[7] == 0x47614f4794c81039ULL);
    for (i = 0; i < 8; i++) {
        nv_chkxmm(uc, i, FIPS256_CT_LO, FIPS256_CT_HI);
    }
    TEST_CHECK(nv_reg(uc, UC_X86_REG_R8) == 0);
    /* the no-decrypt handle refuses AESDECWIDE256KL: ZF = 1, XMM0-7 unchanged */
    OK(uc_mem_write(uc, code_start, wide_dec, sizeof(wide_dec) - 1));
    OK(nv_run(uc, sizeof(wide_dec) - 1));
    TEST_CHECK(nv_reg(uc, UC_X86_REG_R9) == 1);
    for (i = 0; i < 8; i++) {
        nv_chkxmm(uc, i, FIPS256_CT_LO, FIPS256_CT_HI);
    }
    /* a handle without restrictions decrypts XMM0-7 back */
    OK(uc_mem_write(uc, code_start, KL_ENCODEKEY256_EAX_ECX KL_STORE_HANDLE4,
                    sizeof(KL_ENCODEKEY256_EAX_ECX KL_STORE_HANDLE4) - 1));
    for (i = 0; i < 8; i++) {
        nv_setxmm(uc, i, FIPS256_CT_LO, FIPS256_CT_HI);
    }
    nv_setxmm(uc, 0, 0x0706050403020100ULL, 0x0f0e0d0c0b0a0908ULL);
    nv_setxmm(uc, 1, 0x1716151413121110ULL, 0x1f1e1d1c1b1a1918ULL);
    nv_setreg(uc, UC_X86_REG_RCX, 0);
    OK(nv_run(uc, sizeof(KL_ENCODEKEY256_EAX_ECX KL_STORE_HANDLE4) - 1));
    for (i = 0; i < 8; i++) {
        nv_setxmm(uc, i, FIPS256_CT_LO, FIPS256_CT_HI);
    }
    OK(uc_mem_write(uc, code_start, wide_dec, sizeof(wide_dec) - 1));
    OK(nv_run(uc, sizeof(wide_dec) - 1));
    TEST_CHECK(nv_reg(uc, UC_X86_REG_R9) == 0);
    for (i = 0; i < 8; i++) {
        nv_chkxmm(uc, i, FIPS_PT_LO, FIPS_PT_HI);
    }
    OK(uc_close(uc));
}

/* U100: handle violations (ZF = 1, destination unchanged) and #GP/#UD conditions */
static void test_x86_keylocker_faults(void)
{
    const char make[] = KL_ENCODEKEY128_EAX_ECX KL_STORE_HANDLE3;
    const char enc[] = "\xf3\x0f\x38\xdc\x3e" NV_SETZ_R8B;
    const char dec[] = "\xf3\x0f\x38\xdd\x3e" NV_SETZ_R8B;
    const char e256[] = "\xf3\x0f\x38\xdf\x3e" NV_SETZ_R8B;   /* AES-128 handle, AESDEC256KL */
    nv_intr_t intr;
    uc_engine *uc = nv_open(KL_LOADIWKEY_X1_X2, 5, &intr);
    uint8_t b;

    /*
     * a non-zero IWKey: with IWKey = 0 the POLYVAL key is 0 and the integrity
     * check cannot detect a changed ciphertext (Key Locker spec page 45)
     */
    kl_set_iwkey_inputs(uc);
    OK(nv_run(uc, 5));
    OK(uc_mem_write(uc, code_start, make, sizeof(make) - 1));
    /* no-encrypt handle (SRC = 2) */
    nv_setxmm(uc, 0, 0x0706050403020100ULL, 0x0f0e0d0c0b0a0908ULL);
    nv_setreg(uc, UC_X86_REG_RCX, 2);
    OK(nv_run(uc, sizeof(make) - 1));
    nv_setxmm(uc, 7, FIPS_PT_LO, FIPS_PT_HI);
    OK(uc_mem_write(uc, code_start, enc, sizeof(enc) - 1));
    OK(nv_run(uc, sizeof(enc) - 1));
    TEST_CHECK(nv_reg(uc, UC_X86_REG_R8) == 1);
    nv_chkxmm(uc, 7, FIPS_PT_LO, FIPS_PT_HI);
    nv_setxmm(uc, 7, FIPS128_CT_LO, FIPS128_CT_HI);
    OK(uc_mem_write(uc, code_start, dec, sizeof(dec) - 1));
    OK(nv_run(uc, sizeof(dec) - 1));
    TEST_CHECK(nv_reg(uc, UC_X86_REG_R8) == 0);
    nv_chkxmm(uc, 7, FIPS_PT_LO, FIPS_PT_HI);
    OK(uc_mem_write(uc, code_start, e256, sizeof(e256) - 1));
    OK(nv_run(uc, sizeof(e256) - 1));
    TEST_CHECK(nv_reg(uc, UC_X86_REG_R8) == 1);
    /* integrity: one ciphertext bit flipped */
    OK(uc_mem_read(uc, NV_HANDLE + 40, &b, 1));
    b ^= 0x10;
    OK(uc_mem_write(uc, NV_HANDLE + 40, &b, 1));
    OK(uc_mem_write(uc, code_start, dec, sizeof(dec) - 1));
    OK(nv_run(uc, sizeof(dec) - 1));
    TEST_CHECK(nv_reg(uc, UC_X86_REG_R8) == 1);
    nv_chkxmm(uc, 7, FIPS_PT_LO, FIPS_PT_HI);
    /* ENCODEKEY128: reserved SRC bit 3 -> #GP */
    nv_setreg(uc, UC_X86_REG_RCX, 8);
    OK(uc_mem_write(uc, code_start, make, sizeof(make) - 1));
    OK(nv_run(uc, sizeof(make) - 1));
    TEST_CHECK(intr.count == 1 && intr.intno == 13);
    /* LOADIWKEY: KeySource 1 (not enumerated) -> #GP */
    memset(&intr, 0, sizeof(intr));
    nv_setreg(uc, UC_X86_REG_RAX, 2);
    OK(uc_mem_write(uc, code_start, KL_LOADIWKEY_X1_X2, 5));
    OK(nv_run(uc, 5));
    TEST_CHECK(intr.count == 1 && intr.intno == 13);
    /* memory form of ENCODEKEY128, LOCK, register form of AESDEC128KL, D8 /4 -> #UD */
    OK(uc_mem_write(uc, code_start, "\xf3\x0f\x38\xfa\x06", 5));
    uc_assert_err(UC_ERR_INSN_INVALID, nv_run(uc, 5));
    OK(uc_mem_write(uc, code_start, "\xf0\xf3\x0f\x38\xfa\xc1", 6));
    uc_assert_err(UC_ERR_INSN_INVALID, nv_run(uc, 6));
    OK(uc_mem_write(uc, code_start, "\xf3\x0f\x38\xdd\xc1", 5));
    uc_assert_err(UC_ERR_INSN_INVALID, nv_run(uc, 5));
    OK(uc_mem_write(uc, code_start, "\xf3\x0f\x38\xd8\x26", 5));
    uc_assert_err(UC_ERR_INSN_INVALID, nv_run(uc, 5));
    OK(uc_close(uc));
}

/* U100: CPL 3: LOADIWKEY #GP; a CPL0-only handle sets ZF; ENCODEKEY works */
static void test_x86_keylocker_cpl3(void)
{
    const char code[] = "\x48\xcf" "\xf3\x0f\x38\xfa\xc1" KL_STORE_HANDLE3 "\xf3\x0f\x38\xdc\x3e" NV_SETZ_R8B;
    const char load[] = "\x48\xcf" KL_LOADIWKEY_X1_X2;
    nv_intr_t intr;
    uc_engine *uc = nv_open(code, sizeof(code) - 1, &intr);

    nv_setup_cpl3(uc);
    nv_setxmm(uc, 0, 0x0706050403020100ULL, 0x0f0e0d0c0b0a0908ULL);
    nv_setxmm(uc, 7, FIPS_PT_LO, FIPS_PT_HI);
    nv_setreg(uc, UC_X86_REG_RCX, 1);
    OK(nv_run(uc, sizeof(code) - 1));
    TEST_CHECK(intr.count == 0);
    TEST_CHECK((nv_reg(uc, UC_X86_REG_CS) & 3) == 3);
    TEST_CHECK(nv_reg(uc, UC_X86_REG_R8) == 1);
    nv_chkxmm(uc, 7, FIPS_PT_LO, FIPS_PT_HI);
    OK(uc_close(uc));

    uc = nv_open(code, sizeof(code) - 1, &intr);
    nv_setup_cpl3(uc);
    nv_setxmm(uc, 0, 0x0706050403020100ULL, 0x0f0e0d0c0b0a0908ULL);
    nv_setxmm(uc, 7, FIPS_PT_LO, FIPS_PT_HI);
    nv_setreg(uc, UC_X86_REG_RCX, 0);
    OK(nv_run(uc, sizeof(code) - 1));
    TEST_CHECK(nv_reg(uc, UC_X86_REG_R8) == 0);
    nv_chkxmm(uc, 7, FIPS128_CT_LO, FIPS128_CT_HI);
    OK(uc_close(uc));

    uc = nv_open(load, sizeof(load) - 1, &intr);
    nv_setup_cpl3(uc);
    OK(nv_run(uc, sizeof(load) - 1));
    TEST_CHECK(intr.count == 1 && intr.intno == 13);
    OK(uc_close(uc));
}

/* U101: RAO-INT AADD/AAND/AOR/AXOR (no flags), alignment #GP, register form / LOCK #UD */
static void test_x86_rao_int(void)
{
    const char code[] = "\x0f\x38\xfc\x06"                 /* aadd [rsi], eax */
                        "\x48\x0f\x38\xfc\x46\x08"         /* aadd [rsi+8], rax */
                        "\x66\x0f\x38\xfc\x46\x10"         /* aand [rsi+0x10], eax */
                        "\x66\x48\x0f\x38\xfc\x46\x18"     /* aand [rsi+0x18], rax */
                        "\xf2\x0f\x38\xfc\x46\x20"         /* aor [rsi+0x20], eax */
                        "\xf2\x48\x0f\x38\xfc\x46\x28"     /* aor [rsi+0x28], rax */
                        "\xf3\x0f\x38\xfc\x46\x30"         /* axor [rsi+0x30], eax */
                        "\xf3\x48\x0f\x38\xfc\x46\x38";    /* axor [rsi+0x38], rax */
    const uint64_t exp[8] = {0xf0f0f0f000000000ULL, 0x00e100e100000000ULL,
                             0xf0f0f0f000000001ULL, 0x00f000f000000001ULL,
                             0xf0f0f0f0ffffffffULL, 0xfff0fff0ffffffffULL,
                             0xf0f0f0f0fffffffeULL, 0xff00ff00fffffffeULL};
    uint64_t mem[8];
    nv_intr_t intr;
    uc_engine *uc = nv_open(code, sizeof(code) - 1, &intr);
    int i;

    for (i = 0; i < 8; i++) {
        mem[i] = 0xf0f0f0f0ffffffffULL;
    }
    OK(uc_mem_write(uc, NV_HANDLE, mem, sizeof(mem)));
    nv_setreg(uc, UC_X86_REG_RAX, 0x0ff00ff000000001ULL);
    nv_setreg(uc, UC_X86_REG_EFLAGS, 0x8d7);
    OK(nv_run(uc, sizeof(code) - 1));
    TEST_CHECK(intr.count == 0);
    OK(uc_mem_read(uc, NV_HANDLE, mem, sizeof(mem)));
    for (i = 0; i < 8; i++) {
        TEST_CHECK(mem[i] == exp[i]);
        TEST_MSG("slot %d: %016" PRIx64 " expected %016" PRIx64, i, mem[i], exp[i]);
    }
    TEST_CHECK((nv_reg(uc, UC_X86_REG_EFLAGS) & 0x8d7) == 0x8d7);
    /* misaligned: m32 at +2, m64 at +4 -> #GP */
    OK(uc_mem_write(uc, code_start, "\x0f\x38\xfc\x46\x02", 5));
    OK(nv_run(uc, 5));
    TEST_CHECK(intr.count == 1 && intr.intno == 13);
    memset(&intr, 0, sizeof(intr));
    OK(uc_mem_write(uc, code_start, "\xf3\x48\x0f\x38\xfc\x46\x04", 7));
    OK(nv_run(uc, 7));
    TEST_CHECK(intr.count == 1 && intr.intno == 13);
    /* register form, LOCK -> #UD */
    OK(uc_mem_write(uc, code_start, "\x0f\x38\xfc\xc0", 4));
    uc_assert_err(UC_ERR_INSN_INVALID, nv_run(uc, 4));
    OK(uc_mem_write(uc, code_start, "\xf0\x0f\x38\xfc\x06", 5));
    uc_assert_err(UC_ERR_INSN_INVALID, nv_run(uc, 5));
    OK(uc_close(uc));
}

TEST_LIST = {
    {"test_x86_in", test_x86_in},
    {"test_x86_out", test_x86_out},
    {"test_x86_mem_hook_all", test_x86_mem_hook_all},
    {"test_x86_inc_dec_pxor", test_x86_inc_dec_pxor},
    {"test_x86_avx_vpxor_ymm", test_x86_avx_vpxor_ymm},
    {"test_x86_avx_vex128_zero_upper", test_x86_avx_vex128_zero_upper},
    {"test_x86_avx_scalar_zero_upper", test_x86_avx_scalar_zero_upper},
    {"test_x86_avx_fma_ps", test_x86_avx_fma_ps},
    {"test_x86_fma_scalar_variants", test_x86_fma_scalar_variants},
    {"test_x86_avx2_broadcast_permute", test_x86_avx2_broadcast_permute},
    {"test_x86_avx2_variable_shifts", test_x86_avx2_variable_shifts},
    {"test_x86_avx2_mask_gather", test_x86_avx2_mask_gather},
    {"test_x86_avx_vzeroall", test_x86_avx_vzeroall},
    {"test_x86_aes_pclmul", test_x86_aes_pclmul},
    {"test_x86_avx512_tcg_mask", test_x86_avx512_tcg_mask},
    {"test_x86_vaes_vex_gating", test_x86_vaes_vex_gating},
    {"test_x86_vpclmulqdq_tcg_mask", test_x86_vpclmulqdq_tcg_mask},
    {"test_x86_vnni_ifma_ne_cpuid", test_x86_vnni_ifma_ne_cpuid},
    {"test_x86_vnni_ifma_ne_vectors", test_x86_vnni_ifma_ne_vectors},
    {"test_x86_vnni_ifma_ne_ud", test_x86_vnni_ifma_ne_ud},
    {"test_x86_ptwrite_quirk", test_x86_ptwrite_quirk},
    {"test_x86_vsha512", test_x86_vsha512},
    {"test_x86_vsm3", test_x86_vsm3},
    {"test_x86_vsm4", test_x86_vsm4},
    {"test_x86_relative_jump", test_x86_relative_jump},
    {"test_x86_loop", test_x86_loop},
    {"test_x86_invalid_mem_read", test_x86_invalid_mem_read},
    {"test_x86_invalid_mem_write", test_x86_invalid_mem_write},
    {"test_x86_invalid_jump", test_x86_invalid_jump},
    {"test_x86_64_syscall", test_x86_64_syscall},
    {"test_x86_16_add", test_x86_16_add},
    {"test_x86_reg_save", test_x86_reg_save},
    {"test_x86_invalid_mem_read_stop_in_cb",
     test_x86_invalid_mem_read_stop_in_cb},
    {"test_x86_x87_fnstenv", test_x86_x87_fnstenv},
    {"test_x86_mmio", test_x86_mmio},
    {"test_x86_missing_code", test_x86_missing_code},
    {"test_x86_smc_xor", test_x86_smc_xor},
    {"test_x86_smc_add", test_x86_smc_add},
    {"test_x86_smc_mem_hook", test_x86_smc_mem_hook},
    {"test_x86_mmio_uc_mem_rw", test_x86_mmio_uc_mem_rw},
    {"test_x86_sysenter", test_x86_sysenter},
    {"test_x86_hook_cpuid", test_x86_hook_cpuid},
    {"test_x86_486_cpuid", test_x86_486_cpuid},
    {"test_x86_qemu72_xsave_cpuid", test_x86_qemu72_xsave_cpuid},
    {"test_x86_opmask_registers", test_x86_opmask_registers},
    {"test_x86_qemu72_msr_state", test_x86_qemu72_msr_state},
    {"test_x86_clear_tb_cache", test_x86_clear_tb_cache},
    {"test_x86_clear_empty_tb", test_x86_clear_empty_tb},
    {"test_x86_self_linked_tb_guest_smc", test_x86_self_linked_tb_guest_smc},
    {"test_x86_two_page_tb_invalidation",
     test_x86_two_page_tb_invalidation},
    {"test_x86_tb_cache_engine_isolation",
     test_x86_tb_cache_engine_isolation},
    {"test_x86_hook_tcg_op", test_x86_hook_tcg_op},
    {"test_x86_cmpxchg", test_x86_cmpxchg},
    {"test_x86_cmpxchg32_accumulator", test_x86_cmpxchg32_accumulator},
    {"test_x86_cmpxchg32_register", test_x86_cmpxchg32_register},
    {"test_x86_ret_imm16_unsigned", test_x86_ret_imm16_unsigned},
    {"test_x86_rorx_rip_relative_imm", test_x86_rorx_rip_relative_imm},
    {"test_x86_shld_rip_relative_imm", test_x86_shld_rip_relative_imm},
    {"test_x86_shrd_rip_relative_imm", test_x86_shrd_rip_relative_imm},
    {"test_x86_pdep32_zero_extend", test_x86_pdep32_zero_extend},
    {"test_x86_pext32_zero_extend", test_x86_pext32_zero_extend},
    {"test_x86_nested_emu_start", test_x86_nested_emu_start},
    {"test_x86_nested_emu_stop", test_x86_nested_emu_stop},
    {"test_x86_64_nested_emu_start_error", test_x86_64_nested_emu_start_error},
    {"test_x86_eflags_reserved_bit", test_x86_eflags_reserved_bit},
    {"test_x86_blsi_cf", test_x86_blsi_cf},
    {"test_x86_blsr_flags", test_x86_blsr_flags},
    {"test_x86_blsmsk_flags", test_x86_blsmsk_flags},
    {"test_x86_bzhi_index_boundary", test_x86_bzhi_index_boundary},
    {"test_x86_nested_uc_emu_start_exits", test_x86_nested_uc_emu_start_exits},
    {"test_x86_clear_count_cache", test_x86_clear_count_cache},
    {"test_x86_correct_address_in_small_jump_hook",
     test_x86_correct_address_in_small_jump_hook},
    {"test_x86_correct_address_in_long_jump_hook",
     test_x86_correct_address_in_long_jump_hook},
    {"test_x86_invalid_vex_l", test_x86_invalid_vex_l},
    {"test_x86_sse_aligned_access", test_x86_sse_aligned_access},
    {"test_x86_data_watchpoint", test_x86_data_watchpoint},
#if !defined(TARGET_READ_INLINED) && defined(BOOST_LITTLE_ENDIAN)
    {"test_x86_unaligned_access", test_x86_unaligned_access},
    {"test_x86_64_unaligned_access", test_x86_64_unaligned_access},

#endif
    {"test_x86_lazy_mapping", test_x86_lazy_mapping},
    {"test_x86_16_incorrect_ip", test_x86_16_incorrect_ip},
    {"test_x86_mmu", test_x86_mmu},
    {"test_x86_read_virtual", test_x86_read_virtual},
    {"test_x86_vtlb", test_x86_vtlb},
    {"test_x86_segmentation", test_x86_segmentation},
    {"test_x86_0xff_lcall", test_x86_0xff_lcall},
    {"test_x86_64_not_overwriting_tmp0_for_pc_update",
     test_x86_64_not_overwriting_tmp0_for_pc_update},
    {"test_fxsave_fpip_x86", test_fxsave_fpip_x86},
    {"test_fxsave_fpip_x64", test_fxsave_fpip_x64},
    {"test_bswap_x64", test_bswap_ax},
    {"test_rex_x64", test_rex_x64},
    {"test_x86_ro_segfault", test_x86_ro_segfault},
    {"test_x86_vpermilps_null_ptr_call", test_x86_vpermilps_null_ptr_call},
    {"test_x86_hook_insn_rdtsc", test_x86_hook_insn_rdtsc},
    {"test_x86_hook_insn_rdtscp", test_x86_hook_insn_rdtscp},
    {"test_x86_hook_insn_wrmsr", test_x86_hook_insn_wrmsr},
    {"test_x86_hook_insn_rdmsr", test_x86_hook_insn_rdmsr},
    {"test_x86_dr7", test_x86_dr7},
    {"test_x86_hook_block", test_x86_hook_block},
    {"test_x86_mem_hooks_pc_guarantee", test_x86_mem_hooks_pc_guarantee},
    {"test_x86_aaa_flags", test_x86_aaa_flags},
    {"test_x86_aas_flags", test_x86_aas_flags},
    {"test_x86_group_1a", test_x86_group_1a},
    {"test_x86_lock_bt_mem", test_x86_lock_bt_mem},
    {"test_x86_lock_bt_reg", test_x86_lock_bt_reg},
    {"test_x86_lock_btc_mem", test_x86_lock_btc_mem},
    {"test_x86_lock_btc_reg", test_x86_lock_btc_reg},
    {"test_x86_avx512_optin", test_x86_avx512_optin},
    {"test_x86_avx512_xsetbv", test_x86_avx512_xsetbv},
    {"test_x86_zmm_api_32", test_x86_zmm_api_32},
    {"test_x86_avx512_xsave", test_x86_avx512_xsave},
    {"test_x86_avx512_xsavec", test_x86_avx512_xsavec},
    {"test_x86_avx512_xsave_32", test_x86_avx512_xsave_32},
    {"test_x86_avx512_cpuid", test_x86_avx512_cpuid},
    {"test_x86_vex_zero_maxvl", test_x86_vex_zero_maxvl},
    {"test_x86_keylocker_cpuid", test_x86_keylocker_cpuid},
    {"test_x86_keylocker_zero_iwkey_vector", test_x86_keylocker_zero_iwkey_vector},
    {"test_x86_keylocker_aes128", test_x86_keylocker_aes128},
    {"test_x86_keylocker_wide256", test_x86_keylocker_wide256},
    {"test_x86_keylocker_faults", test_x86_keylocker_faults},
    {"test_x86_keylocker_cpl3", test_x86_keylocker_cpl3},
    {"test_x86_rao_int", test_x86_rao_int},
    {NULL, NULL}};
