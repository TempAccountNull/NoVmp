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
 * NoVmp U80/U534: PTWRITE is #UD (SDM Vol2 PTWRITE: "#UD If CPUID.14H.00H:EBX.PTWRITE[4]
 * = 0"; the fork has no Intel PT). The #UD comes before the memory operand is touched,
 * so an unmapped operand is #UD too, and nothing after it runs. (The i5-13600K executes
 * it: documented deviation "PTWRITE without PT", docs/quirks.md.)
 */
static void test_x86_ptwrite_ud(void)
{
    /* ptwrite qword [rax]; inc rbx */
    static const char code[] = "\xf3\x48\x0f\xae\x20\x48\xff\xc3";
    X86IntrCapture capture = { 0 };
    uc_engine *uc;
    uc_hook hook;
    uint64_t rax = 0x200000, rbx = 0;

    OK(uc_open(UC_ARCH_X86, UC_MODE_64, &uc));
    OK(uc_mem_map(uc, code_start, code_len, UC_PROT_ALL));
    OK(uc_mem_write(uc, code_start, code, sizeof(code) - 1));
    OK(uc_mem_map(uc, 0x200000, 0x1000, UC_PROT_ALL));
    OK(uc_hook_add(uc, &hook, UC_HOOK_INTR, test_x86_intr_capture_cb,
                   &capture, 1, 0));

    /* mapped operand: #UD (raw Unicorn reports it as UC_ERR_INSN_INVALID), RBX untouched */
    OK(uc_reg_write(uc, UC_X86_REG_RAX, &rax));
    OK(uc_reg_write(uc, UC_X86_REG_RBX, &rbx));
    uc_assert_err(UC_ERR_INSN_INVALID,
                  uc_emu_start(uc, code_start, code_start + sizeof(code) - 1, 0, 0));
    OK(uc_reg_read(uc, UC_X86_REG_RBX, &rbx));
    TEST_CHECK(rbx == 0);

    /* unmapped operand: still #UD, not a memory fault */
    rax = 0x300000;
    OK(uc_reg_write(uc, UC_X86_REG_RAX, &rax));
    uc_assert_err(UC_ERR_INSN_INVALID,
                  uc_emu_start(uc, code_start, code_start + sizeof(code) - 1, 0, 0));
    OK(uc_reg_read(uc, UC_X86_REG_RBX, &rbx));
    TEST_CHECK(rbx == 0);

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
        /* U435: a profile is strict by default; these partial profiles (leaf 0DH without
           leaf 1) test XCR0 / CPUID only, so the model's XSAVE stays usable */
        OK(uc_ctl_set_x86_cpuid_strict(m->uc, 0));
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
}

/* ---- NoVmp U100-U104 tests ---- */
/*
 * NoVmp (ledger U100-U104): Key Locker, RAO-INT, MOVRS, USER_MSR and UINTR on
 * UC_CPU_X86_MAX. Expected values: SDM / ISE / Key Locker spec pseudocode,
 * FIPS-197 Appendix C, the Key Locker spec zero-IWKey vector (page 45) and
 * Emulator/tools/isa/ref_keylocker_misc.py (independent reference).
 */
#define NK_DATA 0x50000000ULL     /* GDT at +0, handles at +0x4000, stack at +0x8000 */
#define NK_DATA_SIZE 0x10000
#define NK_HANDLE (NK_DATA + 0x4000)
#define NK_STACK (NK_DATA + 0x8000)

typedef struct {
    int count;
    uint32_t intno;
} nk_intr_t;

static void nk_hook_intr(uc_engine *uc, uint32_t intno, void *user_data)
{
    nk_intr_t *r = (nk_intr_t *)user_data;

    if (r->count++ == 0) {
        r->intno = intno;
    }
    uc_emu_stop(uc);
}

static uc_engine *nk_open(const char *code, size_t len, nk_intr_t *intr)
{
    uc_engine *uc;
    uc_hook h;
    uint64_t rsp = NK_STACK, rsi = NK_HANDLE;

    OK(uc_open(UC_ARCH_X86, UC_MODE_64, &uc));
    OK(uc_ctl_set_cpu_model(uc, UC_CPU_X86_MAX));
    OK(uc_mem_map(uc, code_start, code_len, UC_PROT_ALL));
    OK(uc_mem_write(uc, code_start, code, len));
    OK(uc_mem_map(uc, NK_DATA, NK_DATA_SIZE, UC_PROT_ALL));
    OK(uc_reg_write(uc, UC_X86_REG_RSP, &rsp));
    OK(uc_reg_write(uc, UC_X86_REG_RSI, &rsi));
    memset(intr, 0, sizeof(*intr));
    OK(uc_hook_add(uc, &h, UC_HOOK_INTR, nk_hook_intr, intr, 1, 0));
    return uc;
}

static uc_err nk_run(uc_engine *uc, size_t len)
{
    return uc_emu_start(uc, code_start, code_start + len, 0, 0);
}

static int nk_fault(uc_engine *uc, nk_intr_t *intr, const char *code, size_t len);

/*
 * runs code from its own 64-byte slot of the code page: code that already ran is never
 * rewritten in place (a later run at the same address may reuse a stale translation)
 */
static uint64_t nk_slot(void)
{
    static unsigned slot;

    return code_start + 0x1000 + 0x40 * (slot++ % 0xc0);
}

static uc_err nk_exec(uc_engine *uc, const char *code, size_t len)
{
    uint64_t at = nk_slot();

    OK(uc_mem_write(uc, at, code, len));
    return uc_emu_start(uc, at, at + len, 0, 0);
}

static void nk_setxmm(uc_engine *uc, int i, uint64_t lo, uint64_t hi)
{
    uint64_t v[2] = {lo, hi};

    OK(uc_reg_write(uc, UC_X86_REG_XMM0 + i, v));
}

static void nk_chkxmm(uc_engine *uc, int i, uint64_t lo, uint64_t hi)
{
    uint64_t v[2];

    OK(uc_reg_read(uc, UC_X86_REG_XMM0 + i, v));
    TEST_CHECK(v[0] == lo && v[1] == hi);
    TEST_MSG("xmm%d = %016" PRIx64 "%016" PRIx64 ", expected %016" PRIx64 "%016" PRIx64,
             i, v[1], v[0], hi, lo);
}

static void nk_setreg(uc_engine *uc, int reg, uint64_t v)
{
    OK(uc_reg_write(uc, reg, &v));
}

static uint64_t nk_reg(uc_engine *uc, int reg)
{
    uint64_t v = 0;

    OK(uc_reg_read(uc, reg, &v));
    return v;
}

/*
 * CPL3 for the code after a leading IRETQ (48 CF): GDT with a DPL3 data
 * (0x10) and DPL3 64-bit code (0x18) descriptor; the IRETQ frame returns to
 * code_start + 2 with CS = 0x1B, SS = 0x13, RSP = NK_STACK.
 */
static void nk_setup_cpl3(uc_engine *uc)
{
    uint64_t gdt[4] = {0, 0, 0x00CFF2000000FFFFULL, 0x00AFFA000000FFFFULL};
    uint64_t frame[5] = {code_start + 2, 0x1B, 0x202, NK_STACK, 0x13};
    uc_x86_mmr gdtr = {0, NK_DATA, sizeof(gdt) - 1, 0};
    uint64_t rsp = NK_STACK - 0x100;

    OK(uc_mem_write(uc, NK_DATA, gdt, sizeof(gdt)));
    OK(uc_reg_write(uc, UC_X86_REG_GDTR, &gdtr));
    OK(uc_mem_write(uc, rsp, frame, sizeof(frame)));
    OK(uc_reg_write(uc, UC_X86_REG_RSP, &rsp));
}

/* LOADIWKEY inputs used by the tests (also in ref_keylocker_misc.py) */
static void kl_set_iwkey_inputs(uc_engine *uc)
{
    nk_setxmm(uc, 0, 0x0706050403020100ULL, 0x0f0e0d0c0b0a0908ULL);    /* integrity key */
    nk_setxmm(uc, 1, 0x2726252423222120ULL, 0x2f2e2d2c2b2a2928ULL);    /* EncryptionKey[255:128] */
    nk_setxmm(uc, 2, 0x1716151413121110ULL, 0x1f1e1d1c1b1a1918ULL);    /* EncryptionKey[127:0] */
}

#define KL_LOADIWKEY_X1_X2 "\xf3\x0f\x38\xdc\xca"
#define KL_ENCODEKEY128_EAX_ECX "\xf3\x0f\x38\xfa\xc1"
#define KL_ENCODEKEY256_EAX_ECX "\xf3\x0f\x38\xfb\xc1"
#define KL_STORE_HANDLE3 "\xf3\x0f\x7f\x06\xf3\x0f\x7f\x4e\x10\xf3\x0f\x7f\x56\x20"
#define KL_STORE_HANDLE4 KL_STORE_HANDLE3 "\xf3\x0f\x7f\x5e\x30"
#define NK_SETZ_R8B "\x41\x0f\x94\xc0"
#define NK_SETZ_R9B "\x41\x0f\x94\xc1"
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
    nk_intr_t intr;
    uc_engine *uc = nk_open(code, sizeof(code) - 1, &intr);
    uint64_t cr4;

    cr4 = nk_reg(uc, UC_X86_REG_CR4);
    TEST_CHECK(cr4 & (1ULL << 19));
    OK(nk_run(uc, sizeof(code) - 1));
    TEST_CHECK(nk_reg(uc, UC_X86_REG_R8) & (1u << 23));
    TEST_CHECK(nk_reg(uc, UC_X86_REG_RAX) == 7);
    TEST_CHECK(nk_reg(uc, UC_X86_REG_RBX) == 5);
    TEST_CHECK(nk_reg(uc, UC_X86_REG_RCX) == 1);
    TEST_CHECK(nk_reg(uc, UC_X86_REG_RDX) == 0);
    /* CR4.KL = 0: AESKLE reads 0 and every Key Locker instruction #UDs */
    nk_setreg(uc, UC_X86_REG_CR4, cr4 & ~(1ULL << 19));
    OK(nk_run(uc, sizeof(code) - 1));
    TEST_CHECK(nk_reg(uc, UC_X86_REG_RBX) == 4);
    TEST_CHECK(nk_fault(uc, &intr, enc, sizeof(enc) - 1) == 6);
    OK(uc_close(uc));
}

/* U100: Key Locker spec page 45 footnote: IWKey = 0 (reset), ENCODEKEY128 of key 0 */
static void test_x86_keylocker_zero_iwkey_vector(void)
{
    const char code[] = "\xf3\x0f\x38\xfa\xc0"; /* encodekey128 eax, eax */
    nk_intr_t intr;
    uc_engine *uc = nk_open(code, sizeof(code) - 1, &intr);
    int i;

    nk_setreg(uc, UC_X86_REG_RAX, 0xffffffff00000000ULL);
    nk_setreg(uc, UC_X86_REG_EFLAGS, 0x8d7);
    nk_setxmm(uc, 3, 0x33, 0x33);
    for (i = 4; i <= 6; i++) {
        nk_setxmm(uc, i, ~0ULL, ~0ULL);
    }
    OK(nk_run(uc, sizeof(code) - 1));
    nk_chkxmm(uc, 0, 0, 0);                                         /* AAD */
    nk_chkxmm(uc, 1, 0x898940a278c095dcULL, 0x8720849214a248adULL); /* tag */
    nk_chkxmm(uc, 2, 0x3382228c8474c308ULL, 0xd3e9d22b334fb3c2ULL); /* ciphertext */
    nk_chkxmm(uc, 3, 0x33, 0x33);
    for (i = 4; i <= 6; i++) {
        nk_chkxmm(uc, i, 0, 0);
    }
    TEST_CHECK(nk_reg(uc, UC_X86_REG_RAX) == 0); /* NoBackup 0, KeySource 0, r32 zero-extended */
    TEST_CHECK((nk_reg(uc, UC_X86_REG_EFLAGS) & 0x8d5) == 0);
    OK(uc_close(uc));
}

/* U100: LOADIWKEY + ENCODEKEY128 handle (reference) + AESENC128KL/AESDEC128KL == FIPS-197 C.1 */
static void test_x86_keylocker_aes128(void)
{
    const char code[] = KL_LOADIWKEY_X1_X2 "\x66\x0f\x6f\xc3" KL_ENCODEKEY128_EAX_ECX
                        KL_STORE_HANDLE3
                        "\xf3\x0f\x38\xdc\x3e"     /* aesenc128kl xmm7, [rsi] */
                        NK_SETZ_R8B
                        "\x66\x0f\x6f\xef"         /* movdqa xmm5, xmm7 */
                        "\xf3\x0f\x38\xdd\x3e"     /* aesdec128kl xmm7, [rsi] */
                        NK_SETZ_R9B;
    nk_intr_t intr;
    uc_engine *uc = nk_open(code, sizeof(code) - 1, &intr);

    kl_set_iwkey_inputs(uc);
    nk_setxmm(uc, 3, 0x0706050403020100ULL, 0x0f0e0d0c0b0a0908ULL); /* FIPS-197 C.1 key */
    nk_setxmm(uc, 7, FIPS_PT_LO, FIPS_PT_HI);
    nk_setreg(uc, UC_X86_REG_RAX, 0);
    nk_setreg(uc, UC_X86_REG_RCX, 0);
    OK(nk_run(uc, sizeof(code) - 1));
    TEST_CHECK(intr.count == 0);
    nk_chkxmm(uc, 0, 0, 0);
    nk_chkxmm(uc, 1, 0x634224a78ec0fc82ULL, 0xf011afe7aa419640ULL);
    nk_chkxmm(uc, 2, 0x07d7a1ff305028f3ULL, 0xfcd0b2449592ab3fULL);
    nk_chkxmm(uc, 5, FIPS128_CT_LO, FIPS128_CT_HI);
    nk_chkxmm(uc, 7, FIPS_PT_LO, FIPS_PT_HI);
    TEST_CHECK(nk_reg(uc, UC_X86_REG_R8) == 0 && nk_reg(uc, UC_X86_REG_R9) == 0);
    OK(uc_close(uc));
}

/* U100: ENCODEKEY256 (restrictions 5) handle, AESENCWIDE256KL / AESDECWIDE256KL == FIPS-197 C.3 */
static void test_x86_keylocker_wide256(void)
{
    char code[256];
    size_t n = 0;
    const char head[] = KL_LOADIWKEY_X1_X2 "\x66\x0f\x6f\xc3" "\x66\x0f\x6f\xcc"
                        KL_ENCODEKEY256_EAX_ECX KL_STORE_HANDLE4;
    const char wide_enc[] = "\xf3\x0f\x38\xd8\x16" NK_SETZ_R8B;   /* aesencwide256kl [rsi] */
    const char wide_dec[] = "\xf3\x0f\x38\xd8\x1e" NK_SETZ_R9B;   /* aesdecwide256kl [rsi] */
    uint64_t pt[2] = {FIPS_PT_LO, FIPS_PT_HI}, hnd[8];
    nk_intr_t intr;
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
    uc = nk_open(code, n, &intr);
    kl_set_iwkey_inputs(uc);
    nk_setxmm(uc, 3, 0x0706050403020100ULL, 0x0f0e0d0c0b0a0908ULL); /* FIPS-197 C.3 key */
    nk_setxmm(uc, 4, 0x1716151413121110ULL, 0x1f1e1d1c1b1a1918ULL);
    nk_setreg(uc, UC_X86_REG_RCX, 5); /* CPL0-only + no-decrypt (CPL 0 here) */
    OK(uc_mem_write(uc, NK_HANDLE + 0x100, pt, sizeof(pt)));
    OK(nk_run(uc, n));
    TEST_CHECK(intr.count == 0);
    OK(uc_mem_read(uc, NK_HANDLE, hnd, sizeof(hnd)));
    TEST_CHECK(hnd[0] == 0x0000000001000005ULL && hnd[1] == 0);
    TEST_CHECK(hnd[2] == 0x01e20cd8b6763808ULL && hnd[3] == 0x7813f6924c30cb0eULL);
    TEST_CHECK(hnd[4] == 0x2ff383ae0a96b9afULL && hnd[5] == 0x43b14021c4e562ecULL);
    TEST_CHECK(hnd[6] == 0xddea96a39ab8e43fULL && hnd[7] == 0x47614f4794c81039ULL);
    for (i = 0; i < 8; i++) {
        nk_chkxmm(uc, i, FIPS256_CT_LO, FIPS256_CT_HI);
    }
    TEST_CHECK(nk_reg(uc, UC_X86_REG_R8) == 0);
    /* the no-decrypt handle refuses AESDECWIDE256KL: ZF = 1, XMM0-7 unchanged */
    OK(nk_exec(uc, wide_dec, sizeof(wide_dec) - 1));
    TEST_CHECK(nk_reg(uc, UC_X86_REG_R9) == 1);
    for (i = 0; i < 8; i++) {
        nk_chkxmm(uc, i, FIPS256_CT_LO, FIPS256_CT_HI);
    }
    /* a handle without restrictions decrypts XMM0-7 back */
    for (i = 0; i < 8; i++) {
        nk_setxmm(uc, i, FIPS256_CT_LO, FIPS256_CT_HI);
    }
    nk_setxmm(uc, 0, 0x0706050403020100ULL, 0x0f0e0d0c0b0a0908ULL);
    nk_setxmm(uc, 1, 0x1716151413121110ULL, 0x1f1e1d1c1b1a1918ULL);
    nk_setreg(uc, UC_X86_REG_RCX, 0);
    OK(nk_exec(uc, KL_ENCODEKEY256_EAX_ECX KL_STORE_HANDLE4,
               sizeof(KL_ENCODEKEY256_EAX_ECX KL_STORE_HANDLE4) - 1));
    for (i = 0; i < 8; i++) {
        nk_setxmm(uc, i, FIPS256_CT_LO, FIPS256_CT_HI);
    }
    OK(nk_exec(uc, wide_dec, sizeof(wide_dec) - 1));
    TEST_CHECK(nk_reg(uc, UC_X86_REG_R9) == 0);
    for (i = 0; i < 8; i++) {
        nk_chkxmm(uc, i, FIPS_PT_LO, FIPS_PT_HI);
    }
    OK(uc_close(uc));
}

/* U100: handle violations (ZF = 1, destination unchanged) and #GP/#UD conditions */
static void test_x86_keylocker_faults(void)
{
    const char make[] = KL_ENCODEKEY128_EAX_ECX KL_STORE_HANDLE3;
    const char enc[] = "\xf3\x0f\x38\xdc\x3e" NK_SETZ_R8B;
    const char dec[] = "\xf3\x0f\x38\xdd\x3e" NK_SETZ_R8B;
    const char e256[] = "\xf3\x0f\x38\xdf\x3e" NK_SETZ_R8B;   /* AES-128 handle, AESDEC256KL */
    nk_intr_t intr;
    uc_engine *uc = nk_open(KL_LOADIWKEY_X1_X2, 5, &intr);
    uint8_t b;

    /*
     * a non-zero IWKey: with IWKey = 0 the POLYVAL key is 0 and the integrity
     * check cannot detect a changed ciphertext (Key Locker spec page 45)
     */
    kl_set_iwkey_inputs(uc);
    OK(nk_run(uc, 5));
    /* no-encrypt handle (SRC = 2) */
    nk_setxmm(uc, 0, 0x0706050403020100ULL, 0x0f0e0d0c0b0a0908ULL);
    nk_setreg(uc, UC_X86_REG_RCX, 2);
    OK(nk_exec(uc, make, sizeof(make) - 1));
    nk_setxmm(uc, 7, FIPS_PT_LO, FIPS_PT_HI);
    OK(nk_exec(uc, enc, sizeof(enc) - 1));
    TEST_CHECK(nk_reg(uc, UC_X86_REG_R8) == 1);
    nk_chkxmm(uc, 7, FIPS_PT_LO, FIPS_PT_HI);
    nk_setxmm(uc, 7, FIPS128_CT_LO, FIPS128_CT_HI);
    OK(nk_exec(uc, dec, sizeof(dec) - 1));
    TEST_CHECK(nk_reg(uc, UC_X86_REG_R8) == 0);
    nk_chkxmm(uc, 7, FIPS_PT_LO, FIPS_PT_HI);
    OK(nk_exec(uc, e256, sizeof(e256) - 1));
    TEST_CHECK(nk_reg(uc, UC_X86_REG_R8) == 1);
    /* integrity: one ciphertext bit flipped */
    OK(uc_mem_read(uc, NK_HANDLE + 40, &b, 1));
    b ^= 0x10;
    OK(uc_mem_write(uc, NK_HANDLE + 40, &b, 1));
    OK(nk_exec(uc, dec, sizeof(dec) - 1));
    TEST_CHECK(nk_reg(uc, UC_X86_REG_R8) == 1);
    nk_chkxmm(uc, 7, FIPS_PT_LO, FIPS_PT_HI);
    /* ENCODEKEY128: reserved SRC bit 3 -> #GP */
    nk_setreg(uc, UC_X86_REG_RCX, 8);
    OK(nk_exec(uc, make, sizeof(make) - 1));
    TEST_CHECK(intr.count == 1 && intr.intno == 13);
    /* LOADIWKEY: KeySource 1 (not enumerated) -> #GP */
    memset(&intr, 0, sizeof(intr));
    nk_setreg(uc, UC_X86_REG_RAX, 2);
    OK(nk_exec(uc, KL_LOADIWKEY_X1_X2, 5));
    TEST_CHECK(intr.count == 1 && intr.intno == 13);
    /* memory form of ENCODEKEY128, LOCK, register form of AESDEC128KL, D8 /4 -> #UD */
    TEST_CHECK(nk_fault(uc, &intr, "\xf3\x0f\x38\xfa\x06", 5) == 6);
    TEST_CHECK(nk_fault(uc, &intr, "\xf0\xf3\x0f\x38\xfa\xc1", 6) == 6);
    TEST_CHECK(nk_fault(uc, &intr, "\xf3\x0f\x38\xdd\xc1", 5) == 6);
    TEST_CHECK(nk_fault(uc, &intr, "\xf3\x0f\x38\xd8\x26", 5) == 6);
    OK(uc_close(uc));
}

/* U100: CPL 3: LOADIWKEY #GP; a CPL0-only handle sets ZF; ENCODEKEY works */
static void test_x86_keylocker_cpl3(void)
{
    const char code[] = "\x48\xcf" "\xf3\x0f\x38\xfa\xc1" KL_STORE_HANDLE3 "\xf3\x0f\x38\xdc\x3e" NK_SETZ_R8B;
    const char load[] = "\x48\xcf" KL_LOADIWKEY_X1_X2;
    nk_intr_t intr;
    uc_engine *uc = nk_open(code, sizeof(code) - 1, &intr);

    nk_setup_cpl3(uc);
    nk_setxmm(uc, 0, 0x0706050403020100ULL, 0x0f0e0d0c0b0a0908ULL);
    nk_setxmm(uc, 7, FIPS_PT_LO, FIPS_PT_HI);
    nk_setreg(uc, UC_X86_REG_RCX, 1);
    OK(nk_run(uc, sizeof(code) - 1));
    TEST_CHECK(intr.count == 0);
    TEST_CHECK((nk_reg(uc, UC_X86_REG_CS) & 3) == 3);
    TEST_CHECK(nk_reg(uc, UC_X86_REG_R8) == 1);
    nk_chkxmm(uc, 7, FIPS_PT_LO, FIPS_PT_HI);
    OK(uc_close(uc));

    uc = nk_open(code, sizeof(code) - 1, &intr);
    nk_setup_cpl3(uc);
    nk_setxmm(uc, 0, 0x0706050403020100ULL, 0x0f0e0d0c0b0a0908ULL);
    nk_setxmm(uc, 7, FIPS_PT_LO, FIPS_PT_HI);
    nk_setreg(uc, UC_X86_REG_RCX, 0);
    OK(nk_run(uc, sizeof(code) - 1));
    TEST_CHECK(nk_reg(uc, UC_X86_REG_R8) == 0);
    nk_chkxmm(uc, 7, FIPS128_CT_LO, FIPS128_CT_HI);
    OK(uc_close(uc));

    uc = nk_open(load, sizeof(load) - 1, &intr);
    nk_setup_cpl3(uc);
    OK(nk_run(uc, sizeof(load) - 1));
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
    nk_intr_t intr;
    uc_engine *uc = nk_open(code, sizeof(code) - 1, &intr);
    int i;

    for (i = 0; i < 8; i++) {
        mem[i] = 0xf0f0f0f0ffffffffULL;
    }
    OK(uc_mem_write(uc, NK_HANDLE, mem, sizeof(mem)));
    nk_setreg(uc, UC_X86_REG_RAX, 0x0ff00ff000000001ULL);
    nk_setreg(uc, UC_X86_REG_EFLAGS, 0x8d7);
    OK(nk_run(uc, sizeof(code) - 1));
    TEST_CHECK(intr.count == 0);
    OK(uc_mem_read(uc, NK_HANDLE, mem, sizeof(mem)));
    for (i = 0; i < 8; i++) {
        TEST_CHECK(mem[i] == exp[i]);
        TEST_MSG("slot %d: %016" PRIx64 " expected %016" PRIx64, i, mem[i], exp[i]);
    }
    TEST_CHECK((nk_reg(uc, UC_X86_REG_EFLAGS) & 0x8d7) == 0x8d7);
    /* misaligned: m32 at +2, m64 at +4 -> #GP */
    OK(nk_exec(uc, "\x0f\x38\xfc\x46\x02", 5));
    TEST_CHECK(intr.count == 1 && intr.intno == 13);
    memset(&intr, 0, sizeof(intr));
    OK(nk_exec(uc, "\xf3\x48\x0f\x38\xfc\x46\x04", 7));
    TEST_CHECK(intr.count == 1 && intr.intno == 13);
    /* register form, LOCK -> #UD */
    TEST_CHECK(nk_fault(uc, &intr, "\x0f\x38\xfc\xc0", 4) == 6);
    TEST_CHECK(nk_fault(uc, &intr, "\xf0\x0f\x38\xfc\x06", 5) == 6);
    OK(uc_close(uc));
}

/* U102: MOVRS r, m (64-bit only, NOREP) and PREFETCHRST2 m8 */
static void test_x86_movrs_prefetchrst2(void)
{
    const char code[] = "\x0f\x38\x8b\x06"             /* movrs eax, [rsi] */
                        "\x48\x0f\x38\x8b\x1e"         /* movrs rbx, [rsi] */
                        "\x66\x0f\x38\x8b\x0e"         /* movrs cx, [rsi] */
                        "\x0f\x38\x8a\x16"             /* movrs dl, [rsi] */
                        "\x0f\x38\x8a\x26"             /* movrs ah, [rsi] */
                        "\x44\x0f\x38\x8a\x06"         /* movrs r8b, [rsi] */
                        "\x4c\x0f\x38\x8b\x3e"         /* movrs r15, [rsi] */
                        "\x0f\x18\x26";                /* prefetchrst2 [rsi] */
    const char *bad[] = {"\xf3\x0f\x38\x8b\x06", "\xf2\x0f\x38\x8a\x06", "\xf0\x0f\x38\x8b\x06",
                         "\x0f\x38\x8b\xc0\x90", "\xf0\x0f\x18\x26\x90"};
    uint64_t m = 0x8877665544332211ULL;
    nk_intr_t intr;
    uc_engine *uc = nk_open(code, sizeof(code) - 1, &intr);
    int i;

    OK(uc_mem_write(uc, NK_HANDLE, &m, 8));
    nk_setreg(uc, UC_X86_REG_RAX, ~0ULL);
    nk_setreg(uc, UC_X86_REG_RCX, ~0ULL);
    nk_setreg(uc, UC_X86_REG_RDX, ~0ULL);
    nk_setreg(uc, UC_X86_REG_R8, ~0ULL);
    nk_setreg(uc, UC_X86_REG_EFLAGS, 0x8d7);
    OK(nk_run(uc, sizeof(code) - 1));
    TEST_CHECK(intr.count == 0);
    TEST_CHECK(nk_reg(uc, UC_X86_REG_RAX) == 0x44331111ULL);
    TEST_CHECK(nk_reg(uc, UC_X86_REG_RBX) == m);
    TEST_CHECK(nk_reg(uc, UC_X86_REG_RCX) == 0xffffffffffff2211ULL);
    TEST_CHECK(nk_reg(uc, UC_X86_REG_RDX) == 0xffffffffffffff11ULL);
    TEST_CHECK(nk_reg(uc, UC_X86_REG_R8) == 0xffffffffffffff11ULL);
    TEST_CHECK(nk_reg(uc, UC_X86_REG_R15) == m);
    TEST_CHECK((nk_reg(uc, UC_X86_REG_EFLAGS) & 0x8d7) == 0x8d7);
    for (i = 0; i < 5; i++) {
        TEST_CHECK(nk_fault(uc, &intr, bad[i], 5) == 6);
        TEST_MSG("case %d", i);
    }
    OK(uc_close(uc));

    /* not encodable outside 64-bit mode */
    OK(uc_open(UC_ARCH_X86, UC_MODE_32, &uc));
    OK(uc_ctl_set_cpu_model(uc, UC_CPU_X86_MAX));
    OK(uc_mem_map(uc, code_start, code_len, UC_PROT_ALL));
    OK(uc_mem_write(uc, code_start, "\x0f\x38\x8b\x06", 4));
    uc_assert_err(UC_ERR_INSN_INVALID, uc_emu_start(uc, code_start, code_start + 4, 0, 0));
    OK(uc_close(uc));
}

static void nk_wrmsr(uc_engine *uc, uint32_t msr, uint64_t value)
{
    uc_x86_msr m = {msr, value};

    OK(uc_reg_write(uc, UC_X86_REG_MSR, &m));
}

static uint64_t nk_rdmsr(uc_engine *uc, uint32_t msr)
{
    uc_x86_msr m = {msr, 0};

    OK(uc_reg_read(uc, UC_X86_REG_MSR, &m));
    return m.value;
}

/*
 * runs code from its own 64-byte slot of the code page (never rewriting code
 * that already ran); returns the fault vector, -1 none, 6 for #UD
 */
static int nk_fault(uc_engine *uc, nk_intr_t *intr, const char *code, size_t len)
{
    uint64_t at = nk_slot();
    uc_err e;

    memset(intr, 0, sizeof(*intr));
    OK(uc_mem_write(uc, at, code, len));
    e = uc_emu_start(uc, at, at + len, 0, 0);
    if (e == UC_ERR_INSN_INVALID) {
        return 6;
    }
    OK(e);
    return intr->count ? (int)intr->intno : -1;
}

/* U103: URDMSR / UWRMSR, IA32_USER_MSR_CTL bitmap, IA32_UARCH_MISC_CTL */
static void test_x86_user_msr(void)
{
    const char code[] = "\xf2\x0f\x38\xf8\xcb"                     /* urdmsr rbx, rcx */
                        "\xc4\xe7\x7a\xf8\xc7\x01\x1b\x00\x00"     /* uwrmsr 0x1b01, rdi */
                        "\xc4\xe7\x7b\xf8\xc2\x01\x1b\x00\x00"     /* urdmsr rdx, 0x1b01 */
                        "\xf3\x45\x0f\x38\xf8\xca"                 /* uwrmsr r9, r10 */
                        "\xf2\x45\x0f\x38\xf8\xcb"                 /* urdmsr r11, r9 */
                        "\xb8\x07\x00\x00\x00\xb9\x01\x00\x00\x00\x0f\xa2"; /* cpuid 7.1 */
    const uint64_t ctl = (NK_DATA + 0x2000) | 1;
    uint8_t bits = 0x10;
    nk_intr_t intr;
    uc_engine *uc = nk_open(code, sizeof(code) - 1, &intr);

    /* ENABLE = 0 (reset): #UD */
    nk_setreg(uc, UC_X86_REG_RCX, 0x1c);
    TEST_CHECK(nk_fault(uc, &intr, code, 5) == 6);
    /* bitmap: read 1CH and 1B01H, write 1B01H */
    OK(uc_mem_write(uc, NK_DATA + 0x2000 + 3, &bits, 1));
    bits = 0x02;
    OK(uc_mem_write(uc, NK_DATA + 0x2000 + 0x360, &bits, 1));
    OK(uc_mem_write(uc, NK_DATA + 0x2000 + 0xb60, &bits, 1));
    nk_wrmsr(uc, 0x1c, ctl);
    TEST_CHECK(nk_rdmsr(uc, 0x1c) == ctl);
    nk_setreg(uc, UC_X86_REG_RDI, 1);
    nk_setreg(uc, UC_X86_REG_R9, 0x1b01);
    nk_setreg(uc, UC_X86_REG_R10, 0);
    nk_setreg(uc, UC_X86_REG_R11, ~0ULL);
    nk_setreg(uc, UC_X86_REG_RCX, 0x1c);
    TEST_CHECK(nk_fault(uc, &intr, code, sizeof(code) - 1) == -1);
    TEST_CHECK(nk_reg(uc, UC_X86_REG_RDX) & (1u << 15));    /* CPUID.(07H,01H):EDX.USER_MSR */
    OK(uc_mem_write(uc, code_start, code, 5 + 9 + 9 + 6 + 6));
    nk_setreg(uc, UC_X86_REG_RCX, 0x1c);
    OK(nk_run(uc, 5 + 9 + 9 + 6 + 6));
    TEST_CHECK(nk_reg(uc, UC_X86_REG_RBX) == ctl);
    TEST_CHECK(nk_reg(uc, UC_X86_REG_RDX) == 1);             /* DOITM written by the imm form */
    TEST_CHECK(nk_reg(uc, UC_X86_REG_R11) == 0);             /* cleared by the register form */
    TEST_CHECK(nk_rdmsr(uc, 0x1b01) == 0);
    /* #GP: bitmap bit clear, address >= 4000H, UWRMSR of an MSR other than 1B01H, reserved DOITM bits */
    nk_setreg(uc, UC_X86_REG_RCX, 0x10);
    TEST_CHECK(nk_fault(uc, &intr, code, 5) == 13);
    nk_setreg(uc, UC_X86_REG_RCX, 0x4000);
    TEST_CHECK(nk_fault(uc, &intr, code, 5) == 13);
    bits = 0x10;
    OK(uc_mem_write(uc, NK_DATA + 0x2000 + 0x803, &bits, 1));
    nk_setreg(uc, UC_X86_REG_R9, 0x1c);
    TEST_CHECK(nk_fault(uc, &intr, "\xf3\x45\x0f\x38\xf8\xca", 6) == 13);
    nk_setreg(uc, UC_X86_REG_RDI, 2);
    TEST_CHECK(nk_fault(uc, &intr, "\xc4\xe7\x7a\xf8\xc7\x01\x1b\x00\x00", 9) == 13);
    /* memory form is ENQCMD since U112: IA32_PASID[31] = 0 -> #GP(0) (SDM ENQCMD) */
    TEST_CHECK(nk_fault(uc, &intr, "\xf2\x0f\x38\xf8\x0b", 5) == 13);
    /* #UD: VEX.L1, VEX.W1, ModRM.reg != 0, vvvv != 1111b, LOCK */
    TEST_CHECK(nk_fault(uc, &intr, "\xc4\xe7\x7f\xf8\xc2\x01\x1b\x00\x00", 9) == 6);
    TEST_CHECK(nk_fault(uc, &intr, "\xc4\xe7\xfb\xf8\xc2\x01\x1b\x00\x00", 9) == 6);
    TEST_CHECK(nk_fault(uc, &intr, "\xc4\xe7\x7b\xf8\xca\x01\x1b\x00\x00", 9) == 6);
    TEST_CHECK(nk_fault(uc, &intr, "\xc4\xe7\x73\xf8\xc2\x01\x1b\x00\x00", 9) == 6);
    TEST_CHECK(nk_fault(uc, &intr, "\xf0\xf2\x0f\x38\xf8\xcb", 6) == 6);
    /* WRMSR IA32_USER_MSR_CTL: reserved bit 1 / non-canonical bitmap address -> #GP */
    nk_setreg(uc, UC_X86_REG_RCX, 0x1c);
    nk_setreg(uc, UC_X86_REG_RAX, 2);
    nk_setreg(uc, UC_X86_REG_RDX, 0);
    TEST_CHECK(nk_fault(uc, &intr, "\x0f\x30", 2) == 13);
    nk_setreg(uc, UC_X86_REG_RAX, 1);
    nk_setreg(uc, UC_X86_REG_RDX, 0x10000000);
    TEST_CHECK(nk_fault(uc, &intr, "\x0f\x30", 2) == 13);
    TEST_CHECK(nk_rdmsr(uc, 0x1c) == ctl);
    OK(uc_close(uc));
}

/* U104: CLUI / STUI / TESTUI, CR4.UINTR, CPUID bits */
static void test_x86_uintr_uif(void)
{
    const char code[] = "\xf3\x0f\x01\xed\x41\x0f\x92\xc0"     /* testui; setc r8b */
                        "\xf3\x0f\x01\xef"                     /* stui */
                        "\xf3\x0f\x01\xed\x41\x0f\x92\xc1"     /* testui; setc r9b */
                        "\xf3\x0f\x01\xee"                     /* clui */
                        "\xf3\x0f\x01\xed\x41\x0f\x92\xc2"     /* testui; setc r10b */
                        "\xf3\x0f\x01\xef\xf3\x0f\x01\xed"     /* stui; testui */
                        "\xb8\x07\x00\x00\x00\x31\xc9\x0f\xa2\x41\x89\xd3"     /* cpuid 7.0 -> r11d = edx */
                        "\xb8\x07\x00\x00\x00\xb9\x01\x00\x00\x00\x0f\xa2"; /* cpuid 7.1 */
    nk_intr_t intr;
    uc_engine *uc = nk_open(code, sizeof(code) - 1, &intr);
    uint64_t cr4 = nk_reg(uc, UC_X86_REG_CR4);

    TEST_CHECK(cr4 & (1ULL << 25));
    TEST_CHECK(nk_fault(uc, &intr, code, sizeof(code) - 1) == -1);
    TEST_CHECK(nk_reg(uc, UC_X86_REG_R8) == 0);
    TEST_CHECK(nk_reg(uc, UC_X86_REG_R9) == 1);
    TEST_CHECK(nk_reg(uc, UC_X86_REG_R10) == 0);
    TEST_CHECK(nk_reg(uc, UC_X86_REG_R11) & (1u << 5));       /* CPUID.(07H,0):EDX.UINTR */
    TEST_CHECK(nk_reg(uc, UC_X86_REG_RDX) & (1u << 17));      /* CPUID.(07H,01H):EDX.UIRET_UIF */
    /* TESTUI: CF := UIF, ZF AF OF PF SF := 0 */
    nk_setreg(uc, UC_X86_REG_EFLAGS, 0xad7);
    TEST_CHECK(nk_fault(uc, &intr, "\xf3\x0f\x01\xed", 4) == -1);
    TEST_CHECK((nk_reg(uc, UC_X86_REG_EFLAGS) & 0xfff) == 0x203);
    /* LOCK, 66 -> #UD; CR4.UINTR = 0 -> #UD */
    TEST_CHECK(nk_fault(uc, &intr, "\xf0\xf3\x0f\x01\xed", 5) == 6);
    TEST_CHECK(nk_fault(uc, &intr, "\x66\xf3\x0f\x01\xee", 5) == 6);
    nk_setreg(uc, UC_X86_REG_CR4, cr4 & ~(1ULL << 25));
    TEST_CHECK(nk_fault(uc, &intr, "\xf3\x0f\x01\xef", 4) == 6);
    TEST_CHECK(nk_fault(uc, &intr, "\xf3\x0f\x01\xec", 4) == 6);
    TEST_CHECK(nk_fault(uc, &intr, "\xf3\x0f\xc7\xf0", 4) == 6);
    OK(uc_close(uc));

    /* not recognized outside 64-bit mode */
    OK(uc_open(UC_ARCH_X86, UC_MODE_32, &uc));
    OK(uc_ctl_set_cpu_model(uc, UC_CPU_X86_MAX));
    OK(uc_mem_map(uc, code_start, code_len, UC_PROT_ALL));
    OK(uc_mem_write(uc, code_start, "\xf3\x0f\x01\xed", 4));
    uc_assert_err(UC_ERR_INSN_INVALID, uc_emu_start(uc, code_start, code_start + 4, 0, 0));
    OK(uc_close(uc));
}

#define NK_UITT (NK_DATA + 0x3000)
#define NK_UPID (NK_DATA + 0x3100)
#define NK_UPID2 (NK_DATA + 0x3140)

static void uintr_setup_tables(uc_engine *uc, uint64_t upid_lo)
{
    uint64_t uitt[4] = {1 | (5 << 8), NK_UPID, 1 | (7 << 8), NK_UPID2};
    uint64_t upid[2] = {upid_lo, 0};
    uint64_t upid2[2] = {0xecULL << 16, 0};

    OK(uc_mem_write(uc, NK_UITT, uitt, sizeof(uitt)));
    OK(uc_mem_write(uc, NK_UPID, upid, sizeof(upid)));
    OK(uc_mem_write(uc, NK_UPID2, upid2, sizeof(upid2)));
    nk_wrmsr(uc, 0x98a, NK_UITT | 1);           /* IA32_UINTR_TT: UITTADDR, SENDUIPI enable */
    nk_wrmsr(uc, 0x988, 1 | (0x40ULL << 32));   /* IA32_UINTR_MISC: UITTSZ 1, UINV 40H */
    nk_wrmsr(uc, 0x989, NK_UPID);               /* IA32_UINTR_PD */
}

/* U104: SENDUIPI posting, self notification (NV = UINV) and its #GP / #UD conditions */
static void test_x86_uintr_senduipi(void)
{
    const char send_rax[] = "\xf3\x0f\xc7\xf0";
    const char send_rcx[] = "\x66\xf3\x0f\xc7\xf1";  /* 66 is ignored */
    uint64_t u[2];
    nk_intr_t intr;
    uc_engine *uc = nk_open(send_rax, 4, &intr);

    /* SENDUIPI #UD while IA32_UINTR_TT[0] = 0 */
    TEST_CHECK(nk_fault(uc, &intr, send_rax, 4) == 6);
    uintr_setup_tables(uc, 0x40ULL << 16);      /* NV = UINV, NDST = 0 (own APIC ID) */
    nk_setreg(uc, UC_X86_REG_EFLAGS, 0x202);
    nk_setreg(uc, UC_X86_REG_RAX, 0);
    TEST_CHECK(nk_fault(uc, &intr, send_rax, 4) == -1);
    /* posted (PIR[5], ON) then processed as a notification: ON := 0, PIR -> UIRR */
    OK(uc_mem_read(uc, NK_UPID, u, sizeof(u)));
    TEST_CHECK(u[0] == (0x40ULL << 16) && u[1] == 0);
    TEST_CHECK(nk_rdmsr(uc, 0x985) == (1ULL << 5));
    /* NV != UINV: posted, ON set, the IPI is not a notification (dropped) */
    nk_setreg(uc, UC_X86_REG_RCX, 1);
    TEST_CHECK(nk_fault(uc, &intr, send_rcx, 5) == -1);
    OK(uc_mem_read(uc, NK_UPID2, u, sizeof(u)));
    TEST_CHECK(u[0] == ((0xecULL << 16) | 1) && u[1] == (1ULL << 7));
    TEST_CHECK(nk_rdmsr(uc, 0x985) == (1ULL << 5));
    /* ON already set: only PIR is updated */
    OK(uc_mem_write(uc, NK_UITT + 16, "\x01\x09", 2));          /* UV = 9 */
    TEST_CHECK(nk_fault(uc, &intr, send_rcx, 5) == -1);
    OK(uc_mem_read(uc, NK_UPID2, u, sizeof(u)));
    TEST_CHECK(u[0] == ((0xecULL << 16) | 1) && u[1] == ((1ULL << 7) | (1ULL << 9)));
    /* self notification while RFLAGS.IF = 0: posted, ON stays 1, UIRR unchanged */
    nk_wrmsr(uc, 0x985, 0);
    nk_setreg(uc, UC_X86_REG_EFLAGS, 0x2);
    TEST_CHECK(nk_fault(uc, &intr, send_rax, 4) == -1);
    OK(uc_mem_read(uc, NK_UPID, u, sizeof(u)));
    TEST_CHECK(u[0] == ((0x40ULL << 16) | 1) && u[1] == (1ULL << 5));
    TEST_CHECK(nk_rdmsr(uc, 0x985) == 0);
    /* #GP: index > UITTSZ, invalid UITTE, reserved UPID bits */
    nk_setreg(uc, UC_X86_REG_RAX, 2);
    TEST_CHECK(nk_fault(uc, &intr, send_rax, 4) == 13);
    nk_setreg(uc, UC_X86_REG_RAX, 0);
    OK(uc_mem_write(uc, NK_UITT, "\x00", 1));                  /* V = 0 */
    TEST_CHECK(nk_fault(uc, &intr, send_rax, 4) == 13);
    OK(uc_mem_write(uc, NK_UITT, "\x01\x45", 2));              /* UV = 45H: bit 14 set */
    TEST_CHECK(nk_fault(uc, &intr, send_rax, 4) == 13);
    OK(uc_mem_write(uc, NK_UITT, "\x01\x05", 2));
    OK(uc_mem_write(uc, NK_UPID, "\x04", 1));                  /* UPID bit 2 */
    TEST_CHECK(nk_fault(uc, &intr, send_rax, 4) == 13);
    /* LOCK #UD; F3 0F C7 /6 with a memory operand (VMXON) #UD */
    TEST_CHECK(nk_fault(uc, &intr, "\xf0\xf3\x0f\xc7\xf0", 5) == 6);
    TEST_CHECK(nk_fault(uc, &intr, "\xf3\x0f\xc7\x36", 4) == 6);
    /* WRMSR #GP: IA32_UINTR_MISC[63:40], IA32_UINTR_PD[5:0], IA32_UINTR_TT[3:1] */
    nk_setreg(uc, UC_X86_REG_RCX, 0x988);
    nk_setreg(uc, UC_X86_REG_RAX, 0);
    nk_setreg(uc, UC_X86_REG_RDX, 0x100);
    TEST_CHECK(nk_fault(uc, &intr, "\x0f\x30", 2) == 13);
    nk_setreg(uc, UC_X86_REG_RCX, 0x989);
    nk_setreg(uc, UC_X86_REG_RAX, 0x20);
    nk_setreg(uc, UC_X86_REG_RDX, 0);
    TEST_CHECK(nk_fault(uc, &intr, "\x0f\x30", 2) == 13);
    nk_setreg(uc, UC_X86_REG_RCX, 0x98a);
    nk_setreg(uc, UC_X86_REG_RAX, 0x3);
    TEST_CHECK(nk_fault(uc, &intr, "\x0f\x30", 2) == 13);
    OK(uc_close(uc));
}

/*
 * U104: user-interrupt delivery at CPL 3 after a self-SENDUIPI, the handler's
 * stack frame, UIF, UIRET (with and without RFLAGS[1] cleared in the frame)
 */
static void uintr_delivery_run(bool clear_uif_bit)
{
    char code[96];
    size_t n = 0, jmp_at, handler, end;
    uint64_t frame[4];
    nk_intr_t intr;
    uc_engine *uc;

#define EMIT(s) do { memcpy(code + n, s, sizeof(s) - 1); n += sizeof(s) - 1; } while (0)
    EMIT("\x48\xcf");                           /* iretq -> CPL 3 */
    EMIT("\xf3\x0f\x01\xef");                   /* stui */
    EMIT("\xf3\x0f\xc7\xf0");                   /* senduipi rax */
    EMIT("\x41\xbf\x01\x00\x00\x00");           /* mov r15d, 1 (after UIRET) */
    EMIT("\xf3\x0f\x01\xed\x41\x0f\x92\xc6");   /* testui; setc r14b */
    jmp_at = n;
    EMIT("\xeb\x00");                           /* jmp end */
    handler = n;
    EMIT("\x5b");                               /* pop rbx (vector) */
    EMIT("\xf3\x0f\x01\xed\x41\x0f\x92\xc5");   /* testui; setc r13b */
    EMIT("\x48\x89\xe5");                       /* mov rbp, rsp */
    if (clear_uif_bit) {
        EMIT("\x48\x83\x64\x24\x08\xfd");       /* and qword ptr [rsp+8], -3 */
    }
    EMIT("\xf3\x0f\x01\xec");                   /* uiret */
    end = n;
    code[jmp_at + 1] = (char)(end - handler);
#undef EMIT

    uc = nk_open(code, n, &intr);
    nk_setup_cpl3(uc);
    uintr_setup_tables(uc, 0x40ULL << 16);
    nk_wrmsr(uc, 0x986, code_start + handler);  /* UIHANDLER */
    nk_wrmsr(uc, 0x987, 0x80);                  /* UISTACKADJUST: RSP -= 80H */
    nk_setreg(uc, UC_X86_REG_RAX, 0);
    nk_setreg(uc, UC_X86_REG_R13, 0x55);
    OK(uc_emu_start(uc, code_start, code_start + end, 0, 0));
    TEST_CHECK(intr.count == 0);
    TEST_CHECK(nk_reg(uc, UC_X86_REG_RBX) == 5);             /* UIRRV */
    TEST_CHECK(nk_reg(uc, UC_X86_REG_R13) == 0);             /* UIF = 0 in the handler */
    TEST_CHECK(nk_reg(uc, UC_X86_REG_R15) == 1);             /* returned after SENDUIPI */
    TEST_CHECK(nk_reg(uc, UC_X86_REG_R14) == (clear_uif_bit ? 0 : 1));
    TEST_CHECK(nk_reg(uc, UC_X86_REG_RSP) == NK_STACK);      /* restored by UIRET */
    TEST_CHECK(nk_reg(uc, UC_X86_REG_RBP) == NK_STACK - 0x80 - 24);
    OK(uc_mem_read(uc, NK_STACK - 0x80 - 32, frame, sizeof(frame)));
    TEST_CHECK(frame[0] == 5);
    TEST_CHECK(frame[1] == code_start + 10);                 /* RIP after SENDUIPI */
    TEST_CHECK((frame[2] & ~2ULL) == 0x200);                 /* RFLAGS (IF) */
    TEST_CHECK(frame[3] == NK_STACK);                        /* old RSP */
    TEST_CHECK(nk_rdmsr(uc, 0x985) == 0);                    /* UIRR[5] cleared */
    OK(uc_close(uc));
}

static void test_x86_uintr_delivery(void)
{
    uintr_delivery_run(false);
    uintr_delivery_run(true);
}

/*
 * NoVmp U110-U117 test helpers: one engine with code at code_start, data at
 * 0x200000 (8 KiB) and an interrupt hook that records the vector and stops.
 * Raw Unicorn reports #UD as UC_ERR_INSN_INVALID; other exceptions reach the
 * hook (count/intno) with RIP at the faulting instruction.
 */
typedef struct NvRun {
    uc_engine *uc;
    X86IntrCapture cap;
    uc_hook hook;
    uint64_t next, last; /* snippet slots: a fresh address per run (no stale TBs) */
} NvRun;

static void nv_open(NvRun *r, uc_mode mode, int model)
{
    memset(r, 0, sizeof(*r));
    OK(uc_open(UC_ARCH_X86, mode, &r->uc));
    if (model >= 0) {
        OK(uc_ctl_set_cpu_model(r->uc, model));
    }
    OK(uc_mem_map(r->uc, code_start, code_len, UC_PROT_ALL));
    OK(uc_mem_map(r->uc, 0x200000, 0x2000, UC_PROT_ALL));
    OK(uc_hook_add(r->uc, &r->hook, UC_HOOK_INTR, test_x86_intr_capture_cb,
                   &r->cap, 1, 0));
    r->next = code_start;
}

static uc_err nv_run_at(NvRun *r, uint64_t addr, const char *code, size_t len)
{
    r->cap.count = 0;
    r->cap.intno = 0;
    r->last = addr;
    OK(uc_mem_write(r->uc, addr, code, len));
    return uc_emu_start(r->uc, addr, addr + len, 0, 0);
}

static uc_err nv_run_n(NvRun *r, const char *code, size_t len)
{
    uint64_t addr = r->next;

    r->next += (len + 0x3f) & ~(uint64_t)0x3f;
    return nv_run_at(r, addr, code, len);
}

#define nv_run(r, code) nv_run_n((r), (code), sizeof(code) - 1)

static uint64_t nv_get(NvRun *r, int reg)
{
    uint64_t v = 0;
    OK(uc_reg_read(r->uc, reg, &v));
    return v;
}

static void nv_set(NvRun *r, int reg, uint64_t v)
{
    OK(uc_reg_write(r->uc, reg, &v));
}

/* CPUID profile: max basic leaf 7, leaf 7.0 = (ebx, ecx, edx), strict #UD on hidden features */
static void nv_profile7(NvRun *r, uint32_t ebx, uint32_t ecx, uint32_t edx)
{
    uc_x86_cpuid p[2] = {
        {0, 0, 7, 0x756e6547, 0x6c65746e, 0x49656e69},
        {7, 0, 0, 0, 0, 0},
    };

    p[1].ebx = ebx;
    p[1].ecx = ecx;
    p[1].edx = edx;
    OK(uc_ctl_set_x86_cpuid(r->uc, p, 2));
    OK(uc_ctl_set_x86_cpuid_strict(r->uc, 1));
}

/*
 * NoVmp U110: RTM with every transaction aborting at XBEGIN (RTM_ALWAYS_ABORT),
 * HLE hints ignored, TSXLDTRK NOPs (SDM Vol2 XBEGIN/XABORT/XEND/XTEST/
 * XSUSLDTRK/XRESLDTRK/XACQUIRE-XRELEASE; Vol1 17.3.5).
 */
static void test_x86_tsx_rtm_always_abort(void)
{
    NvRun r;
    uint32_t m0 = 0, m1 = 0;

    nv_open(&r, UC_MODE_64, UC_CPU_X86_MAX);

    /* CPUID.7.0: EBX HLE[4] RTM[11], EDX RTM_ALWAYS_ABORT[11] TSXLDTRK[16] */
    nv_set(&r, UC_X86_REG_RAX, 7);
    nv_set(&r, UC_X86_REG_RCX, 0);
    OK(nv_run(&r, "\x0f\xa2"));
    TEST_CHECK((nv_get(&r, UC_X86_REG_RBX) & 0x810) == 0x810);
    TEST_CHECK((nv_get(&r, UC_X86_REG_RDX) & 0x10800) == 0x10800);

    /* xbegin +2 (rel32); inc ebx; inc rcx: aborts, EAX = 0 (RAX[63:32] = 0), at the fallback */
    nv_set(&r, UC_X86_REG_RAX, 0xffffffff12345678ull);
    nv_set(&r, UC_X86_REG_RBX, 0);
    nv_set(&r, UC_X86_REG_RCX, 0);
    nv_set(&r, UC_X86_REG_RFLAGS, 0x897);
    OK(nv_run(&r, "\xc7\xf8\x02\x00\x00\x00\xff\xc3\x48\xff\xc1"));
    TEST_CHECK(nv_get(&r, UC_X86_REG_RAX) == 0);
    TEST_CHECK(nv_get(&r, UC_X86_REG_RBX) == 0);
    TEST_CHECK(nv_get(&r, UC_X86_REG_RCX) == 1);
    TEST_CHECK(r.cap.count == 0);

    /* 66 xbegin rel16 high in memory: the fallback address is not truncated to 16 bits */
    nv_set(&r, UC_X86_REG_RAX, 0x55);
    nv_set(&r, UC_X86_REG_RBX, 0);
    nv_set(&r, UC_X86_REG_RCX, 0);
    OK(uc_mem_map(r.uc, 0x7ffffffff000ull, 0x1000, UC_PROT_ALL));
    OK(nv_run_at(&r, 0x7ffffffff000ull,
                 "\x66\xc7\xf8\x02\x00\xff\xc3\x48\xff\xc1", 10));
    TEST_CHECK(nv_get(&r, UC_X86_REG_RAX) == 0);
    TEST_CHECK(nv_get(&r, UC_X86_REG_RBX) == 0);
    TEST_CHECK(nv_get(&r, UC_X86_REG_RCX) == 1);

    /* non-canonical fallback: #GP(0), EAX and RIP unchanged */
    nv_set(&r, UC_X86_REG_RAX, 0x55);
    OK(nv_run_at(&r, 0x7ffffffff100ull, "\xc7\xf8\x00\x00\x10\x00", 6));
    TEST_CHECK(r.cap.count == 1 && r.cap.intno == 13);
    TEST_CHECK(nv_get(&r, UC_X86_REG_RAX) == 0x55);
    TEST_CHECK(nv_get(&r, UC_X86_REG_RIP) == 0x7ffffffff100ull);

    /* LOCK xbegin: #UD */
    uc_assert_err(UC_ERR_INSN_INVALID, nv_run(&r, "\xf0\xc7\xf8\x00\x00\x00\x00"));

    /* xabort 0x55 outside a transaction: NOP */
    nv_set(&r, UC_X86_REG_RAX, 0x1234);
    nv_set(&r, UC_X86_REG_RCX, 0);
    OK(nv_run(&r, "\xc6\xf8\x55\x48\xff\xc1"));
    TEST_CHECK(nv_get(&r, UC_X86_REG_RAX) == 0x1234);
    TEST_CHECK(nv_get(&r, UC_X86_REG_RCX) == 1);

    /* xend outside a transaction: #GP(0) at the XEND; 66 xend: #UD */
    OK(nv_run(&r, "\x0f\x01\xd5"));
    TEST_CHECK(r.cap.count == 1 && r.cap.intno == 13);
    TEST_CHECK(nv_get(&r, UC_X86_REG_RIP) == r.last);
    uc_assert_err(UC_ERR_INSN_INVALID, nv_run(&r, "\x66\x0f\x01\xd5"));

    /* xtest: ZF = 1, CF/OF/SF/PF/AF = 0 */
    nv_set(&r, UC_X86_REG_RFLAGS, 0x897);
    OK(nv_run(&r, "\x0f\x01\xd6"));
    TEST_CHECK((nv_get(&r, UC_X86_REG_RFLAGS) & 0xfff) == 0x042);
    uc_assert_err(UC_ERR_INSN_INVALID, nv_run(&r, "\xf3\x0f\x01\xd6"));

    /* xsusldtrk; xresldtrk; inc rcx: NOPs outside a transaction */
    nv_set(&r, UC_X86_REG_RCX, 0);
    OK(nv_run(&r, "\xf2\x0f\x01\xe8\xf2\x0f\x01\xe9\x48\xff\xc1"));
    TEST_CHECK(nv_get(&r, UC_X86_REG_RCX) == 1);
    uc_assert_err(UC_ERR_INSN_INVALID, nv_run(&r, "\xf0\xf2\x0f\x01\xe8"));
    uc_assert_err(UC_ERR_INSN_INVALID, nv_run(&r, "\x0f\x01\xe9"));

    /* xacquire lock add [rdx], eax; xrelease mov [rdx+4], eax: plain instructions */
    nv_set(&r, UC_X86_REG_RAX, 5);
    nv_set(&r, UC_X86_REG_RDX, 0x200000);
    OK(uc_mem_write(r.uc, 0x200000, "\x01\x00\x00\x00\x00\x00\x00\x00", 8));
    OK(nv_run(&r, "\xf2\xf0\x01\x02\xf3\x89\x42\x04"));
    OK(uc_mem_read(r.uc, 0x200000, &m0, 4));
    OK(uc_mem_read(r.uc, 0x200004, &m1, 4));
    TEST_CHECK(m0 == 6 && m1 == 5);

    /* strict profile with HLE only: XTEST runs, XBEGIN/XABORT/XEND/XSUSLDTRK #UD */
    nv_profile7(&r, 1u << 4, 0, 0);
    OK(nv_run(&r, "\x0f\x01\xd6"));
    uc_assert_err(UC_ERR_INSN_INVALID, nv_run(&r, "\xc7\xf8\x00\x00\x00\x00"));
    uc_assert_err(UC_ERR_INSN_INVALID, nv_run(&r, "\xc6\xf8\x00"));
    uc_assert_err(UC_ERR_INSN_INVALID, nv_run(&r, "\x0f\x01\xd5"));
    uc_assert_err(UC_ERR_INSN_INVALID, nv_run(&r, "\xf2\x0f\x01\xe8"));
    /* i5-13600K-like profile (no HLE/RTM/TSXLDTRK): XTEST #UD */
    nv_profile7(&r, 0, 0, 0);
    uc_assert_err(UC_ERR_INSN_INVALID, nv_run(&r, "\x0f\x01\xd6"));
    OK(uc_close(r.uc));

    /* 32-bit mode: 66 xbegin rel16 above 64 KiB, fallback EIP not truncated */
    nv_open(&r, UC_MODE_32, UC_CPU_X86_MAX);
    nv_set(&r, UC_X86_REG_EAX, 0x55);
    nv_set(&r, UC_X86_REG_EBX, 0);
    nv_set(&r, UC_X86_REG_ECX, 0);
    OK(uc_mem_map(r.uc, 0x10000, 0x1000, UC_PROT_ALL));
    OK(nv_run_at(&r, 0x10ff0, "\x66\xc7\xf8\x02\x00\x43\x43\x41", 8));
    TEST_CHECK(nv_get(&r, UC_X86_REG_EAX) == 0);
    TEST_CHECK(nv_get(&r, UC_X86_REG_EBX) == 0);
    TEST_CHECK(nv_get(&r, UC_X86_REG_ECX) == 1);
    OK(uc_close(r.uc));

    /* real-address mode: a fallback beyond 0FFFFh is #GP(0), EAX unchanged */
    nv_open(&r, UC_MODE_16, UC_CPU_X86_MAX);
    nv_set(&r, UC_X86_REG_EAX, 0x55);
    nv_set(&r, UC_X86_REG_ECX, 0);
    OK(uc_mem_map(r.uc, 0xf000, 0x1000, UC_PROT_ALL));
    OK(nv_run_at(&r, 0xff00, "\xc7\xf8\x00\x01", 4));
    TEST_CHECK(r.cap.count == 1 && r.cap.intno == 13);
    TEST_CHECK((uint16_t)nv_get(&r, UC_X86_REG_AX) == 0x55);
    /* ... and within it the abort reaches the fallback */
    OK(nv_run_at(&r, 0xfe00, "\xc7\xf8\x01\x00\x43\x41", 6));
    TEST_CHECK((uint16_t)nv_get(&r, UC_X86_REG_AX) == 0);
    TEST_CHECK((uint16_t)nv_get(&r, UC_X86_REG_CX) == 1);
    OK(uc_close(r.uc));
}

/*
 * NoVmp U111-U117: run a snippet at CPL3 in 64-bit mode. A flat GDT is built
 * at 0x201000 (08h code64 DPL0, 10h data DPL0, 20h data DPL3, 28h code64 DPL3)
 * and a CPL0 stub IRETQs to the snippet with CS = 2Bh, SS = 23h, RSP = 201F00h.
 * The stub's own instructions are not part of the snippet's results.
 */
static void nv_gdt(NvRun *r)
{
    static const uint64_t gdt[6] = {
        0, 0x00209a0000000000ull, 0x0000920000000000ull,
        0, 0x0000f20000000000ull, 0x0020fa0000000000ull,
    };
    uc_x86_mmr gdtr = {0, 0x201000, sizeof(gdt) - 1, 0};

    OK(uc_mem_write(r->uc, 0x201000, gdt, sizeof(gdt)));
    OK(uc_reg_write(r->uc, UC_X86_REG_GDTR, &gdtr));
}

static uc_err nv_run3_n(NvRun *r, const char *code, size_t len)
{
    /* push 23h; push 201F00h; pushfq; push 2Bh; push code; iretq */
    char stub[] = "\x6a\x23\x68\x00\x1f\x20\x00\x9c\x6a\x2b\x68\x00\x00\x00\x00\x48\xcf";
    uint64_t sa = r->next, ca = r->next + 0x40;
    uint32_t ca32 = (uint32_t)ca;

    nv_gdt(r);
    memcpy(stub + 11, &ca32, 4);
    r->next += 0x40 + ((len + 0x3f) & ~(uint64_t)0x3f);
    nv_set(r, UC_X86_REG_RSP, 0x201e00);
    r->cap.count = 0;
    r->cap.intno = 0;
    r->last = ca;
    OK(uc_mem_write(r->uc, sa, stub, sizeof(stub) - 1));
    OK(uc_mem_write(r->uc, ca, code, len));
    return uc_emu_start(r->uc, sa, ca + len, 0, 0);
}

#define nv_run3(r, code) nv_run3_n((r), (code), sizeof(code) - 1)

/* CPL from CS[1:0] after a run */
static int nv_cpl(NvRun *r)
{
    return (int)(nv_get(r, UC_X86_REG_CS) & 3);
}

static uint64_t nv_rdmsr(NvRun *r, uint32_t msr)
{
    uc_x86_msr m = {msr, 0};

    OK(uc_reg_read(r->uc, UC_X86_REG_MSR, &m));
    return m.value;
}

static void nv_wrmsr(NvRun *r, uint32_t msr, uint64_t value)
{
    uc_x86_msr m = {msr, value};

    OK(uc_reg_write(r->uc, UC_X86_REG_MSR, &m));
}

/*
 * NoVmp U111: WAITPKG (SDM Vol2 UMONITOR/UMWAIT/TPAUSE). Timing model: the
 * optimized state is left at once, so CF = 0 (the OS time limit never expires)
 * and AF/PF/SF/ZF/OF = 0; #GP(0) for src[31:1] != 0 or CR4.TSD at CPL > 0.
 */
static void test_x86_waitpkg(void)
{
    NvRun r;
    uint64_t cr4;

    nv_open(&r, UC_MODE_64, UC_CPU_X86_MAX);

    nv_set(&r, UC_X86_REG_RAX, 7);
    nv_set(&r, UC_X86_REG_RCX, 0);
    OK(nv_run(&r, "\x0f\xa2"));
    TEST_CHECK(nv_get(&r, UC_X86_REG_RCX) & (1u << 5));

    /* umonitor rdx (DS:RDX, byte-load checks); tpause ecx; umwait ecx, deadline far away */
    nv_set(&r, UC_X86_REG_RDX, 0x200100);
    nv_set(&r, UC_X86_REG_RCX, 1);
    nv_set(&r, UC_X86_REG_RFLAGS, 0x8d7);
    OK(nv_run(&r, "\xf3\x0f\xae\xf2"));
    nv_set(&r, UC_X86_REG_RDX, 0x7fffffff);
    nv_set(&r, UC_X86_REG_RAX, 0xffffffff);
    OK(nv_run(&r, "\x66\x0f\xae\xf1"));
    TEST_CHECK((nv_get(&r, UC_X86_REG_RFLAGS) & 0xfff) == 0x002);
    nv_set(&r, UC_X86_REG_RFLAGS, 0x8d7);
    nv_set(&r, UC_X86_REG_RCX, 0);
    OK(nv_run(&r, "\xf2\x0f\xae\xf1"));
    TEST_CHECK((nv_get(&r, UC_X86_REG_RFLAGS) & 0xfff) == 0x002);
    TEST_CHECK(r.cap.count == 0);

    /* OS time limit set (IA32_UMWAIT_CONTROL = 400h quanta, C0.2 disabled): still CF = 0 */
    nv_wrmsr(&r, 0xe1, 0x401);
    TEST_CHECK(nv_rdmsr(&r, 0xe1) == 0x401);
    nv_set(&r, UC_X86_REG_RFLAGS, 0x8d7);
    OK(nv_run(&r, "\x66\x0f\xae\xf1"));
    TEST_CHECK((nv_get(&r, UC_X86_REG_RFLAGS) & 0xfff) == 0x002);
    /* reserved MSR bits: WRMSR #GP(0); through the API the write is dropped */
    nv_set(&r, UC_X86_REG_RCX, 0xe1);
    nv_set(&r, UC_X86_REG_RAX, 2);
    nv_set(&r, UC_X86_REG_RDX, 0);
    OK(nv_run(&r, "\x0f\x30"));
    TEST_CHECK(r.cap.count == 1 && r.cap.intno == 13);
    nv_wrmsr(&r, 0xe1, 0x100000000ull);
    TEST_CHECK(nv_rdmsr(&r, 0xe1) == 0x401);
    nv_set(&r, UC_X86_REG_RAX, 0x800);
    OK(nv_run(&r, "\x0f\x30"));
    TEST_CHECK(r.cap.count == 0 && nv_rdmsr(&r, 0xe1) == 0x800);

    /* src[31:1] != 0: #GP(0), REX.W ignored (r32 operand) */
    nv_set(&r, UC_X86_REG_RCX, 0x100000000ull);
    OK(nv_run(&r, "\x66\x48\x0f\xae\xf1"));
    TEST_CHECK(r.cap.count == 0);
    nv_set(&r, UC_X86_REG_RCX, 2);
    OK(nv_run(&r, "\x66\x0f\xae\xf1"));
    TEST_CHECK(r.cap.count == 1 && r.cap.intno == 13);
    OK(nv_run(&r, "\xf2\x0f\xae\xf1"));
    TEST_CHECK(r.cap.count == 1 && r.cap.intno == 13);

    /* LOCK: #UD; memory form of F3 0F AE /6 is not UMONITOR */
    uc_assert_err(UC_ERR_INSN_INVALID, nv_run(&r, "\xf0\x66\x0f\xae\xf1"));

    /* UMONITOR of an unmapped address faults like a byte load */
    nv_set(&r, UC_X86_REG_RDX, 0x900000);
    uc_assert_err(UC_ERR_READ_UNMAPPED, nv_run(&r, "\xf3\x0f\xae\xf2"));

    /* CR4.TSD: allowed at CPL0, #GP(0) at CPL3 (UMONITOR is not affected) */
    OK(uc_reg_read(r.uc, UC_X86_REG_CR4, &cr4));
    cr4 |= 4;
    OK(uc_reg_write(r.uc, UC_X86_REG_CR4, &cr4));
    nv_set(&r, UC_X86_REG_RCX, 0);
    OK(nv_run(&r, "\x66\x0f\xae\xf1"));
    TEST_CHECK(r.cap.count == 0);
    nv_set(&r, UC_X86_REG_RDX, 0x200100);
    OK(nv_run3(&r, "\xf3\x0f\xae\xf2\x66\x0f\xae\xf1"));
    TEST_CHECK(nv_cpl(&r) == 3);
    TEST_CHECK(r.cap.count == 1 && r.cap.intno == 13);
    TEST_CHECK(nv_get(&r, UC_X86_REG_RIP) == r.last + 4);

    /* strict profile without WAITPKG: #UD */
    nv_profile7(&r, 0, 0, 0);
    uc_assert_err(UC_ERR_INSN_INVALID, nv_run(&r, "\x66\x0f\xae\xf1"));
    uc_assert_err(UC_ERR_INSN_INVALID, nv_run(&r, "\xf2\x0f\xae\xf1"));
    uc_assert_err(UC_ERR_INSN_INVALID, nv_run(&r, "\xf3\x0f\xae\xf2"));
    OK(uc_close(r.uc));
}

/*
 * NoVmp U112: ENQCMD / ENQCMDS (SDM Vol2). No enqueue register exists, so a
 * well-formed command returns the retry status ZF = 1 (other flags 0) and the
 * destination is not written; #GP(0) for an invalid IA32_PASID (ENQCMD), CPL > 0
 * (ENQCMDS), an unaligned destination, or reserved source bits.
 */
static void test_x86_enqcmd(void)
{
    NvRun r;
    uint8_t pat[64], mem[64];
    uint64_t src0;

    memset(pat, 0xa5, sizeof(pat));
    nv_open(&r, UC_MODE_64, UC_CPU_X86_MAX);
    nv_set(&r, UC_X86_REG_RAX, 7);
    nv_set(&r, UC_X86_REG_RCX, 0);
    OK(nv_run(&r, "\x0f\xa2"));
    TEST_CHECK(nv_get(&r, UC_X86_REG_RCX) & (1u << 29));

    /* source at 200000h (bits 31:0 zero), destination 200400h */
    src0 = 0x1122334400000000ull;
    OK(uc_mem_write(r.uc, 0x200000, &src0, 8));
    OK(uc_mem_write(r.uc, 0x200400, pat, sizeof(pat)));
    nv_set(&r, UC_X86_REG_RSI, 0x200000);

    /* IA32_PASID invalid (reset value 0): ENQCMD #GP(0) */
    nv_set(&r, UC_X86_REG_RAX, 0x200400);
    OK(nv_run(&r, "\xf2\x0f\x38\xf8\x06"));
    TEST_CHECK(r.cap.count == 1 && r.cap.intno == 13);

    /* WRMSR IA32_PASID: reserved bit 20 #GP(0), then valid PASID 12345h */
    nv_set(&r, UC_X86_REG_RCX, 0xd93);
    nv_set(&r, UC_X86_REG_RAX, 0x80100000);
    nv_set(&r, UC_X86_REG_RDX, 0);
    OK(nv_run(&r, "\x0f\x30"));
    TEST_CHECK(r.cap.count == 1 && r.cap.intno == 13);
    nv_set(&r, UC_X86_REG_RAX, 0x80012345);
    OK(nv_run(&r, "\x0f\x30"));
    TEST_CHECK(r.cap.count == 0 && nv_rdmsr(&r, 0xd93) == 0x80012345);

    /* ENQCMD rax, [rsi]: retry status, nothing written */
    nv_set(&r, UC_X86_REG_RAX, 0x200400);
    nv_set(&r, UC_X86_REG_RFLAGS, 0x8d7 & ~0x40);
    OK(nv_run(&r, "\xf2\x0f\x38\xf8\x06"));
    TEST_CHECK(r.cap.count == 0);
    TEST_CHECK((nv_get(&r, UC_X86_REG_RFLAGS) & 0xfff) == 0x042);
    OK(uc_mem_read(r.uc, 0x200400, mem, sizeof(mem)));
    TEST_CHECK(memcmp(mem, pat, sizeof(mem)) == 0);

    /* destination with no memory behind it: also "not an enqueue register" */
    nv_set(&r, UC_X86_REG_RAX, 0x900000);
    nv_set(&r, UC_X86_REG_RFLAGS, 0x2);
    OK(nv_run(&r, "\xf2\x0f\x38\xf8\x06"));
    TEST_CHECK(r.cap.count == 0 && (nv_get(&r, UC_X86_REG_RFLAGS) & 0xfff) == 0x042);

    /* unaligned destination: #GP(0); source bits 31:0 not zero: #GP(0) */
    nv_set(&r, UC_X86_REG_RAX, 0x200408);
    OK(nv_run(&r, "\xf2\x0f\x38\xf8\x06"));
    TEST_CHECK(r.cap.count == 1 && r.cap.intno == 13);
    nv_set(&r, UC_X86_REG_RAX, 0x200400);
    nv_set(&r, UC_X86_REG_RSI, 0x200001);
    OK(nv_run(&r, "\xf2\x0f\x38\xf8\x06"));
    TEST_CHECK(r.cap.count == 1 && r.cap.intno == 13);
    /* ... unless the low dword is zero at the unaligned source */
    OK(uc_mem_write(r.uc, 0x200040, "\x00\x00\x00\x00\x00\x00\x00\x00", 8));
    nv_set(&r, UC_X86_REG_RSI, 0x200041);
    OK(nv_run(&r, "\xf2\x0f\x38\xf8\x06"));
    TEST_CHECK(r.cap.count == 0);

    /* ENQCMDS at CPL0: source bits 30:20 must be 0; bit 31 / PASID bits pass */
    src0 = 0x0000000080054321ull;
    OK(uc_mem_write(r.uc, 0x200000, &src0, 8));
    nv_set(&r, UC_X86_REG_RSI, 0x200000);
    nv_set(&r, UC_X86_REG_RFLAGS, 0x8d7 & ~0x40);
    OK(nv_run(&r, "\xf3\x0f\x38\xf8\x06"));
    TEST_CHECK(r.cap.count == 0);
    TEST_CHECK((nv_get(&r, UC_X86_REG_RFLAGS) & 0xfff) == 0x042);
    src0 = 0x0000000000100000ull;
    OK(uc_mem_write(r.uc, 0x200000, &src0, 8));
    OK(nv_run(&r, "\xf3\x0f\x38\xf8\x06"));
    TEST_CHECK(r.cap.count == 1 && r.cap.intno == 13);

    /* CPL3: ENQCMDS #GP(0), ENQCMD allowed */
    src0 = 0;
    OK(uc_mem_write(r.uc, 0x200000, &src0, 8));
    OK(nv_run3(&r, "\xf3\x0f\x38\xf8\x06"));
    TEST_CHECK(nv_cpl(&r) == 3 && r.cap.count == 1 && r.cap.intno == 13);
    OK(nv_run3(&r, "\xf2\x0f\x38\xf8\x06"));
    TEST_CHECK(nv_cpl(&r) == 3 && r.cap.count == 0);
    TEST_CHECK(nv_get(&r, UC_X86_REG_RFLAGS) & 0x40);

    /* register form, LOCK: #UD; strict profile without ENQCMD: #UD */
    uc_assert_err(UC_ERR_INSN_INVALID, nv_run(&r, "\xf2\x0f\x38\xf8\xc6"));
    uc_assert_err(UC_ERR_INSN_INVALID, nv_run(&r, "\xf0\xf2\x0f\x38\xf8\x06"));
    nv_profile7(&r, 0, 0, 0);
    uc_assert_err(UC_ERR_INSN_INVALID, nv_run(&r, "\xf2\x0f\x38\xf8\x06"));
    uc_assert_err(UC_ERR_INSN_INVALID, nv_run(&r, "\xf3\x0f\x38\xf8\x06"));
    OK(uc_close(r.uc));
}

/*
 * NoVmp U113: GETSEC (SMX without a TXT chipset), PCONFIG and SGX faults
 * (SDM Vol2 chapter 7, PCONFIG; Vol3D 41 ENCLS/ENCLU).
 */
static void test_x86_smx_pconfig_sgx(void)
{
    NvRun r;
    uint64_t cr4;

    nv_open(&r, UC_MODE_64, UC_CPU_X86_MAX);
    nv_set(&r, UC_X86_REG_RAX, 1);
    OK(nv_run(&r, "\x0f\xa2"));
    TEST_CHECK(nv_get(&r, UC_X86_REG_RCX) & (1u << 6));

    /* CR4.SMXE = 0: GETSEC #UD */
    nv_set(&r, UC_X86_REG_RAX, 0);
    uc_assert_err(UC_ERR_INSN_INVALID, nv_run(&r, "\x0f\x37"));

    /* mov rax, cr4; bts rax, 14; mov cr4, rax: allowed with SMX */
    OK(nv_run(&r, "\x0f\x20\xe0\x48\x0f\xba\xe8\x0e\x0f\x22\xe0"));
    TEST_CHECK(r.cap.count == 0);
    OK(uc_reg_read(r.uc, UC_X86_REG_CR4, &cr4));
    TEST_CHECK(cr4 & 0x4000);

    /* GETSEC[CAPABILITIES]: EAX = 0 (no chipset, no leaves), RAX[63:32] cleared */
    nv_set(&r, UC_X86_REG_RAX, 0xffffffff00000000ull);
    nv_set(&r, UC_X86_REG_RBX, 0);
    OK(nv_run(&r, "\x0f\x37"));
    TEST_CHECK(nv_get(&r, UC_X86_REG_RAX) == 0);
    nv_set(&r, UC_X86_REG_RBX, 1);
    OK(nv_run(&r, "\x48\x0f\x37"));
    TEST_CHECK(nv_get(&r, UC_X86_REG_RAX) == 0);
    /* ... also at CPL3 */
    nv_set(&r, UC_X86_REG_RAX, 0);
    OK(nv_run3(&r, "\x0f\x37\x90"));
    TEST_CHECK(nv_cpl(&r) == 3 && r.cap.count == 0);
    /* leaves not reported by CAPABILITIES (PARAMETERS 6, SENTER 4): #UD at any CPL */
    nv_set(&r, UC_X86_REG_RAX, 6);
    uc_assert_err(UC_ERR_INSN_INVALID, nv_run(&r, "\x0f\x37"));
    nv_set(&r, UC_X86_REG_RAX, 4);
    uc_assert_err(UC_ERR_INSN_INVALID, nv_run3(&r, "\x0f\x37"));
    /* 66/F3/LOCK GETSEC: #UD */
    nv_set(&r, UC_X86_REG_RAX, 0);
    uc_assert_err(UC_ERR_INSN_INVALID, nv_run(&r, "\x66\x0f\x37"));
    uc_assert_err(UC_ERR_INSN_INVALID, nv_run(&r, "\xf3\x0f\x37"));
    uc_assert_err(UC_ERR_INSN_INVALID, nv_run(&r, "\xf0\x0f\x37"));

    /* PCONFIG (not reported), ENCLS/ENCLU/ENCLV (no SGX, no VMX): #UD at CPL0 and CPL3 */
    uc_assert_err(UC_ERR_INSN_INVALID, nv_run(&r, "\x0f\x01\xc5"));
    uc_assert_err(UC_ERR_INSN_INVALID, nv_run3(&r, "\x0f\x01\xc5"));
    uc_assert_err(UC_ERR_INSN_INVALID, nv_run(&r, "\x0f\x01\xcf"));
    uc_assert_err(UC_ERR_INSN_INVALID, nv_run3(&r, "\x0f\x01\xcf"));
    uc_assert_err(UC_ERR_INSN_INVALID, nv_run(&r, "\x0f\x01\xd7"));
    uc_assert_err(UC_ERR_INSN_INVALID, nv_run3(&r, "\x0f\x01\xd7"));
    uc_assert_err(UC_ERR_INSN_INVALID, nv_run3(&r, "\x0f\x01\xc0"));

    /* strict profile hiding SMX (leaf 1 not listed): GETSEC #UD although CR4.SMXE = 1 */
    nv_profile7(&r, 0, 0, 0);
    uc_assert_err(UC_ERR_INSN_INVALID, nv_run(&r, "\x0f\x37"));
    OK(uc_close(r.uc));

    /* a model without SMX (Haswell): MOV CR4 with SMXE is #GP(0); GETSEC #UD even if
       CR4.SMXE is forced through uc_reg_write (raw register write) */
    nv_open(&r, UC_MODE_64, UC_CPU_X86_HASWELL);
    OK(nv_run(&r, "\x0f\x20\xe0\x48\x0f\xba\xe8\x0e\x0f\x22\xe0"));
    TEST_CHECK(r.cap.count == 1 && r.cap.intno == 13);
    OK(uc_reg_read(r.uc, UC_X86_REG_CR4, &cr4));
    cr4 |= 0x4000;
    OK(uc_reg_write(r.uc, UC_X86_REG_CR4, &cr4));
    nv_set(&r, UC_X86_REG_RAX, 0);
    uc_assert_err(UC_ERR_INSN_INVALID, nv_run(&r, "\x0f\x37"));
    OK(uc_close(r.uc));
}

static uint64_t nv_ld64(NvRun *r, uint64_t a)
{
    uint64_t v = 0;

    OK(uc_mem_read(r->uc, a, &v, 8));
    return v;
}

static void nv_st64(NvRun *r, uint64_t a, uint64_t v)
{
    OK(uc_mem_write(r->uc, a, &v, 8));
}

/* CR0.WP = 1 (needed for CR4.CET), then MOV CR4 with CET (bit 23) at CPL0 */
static void nv_cet_on(NvRun *r)
{
    uint64_t cr0;

    OK(uc_reg_read(r->uc, UC_X86_REG_CR0, &cr0));
    cr0 |= 0x10000;
    OK(uc_reg_write(r->uc, UC_X86_REG_CR0, &cr0));
    /* mov rax, cr4; bts rax, 23; mov cr4, rax */
    OK(nv_run(r, "\x0f\x20\xe0\x48\x0f\xba\xe8\x17\x0f\x22\xe0"));
    TEST_CHECK(r->cap.count == 0);
}

/*
 * NoVmp U114: CET shadow-stack state and management instructions (SDM Vol1
 * 18.2; Vol2 RDSSP, INCSSP, WRSS, WRUSS, SETSSBSY, CLRSSBSY, RSTORSSP,
 * SAVEPREVSSP; Vol3 CR4.CET; Vol4 CET MSRs). Shadow stacks live at 300000h
 * (no paging, so every linear address may hold one).
 */
static void test_x86_cet_shadow_stack(void)
{
    NvRun r;
    uint64_t cr4;

    nv_open(&r, UC_MODE_64, UC_CPU_X86_MAX);
    OK(uc_mem_map(r.uc, 0x300000, 0x2000, UC_PROT_ALL));
    nv_set(&r, UC_X86_REG_RAX, 7);
    nv_set(&r, UC_X86_REG_RCX, 0);
    OK(nv_run(&r, "\x0f\xa2"));
    TEST_CHECK(nv_get(&r, UC_X86_REG_RCX) & (1u << 7));

    /* CR4.CET needs CR0.WP: #GP(0) while WP = 0 */
    OK(nv_run(&r, "\x0f\x20\xe0\x48\x0f\xba\xe8\x17\x0f\x22\xe0"));
    TEST_CHECK(r.cap.count == 1 && r.cap.intno == 13);
    nv_cet_on(&r);
    OK(uc_reg_read(r.uc, UC_X86_REG_CR4, &cr4));
    TEST_CHECK(cr4 & (1u << 23));
    /* ... and WP cannot be cleared while CR4.CET = 1: mov rax, cr0; btr rax, 16; mov cr0, rax */
    OK(nv_run(&r, "\x0f\x20\xc0\x48\x0f\xba\xf0\x10\x0f\x22\xc0"));
    TEST_CHECK(r.cap.count == 1 && r.cap.intno == 13);

    /* shadow stacks still off (IA32_S_CET = 0): RDSSP is a NOP, INCSSP/SAVEPREVSSP/RSTORSSP/WRSS #UD */
    nv_set(&r, UC_X86_REG_SSP, 0x300800);
    TEST_CHECK(nv_get(&r, UC_X86_REG_SSP) == 0x300800);
    nv_set(&r, UC_X86_REG_RAX, 0x1111);
    OK(nv_run(&r, "\xf3\x48\x0f\x1e\xc8"));
    TEST_CHECK(nv_get(&r, UC_X86_REG_RAX) == 0x1111);
    uc_assert_err(UC_ERR_INSN_INVALID, nv_run(&r, "\xf3\x48\x0f\xae\xe8"));
    uc_assert_err(UC_ERR_INSN_INVALID, nv_run(&r, "\xf3\x0f\x01\xea"));
    nv_set(&r, UC_X86_REG_RDX, 0x301000);
    uc_assert_err(UC_ERR_INSN_INVALID, nv_run(&r, "\xf3\x0f\x01\x2a"));
    uc_assert_err(UC_ERR_INSN_INVALID, nv_run(&r, "\x48\x0f\x38\xf6\x02"));
    /* SETSSBSY/CLRSSBSY: #UD while IA32_S_CET.SH_STK_EN = 0 */
    uc_assert_err(UC_ERR_INSN_INVALID, nv_run(&r, "\xf3\x0f\x01\xe8"));
    uc_assert_err(UC_ERR_INSN_INVALID, nv_run(&r, "\xf3\x0f\xae\x32"));

    /* WRMSR IA32_S_CET: reserved bits 6 and 9 #GP(0) (bits 5:2 need CET_IBT, see U116); SH_STK_EN | WR_SHSTK_EN ok */
    nv_set(&r, UC_X86_REG_RCX, 0x6a2);
    nv_set(&r, UC_X86_REG_RDX, 0);
    nv_set(&r, UC_X86_REG_RAX, 0x41);
    OK(nv_run(&r, "\x0f\x30"));
    TEST_CHECK(r.cap.count == 1 && r.cap.intno == 13);
    nv_set(&r, UC_X86_REG_RAX, 0x201);
    OK(nv_run(&r, "\x0f\x30"));
    TEST_CHECK(r.cap.count == 1 && r.cap.intno == 13);
    nv_set(&r, UC_X86_REG_RAX, 0x3);
    OK(nv_run(&r, "\x0f\x30"));
    TEST_CHECK(r.cap.count == 0 && nv_rdmsr(&r, 0x6a2) == 3);
    /* IA32_PL0_SSP: bits 1:0 must be 0, canonical */
    nv_set(&r, UC_X86_REG_RCX, 0x6a4);
    nv_set(&r, UC_X86_REG_RAX, 0x301f02);
    OK(nv_run(&r, "\x0f\x30"));
    TEST_CHECK(r.cap.count == 1 && r.cap.intno == 13);
    nv_set(&r, UC_X86_REG_RAX, 0x301f00);
    nv_set(&r, UC_X86_REG_RDX, 0x80000000);
    OK(nv_run(&r, "\x0f\x30"));
    TEST_CHECK(r.cap.count == 1 && r.cap.intno == 13);
    nv_set(&r, UC_X86_REG_RDX, 0);
    OK(nv_run(&r, "\x0f\x30"));
    TEST_CHECK(r.cap.count == 0 && nv_rdmsr(&r, 0x6a4) == 0x301f00);

    /* RDSSPQ rax = SSP; RDSSPD eax = SSP[31:0] zero-extended; LOCK #UD */
    nv_set(&r, UC_X86_REG_SSP, 0x1234567800300800ull);
    nv_set(&r, UC_X86_REG_RAX, 0x1111);
    OK(nv_run(&r, "\xf3\x48\x0f\x1e\xc8"));
    TEST_CHECK(nv_get(&r, UC_X86_REG_RAX) == 0x1234567800300800ull);
    nv_set(&r, UC_X86_REG_RAX, ~0ull);
    OK(nv_run(&r, "\xf3\x0f\x1e\xc8"));
    TEST_CHECK(nv_get(&r, UC_X86_REG_RAX) == 0x00300800);
    uc_assert_err(UC_ERR_INSN_INVALID, nv_run(&r, "\xf0\xf3\x0f\x1e\xc8"));

    /* INCSSPQ rax (rax[7:0] = 2): SSP += 16; INCSSPD ecx (103h -> 3): SSP += 12; 0: no change */
    nv_set(&r, UC_X86_REG_SSP, 0x300800);
    nv_set(&r, UC_X86_REG_RAX, 0x7702);
    OK(nv_run(&r, "\xf3\x48\x0f\xae\xe8"));
    TEST_CHECK(nv_get(&r, UC_X86_REG_SSP) == 0x300810);
    nv_set(&r, UC_X86_REG_RCX, 0x103);
    OK(nv_run(&r, "\xf3\x0f\xae\xe9"));
    TEST_CHECK(nv_get(&r, UC_X86_REG_SSP) == 0x30081c);
    nv_set(&r, UC_X86_REG_RAX, 0);
    OK(nv_run(&r, "\xf3\x48\x0f\xae\xe8"));
    TEST_CHECK(nv_get(&r, UC_X86_REG_SSP) == 0x30081c);
    /* the shadow-stack loads fault (SSP unchanged): first element, then the last element */
    nv_set(&r, UC_X86_REG_SSP, 0x900000);
    uc_assert_err(UC_ERR_READ_UNMAPPED, nv_run(&r, "\xf3\x48\x0f\xae\xe8"));
    TEST_CHECK(nv_get(&r, UC_X86_REG_SSP) == 0x900000);
    nv_set(&r, UC_X86_REG_SSP, 0x301ff8);
    nv_set(&r, UC_X86_REG_RAX, 2);
    uc_assert_err(UC_ERR_READ_UNMAPPED, nv_run(&r, "\xf3\x48\x0f\xae\xe8"));
    TEST_CHECK(nv_get(&r, UC_X86_REG_SSP) == 0x301ff8);

    /* WRSSQ [rdx], rax / WRSSD [rdx], eax; alignment #GP(0); without WR_SHSTK_EN #UD */
    nv_set(&r, UC_X86_REG_RDX, 0x300100);
    nv_set(&r, UC_X86_REG_RAX, 0x1122334455667788ull);
    OK(nv_run(&r, "\x48\x0f\x38\xf6\x02"));
    TEST_CHECK(nv_ld64(&r, 0x300100) == 0x1122334455667788ull);
    nv_set(&r, UC_X86_REG_RDX, 0x300104);
    nv_set(&r, UC_X86_REG_RAX, 0xaabbccdd);
    OK(nv_run(&r, "\x0f\x38\xf6\x02"));
    TEST_CHECK(nv_ld64(&r, 0x300100) == 0xaabbccdd55667788ull);
    OK(nv_run(&r, "\x48\x0f\x38\xf6\x02"));
    TEST_CHECK(r.cap.count == 1 && r.cap.intno == 13);
    uc_assert_err(UC_ERR_INSN_INVALID, nv_run(&r, "\x0f\x38\xf6\xc0"));
    uc_assert_err(UC_ERR_INSN_INVALID, nv_run(&r, "\xf0\x0f\x38\xf6\x02"));

    /* WRUSSQ [rdx], rax at CPL0 */
    nv_set(&r, UC_X86_REG_RDX, 0x300108);
    nv_set(&r, UC_X86_REG_RAX, 0x0102030405060708ull);
    OK(nv_run(&r, "\x66\x48\x0f\x38\xf5\x02"));
    TEST_CHECK(nv_ld64(&r, 0x300108) == 0x0102030405060708ull);

    /* SETSSBSY: token at IA32_PL0_SSP = 301F00h becomes busy, SSP = 301F00h; busy again: #CP(5) */
    nv_st64(&r, 0x301f00, 0x301f00);
    OK(nv_run(&r, "\xf3\x0f\x01\xe8"));
    TEST_CHECK(r.cap.count == 0);
    TEST_CHECK(nv_ld64(&r, 0x301f00) == 0x301f01);
    TEST_CHECK(nv_get(&r, UC_X86_REG_SSP) == 0x301f00);
    nv_set(&r, UC_X86_REG_SSP, 0x300800);
    OK(nv_run(&r, "\xf3\x0f\x01\xe8"));
    TEST_CHECK(r.cap.count == 1 && r.cap.intno == 21);
    TEST_CHECK(nv_ld64(&r, 0x301f00) == 0x301f01);
    TEST_CHECK(nv_get(&r, UC_X86_REG_SSP) == 0x300800);

    /* CLRSSBSY [rdx]: busy token cleared, CF = 0, SSP = 0; not busy: CF = 1, unchanged */
    nv_set(&r, UC_X86_REG_RDX, 0x301f00);
    nv_set(&r, UC_X86_REG_RFLAGS, 0x8d7);
    OK(nv_run(&r, "\xf3\x0f\xae\x32"));
    TEST_CHECK(nv_ld64(&r, 0x301f00) == 0x301f00);
    TEST_CHECK((nv_get(&r, UC_X86_REG_RFLAGS) & 0xfff) == 0x002);
    TEST_CHECK(nv_get(&r, UC_X86_REG_SSP) == 0);
    nv_set(&r, UC_X86_REG_SSP, 0x300800);
    OK(nv_run(&r, "\xf3\x0f\xae\x32"));
    TEST_CHECK(nv_ld64(&r, 0x301f00) == 0x301f00);
    TEST_CHECK((nv_get(&r, UC_X86_REG_RFLAGS) & 0xfff) == 0x003);
    TEST_CHECK(nv_get(&r, UC_X86_REG_SSP) == 0);
    nv_set(&r, UC_X86_REG_RDX, 0x301f04);
    OK(nv_run(&r, "\xf3\x0f\xae\x32"));
    TEST_CHECK(r.cap.count == 1 && r.cap.intno == 13);

    /*
     * RSTORSSP / SAVEPREVSSP (SDM Vol1 Figures 18-2/18-3): SSP = 300400h, the new
     * stack's restore token at 301FF8h holds 302000h | 1 (64-bit).
     */
    nv_set(&r, UC_X86_REG_SSP, 0x300400);
    nv_st64(&r, 0x301ff8, 0x302001);
    nv_set(&r, UC_X86_REG_RDX, 0x301ff8);
    nv_set(&r, UC_X86_REG_RFLAGS, 0x8d7);
    OK(nv_run(&r, "\xf3\x0f\x01\x2a"));
    TEST_CHECK(r.cap.count == 0);
    TEST_CHECK(nv_get(&r, UC_X86_REG_SSP) == 0x301ff8);
    TEST_CHECK(nv_ld64(&r, 0x301ff8) == 0x300403);       /* previous-ssp token */
    TEST_CHECK((nv_get(&r, UC_X86_REG_RFLAGS) & 0xfff) == 0x002);
    nv_st64(&r, 0x3003f8, 0);
    OK(nv_run(&r, "\xf3\x0f\x01\xea"));
    TEST_CHECK(r.cap.count == 0);
    TEST_CHECK(nv_get(&r, UC_X86_REG_SSP) == 0x302000);
    TEST_CHECK(nv_ld64(&r, 0x3003f8) == 0x300401);       /* restore token on the old stack */
    /* back to the old stack */
    nv_set(&r, UC_X86_REG_RDX, 0x3003f8);
    OK(nv_run(&r, "\xf3\x0f\x01\x2a"));
    TEST_CHECK(r.cap.count == 0 && nv_get(&r, UC_X86_REG_SSP) == 0x3003f8);
    TEST_CHECK(nv_ld64(&r, 0x3003f8) == 0x302003);
    /* token with bit 2 (alignment hole) set: CF = 1 */
    nv_st64(&r, 0x301ff8, 0x302005);
    nv_set(&r, UC_X86_REG_RDX, 0x301ff8);
    OK(nv_run(&r, "\xf3\x0f\x01\x2a"));
    TEST_CHECK((nv_get(&r, UC_X86_REG_RFLAGS) & 0xfff) == 0x003);
    /* SAVEPREVSSP with CF = 1 in 64-bit mode: #GP(0) (stc; saveprevssp) */
    OK(nv_run(&r, "\xf9\xf3\x0f\x01\xea"));
    TEST_CHECK(r.cap.count == 1 && r.cap.intno == 13);
    TEST_CHECK(nv_get(&r, UC_X86_REG_SSP) == 0x301ff8);
    /* invalid token (address mismatch): #CP(4), token rewritten unchanged, SSP unchanged */
    nv_st64(&r, 0x301ff0, 0x302001);
    nv_set(&r, UC_X86_REG_RDX, 0x301ff0);
    OK(nv_run(&r, "\xf3\x0f\x01\x2a"));
    TEST_CHECK(r.cap.count == 1 && r.cap.intno == 21);
    TEST_CHECK(nv_ld64(&r, 0x301ff0) == 0x302001);
    TEST_CHECK(nv_get(&r, UC_X86_REG_SSP) == 0x301ff8);
    /* compatibility-mode token (bit 0 = 0) in 64-bit mode: #CP(4); misaligned operand: #GP(0) */
    nv_st64(&r, 0x301ff8, 0x302000);
    nv_set(&r, UC_X86_REG_RDX, 0x301ff8);
    OK(nv_run(&r, "\xf3\x0f\x01\x2a"));
    TEST_CHECK(r.cap.count == 1 && r.cap.intno == 21);
    nv_set(&r, UC_X86_REG_RDX, 0x301ff4);
    OK(nv_run(&r, "\xf3\x0f\x01\x2a"));
    TEST_CHECK(r.cap.count == 1 && r.cap.intno == 13);

    /* CPL3: IA32_U_CET = 0 so RDSSP NOP, INCSSP #UD; SETSSBSY #GP(0) (S_CET on); WRUSS #GP(0) */
    nv_set(&r, UC_X86_REG_RAX, 0x1111);
    OK(nv_run3(&r, "\xf3\x48\x0f\x1e\xc8\x90"));
    TEST_CHECK(nv_cpl(&r) == 3 && nv_get(&r, UC_X86_REG_RAX) == 0x1111);
    uc_assert_err(UC_ERR_INSN_INVALID, nv_run3(&r, "\xf3\x48\x0f\xae\xe8"));
    OK(nv_run3(&r, "\xf3\x0f\x01\xe8"));
    TEST_CHECK(r.cap.count == 1 && r.cap.intno == 13);
    nv_set(&r, UC_X86_REG_RDX, 0x300108);
    OK(nv_run3(&r, "\x66\x48\x0f\x38\xf5\x02"));
    TEST_CHECK(r.cap.count == 1 && r.cap.intno == 13);
    /* user shadow stacks on (IA32_U_CET = SH_STK_EN): RDSSP / INCSSP work at CPL3, WRSS #UD */
    nv_wrmsr(&r, 0x6a0, 1);
    TEST_CHECK(nv_rdmsr(&r, 0x6a0) == 1);
    nv_set(&r, UC_X86_REG_SSP, 0x300800);
    nv_set(&r, UC_X86_REG_RAX, 1);
    OK(nv_run3(&r, "\xf3\x48\x0f\xae\xe8\xf3\x48\x0f\x1e\xc9"));
    TEST_CHECK(nv_cpl(&r) == 3 && r.cap.count == 0);
    TEST_CHECK(nv_get(&r, UC_X86_REG_RCX) == 0x300808);
    uc_assert_err(UC_ERR_INSN_INVALID, nv_run3(&r, "\x48\x0f\x38\xf6\x02"));
    /* the API cannot store an invalid MSR value (dropped) */
    nv_wrmsr(&r, 0x6a0, 0x40);
    TEST_CHECK(nv_rdmsr(&r, 0x6a0) == 1);

    /* CR4.CET = 0: WRUSS #UD at any CPL (the i5-13600K profile state) */
    cr4 &= ~(uint64_t)(1u << 23);
    OK(uc_reg_write(r.uc, UC_X86_REG_CR4, &cr4));
    uc_assert_err(UC_ERR_INSN_INVALID, nv_run(&r, "\x66\x48\x0f\x38\xf5\x02"));
    uc_assert_err(UC_ERR_INSN_INVALID, nv_run(&r, "\xf3\x0f\x01\xe8"));
    nv_set(&r, UC_X86_REG_RAX, 0x1111);
    OK(nv_run(&r, "\xf3\x48\x0f\x1e\xc8"));
    TEST_CHECK(nv_get(&r, UC_X86_REG_RAX) == 0x1111);
    OK(uc_close(r.uc));

    /* strict profile without CET_SS: every form #UD (RDSSP stays a NOP) although enabled */
    nv_open(&r, UC_MODE_64, UC_CPU_X86_MAX);
    nv_cet_on(&r);
    nv_wrmsr(&r, 0x6a2, 3);
    nv_profile7(&r, 0, 0, 0);
    nv_set(&r, UC_X86_REG_RAX, 0x1111);
    OK(nv_run(&r, "\xf3\x48\x0f\x1e\xc8"));
    TEST_CHECK(nv_get(&r, UC_X86_REG_RAX) == 0x1111);
    uc_assert_err(UC_ERR_INSN_INVALID, nv_run(&r, "\xf3\x48\x0f\xae\xe8"));
    uc_assert_err(UC_ERR_INSN_INVALID, nv_run(&r, "\xf3\x0f\x01\xe8"));
    uc_assert_err(UC_ERR_INSN_INVALID, nv_run(&r, "\x48\x0f\x38\xf6\x02"));
    uc_assert_err(UC_ERR_INSN_INVALID, nv_run(&r, "\x66\x48\x0f\x38\xf5\x02"));
    OK(uc_close(r.uc));
}

/* NoVmp U114: 32-bit mode - SSP and shadow-stack addresses are 32 bits, tokens use mode bit 0 */
static void test_x86_cet_shadow_stack_32(void)
{
    NvRun r;
    uint64_t cr0, cr4;
    uc_x86_msr m = {0x6a2, 1};
    uint32_t tok[2];

    nv_open(&r, UC_MODE_32, UC_CPU_X86_MAX);
    OK(uc_mem_map(r.uc, 0x300000, 0x2000, UC_PROT_ALL));
    OK(uc_reg_read(r.uc, UC_X86_REG_CR0, &cr0));
    cr0 |= 0x10000;
    OK(uc_reg_write(r.uc, UC_X86_REG_CR0, &cr0));
    OK(uc_reg_read(r.uc, UC_X86_REG_CR4, &cr4));
    cr4 |= 1u << 23;
    OK(uc_reg_write(r.uc, UC_X86_REG_CR4, &cr4));
    OK(uc_reg_write(r.uc, UC_X86_REG_MSR, &m));

    /* INCSSPD: SSP 32-bit, RDSSPD */
    nv_set(&r, UC_X86_REG_SSP, 0x300800);
    nv_set(&r, UC_X86_REG_EAX, 2);
    OK(nv_run(&r, "\xf3\x0f\xae\xe8\xf3\x0f\x1e\xc9"));
    TEST_CHECK(nv_get(&r, UC_X86_REG_SSP) == 0x300808);
    TEST_CHECK(nv_get(&r, UC_X86_REG_ECX) == 0x300808);

    /* RSTORSSP with a legacy token (bit 0 = 0) at 301FF8h for 302000h */
    tok[0] = 0x302000;
    tok[1] = 0;
    OK(uc_mem_write(r.uc, 0x301ff8, tok, 8));
    nv_set(&r, UC_X86_REG_EDX, 0x301ff8);
    OK(nv_run(&r, "\xf3\x0f\x01\x2a"));
    TEST_CHECK(r.cap.count == 0 && nv_get(&r, UC_X86_REG_SSP) == 0x301ff8);
    OK(uc_mem_read(r.uc, 0x301ff8, tok, 8));
    TEST_CHECK(tok[0] == 0x30080a && tok[1] == 0);       /* old SSP | 2, mode 0 */
    /* a 64-bit token (bit 0 = 1) is refused: #CP(4) */
    tok[0] = 0x302001;
    OK(uc_mem_write(r.uc, 0x301ff0, tok, 8));
    nv_set(&r, UC_X86_REG_EDX, 0x301ff0);
    OK(nv_run(&r, "\xf3\x0f\x01\x2a"));
    TEST_CHECK(r.cap.count == 1 && r.cap.intno == 21);
    OK(uc_close(r.uc));
}

/*
 * NoVmp U115: near CALL/RET with shadow stacks (SDM Vol2 CALL, RET): CALL pushes
 * the return address on the shadow stack too (not for CALL rel 0), RET compares
 * the two copies, #CP(NEAR-RET) = vector 21 on a mismatch with RSP/SSP unchanged.
 */
static void test_x86_cet_call_ret(void)
{
    NvRun r;
    uint64_t rsp, base;
    uint32_t v32;

    nv_open(&r, UC_MODE_64, UC_CPU_X86_MAX);
    OK(uc_mem_map(r.uc, 0x300000, 0x2000, UC_PROT_ALL));
    nv_cet_on(&r);
    nv_wrmsr(&r, 0x6a2, 1);                 /* IA32_S_CET.SH_STK_EN */
    nv_set(&r, UC_X86_REG_SSP, 0x300800);
    nv_set(&r, UC_X86_REG_RSP, 0x201800);

    /*
     *  0: call 12; 5: rdsspq rdx; 10: jmp 18; 12: rdsspq rcx; 17: ret
     */
    OK(nv_run(&r, "\xe8\x07\x00\x00\x00\xf3\x48\x0f\x1e\xca\xeb\x06"
                  "\xf3\x48\x0f\x1e\xc9\xc3"));
    base = r.last;
    TEST_CHECK(r.cap.count == 0);
    TEST_CHECK(nv_get(&r, UC_X86_REG_RCX) == 0x3007f8);
    TEST_CHECK(nv_ld64(&r, 0x3007f8) == base + 5);
    TEST_CHECK(nv_get(&r, UC_X86_REG_RDX) == 0x300800);
    TEST_CHECK(nv_get(&r, UC_X86_REG_SSP) == 0x300800);
    TEST_CHECK(nv_get(&r, UC_X86_REG_RSP) == 0x201800);

    /* call rel 0 (get-PC idiom): no shadow-stack push */
    OK(nv_run(&r, "\xe8\x00\x00\x00\x00\x58"));
    TEST_CHECK(nv_get(&r, UC_X86_REG_SSP) == 0x300800);
    TEST_CHECK(nv_get(&r, UC_X86_REG_RAX) == r.last + 5);

    /* indirect call rax pushes; ret imm16 pops and checks */
    nv_set(&r, UC_X86_REG_RAX, code_start + 0x3000);
    OK(uc_mem_write(r.uc, code_start + 0x3000, "\xf3\x48\x0f\x1e\xc9\xc2\x00\x00", 8));
    OK(nv_run(&r, "\xff\xd0\x90"));
    TEST_CHECK(r.cap.count == 0);
    TEST_CHECK(nv_get(&r, UC_X86_REG_RCX) == 0x3007f8);
    TEST_CHECK(nv_get(&r, UC_X86_REG_SSP) == 0x300800);

    /*
     * return address changed on the data stack: #CP(NEAR-RET) at the RET, RSP
     * and SSP as before the RET. 0: call 5; 5: mov [rsp], rax; 9: ret
     */
    nv_set(&r, UC_X86_REG_RAX, 0x1234);
    OK(nv_run(&r, "\xe8\x01\x00\x00\x00\x90\x48\x89\x04\x24\xc3"));
    TEST_CHECK(r.cap.count == 1 && r.cap.intno == 21);
    TEST_CHECK(nv_get(&r, UC_X86_REG_RIP) == r.last + 10);
    TEST_CHECK(nv_get(&r, UC_X86_REG_SSP) == 0x3007f8);
    TEST_CHECK(nv_get(&r, UC_X86_REG_RSP) == 0x2017f8);

    /* a shadow-stack push that faults (#GP, non-canonical) leaves RSP and SSP unchanged */
    nv_set(&r, UC_X86_REG_SSP, 0x800000000010ull);
    nv_set(&r, UC_X86_REG_RSP, 0x201800);
    OK(nv_run(&r, "\xe8\x01\x00\x00\x00\x90\xc3"));
    TEST_CHECK(r.cap.count == 1 && r.cap.intno == 13);
    TEST_CHECK(nv_get(&r, UC_X86_REG_RIP) == r.last);
    TEST_CHECK(nv_get(&r, UC_X86_REG_SSP) == 0x800000000010ull);
    TEST_CHECK(nv_get(&r, UC_X86_REG_RSP) == 0x201800);

    /* shadow stacks off at CPL0 (IA32_S_CET = 0): CALL/RET leave SSP alone */
    nv_wrmsr(&r, 0x6a2, 0);
    nv_set(&r, UC_X86_REG_SSP, 0x300800);
    OK(nv_run(&r, "\xe8\x07\x00\x00\x00\x90\x90\x90\x90\x90\xeb\x06"
                  "\x90\x90\x90\x90\x90\xc3"));
    TEST_CHECK(r.cap.count == 0 && nv_get(&r, UC_X86_REG_SSP) == 0x300800);
    /* ... but at CPL3 with IA32_U_CET.SH_STK_EN: user shadow stack */
    nv_wrmsr(&r, 0x6a0, 1);
    OK(nv_run3(&r, "\xe8\x07\x00\x00\x00\xf3\x48\x0f\x1e\xca\xeb\x06"
                   "\xf3\x48\x0f\x1e\xc9\xc3"));
    TEST_CHECK(nv_cpl(&r) == 3 && r.cap.count == 0);
    TEST_CHECK(nv_get(&r, UC_X86_REG_RCX) == 0x3007f8);
    TEST_CHECK(nv_get(&r, UC_X86_REG_RDX) == 0x300800);
    OK(uc_close(r.uc));

    /* 32-bit mode: 4-byte shadow-stack entries; 66 call/ret rel16 push IP zero-extended */
    nv_open(&r, UC_MODE_32, UC_CPU_X86_MAX);
    OK(uc_mem_map(r.uc, 0x300000, 0x2000, UC_PROT_ALL));
    {
        uint64_t cr0, cr4;
        uc_x86_msr m = {0x6a2, 1};

        OK(uc_reg_read(r.uc, UC_X86_REG_CR0, &cr0));
        cr0 |= 0x10000;
        OK(uc_reg_write(r.uc, UC_X86_REG_CR0, &cr0));
        OK(uc_reg_read(r.uc, UC_X86_REG_CR4, &cr4));
        cr4 |= 1u << 23;
        OK(uc_reg_write(r.uc, UC_X86_REG_CR4, &cr4));
        OK(uc_reg_write(r.uc, UC_X86_REG_MSR, &m));
    }
    nv_set(&r, UC_X86_REG_SSP, 0x300800);
    nv_set(&r, UC_X86_REG_ESP, 0x201800);
    /* 0: call 7; 5: jmp 12; 7: rdsspd ecx; 11: ret */
    OK(nv_run(&r, "\xe8\x02\x00\x00\x00\xeb\x05\xf3\x0f\x1e\xc9\xc3"));
    base = r.last;
    TEST_CHECK(r.cap.count == 0);
    TEST_CHECK(nv_get(&r, UC_X86_REG_ECX) == 0x3007fc);
    OK(uc_mem_read(r.uc, 0x3007fc, &v32, 4));
    TEST_CHECK(v32 == base + 5);
    TEST_CHECK(nv_get(&r, UC_X86_REG_SSP) == 0x300800);
    /* 0: 66 call 6 (rel16); 4: jmp 12; 6: rdsspd ecx; 10: 66 ret */
    OK(nv_run(&r, "\x66\xe8\x02\x00\xeb\x06\xf3\x0f\x1e\xc9\x66\xc3"));
    base = r.last;
    TEST_CHECK(r.cap.count == 0);
    TEST_CHECK(nv_get(&r, UC_X86_REG_ECX) == 0x3007fc);
    OK(uc_mem_read(r.uc, 0x3007fc, &v32, 4));
    TEST_CHECK(v32 == ((base + 4) & 0xffff));
    TEST_CHECK(nv_get(&r, UC_X86_REG_SSP) == 0x300800);
    OK(uc_reg_read(r.uc, UC_X86_REG_ESP, &rsp));
    TEST_CHECK((uint32_t)rsp == 0x201800);
    OK(uc_close(r.uc));
}

/*
 * NoVmp U116: CET indirect branch tracking (SDM Vol1 18.3; Vol2 CALL, JMP,
 * ENDBR32/64). Branch target slots are at code_start + 0x3000 + n * 0x40.
 */
static uint64_t nv_ibt_target(NvRun *r, int n, const char *code, size_t len)
{
    uint64_t a = code_start + 0x3000 + (uint64_t)n * 0x40;

    OK(uc_mem_write(r->uc, a, code, len));
    return a;
}

static uc_err nv_run3_to(NvRun *r, const char *code, size_t len, uint64_t until)
{
    char stub[] = "\x6a\x23\x68\x00\x1f\x20\x00\x9c\x6a\x2b\x68\x00\x00\x00\x00\x48\xcf";
    uint64_t sa = r->next, ca = r->next + 0x40;
    uint32_t ca32 = (uint32_t)ca;

    nv_gdt(r);
    memcpy(stub + 11, &ca32, 4);
    r->next += 0x40 + ((len + 0x3f) & ~(uint64_t)0x3f);
    nv_set(r, UC_X86_REG_RSP, 0x201e00);
    r->cap.count = 0;
    r->cap.intno = 0;
    r->last = ca;
    OK(uc_mem_write(r->uc, sa, stub, sizeof(stub) - 1));
    OK(uc_mem_write(r->uc, ca, code, len));
    return uc_emu_start(r->uc, sa, until, 0, 0);
}

static void test_x86_cet_ibt(void)
{
    NvRun r;
    uint64_t t_endbr, t_plain, t_endbr32, t_int3, farp[2];
    uint8_t bitmap;

    nv_open(&r, UC_MODE_64, UC_CPU_X86_MAX);
    OK(uc_mem_map(r.uc, 0x300000, 0x2000, UC_PROT_ALL));
    nv_set(&r, UC_X86_REG_RAX, 7);
    nv_set(&r, UC_X86_REG_RCX, 0);
    OK(nv_run(&r, "\x0f\xa2"));
    TEST_CHECK(nv_get(&r, UC_X86_REG_RDX) & (1u << 20));
    nv_cet_on(&r);

    /* targets: endbr64; inc rbx | inc rbx | endbr32; inc rbx | int3 */
    t_endbr = nv_ibt_target(&r, 0, "\xf3\x0f\x1e\xfa\x48\xff\xc3", 7);
    t_plain = nv_ibt_target(&r, 1, "\x48\xff\xc3", 3);
    t_endbr32 = nv_ibt_target(&r, 2, "\xf3\x0f\x1e\xfb\x48\xff\xc3", 7);
    t_int3 = nv_ibt_target(&r, 3, "\xcc", 1);

    /* IA32_S_CET: TRACKER and SUPPRESS together are refused (#GP(0) via WRMSR) */
    nv_set(&r, UC_X86_REG_RCX, 0x6a2);
    nv_set(&r, UC_X86_REG_RAX, 0xc04);
    nv_set(&r, UC_X86_REG_RDX, 0);
    OK(nv_run(&r, "\x0f\x30"));
    TEST_CHECK(r.cap.count == 1 && r.cap.intno == 13);
    nv_wrmsr(&r, 0x6a2, 0x4);                       /* ENDBR_EN */
    TEST_CHECK(nv_rdmsr(&r, 0x6a2) == 0x4);

    /* jmp rax to ENDBR64: fine, tracker back to IDLE */
    nv_set(&r, UC_X86_REG_RBX, 0);
    nv_set(&r, UC_X86_REG_RAX, t_endbr);
    OK(uc_mem_write(r.uc, code_start + 0x2000, "\xff\xe0", 2));
    r.cap.count = 0;
    OK(uc_emu_start(r.uc, code_start + 0x2000, t_endbr + 7, 0, 0));
    TEST_CHECK(r.cap.count == 0 && nv_get(&r, UC_X86_REG_RBX) == 1);
    TEST_CHECK(nv_rdmsr(&r, 0x6a2) == 0x4);

    /* jmp rax to a target without ENDBR: #CP(ENDBRANCH) at the target, tracker waits */
    nv_set(&r, UC_X86_REG_RBX, 0);
    nv_set(&r, UC_X86_REG_RAX, t_plain);
    r.cap.count = 0;
    OK(uc_emu_start(r.uc, code_start + 0x2000, t_plain + 3, 0, 0));
    TEST_CHECK(r.cap.count == 1 && r.cap.intno == 21);
    TEST_CHECK(nv_get(&r, UC_X86_REG_RIP) == t_plain);
    TEST_CHECK(nv_get(&r, UC_X86_REG_RBX) == 0);
    TEST_CHECK(nv_rdmsr(&r, 0x6a2) == 0x804);
    /* still waiting: the next instruction must be ENDBR64; ENDBR32 does not count in 64-bit mode */
    r.cap.count = 0;
    OK(uc_emu_start(r.uc, t_endbr32, t_endbr32 + 7, 0, 0));
    TEST_CHECK(r.cap.count == 1 && r.cap.intno == 21);
    /* INT3 at the target: #BP first, tracker unchanged */
    r.cap.count = 0;
    OK(uc_emu_start(r.uc, t_int3, t_int3 + 1, 0, 0));
    TEST_CHECK(r.cap.count == 1 && r.cap.intno == 3);
    TEST_CHECK(nv_rdmsr(&r, 0x6a2) == 0x804);
    /* ENDBR64 clears it */
    r.cap.count = 0;
    OK(uc_emu_start(r.uc, t_endbr, t_endbr + 7, 0, 0));
    TEST_CHECK(r.cap.count == 0 && nv_rdmsr(&r, 0x6a2) == 0x4);

    /* call rax: tracked as well */
    nv_set(&r, UC_X86_REG_RSP, 0x201800);
    nv_set(&r, UC_X86_REG_RAX, t_plain);
    OK(nv_run(&r, "\xff\xd0"));
    TEST_CHECK(r.cap.count == 1 && r.cap.intno == 21);
    nv_wrmsr(&r, 0x6a2, 0x4);

    /* NOTRACK (3E) jmp: tracked while NO_TRACK_EN = 0, untracked with NO_TRACK_EN = 1 */
    nv_set(&r, UC_X86_REG_RAX, t_plain);
    OK(nv_run(&r, "\x3e\xff\xe0"));
    TEST_CHECK(r.cap.count == 1 && r.cap.intno == 21);
    nv_wrmsr(&r, 0x6a2, 0x14);                      /* ENDBR_EN | NO_TRACK_EN */
    nv_set(&r, UC_X86_REG_RBX, 0);
    OK(uc_mem_write(r.uc, code_start + 0x2100, "\x3e\xff\xe0", 3));
    r.cap.count = 0;
    OK(uc_emu_start(r.uc, code_start + 0x2100, t_plain + 3, 0, 0));
    TEST_CHECK(r.cap.count == 0 && nv_get(&r, UC_X86_REG_RBX) == 1);
    TEST_CHECK(nv_rdmsr(&r, 0x6a2) == 0x14);
    /* ... but not with a 64H/65H prefix in 64-bit mode */
    OK(nv_run(&r, "\x3e\x64\xff\xe0"));
    TEST_CHECK(r.cap.count == 1 && r.cap.intno == 21);
    nv_wrmsr(&r, 0x6a2, 0x14);

    /*
     * legacy compatibility (LEG_IW_EN, bitmap at 300000h): an allowed legacy page
     * makes the tracker IDLE and suppressed (SUPPRESS_DIS = 0); while suppressed,
     * indirect branches are not tracked; ENDBR64 unsuppresses.
     */
    bitmap = (uint8_t)(1u << ((t_plain >> 12) & 7));
    OK(uc_mem_write(r.uc, 0x300000 + (t_plain >> 15), &bitmap, 1));
    nv_wrmsr(&r, 0x6a2, 0x300000 | 0xc);            /* base | LEG_IW_EN | ENDBR_EN */
    nv_set(&r, UC_X86_REG_RBX, 0);
    OK(uc_mem_write(r.uc, code_start + 0x2140, "\xff\xe0", 2));
    r.cap.count = 0;
    OK(uc_emu_start(r.uc, code_start + 0x2140, t_plain + 3, 0, 0));
    TEST_CHECK(r.cap.count == 0 && nv_get(&r, UC_X86_REG_RBX) == 1);
    TEST_CHECK(nv_rdmsr(&r, 0x6a2) == (0x300000 | 0x40c));
    OK(uc_emu_start(r.uc, code_start + 0x2140, t_plain + 3, 0, 0));
    TEST_CHECK(r.cap.count == 0 && nv_rdmsr(&r, 0x6a2) == (0x300000 | 0x40c));
    r.cap.count = 0;
    OK(uc_emu_start(r.uc, t_endbr, t_endbr + 7, 0, 0));
    TEST_CHECK(nv_rdmsr(&r, 0x6a2) == (0x300000 | 0xc));
    /* page not in the bitmap: #CP(ENDBRANCH) */
    bitmap = 0;
    OK(uc_mem_write(r.uc, 0x300000 + (t_plain >> 15), &bitmap, 1));
    r.cap.count = 0;
    OK(uc_emu_start(r.uc, code_start + 0x2140, t_plain + 3, 0, 0));
    TEST_CHECK(r.cap.count == 1 && r.cap.intno == 21);
    nv_wrmsr(&r, 0x6a2, 0x4);

    /* far JMP m16:64 (to selector 08h, CPL0): WAIT_FOR_ENDBRANCH */
    nv_gdt(&r);
    farp[0] = t_plain;
    farp[1] = 0x08;
    OK(uc_mem_write(r.uc, 0x200800, farp, 10));
    nv_set(&r, UC_X86_REG_RAX, 0x200800);
    OK(nv_run(&r, "\x48\xff\x28"));
    TEST_CHECK(r.cap.count == 1 && r.cap.intno == 21);
    TEST_CHECK(nv_get(&r, UC_X86_REG_RIP) == t_plain);
    nv_wrmsr(&r, 0x6a2, 0x4);

    /* CPL3 uses IA32_U_CET: off there, so untracked; on, tracked */
    nv_set(&r, UC_X86_REG_RAX, t_plain);
    nv_set(&r, UC_X86_REG_RBX, 0);
    OK(nv_run3_to(&r, "\xff\xe0", 2, t_plain + 3));
    TEST_CHECK(nv_cpl(&r) == 3 && r.cap.count == 0 && nv_get(&r, UC_X86_REG_RBX) == 1);
    nv_wrmsr(&r, 0x6a0, 0x4);
    OK(nv_run3_to(&r, "\xff\xe0", 2, t_plain + 3));
    TEST_CHECK(nv_cpl(&r) == 3 && r.cap.count == 1 && r.cap.intno == 21);
    TEST_CHECK(nv_rdmsr(&r, 0x6a0) == 0x804);
    TEST_CHECK(nv_rdmsr(&r, 0x6a2) == 0x4);
    OK(uc_close(r.uc));

    /* strict profile without CET_IBT: no tracking, ENDBR64 a NOP */
    nv_open(&r, UC_MODE_64, UC_CPU_X86_MAX);
    nv_cet_on(&r);
    nv_wrmsr(&r, 0x6a2, 0x4);
    nv_profile7(&r, 0, 1u << 7, 0);
    t_plain = nv_ibt_target(&r, 1, "\x48\xff\xc3", 3);
    nv_set(&r, UC_X86_REG_RAX, t_plain);
    OK(uc_mem_write(r.uc, code_start + 0x2000, "\xff\xe0", 2));
    r.cap.count = 0;
    OK(uc_emu_start(r.uc, code_start + 0x2000, t_plain + 3, 0, 0));
    TEST_CHECK(r.cap.count == 0);
    OK(uc_close(r.uc));
}

/*
 * NoVmp U117: shadow-stack page types with 4-level paging (SDM Vol3 5.6.1).
 * Identity map: 0-2 MB and 2-4 MB through 4 KB page tables at 400000h. 300000h is a
 * user shadow-stack page (R/W = 0, D = 1, U/S = 1), 301000h a supervisor one,
 * 302000h an ordinary writable page; 380000h is not present.
 */
static void nv_paging_ss(NvRun *r)
{
    uint64_t e, cr0, cr4, a;
    uc_x86_msr efer = {0xc0000080, 0};

    OK(uc_mem_map(r->uc, 0x300000, 0x3000, UC_PROT_ALL));
    OK(uc_mem_map(r->uc, 0x400000, 0x5000, UC_PROT_ALL));
    e = 0x401007; nv_st64(r, 0x400000, e);            /* PML4[0] -> PDPT */
    e = 0x402007; nv_st64(r, 0x401000, e);            /* PDPT[0] -> PD */
    e = 0x403007; nv_st64(r, 0x402000, e);            /* PD[0] -> PT 0-2 MB */
    e = 0x404007; nv_st64(r, 0x402008, e);            /* PD[1] -> PT 2-4 MB */
    for (a = code_start; a < code_start + code_len; a += 0x1000) {
        nv_st64(r, 0x403000 + (a >> 12) * 8, a | 7);
    }
    nv_st64(r, 0x404000 + ((0x200000 - 0x200000) >> 12) * 8, 0x200000 | 7);
    nv_st64(r, 0x404000 + ((0x201000 - 0x200000) >> 12) * 8, 0x201000 | 7);
    nv_st64(r, 0x404000 + ((0x300000 - 0x200000) >> 12) * 8, 0x300000 | 0x45);
    nv_st64(r, 0x404000 + ((0x301000 - 0x200000) >> 12) * 8, 0x301000 | 0x41);
    nv_st64(r, 0x404000 + ((0x302000 - 0x200000) >> 12) * 8, 0x302000 | 7);
    a = 0x400000;
    OK(uc_reg_write(r->uc, UC_X86_REG_CR3, &a));
    OK(uc_reg_read(r->uc, UC_X86_REG_CR4, &cr4));
    cr4 |= (1u << 5) | (1u << 23);                    /* PAE, CET */
    OK(uc_reg_read(r->uc, UC_X86_REG_MSR, &efer));
    efer.value |= 1u << 8;                            /* LME */
    OK(uc_reg_write(r->uc, UC_X86_REG_MSR, &efer));
    OK(uc_reg_read(r->uc, UC_X86_REG_CR0, &cr0));
    cr0 |= 0x80010000ull;                             /* PG, WP */
    OK(uc_reg_write(r->uc, UC_X86_REG_CR4, &cr4));
    OK(uc_reg_write(r->uc, UC_X86_REG_CR0, &cr0));
}

static void test_x86_cet_shadow_stack_paging(void)
{
    NvRun r;
    uint64_t cr2;

    nv_open(&r, UC_MODE_64, UC_CPU_X86_MAX);
    nv_paging_ss(&r);
    nv_wrmsr(&r, 0x6a2, 3);                           /* SH_STK_EN | WR_SHSTK_EN */
    nv_set(&r, UC_X86_REG_RAX, 1);

    /* supervisor shadow-stack page: INCSSP reads it */
    nv_set(&r, UC_X86_REG_SSP, 0x301800);
    OK(nv_run(&r, "\xf3\x48\x0f\xae\xe8"));
    TEST_CHECK(r.cap.count == 0 && nv_get(&r, UC_X86_REG_SSP) == 0x301808);
    /* a user shadow-stack page, an ordinary page, a not-present page: #PF, SSP unchanged */
    nv_set(&r, UC_X86_REG_SSP, 0x300800);
    OK(nv_run(&r, "\xf3\x48\x0f\xae\xe8"));
    TEST_CHECK(r.cap.count == 1 && r.cap.intno == 14);
    OK(uc_reg_read(r.uc, UC_X86_REG_CR2, &cr2));
    TEST_CHECK(cr2 == 0x300800 && nv_get(&r, UC_X86_REG_SSP) == 0x300800);
    nv_set(&r, UC_X86_REG_SSP, 0x302800);
    OK(nv_run(&r, "\xf3\x48\x0f\xae\xe8"));
    TEST_CHECK(r.cap.count == 1 && r.cap.intno == 14);
    nv_set(&r, UC_X86_REG_SSP, 0x380000);
    OK(nv_run(&r, "\xf3\x48\x0f\xae\xe8"));
    TEST_CHECK(r.cap.count == 1 && r.cap.intno == 14);

    /* ordinary accesses: reading a shadow-stack page is fine, writing it is #PF (R/W = 0, WP) */
    nv_set(&r, UC_X86_REG_RDX, 0x301800);
    OK(nv_run(&r, "\x48\x8b\x0a"));
    TEST_CHECK(r.cap.count == 0);
    OK(nv_run(&r, "\x48\x89\x0a"));
    TEST_CHECK(r.cap.count == 1 && r.cap.intno == 14);

    /* WRSSQ: supervisor shadow-stack page ok, ordinary page #PF */
    nv_set(&r, UC_X86_REG_RAX, 0x1122334455667788ull);
    OK(nv_run(&r, "\x48\x0f\x38\xf6\x02"));
    TEST_CHECK(r.cap.count == 0 && nv_ld64(&r, 0x301800) == 0x1122334455667788ull);
    nv_set(&r, UC_X86_REG_RDX, 0x302000);
    OK(nv_run(&r, "\x48\x0f\x38\xf6\x02"));
    TEST_CHECK(r.cap.count == 1 && r.cap.intno == 14);
    /* WRUSSQ (user access): user shadow-stack page ok, supervisor one #PF */
    nv_set(&r, UC_X86_REG_RDX, 0x300808);
    OK(nv_run(&r, "\x66\x48\x0f\x38\xf5\x02"));
    TEST_CHECK(r.cap.count == 0 && nv_ld64(&r, 0x300808) == 0x1122334455667788ull);
    nv_set(&r, UC_X86_REG_RDX, 0x301808);
    OK(nv_run(&r, "\x66\x48\x0f\x38\xf5\x02"));
    TEST_CHECK(r.cap.count == 1 && r.cap.intno == 14);

    /* CALL/RET on the supervisor shadow-stack page; with SSP on an ordinary page the CALL faults */
    nv_set(&r, UC_X86_REG_SSP, 0x303000);
    nv_set(&r, UC_X86_REG_RSP, 0x201800);
    OK(nv_run(&r, "\xe8\x02\x00\x00\x00\xeb\x01\xc3"));
    TEST_CHECK(r.cap.count == 1 && r.cap.intno == 14);
    TEST_CHECK(nv_get(&r, UC_X86_REG_RSP) == 0x201800);
    nv_set(&r, UC_X86_REG_SSP, 0x301ff8);
    OK(nv_run(&r, "\xe8\x02\x00\x00\x00\xeb\x01\xc3"));
    TEST_CHECK(r.cap.count == 0 && nv_get(&r, UC_X86_REG_SSP) == 0x301ff8);
    TEST_CHECK(nv_ld64(&r, 0x301ff0) == r.last + 5);

    /* CPL3 (IA32_U_CET.SH_STK_EN): the user shadow-stack page only */
    nv_wrmsr(&r, 0x6a0, 1);
    nv_set(&r, UC_X86_REG_RAX, 1);
    nv_set(&r, UC_X86_REG_SSP, 0x300800);
    OK(nv_run3(&r, "\xf3\x48\x0f\xae\xe8"));
    TEST_CHECK(nv_cpl(&r) == 3 && r.cap.count == 0 && nv_get(&r, UC_X86_REG_SSP) == 0x300808);
    nv_set(&r, UC_X86_REG_SSP, 0x301800);
    OK(nv_run3(&r, "\xf3\x48\x0f\xae\xe8"));
    TEST_CHECK(r.cap.count == 1 && r.cap.intno == 14);
    OK(uc_close(r.uc));
}

/*
 * ---- NoVmp U127-U133: VEX-encoded opmask instructions (EVEX milestone K) ----
 * Expected values come from the independent SDM model Emulator/tools/isa/ref_opmask.py
 * (x86_opmask_vectors.inc): KAND/KANDN/KOR/KXOR/KXNOR/KADD (B/W/D/Q), KUNPCKBW/WD/DQ,
 * KNOT, KORTEST, KTEST, KMOV (k/m, m, r32/r64 both ways), KSHIFTL/KSHIFTR.
 */
#include "x86_opmask_vectors.inc"

#define KT_DATA 0x200000
#define KT_ALL (UC_X86_AVX512_F | UC_X86_AVX512_DQ | UC_X86_AVX512_BW)

typedef struct KtCfg {
    int avx512;               /* UC_CTL_X86_AVX512 mask (0 = off) */
    int set_xcr0;             /* write UC_X86_REG_XCR0 = xcr0 after init */
    uint64_t xcr0;
    uint64_t cr0_or;          /* bits OR-ed into CR0 (TS = 8) */
    uint64_t cr4_clear;       /* bits cleared in CR4 (OSXSAVE = 1 << 18) */
    const uc_x86_cpuid *prof; /* UC_CTL_X86_CPUID profile */
    size_t nprof;
    int strict;               /* UC_CTL_X86_CPUID_STRICT */
} KtCfg;

/* Run one vector; returns the exception vector (6 #UD, 7 #NM, ...) or -1, state in out */
static int kt_run(const struct x86_kvec *t, const KtCfg *cfg, struct x86_kvec *out)
{
    uc_engine *uc;
    uc_hook hook;
    X86IntrCapture cap = { 0 };
    uc_err err;
    int i, vec;

    *out = *t;
    OK(uc_open(UC_ARCH_X86, t->mode == 64 ? UC_MODE_64 : UC_MODE_32, &uc));
    OK(uc_ctl_set_cpu_model(uc, UC_CPU_X86_MAX));
    if (cfg->avx512) {
        OK(uc_ctl_set_x86_avx512(uc, cfg->avx512));
    }
    if (cfg->nprof) {
        OK(uc_ctl_set_x86_cpuid(uc, cfg->prof, cfg->nprof));
    }
    if (cfg->nprof || cfg->strict) {
        /* explicit (U435: a profile alone is strict), so strict 0 stays non-strict */
        OK(uc_ctl_set_x86_cpuid_strict(uc, cfg->strict));
    }
    OK(uc_mem_map(uc, code_start, code_len, UC_PROT_ALL));
    OK(uc_mem_map(uc, KT_DATA, 0x1000, UC_PROT_ALL));
    OK(uc_hook_add(uc, &hook, UC_HOOK_INTR, test_x86_intr_capture_cb, &cap, 1, 0));
    OK(uc_mem_write(uc, code_start, t->code, t->code_len));
    OK(uc_mem_write(uc, KT_DATA, t->mem, sizeof(t->mem)));
    for (i = 0; i < 8; i++) {
        OK(uc_reg_write(uc, UC_X86_REG_K0 + i, &t->k[i]));
    }
    if (t->mode == 64) {
        uint64_t rsi = KT_DATA;
        OK(uc_reg_write(uc, UC_X86_REG_RAX, &t->rax));
        OK(uc_reg_write(uc, UC_X86_REG_R9, &t->r9));
        OK(uc_reg_write(uc, UC_X86_REG_R10, &t->r10));
        OK(uc_reg_write(uc, UC_X86_REG_RFLAGS, &t->rflags));
        OK(uc_reg_write(uc, UC_X86_REG_RSI, &rsi));
    } else {
        uint32_t eax = (uint32_t)t->rax, efl = (uint32_t)t->rflags, esi = KT_DATA;
        OK(uc_reg_write(uc, UC_X86_REG_EAX, &eax));
        OK(uc_reg_write(uc, UC_X86_REG_EFLAGS, &efl));
        OK(uc_reg_write(uc, UC_X86_REG_ESI, &esi));
    }
    if (cfg->set_xcr0) {
        OK(uc_reg_write(uc, UC_X86_REG_XCR0, &cfg->xcr0));
    }
    if (cfg->cr0_or) {
        uint64_t cr0 = 0;
        OK(uc_reg_read(uc, UC_X86_REG_CR0, &cr0));
        cr0 |= cfg->cr0_or;
        OK(uc_reg_write(uc, UC_X86_REG_CR0, &cr0));
    }
    if (cfg->cr4_clear) {
        uint64_t cr4 = 0;
        OK(uc_reg_read(uc, UC_X86_REG_CR4, &cr4));
        cr4 &= ~cfg->cr4_clear;
        OK(uc_reg_write(uc, UC_X86_REG_CR4, &cr4));
    }
    err = uc_emu_start(uc, code_start, code_start + t->code_len, 0, 0);
    if (err == UC_ERR_INSN_INVALID) {
        vec = 6;
    } else {
        OK(err);
        vec = cap.count ? (int)cap.intno : -1;
    }
    for (i = 0; i < 8; i++) {
        OK(uc_reg_read(uc, UC_X86_REG_K0 + i, &out->ek[i]));
    }
    if (t->mode == 64) {
        OK(uc_reg_read(uc, UC_X86_REG_RAX, &out->erax));
        OK(uc_reg_read(uc, UC_X86_REG_R9, &out->er9));
        OK(uc_reg_read(uc, UC_X86_REG_R10, &out->er10));
        OK(uc_reg_read(uc, UC_X86_REG_RFLAGS, &out->erflags));
    } else {
        uint32_t eax = 0, efl = 0;
        OK(uc_reg_read(uc, UC_X86_REG_EAX, &eax));
        OK(uc_reg_read(uc, UC_X86_REG_EFLAGS, &efl));
        out->erax = eax;
        out->erflags = efl;
        out->er9 = t->er9;
        out->er10 = t->er10;
    }
    OK(uc_mem_read(uc, KT_DATA, out->emem, sizeof(out->emem)));
    OK(uc_close(uc));
    return vec;
}

/* the state after a run equals the vector's expectation (or, unchanged = 1, its input) */
static void kt_check_state(const struct x86_kvec *t, const struct x86_kvec *o, int unchanged)
{
    int i;
    uint64_t rf_mask = 0xcd5; /* OF DF SF ZF AF PF CF (ignore RF/TF/IF bookkeeping) */

    for (i = 0; i < 8; i++) {
        uint64_t exp = unchanged ? t->k[i] : t->ek[i];
        TEST_CHECK_(o->ek[i] == exp, "%s: k%d = %016llx, expected %016llx", t->name, i,
                    (unsigned long long)o->ek[i], (unsigned long long)exp);
    }
    TEST_CHECK_(o->erax == (unchanged ? t->rax : t->erax), "%s: rax = %016llx", t->name,
                (unsigned long long)o->erax);
    TEST_CHECK_(o->er9 == (unchanged ? t->r9 : t->er9), "%s: r9", t->name);
    TEST_CHECK_(o->er10 == (unchanged ? t->r10 : t->er10), "%s: r10 = %016llx", t->name,
                (unsigned long long)o->er10);
    TEST_CHECK_((o->erflags & rf_mask) == ((unchanged ? t->rflags : t->erflags) & rf_mask),
                "%s: rflags = %llx, expected %llx", t->name, (unsigned long long)o->erflags,
                (unsigned long long)(unchanged ? t->rflags : t->erflags));
    TEST_CHECK_(memcmp(o->emem, unchanged ? t->mem : t->emem, sizeof(o->emem)) == 0,
                "%s: memory", t->name);
}

/* every form, every vector: values, flags, memory sizes, zero extension (64- and 32-bit) */
static void test_x86_opmask_vectors(void)
{
    static const KtCfg all = { KT_ALL };
    size_t i;

    for (i = 0; i < sizeof(x86_kvecs) / sizeof(x86_kvecs[0]); i++) {
        const struct x86_kvec *t = &x86_kvecs[i];
        struct x86_kvec o;
        int vec = kt_run(t, &all, &o);

        TEST_CHECK_(vec == -1, "%s executes (vector %d)", t->name, vec);
        kt_check_state(t, &o, 0);
    }
}

/* reserved encodings #UD with everything enabled (and stay #UD, not #NM, with CR0.TS = 1) */
static void test_x86_opmask_ud(void)
{
    static const KtCfg all = { KT_ALL };
    static const KtCfg ts = { KT_ALL, 0, 0, 8 };
    size_t i;

    for (i = 0; i < sizeof(x86_kuds) / sizeof(x86_kuds[0]); i++) {
        const struct x86_kud *u = &x86_kuds[i];
        struct x86_kvec t, o;

        memset(&t, 0, sizeof(t));
        t.name = u->name;
        memcpy(t.code, u->code, sizeof(t.code));
        t.code_len = u->code_len;
        t.mode = u->mode;
        t.rflags = t.erflags = 0x202;
        TEST_CHECK_(kt_run(&t, &all, &o) == 6, "#UD: %s", u->name);
        TEST_CHECK_(kt_run(&t, &ts, &o) == 6, "#UD with CR0.TS = 1: %s", u->name);
    }
}

/*
 * CPUID gating per form (SDM Vol2A: B forms AVX512DQ, W forms AVX512F except KADDW/KTESTW
 * AVX512DQ, D/Q forms AVX512BW): the first vector of every form under each feature mask;
 * a #UD leaves the state untouched.
 */
static void test_x86_opmask_features(void)
{
    static const int masks[] = {
        0, UC_X86_AVX512_F, UC_X86_AVX512_F | UC_X86_AVX512_DQ,
        UC_X86_AVX512_F | UC_X86_AVX512_BW, KT_ALL,
    };
    size_t i, j;

    for (i = 0; i < sizeof(x86_kvecs) / sizeof(x86_kvecs[0]); i++) {
        const struct x86_kvec *t = &x86_kvecs[i];

        if (!t->first) {
            continue;
        }
        for (j = 0; j < sizeof(masks) / sizeof(masks[0]); j++) {
            KtCfg cfg = { masks[j] };
            struct x86_kvec o;
            int runs = masks[j] != 0 && (t->feat & masks[j]) != 0;
            int vec = kt_run(t, &cfg, &o);

            TEST_CHECK_(vec == (runs ? -1 : 6), "%s with AVX-512 mask %d: vector %d",
                        t->name, masks[j], vec);
            kt_check_state(t, &o, !runs);
        }
    }
}

/*
 * State requirements (SDM Vol2A Tables 2-39, 2-65, 2-66): CR4.OSXSAVE = 1 and XCR0 =
 * 111xxx11b, else #UD; CR0.TS = 1 -> #NM; a strict CPUID profile hiding the features
 * -> #UD (non-strict: executes, as on hardware where only the CPUID bit is hidden).
 */
static void test_x86_opmask_state(void)
{
    static const uc_x86_cpuid noavx512[] = {
        {0x0, 0, 0xd, 0x756e6547, 0x6c65746e, 0x49656e69},
        {0x1, 0, 0x000b0671, 0x00800800, 0x7ffafbbf, 0xbfebfbff},
        {0x7, 0, 0, 0x239c27eb, 0x98c007bc, 0xfc184410},
        {0xd, 0, 0xe7, 0xa88, 0xa88, 0},
    };
    static const struct {
        uint64_t xcr0;
        int runs;
    } xcr0s[] = {
        {0xe7, 1}, {0xe3, 1},      /* XCR0[2] is not required (111xxx11b) */
        {0x07, 0}, {0x03, 0}, {0x63, 0}, {0xa3, 0}, {0xc3, 0}, {0xe1, 0}, {0xe2, 0},
    };
    size_t i, j;

    for (i = 0; i < sizeof(x86_kvecs) / sizeof(x86_kvecs[0]); i++) {
        const struct x86_kvec *t = &x86_kvecs[i];
        struct x86_kvec o;
        int vec;

        if (!t->first) {
            continue;
        }
        for (j = 0; j < sizeof(xcr0s) / sizeof(xcr0s[0]); j++) {
            KtCfg cfg = { KT_ALL, 1, xcr0s[j].xcr0 };

            vec = kt_run(t, &cfg, &o);
            TEST_CHECK_(vec == (xcr0s[j].runs ? -1 : 6), "%s with XCR0 = %llx: vector %d",
                        t->name, (unsigned long long)xcr0s[j].xcr0, vec);
            kt_check_state(t, &o, !xcr0s[j].runs);
        }
        {
            KtCfg cfg = { KT_ALL, 0, 0, 0, 1ULL << 18 };   /* CR4.OSXSAVE = 0 */
            vec = kt_run(t, &cfg, &o);
            TEST_CHECK_(vec == 6, "%s with CR4.OSXSAVE = 0: vector %d", t->name, vec);
            kt_check_state(t, &o, 1);
        }
        {
            KtCfg cfg = { KT_ALL, 0, 0, 8 };                /* CR0.TS = 1 */
            vec = kt_run(t, &cfg, &o);
            TEST_CHECK_(vec == 7, "%s with CR0.TS = 1: vector %d (#NM)", t->name, vec);
            kt_check_state(t, &o, 1);
        }
        {
            KtCfg cfg = { KT_ALL, 0, 0, 0, 0, noavx512, 4, 1 };
            vec = kt_run(t, &cfg, &o);
            TEST_CHECK_(vec == 6, "%s strict profile without AVX-512: vector %d", t->name, vec);
            kt_check_state(t, &o, 1);
        }
        {
            KtCfg cfg = { KT_ALL, 0, 0, 0, 0, noavx512, 4, 0 };
            vec = kt_run(t, &cfg, &o);
            TEST_CHECK_(vec == -1, "%s non-strict profile without AVX-512: vector %d",
                        t->name, vec);
            kt_check_state(t, &o, 0);
        }
    }
}

/* NoVmp U128: UC_CTL_X86_AVX512 is a mask; DQ/BW appear in CPUID.(7,0):EBX; XSETBV path */
static void test_x86_opmask_optin(void)
{
    static const struct {
        int set, get;
        uint32_t ebx;
    } m[] = {
        {UC_X86_AVX512_F, 1, TEST_X86_CPUID_7_0_EBX_AVX512F},
        {UC_X86_AVX512_DQ, 3, TEST_X86_CPUID_7_0_EBX_AVX512F | TEST_X86_CPUID_7_0_EBX_AVX512DQ},
        {UC_X86_AVX512_BW, 5, TEST_X86_CPUID_7_0_EBX_AVX512F | TEST_X86_CPUID_7_0_EBX_AVX512BW},
        {KT_ALL, 7, TEST_X86_CPUID_7_0_EBX_AVX512F | TEST_X86_CPUID_7_0_EBX_AVX512DQ |
                        TEST_X86_CPUID_7_0_EBX_AVX512BW},
        /* U140: UC_X86_AVX512_VL adds AVX512VL */
        {UC_X86_AVX512_VL, 9, TEST_X86_CPUID_7_0_EBX_AVX512F | TEST_X86_CPUID_7_0_EBX_AVX512VL},
        {KT_ALL | UC_X86_AVX512_VL, 15,
         TEST_X86_CPUID_7_0_EBX_AVX512F | TEST_X86_CPUID_7_0_EBX_AVX512DQ |
             TEST_X86_CPUID_7_0_EBX_AVX512BW | TEST_X86_CPUID_7_0_EBX_AVX512VL},
    };
    const uint32_t all = TEST_X86_CPUID_7_0_EBX_AVX512F | TEST_X86_CPUID_7_0_EBX_AVX512DQ |
                         TEST_X86_CPUID_7_0_EBX_AVX512CD | TEST_X86_CPUID_7_0_EBX_AVX512BW |
                         TEST_X86_CPUID_7_0_EBX_AVX512VL;
    uc_engine *uc;
    size_t i;
    int on = -1;
    M0 mm;
    uint32_t r[4];

    for (i = 0; i < sizeof(m) / sizeof(m[0]); i++) {
        OK(uc_open(UC_ARCH_X86, UC_MODE_64, &uc));
        OK(uc_ctl_set_cpu_model(uc, UC_CPU_X86_MAX));
        OK(uc_ctl_set_x86_avx512(uc, m[i].set));
        OK(uc_ctl_get_x86_avx512(uc, &on));
        TEST_CHECK(on == m[i].get);
        OK(uc_close(uc));
    }
    OK(uc_open(UC_ARCH_X86, UC_MODE_64, &uc));
    /* U140: 8 = VL, U320: 16 = CD, U322..: further feature bits; bit 30 is unknown */
    uc_assert_err(UC_ERR_ARG, uc_ctl_set_x86_avx512(uc, 1 << 30));
    uc_assert_err(UC_ERR_ARG, uc_ctl_set_x86_avx512(uc, -1));
    OK(uc_ctl_get_x86_avx512(uc, &on));
    TEST_CHECK(on == 0);
    OK(uc_close(uc));

    for (i = 0; i < sizeof(m) / sizeof(m[0]); i++) {
        memset(&mm, 0, sizeof(mm));
        mm.mode = UC_MODE_64;
        mm.pc = code_start;
        OK(uc_open(UC_ARCH_X86, UC_MODE_64, &mm.uc));
        OK(uc_ctl_set_cpu_model(mm.uc, UC_CPU_X86_MAX));
        OK(uc_ctl_set_x86_avx512(mm.uc, m[i].set));
        OK(uc_mem_map(mm.uc, code_start, code_len, UC_PROT_ALL));
        OK(uc_mem_map(mm.uc, M0_DATA, 0x4000, UC_PROT_ALL));
        OK(uc_hook_add(mm.uc, &mm.hook, UC_HOOK_INTR, test_x86_intr_capture_cb, &mm.cap, 1, 0));
        m0_cpuid(&mm, 7, 0, r);
        TEST_CHECK_((r[1] & all) == m[i].ebx, "mask %d: CPUID.7.0:EBX = %08x", m[i].set, r[1]);
        /* the flag follows XSETBV: KANDW k1, k2, k3 */
        if (i == 0) {
            TEST_CHECK(m0_run(&mm, "\xc5\xec\x41\xcb", 4) == -1);
            TEST_CHECK(m0_xsetbv(&mm, 0, 7) == -1);
            TEST_CHECK(m0_run(&mm, "\xc5\xec\x41\xcb", 4) == 6);
            TEST_CHECK(m0_xsetbv(&mm, 0, M0_XCR0_AVX512) == -1);
            TEST_CHECK(m0_run(&mm, "\xc5\xec\x41\xcb", 4) == -1);
            /* KANDB needs AVX512DQ */
            TEST_CHECK(m0_run(&mm, "\xc5\xed\x41\xcb", 4) == 6);
        }
        OK(uc_close(mm.uc));
    }
}

/* ---- NoVmp U91-U99 tests ---- */
/*
 * U91: LOCK on 0F 19..0F 1F (multi-byte NOP / hint space) is #UD (i5-13600K:
 * cases_fixes.txt, every /r, register and memory form); without LOCK a NOP. With
 * MPX enabled, LOCK on the MPX forms is #UD except BNDMOV m, bnd (66 0F 1B, memory
 * destination: SDM BNDMOV "#UD If the LOCK prefix is used but the destination is
 * not a memory operand").
 */
static void test_x86_lock_hint_nops(void)
{
    static const char ops[] = {0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f};
    static const unsigned char modrms[] = {0xc0, 0xc7, 0x06, 0x3e, 0xf8, 0x36};
    char buf[8];
    nk_intr_t intr;
    uc_engine *uc = nk_open("\x90", 1, &intr);
    uint64_t v[2];
    size_t i, j;

    for (i = 0; i < sizeof(ops); i++) {
        for (j = 0; j < sizeof(modrms); j++) {
            memcpy(buf, "\x0f\x00\x00\x90", 4);
            buf[1] = ops[i];
            buf[2] = (char)modrms[j];
            TEST_CHECK(nk_fault(uc, &intr, buf, 4) == -1);
            TEST_MSG("0f %02x %02x", (unsigned char)ops[i], modrms[j]);
            memcpy(buf, "\xf0\x0f\x00\x00", 4);
            buf[2] = ops[i];
            buf[3] = (char)modrms[j];
            TEST_CHECK(nk_fault(uc, &intr, buf, 4) == 6);
            TEST_MSG("f0 0f %02x %02x", (unsigned char)ops[i], modrms[j]);
            memcpy(buf, "\x66\xf0\x0f\x00\x00", 5);
            buf[3] = ops[i];
            buf[4] = (char)modrms[j];
            TEST_CHECK(nk_fault(uc, &intr, buf, 5) == 6);
            TEST_MSG("66 f0 0f %02x %02x", (unsigned char)ops[i], modrms[j]);
        }
    }
    /* ENDBR64 / ENDBR32 / RDSSPQ / CLDEMOTE space: NOP, LOCK #UD */
    TEST_CHECK(nk_fault(uc, &intr, "\xf3\x0f\x1e\xfa", 4) == -1);
    TEST_CHECK(nk_fault(uc, &intr, "\xf0\xf3\x0f\x1e\xfa", 5) == 6);
    TEST_CHECK(nk_fault(uc, &intr, "\xf0\xf3\x0f\x1e\xfb", 5) == 6);
    TEST_CHECK(nk_fault(uc, &intr, "\xf3\x48\x0f\x1e\xc8", 5) == -1);
    TEST_CHECK(nk_fault(uc, &intr, "\xf0\xf3\x48\x0f\x1e\xc8", 6) == 6);
    TEST_CHECK(nk_fault(uc, &intr, "\x0f\x1c\x06", 3) == -1);
    TEST_CHECK(nk_fault(uc, &intr, "\xf0\x0f\x1c\x06", 4) == 6);

    /* MPX on: CR4.OSXSAVE, XCR0 = x87|SSE|BNDREGS|BNDCSR, IA32_BNDCFGS.EN (CPL0) */
    nk_setreg(uc, UC_X86_REG_CR4, nk_reg(uc, UC_X86_REG_CR4) | (1ULL << 18));
    nk_setreg(uc, UC_X86_REG_XCR0, 0x1b);
    nk_wrmsr(uc, 0xd90, 1);
    /* BNDMK bnd0, [rsi+0x10]: LB = RSI, UB = NOT(RSI + 0x10) */
    TEST_CHECK(nk_fault(uc, &intr, "\xf3\x0f\x1b\x46\x10", 5) == -1);
    /* LOCK BNDMOV [rsi], bnd0: runs (memory destination) */
    memset(v, 0, sizeof(v));
    OK(uc_mem_write(uc, NK_HANDLE, v, sizeof(v)));
    TEST_CHECK(nk_fault(uc, &intr, "\xf0\x66\x0f\x1b\x06", 5) == -1);
    OK(uc_mem_read(uc, NK_HANDLE, v, sizeof(v)));
    TEST_CHECK(v[0] == NK_HANDLE && v[1] == ~(NK_HANDLE + 0x10));
    TEST_MSG("bnd0 stored as %016" PRIx64 " %016" PRIx64, v[0], v[1]);
    /* LOCK: BNDMOV bnd1, [rsi] / BNDMOV bnd1, bnd0 (both forms) / BNDMK / BNDCL / BNDCU / BNDCN */
    TEST_CHECK(nk_fault(uc, &intr, "\xf0\x66\x0f\x1a\x0e", 5) == 6);
    TEST_CHECK(nk_fault(uc, &intr, "\xf0\x66\x0f\x1a\xc8", 5) == 6);
    TEST_CHECK(nk_fault(uc, &intr, "\xf0\x66\x0f\x1b\xc1", 5) == 6);
    TEST_CHECK(nk_fault(uc, &intr, "\xf0\xf3\x0f\x1b\x06", 5) == 6);
    TEST_CHECK(nk_fault(uc, &intr, "\xf0\xf3\x0f\x1a\x06", 5) == 6);
    TEST_CHECK(nk_fault(uc, &intr, "\xf0\xf2\x0f\x1a\x06", 5) == 6);
    TEST_CHECK(nk_fault(uc, &intr, "\xf0\xf2\x0f\x1b\x06", 5) == 6);
    /* without LOCK the same BNDMOV load works: bnd1 = [rsi], then [rdi] = bnd1 */
    nk_setreg(uc, UC_X86_REG_RDI, NK_HANDLE + 0x20);
    TEST_CHECK(nk_fault(uc, &intr, "\x66\x0f\x1a\x0e\x66\x0f\x1b\x0f", 8) == -1);
    OK(uc_mem_read(uc, NK_HANDLE + 0x20, v, sizeof(v)));
    TEST_CHECK(v[0] == NK_HANDLE && v[1] == ~(NK_HANDLE + 0x10));
    OK(uc_close(uc));
}

/*
 * U92: with MPX enabled, the 0F 1A / 0F 1B MPX forms decode their ModRM address
 * once: SIB and disp8/disp32 bytes are not consumed a second time. Every form with
 * a SIB and/or displacement, followed by MOV EAX, imm32 (the marker decodes only
 * if each length is right). BNDCFGS = 1: bound directory at 0 (BNDCFG[63:12]),
 * BDE for base 0x100 at address 0, bound table at NK_DATA + 0xC000.
 */
static void test_x86_mpx_modrm_length(void)
{
    const char code[] =
        "\xf3\x0f\x1b\x44\x1e\x10"                 /* bndmk  bnd0, [rsi+rbx+0x10] */
        "\xf3\x0f\x1a\x86\x00\x01\x00\x00"         /* bndcl  bnd0, [rsi+0x100] */
        "\xf2\x0f\x1a\x46\x08"                     /* bndcu  bnd0, [rsi+8] */
        "\xf2\x0f\x1b\x44\x1e\x08"                 /* bndcn  bnd0, [rsi+rbx+8] */
        "\x66\x0f\x1b\x46\x40"                     /* bndmov [rsi+0x40], bnd0 */
        "\x66\x0f\x1a\x4c\x1e\x40"                 /* bndmov bnd1, [rsi+rbx+0x40] */
        "\x0f\x1b\x8c\x0b\x00\x01\x00\x00"         /* bndstx [rbx+rcx+0x100], bnd1 */
        "\x0f\x1a\x94\x0b\x00\x01\x00\x00"         /* bndldx bnd2, [rbx+rcx+0x100] */
        "\x66\x0f\x1b\x96\x00\x02\x00\x00"         /* bndmov [rsi+0x200], bnd2 */
        "\xb8\x78\x56\x34\x12";                    /* mov eax, 0x12345678 */
    const uint64_t bt = NK_DATA + 0xC000, bde = bt | 1;
    nk_intr_t intr;
    uc_engine *uc = nk_open("\x90", 1, &intr);
    uint64_t v[3];

    OK(uc_mem_map(uc, 0, 0x1000, UC_PROT_ALL));
    OK(uc_mem_write(uc, 0, &bde, 8));
    nk_setreg(uc, UC_X86_REG_CR4, nk_reg(uc, UC_X86_REG_CR4) | (1ULL << 18));
    nk_setreg(uc, UC_X86_REG_XCR0, 0x1b);
    nk_wrmsr(uc, 0xd90, 1);
    nk_setreg(uc, UC_X86_REG_RBX, 0);
    nk_setreg(uc, UC_X86_REG_RCX, 0x777);
    TEST_CHECK(nk_fault(uc, &intr, code, sizeof(code) - 1) == -1);
    TEST_CHECK(nk_reg(uc, UC_X86_REG_RAX) == 0x12345678);
    TEST_MSG("rax = %016" PRIx64, nk_reg(uc, UC_X86_REG_RAX));
    OK(uc_mem_read(uc, NK_HANDLE + 0x40, v, 16));
    TEST_CHECK(v[0] == NK_HANDLE && v[1] == ~(NK_HANDLE + 0x10));
    OK(uc_mem_read(uc, NK_HANDLE + 0x200, v, 16));
    TEST_CHECK(v[0] == NK_HANDLE && v[1] == ~(NK_HANDLE + 0x10));
    /* BTE = BT + (base[19:3] << 5) = BT + 0x400: LB, UB, pointer (RCX) */
    OK(uc_mem_read(uc, bt + 0x400, v, 24));
    TEST_CHECK(v[0] == NK_HANDLE && v[1] == ~(NK_HANDLE + 0x10) && v[2] == 0x777);
    OK(uc_close(uc));
}

/*
 * U93: outside 64-bit mode VEX.B is ignored (SDM Vol2A 2.3.5.4 "In 32-bit modes, this
 * bit is ignored"; Figure 2-9 "Ignored in 32-bit mode"), VEX.vvvv[3] (3-byte VEX byte 2
 * bit 6) is ignored where vvvv names a register (2.3.5.6; Table 2-41 "P[14] ignored";
 * XED XMM_N_32 / VGPR32_N_32) but an unused vvvv must still be 1111b (Table 2-41), and
 * /is4 uses imm8[6:4] (VBLENDVPS "In 32-bit mode, imm8[7] is ignored"). VEX.R / VEX.X
 * (and VEX.R / vvvv[3] of C5) must be 1 there, else the bytes are LES / LDS. Each
 * instruction runs once with the bit set and once clear (which would select r8-r15 /
 * xmm8-xmm15 in 64-bit mode); both must give the same result.
 */
static uc_engine *vb_open(uc_mode mode, nk_intr_t *intr)
{
    uc_engine *uc;
    uc_hook h;

    OK(uc_open(UC_ARCH_X86, mode, &uc));
    OK(uc_ctl_set_cpu_model(uc, UC_CPU_X86_MAX));
    OK(uc_mem_map(uc, code_start, code_len, UC_PROT_ALL));
    OK(uc_mem_map(uc, NK_DATA, NK_DATA_SIZE, UC_PROT_ALL));
    memset(intr, 0, sizeof(*intr));
    OK(uc_hook_add(uc, &h, UC_HOOK_INTR, nk_hook_intr, intr, 1, 0));
    nk_setreg(uc, UC_X86_REG_CR4, nk_reg(uc, UC_X86_REG_CR4) | (1ULL << 18));
    nk_setreg(uc, UC_X86_REG_XCR0, 7);
    return uc;
}

static void vb_xmm(uc_engine *uc, int i, uint64_t v[2])
{
    OK(uc_reg_read(uc, UC_X86_REG_XMM0 + i, v));
}

/* state before every run (32-bit mode): XMM1/2/10, EAX/EBX/ECX/ESI, memory */
static void vb_reset32(uc_engine *uc)
{
    static const uint32_t mem[8] = {0x01010101, 0x02020202, 0x03030303, 0x04040404,
                                    0x50505050, 0x60606060, 0x70707070, 0x80808080};
    uint64_t z[2] = {0, 0}, x1[2] = {0x0000000200000001ULL, 0x0000000400000003ULL},
             x2[2] = {0x000000140000000aULL, 0x000000280000001eULL},
             x3[2] = {0x0000000080000000ULL, 0x0000000080000000ULL};
    int i;

    for (i = 0; i < 8; i++) {
        OK(uc_reg_write(uc, UC_X86_REG_XMM0 + i, z));
    }
    OK(uc_reg_write(uc, UC_X86_REG_XMM1, x1));
    OK(uc_reg_write(uc, UC_X86_REG_XMM2, x2));
    OK(uc_reg_write(uc, UC_X86_REG_XMM3, x3));
    OK(uc_mem_write(uc, NK_DATA, mem, sizeof(mem)));
    nk_setreg(uc, UC_X86_REG_EAX, 0x11223344);
    nk_setreg(uc, UC_X86_REG_EBX, 0x0000ff0f);
    nk_setreg(uc, UC_X86_REG_ECX, 0x12345678);
    nk_setreg(uc, UC_X86_REG_EDX, 3);
    nk_setreg(uc, UC_X86_REG_ESI, NK_DATA);
    nk_setreg(uc, UC_X86_REG_EDI, 0x10);
}

static void test_x86_vex_b_32(void)
{
    static const struct {
        const char *name;
        const char *set, *clr;  /* the same instruction: bit set / bit clear */
        size_t len, clen;       /* lengths of set / clr */
        int xmm;                /* result register: XMM index, -1 = EAX, -2 = EBX */
        uint64_t lo, hi;        /* expected result */
    } t[] = {
        /* vpaddd xmm0, xmm1, xmm2 (VEX.B: rm = xmm2, not xmm10) */
        {"vpaddd rm", "\xc4\xe1\x71\xfe\xc2", "\xc4\xc1\x71\xfe\xc2", 5, 5, 0,
         0x000000160000000bULL, 0x0000002c00000021ULL},
        /* vpaddd xmm0, xmm1, xmm2 with VEX.vvvv[3] = 1 (byte 2 bit 6 = 0): vvvv = xmm1 */
        {"vpaddd vvvv3", "\xc4\xe1\x71\xfe\xc2", "\xc4\xe1\x31\xfe\xc2", 5, 5, 0,
         0x000000160000000bULL, 0x0000002c00000021ULL},
        /* vmovdqu xmm3, [esi] (VEX.B: base esi, not r14) */
        {"vmovdqu [esi]", "\xc4\xe1\x7a\x6f\x1e", "\xc4\xc1\x7a\x6f\x1e", 5, 5, 3,
         0x0202020201010101ULL, 0x0404040403030303ULL},
        /* vmovdqu xmm4, [esi+edi*1] (SIB base esi) */
        {"vmovdqu sib", "\xc4\xe1\x7a\x6f\x24\x3e", "\xc4\xc1\x7a\x6f\x24\x3e", 6, 6, 4,
         0x6060606050505050ULL, 0x8080808070707070ULL},
        /* vmovd xmm5, eax (VEX.B: eax, not r8d) */
        {"vmovd eax", "\xc4\xe1\x79\x6e\xe8", "\xc4\xc1\x79\x6e\xe8", 5, 5, 5,
         0x11223344ULL, 0},
        /* andn eax, ebx, ecx: ~ebx & ecx (VEX.B: ecx, not r9d) */
        {"andn rm", "\xc4\xe2\x60\xf2\xc1", "\xc4\xc2\x60\xf2\xc1", 5, 5, -1,
         0x12340070ULL, 0},
        /* andn eax, ebx, ecx with vvvv[3] = 1: vvvv = ebx, not r11d */
        {"andn vvvv3", "\xc4\xe2\x60\xf2\xc1", "\xc4\xe2\x20\xf2\xc1", 5, 5, -1,
         0x12340070ULL, 0},
        /* andn eax, ebx, [esi] */
        {"andn [esi]", "\xc4\xe2\x60\xf2\x06", "\xc4\xc2\x60\xf2\x06", 5, 5, -1,
         0x01010000ULL, 0},
        /* mulx ecx, ebx, eax: EBX = low half of EDX * EAX (vvvv[3] = 1: ebx, not r11d) */
        {"mulx vvvv3", "\xc4\xe2\x63\xf6\xc8", "\xc4\xe2\x23\xf6\xc8", 5, 5, -2,
         0x336699ccULL, 0},
        /* vblendvps xmm0, xmm1, xmm2, xmm3: /is4 imm8[7] ignored (0xb0 = xmm3, not xmm11) */
        {"vblendvps is4", "\xc4\xe3\x71\x4a\xc2\x30", "\xc4\xe3\x71\x4a\xc2\xb0", 6, 6, 0,
         0x000000020000000aULL, 0x000000040000001eULL},
        /* 2-byte form vpaddd xmm0, xmm1, xmm2 vs the 3-byte one (no B/X bits in C5) */
        {"c5 vpaddd", "\xc5\xf1\xfe\xc2", "\xc4\xc1\x71\xfe\xc2", 4, 5, 0,
         0x000000160000000bULL, 0x0000002c00000021ULL},
    };
    nk_intr_t intr;
    uc_engine *uc = vb_open(UC_MODE_32, &intr);
    uint64_t v[2];
    size_t i;
    int k;

    for (i = 0; i < sizeof(t) / sizeof(t[0]); i++) {
        for (k = 0; k < 2; k++) {
            const char *c = k ? t[i].clr : t[i].set;
            size_t len = k ? t[i].clen : t[i].len;

            int f;

            vb_reset32(uc);
            f = nk_fault(uc, &intr, c, len);
            TEST_CHECK(f == -1);
            TEST_MSG("%s (%s): fault %d", t[i].name, k ? "bit clear" : "bit set", f);
            if (t[i].xmm >= 0) {
                vb_xmm(uc, t[i].xmm, v);
            } else {
                v[0] = nk_reg(uc, t[i].xmm == -1 ? UC_X86_REG_EAX : UC_X86_REG_EBX);
                v[1] = 0;
            }
            TEST_CHECK(v[0] == t[i].lo && v[1] == t[i].hi);
            TEST_MSG("%s (%s): %016" PRIx64 "%016" PRIx64 ", expected %016" PRIx64
                     "%016" PRIx64, t[i].name, k ? "bit clear" : "bit set", v[1], v[0],
                     t[i].hi, t[i].lo);
        }
    }
    /*
     * vvvv not used: it must be 1111b in every mode (Table 2-41 "Otherwise: If !=
     * 1111b", XED NOVSR = VEXDEST3=1 VEXDEST210=111): vmovdqu xmm3, [esi] with
     * vvvv = 0111b is #UD in 32-bit mode too
     */
    vb_reset32(uc);
    TEST_CHECK(nk_fault(uc, &intr, "\xc4\xe1\x3a\x6f\x1e", 5) == 6);
    /* C5 with byte 1 bit 7 (R) or bit 6 (vvvv[3]) clear is LDS: c5 31 = lds esi, [ecx] */
    vb_reset32(uc);
    nk_setreg(uc, UC_X86_REG_ECX, NK_DATA + 0x100);
    OK(uc_mem_write(uc, NK_DATA + 0x100, "\x78\x56\x34\x12\x7b\x00", 6));
    TEST_CHECK(nk_fault(uc, &intr, "\xc5\x31", 2) != 6);
    TEST_CHECK(nk_reg(uc, UC_X86_REG_ESI) == 0x12345678 || intr.count);
    OK(uc_close(uc));

    /* 64-bit mode: the same bits select xmm10 / r14 / r9 */
    uc = vb_open(UC_MODE_64, &intr);
    {
        uint64_t x1[2] = {1, 0}, x10[2] = {0x100, 0}, x2[2] = {0x20, 0};

        OK(uc_reg_write(uc, UC_X86_REG_XMM1, x1));
        OK(uc_reg_write(uc, UC_X86_REG_XMM2, x2));
        OK(uc_reg_write(uc, UC_X86_REG_XMM10, x10));
        TEST_CHECK(nk_fault(uc, &intr, "\xc4\xc1\x71\xfe\xc2", 5) == -1);
        vb_xmm(uc, 0, v);
        TEST_CHECK(v[0] == 0x101 && v[1] == 0);
        /* vblendvps xmm0, xmm1, xmm2, xmm11 (imm8 0xb0): dword 0 from xmm2 */
        x10[0] = 0x80000000ULL;
        OK(uc_reg_write(uc, UC_X86_REG_XMM11, x10));
        TEST_CHECK(nk_fault(uc, &intr, "\xc4\xe3\x71\x4a\xc2\xb0", 6) == -1);
        vb_xmm(uc, 0, v);
        TEST_CHECK(v[0] == 0x20 && v[1] == 0);
        nk_setreg(uc, UC_X86_REG_RBX, 0xff0f);
        nk_setreg(uc, UC_X86_REG_R9, 0x5555);
        TEST_CHECK(nk_fault(uc, &intr, "\xc4\xc2\x60\xf2\xc1", 5) == -1);
        TEST_CHECK(nk_reg(uc, UC_X86_REG_RAX) == 0x50);
    }
    OK(uc_close(uc));
}

/*
 * U94: uc_mem_write over code that already ran drops its translation. Before, only
 * TBs covering the end address - 1 were dropped (exit cleanup), so a TB that stopped
 * earlier (#UD before the end, instruction count limit) ran again from the cache.
 * Each case runs at code_start, rewrites the same bytes in place and runs again.
 */
static uc_err mw_run(uc_engine *uc, const char *code, size_t len, size_t count)
{
    OK(uc_mem_write(uc, code_start, code, len));
    return uc_emu_start(uc, code_start, code_start + len, 0, count);
}

static void test_x86_mem_write_invalidates_tb(void)
{
    nk_intr_t intr;
    uc_engine *uc = nk_open("\x90", 1, &intr);

    /* #UD in the first instruction (decoder #UD, TB shorter than the snippet) */
    uc_assert_err(UC_ERR_INSN_INVALID, mw_run(uc, "\x0f\x38\x8b\xc0\x90", 5, 0));
    OK(mw_run(uc, "\xb8\x11\x00\x00\x00", 5, 0)); /* mov eax, 0x11 */
    TEST_CHECK(nk_reg(uc, UC_X86_REG_RAX) == 0x11);
    TEST_CHECK(nk_reg(uc, UC_X86_REG_RIP) == code_start + 5);
    /* #UD in the middle of a TB (UD2 at +2) */
    uc_assert_err(UC_ERR_INSN_INVALID, mw_run(uc, "\x90\x90\x0f\x0b\x90", 5, 0));
    OK(mw_run(uc, "\x90\x90\x90\x90\x90", 5, 0));
    TEST_CHECK(nk_reg(uc, UC_X86_REG_RIP) == code_start + 5);
    /* F3 MOVRS #UD, then the same length with a valid instruction */
    uc_assert_err(UC_ERR_INSN_INVALID, mw_run(uc, "\xf3\x0f\x38\x8b\x06", 5, 0));
    OK(mw_run(uc, "\xb8\x22\x00\x00\x00", 5, 0)); /* mov eax, 0x22 */
    TEST_CHECK(nk_reg(uc, UC_X86_REG_RAX) == 0x22);
    /* instruction count limit: the first run stops after one instruction */
    OK(mw_run(uc, "\xb8\x33\x00\x00\x00\xb8\x44\x00\x00\x00", 10, 1));
    TEST_CHECK(nk_reg(uc, UC_X86_REG_RAX) == 0x33);
    OK(mw_run(uc, "\xb8\x55\x00\x00\x00\xb8\x66\x00\x00\x00", 10, 1));
    TEST_CHECK(nk_reg(uc, UC_X86_REG_RAX) == 0x55);
    TEST_MSG("rax = %" PRIx64, nk_reg(uc, UC_X86_REG_RAX));
    OK(uc_close(uc));
}

/*
 * ---- NoVmp U170-U180: Intel AMX (VEX) ----
 * Expected values: Emulator/tools/isa/ref_amx.py (independent SDM model) for the TMUL
 * results and the exception matrix (x86_amx_vectors.inc); the load/store/XSAVE/XFD tests
 * below compute their expectations directly from the SDM text they cite.
 */
#include "x86_amx_vectors.inc"

#define AX_CODE 0x100000
#define AX_CODE_SIZE 0x40000
#define AX_DATA 0x200000
#define AX_DATA_SIZE 0x20000
#define AX_XCR0 0x60007ULL     /* x87, SSE, AVX, TILECFG, TILEDATA */

typedef struct AmxT {
    uc_engine *uc;
    uc_mode mode;
    X86IntrCapture cap;
    uc_hook hook;
    uint64_t pc;
} AmxT;

static void ax_open(AmxT *a, uc_mode mode, int mask, const uc_x86_cpuid *prof, size_t nprof)
{
    memset(a, 0, sizeof(*a));
    a->mode = mode;
    a->pc = AX_CODE;
    OK(uc_open(UC_ARCH_X86, mode, &a->uc));
    OK(uc_ctl_set_cpu_model(a->uc, UC_CPU_X86_MAX));
    if (mask) {
        OK(uc_ctl_set_x86_amx(a->uc, mask));
    }
    if (nprof) {
        OK(uc_ctl_set_x86_cpuid(a->uc, prof, nprof));
        /* U435: a profile is strict by default; this partial profile (leaf 0DH without
           leaf 1) tests XCR0 only, so the model's XSAVE stays usable */
        OK(uc_ctl_set_x86_cpuid_strict(a->uc, 0));
    }
    OK(uc_mem_map(a->uc, AX_CODE, AX_CODE_SIZE, UC_PROT_ALL));
    OK(uc_mem_map(a->uc, AX_DATA, AX_DATA_SIZE, UC_PROT_ALL));
    OK(uc_hook_add(a->uc, &a->hook, UC_HOOK_INTR, test_x86_intr_capture_cb, &a->cap, 1, 0));
}

static void ax_close(AmxT *a)
{
    OK(uc_close(a->uc));
}

/* outside 64-bit mode RAX..RDI name EAX..EDI */
static int ax_reg32(int reg)
{
    static const int r64[] = {UC_X86_REG_RAX, UC_X86_REG_RBX, UC_X86_REG_RCX, UC_X86_REG_RDX,
                              UC_X86_REG_RSI, UC_X86_REG_RDI};
    static const int r32[] = {UC_X86_REG_EAX, UC_X86_REG_EBX, UC_X86_REG_ECX, UC_X86_REG_EDX,
                              UC_X86_REG_ESI, UC_X86_REG_EDI};
    size_t i;
    for (i = 0; i < sizeof(r64) / sizeof(r64[0]); i++) {
        if (reg == r64[i]) {
            return r32[i];
        }
    }
    return reg;
}

static void ax_set(AmxT *a, int reg, uint64_t v)
{
    if (a->mode == UC_MODE_64) {
        OK(uc_reg_write(a->uc, reg, &v));
    } else {
        uint32_t v32 = (uint32_t)v;
        OK(uc_reg_write(a->uc, ax_reg32(reg), &v32));
    }
}

static uint64_t ax_get(AmxT *a, int reg)
{
    uint64_t v = 0;
    if (a->mode == UC_MODE_64) {
        OK(uc_reg_read(a->uc, reg, &v));
    } else {
        uint32_t v32 = 0;
        OK(uc_reg_read(a->uc, ax_reg32(reg), &v32));
        v = v32;
    }
    return v;
}

/* run code from a fresh address; the exception vector (6 #UD, 7 #NM, 13 #GP, ...) or -1 */
static int ax_run(AmxT *a, const uint8_t *code, size_t len)
{
    uint64_t pc = a->pc;
    uc_err err;

    a->pc += 0x80;
    TEST_CHECK(len <= 0x80 && a->pc <= AX_CODE + AX_CODE_SIZE);
    a->cap.count = 0;
    OK(uc_mem_write(a->uc, pc, code, len));
    err = uc_emu_start(a->uc, pc, pc + len, 0, 0);
    if (err == UC_ERR_INSN_INVALID) {
        return 6;
    }
    if (err != UC_ERR_OK) {
        return 1000 + (int)err;
    }
    return a->cap.count ? (int)a->cap.intno : -1;
}

static void ax_cpuid(AmxT *a, uint32_t leaf, uint32_t sub, uint32_t r[4])
{
    static const uint8_t cpuid[] = {0x0f, 0xa2};
    ax_set(a, UC_X86_REG_RAX, leaf);
    ax_set(a, UC_X86_REG_RCX, sub);
    TEST_CHECK(ax_run(a, cpuid, 2) == -1);
    r[0] = (uint32_t)ax_get(a, UC_X86_REG_RAX);
    r[1] = (uint32_t)ax_get(a, UC_X86_REG_RBX);
    r[2] = (uint32_t)ax_get(a, UC_X86_REG_RCX);
    r[3] = (uint32_t)ax_get(a, UC_X86_REG_RDX);
}

static uint64_t ax_xcr0(AmxT *a)
{
    uint64_t v = 0;
    OK(uc_reg_read(a->uc, UC_X86_REG_XCR0, &v));
    return v;
}

static int ax_xsetbv(AmxT *a, uint64_t v)
{
    static const uint8_t xsetbv[] = {0x0f, 0x01, 0xd1};
    ax_set(a, UC_X86_REG_RCX, 0);
    ax_set(a, UC_X86_REG_RAX, (uint32_t)v);
    ax_set(a, UC_X86_REG_RDX, (uint32_t)(v >> 32));
    return ax_run(a, xsetbv, 3);
}

static uint64_t ax_xgetbv1(AmxT *a)
{
    static const uint8_t xgetbv[] = {0x0f, 0x01, 0xd0};
    ax_set(a, UC_X86_REG_RCX, 1);
    TEST_CHECK(ax_run(a, xgetbv, 3) == -1);
    return (ax_get(a, UC_X86_REG_RAX) & 0xffffffffULL) | (ax_get(a, UC_X86_REG_RDX) << 32);
}

static void ax_msr_write(AmxT *a, uint32_t id, uint64_t v)
{
    uc_x86_msr msr;
    msr.rid = id;
    msr.value = v;
    OK(uc_reg_write(a->uc, UC_X86_REG_MSR, &msr));
}

static uint64_t ax_msr_read(AmxT *a, uint32_t id)
{
    uc_x86_msr msr;
    msr.rid = id;
    msr.value = 0;
    OK(uc_reg_read(a->uc, UC_X86_REG_MSR, &msr));
    return msr.value;
}

/* WRMSR / RDMSR executed by the guest (CPL 0): the exception vector or -1 */
static int ax_wrmsr(AmxT *a, uint32_t id, uint64_t v)
{
    static const uint8_t wrmsr[] = {0x0f, 0x30};
    ax_set(a, UC_X86_REG_RCX, id);
    ax_set(a, UC_X86_REG_RAX, (uint32_t)v);
    ax_set(a, UC_X86_REG_RDX, (uint32_t)(v >> 32));
    return ax_run(a, wrmsr, 2);
}

static uint64_t ax_rdmsr(AmxT *a, uint32_t id)
{
    static const uint8_t rdmsr[] = {0x0f, 0x32};
    ax_set(a, UC_X86_REG_RCX, id);
    TEST_CHECK(ax_run(a, rdmsr, 2) == -1);
    return (ax_get(a, UC_X86_REG_RAX) & 0xffffffffULL) | (ax_get(a, UC_X86_REG_RDX) << 32);
}

static void ax_hex(const char *s, uint8_t *out, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) {
        unsigned v = 0;
        int k;
        for (k = 0; k < 2; k++) {
            char c = s[2 * i + k];
            v = v * 16 + (unsigned)(c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10);
        }
        out[i] = (uint8_t)v;
    }
}

static void ax_cfg_set(AmxT *a, const uint8_t cfg[64])
{
    OK(uc_reg_write(a->uc, UC_X86_REG_TILECFG, cfg));
}

static void ax_cfg_get(AmxT *a, uint8_t cfg[64])
{
    OK(uc_reg_read(a->uc, UC_X86_REG_TILECFG, cfg));
}

static void ax_tile_set(AmxT *a, int t, const uint8_t tile[1024])
{
    OK(uc_reg_write(a->uc, UC_X86_REG_TMM0 + t, tile));
}

static void ax_tile_get(AmxT *a, int t, uint8_t tile[1024])
{
    OK(uc_reg_read(a->uc, UC_X86_REG_TMM0 + t, tile));
}

/* 64-byte TILECFG image: palette 1, start_row, {tile, rows, colsb} triples (-1 ends) */
static void ax_cfg_make(uint8_t cfg[64], int start_row, const int *spec)
{
    memset(cfg, 0, 64);
    cfg[0] = 1;
    cfg[1] = (uint8_t)start_row;
    for (; spec[0] >= 0; spec += 3) {
        cfg[48 + spec[0]] = (uint8_t)spec[1];
        cfg[16 + 2 * spec[0]] = (uint8_t)spec[2];
        cfg[17 + 2 * spec[0]] = (uint8_t)(spec[2] >> 8);
    }
}

/* ---- encodings: VEX.128.pp.0F38.W0 (pp 0 NP, 1 66, 2 F3, 3 F2) ---- */
static size_t ax_enc_sib(uint8_t *o, int pp, uint8_t op, int reg, int base, int index,
                         int scale, int32_t disp)
{
    int ix = index < 0 ? 4 : index;
    o[0] = 0xc4;
    o[1] = (uint8_t)(((reg & 8) ? 0 : 0x80) | ((ix & 8) ? 0 : 0x40) | ((base & 8) ? 0 : 0x20) | 2);
    o[2] = (uint8_t)(0x78 | pp);
    o[3] = op;
    o[4] = (uint8_t)(0x80 | ((reg & 7) << 3) | 4);
    o[5] = (uint8_t)((scale << 6) | ((ix & 7) << 3) | (base & 7));
    memcpy(o + 6, &disp, 4);
    return 10;
}

/* [base] (base not rsp/rbp/r12/r13), ModRM.reg = 0 */
static size_t ax_enc_mem(uint8_t *o, int pp, int base)
{
    o[0] = 0xc4;
    o[1] = (uint8_t)(0xc0 | ((base & 8) ? 0 : 0x20) | 2);
    o[2] = (uint8_t)(0x78 | pp);
    o[3] = 0x49;
    o[4] = (uint8_t)(base & 7);
    return 5;
}

#define AX_LDTILECFG(o, base) ax_enc_mem(o, 0, base)
#define AX_STTILECFG(o, base) ax_enc_mem(o, 1, base)
#define AX_TILELOADD(o, t, b, i, s, d) ax_enc_sib(o, 3, 0x4b, t, b, i, s, d)
#define AX_TILELOADDT1(o, t, b, i, s, d) ax_enc_sib(o, 1, 0x4b, t, b, i, s, d)
#define AX_TILESTORED(o, t, b, i, s, d) ax_enc_sib(o, 2, 0x4b, t, b, i, s, d)
static const uint8_t ax_tilerelease[] = {0xc4, 0xe2, 0x78, 0x49, 0xc0};

static size_t ax_enc_tilezero(uint8_t *o, int t)
{
    o[0] = 0xc4;
    o[1] = (uint8_t)(((t & 8) ? 0 : 0x80) | 0x60 | 2);
    o[2] = 0x7b;
    o[3] = 0x49;
    o[4] = (uint8_t)(0xc0 | ((t & 7) << 3));
    return 5;
}

/* ---- the tile contents generator shared with ref_amx.py (splitmix64) ---- */
static uint64_t ax_splitmix(uint64_t *st)
{
    uint64_t z;
    *st += 0x9E3779B97F4A7C15ULL;
    z = *st;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

/* ref_amx.py gen_elem: fmt 0 BF16, 1 FP16, 2 FP32; cls 0..5 NORMAL INT WIDE TINY HUGE SPECIAL */
static uint32_t ax_gen_elem(int fmt, int cls, uint64_t r)
{
    const int EB = fmt == 1 ? 5 : 8, FB = fmt == 0 ? 7 : fmt == 1 ? 10 : 23;
    const int W = 1 + EB + FB, BIAS = (1 << (EB - 1)) - 1, EMAX = (1 << EB) - 1;
    const uint32_t sign = (uint32_t)(r >> 63) & 1, c = (uint32_t)r & 0xff;
    const uint32_t fr = (uint32_t)(r >> 8) & ((1u << FB) - 1);
    const uint32_t ex = (uint32_t)(r >> 40) & 0xffff;
    const uint32_t sb = sign << (W - 1);
    int lo = 0, hi = 0;

#define AX_NORM(l, h) (lo = (l), hi = (h), sb | ((uint32_t)(lo + (int)(ex % (uint32_t)(hi - lo + 1))) << FB) | fr)
#define AX_ZERO() (sb)
#define AX_DEN() (sb | (fr ? fr : 1))
#define AX_INF() (sb | ((uint32_t)EMAX << FB))
    switch (cls) {
    case 0:
        return AX_NORM(BIAS - 6, BIAS + 6);
    case 1: {
        int v = (int)(ex % 9), a = v - 4 < 0 ? 4 - v : v - 4;
        uint32_t s = v < 4 ? 1 : (v == 4 ? sign : 0), mag = 0;
        if (a == 1) {
            mag = (uint32_t)BIAS << FB;
        } else if (a == 2) {
            mag = (uint32_t)(BIAS + 1) << FB;
        } else if (a == 3) {
            mag = ((uint32_t)(BIAS + 1) << FB) | (1u << (FB - 1));
        } else if (a == 4) {
            mag = (uint32_t)(BIAS + 2) << FB;
        }
        return (s << (W - 1)) | mag;
    }
    case 2:
        if (c < 8) return AX_ZERO();
        if (c < 16) return AX_DEN();
        if (c < 18) return AX_INF();
        if (c < 20) break;                       /* NaN below */
        return AX_NORM(1, EMAX - 1);
    case 3:
        if (fmt == 1) return c < 128 ? AX_DEN() : AX_NORM(1, 3);
        if (fmt == 0) return c < 64 ? AX_DEN() : AX_NORM(BIAS - 70, BIAS - 56);
        return c < 128 ? AX_DEN() : AX_NORM(1, 8);
    case 4:
        if (fmt == 0) return AX_NORM(BIAS + 60, BIAS + 66);
        if (fmt == 1) return AX_NORM(EMAX - 3, EMAX - 1);
        return AX_NORM(EMAX - 8, EMAX - 1);
    default:
        if (c < 40) return AX_ZERO();
        if (c < 80) return AX_DEN();
        if (c < 120) return AX_INF();
        if (c < 160) break;                      /* NaN below */
        return AX_NORM(BIAS - 2, BIAS + 2);
    }
    {
        uint32_t f = fr & ((1u << (FB - 1)) - 1);
        if ((r >> 56) & 1) {
            f |= 1u << (FB - 1);
        } else if (f == 0) {
            f = 1;
        }
        return sb | ((uint32_t)EMAX << FB) | f;
    }
#undef AX_NORM
#undef AX_ZERO
#undef AX_DEN
#undef AX_INF
}

static void ax_fill_tile(uint64_t seed, int t, int kind, uint8_t out[1024])
{
    uint64_t st = seed + (uint64_t)(t + 1) * 0xD1B54A32D192ED03ULL;
    int i, b;

    memset(out, 0, 1024);
    if (kind == AMX_K_ZERO) {
        return;
    }
    if (kind == AMX_K_BYTES) {
        for (i = 0; i < 128; i++) {
            uint64_t r = ax_splitmix(&st);
            memcpy(out + 8 * i, &r, 8);
        }
        return;
    }
    {
        int fmt = (kind - 2) / 6, cls = (kind - 2) % 6, w = fmt == 2 ? 4 : 2;
        for (i = 0; i < 1024 / w; i++) {
            uint32_t v = ax_gen_elem(fmt, cls, ax_splitmix(&st));
            for (b = 0; b < w; b++) {
                out[w * i + b] = (uint8_t)(v >> (8 * b));
            }
        }
    }
}

static uint64_t ax_fnv(const uint8_t *p, size_t n, uint64_t h)
{
    size_t i;
    for (i = 0; i < n; i++) {
        h = (h ^ p[i]) * 0x100000001B3ULL;
    }
    return h;
}

/* NoVmp U170: UC_CTL_X86_AMX opt-in, CPUID 7/1DH/1EH/0DH, reset XCR0, profile */
static void test_x86_amx_optin(void)
{
    /* i5-13600K leaf 0DH (no AMX components) */
    static const uc_x86_cpuid prof207[] = {
        {0x0, 0, 0x20, 0x756e6547, 0x6c65746e, 0x49656e69},
        {0xd, 0, 0x207, 0x340, 0xa88, 0},
    };
    static const uint8_t tilezero0[] = {0xc4, 0xe2, 0x7b, 0x49, 0xc0};
    AmxT a;
    uint32_t r[4];
    int mask = -1;

    /* default: off - no CPUID bits, leaves 1DH/1EH zero, XCR0[18:17] = 0, TILEZERO #UD */
    ax_open(&a, UC_MODE_64, 0, NULL, 0);
    OK(uc_ctl_get_x86_amx(a.uc, &mask));
    TEST_CHECK(mask == 0);
    ax_cpuid(&a, 7, 0, r);
    TEST_CHECK((r[3] & ((1u << 22) | (1u << 24) | (1u << 25))) == 0);
    ax_cpuid(&a, 7, 1, r);
    TEST_CHECK((r[0] & (1u << 21)) == 0 && (r[3] & (1u << 8)) == 0);
    ax_cpuid(&a, 0xd, 0, r);
    TEST_CHECK((r[0] & 0x60000) == 0);
    ax_cpuid(&a, 0xd, 1, r);
    TEST_CHECK((r[0] & 0x10) == 0);                  /* no XFD */
    TEST_CHECK((ax_xcr0(&a) & 0x60000) == 0);
    TEST_CHECK(ax_run(&a, tilezero0, sizeof(tilezero0)) == 6);
    uc_assert_err(UC_ERR_ARG, uc_ctl_set_x86_amx(a.uc, UC_X86_AMX_ALL));
    ax_close(&a);

    /* unknown bits refused; a non-zero mask implies TILE */
    OK(uc_open(UC_ARCH_X86, UC_MODE_64, &a.uc));
    uc_assert_err(UC_ERR_ARG, uc_ctl_set_x86_amx(a.uc, 32));
    uc_assert_err(UC_ERR_ARG, uc_ctl_set_x86_amx(a.uc, -1));
    OK(uc_ctl_set_x86_amx(a.uc, UC_X86_AMX_BF16));
    OK(uc_ctl_get_x86_amx(a.uc, &mask));
    TEST_CHECK(mask == (UC_X86_AMX_BF16 | UC_X86_AMX_TILE));
    OK(uc_close(a.uc));

    /* everything on */
    ax_open(&a, UC_MODE_64, UC_X86_AMX_ALL, NULL, 0);
    ax_cpuid(&a, 0, 0, r);
    TEST_CHECK(r[0] >= 0x1e);
    ax_cpuid(&a, 7, 0, r);
    TEST_CHECK((r[3] & ((1u << 22) | (1u << 24) | (1u << 25))) ==
               ((1u << 22) | (1u << 24) | (1u << 25)));
    TEST_CHECK(r[0] >= 1);
    ax_cpuid(&a, 7, 1, r);
    TEST_CHECK((r[0] & (1u << 21)) && (r[3] & (1u << 8)));
    ax_cpuid(&a, 0x1d, 0, r);
    TEST_CHECK(r[0] == 1 && r[1] == 0 && r[2] == 0 && r[3] == 0);
    ax_cpuid(&a, 0x1d, 1, r);
    TEST_CHECK(r[0] == 0x04002000 && r[1] == 0x00080040 && r[2] == 0x10 && r[3] == 0);
    ax_cpuid(&a, 0x1d, 2, r);
    TEST_CHECK(r[0] == 0 && r[1] == 0 && r[2] == 0 && r[3] == 0);
    ax_cpuid(&a, 0x1e, 0, r);
    TEST_CHECK(r[0] == 1 && r[1] == 0x4010 && r[2] == 0 && r[3] == 0);
    ax_cpuid(&a, 0x1e, 1, r);
    TEST_CHECK(r[0] == 0xf && r[1] == 0 && r[2] == 0 && r[3] == 0);
    ax_cpuid(&a, 0xd, 0, r);
    TEST_CHECK((r[0] & 0x60000) == 0x60000);
    TEST_CHECK(r[2] >= 0x2b00);                      /* max size, standard format */
    ax_cpuid(&a, 0xd, 1, r);
    TEST_CHECK(r[0] & 0x10);                         /* XFD */
    ax_cpuid(&a, 0xd, 17, r);
    TEST_CHECK(r[0] == 0x40 && r[1] == 0xac0 && r[2] == 2 && r[3] == 0);
    ax_cpuid(&a, 0xd, 18, r);
    TEST_CHECK(r[0] == 0x2000 && r[1] == 0xb00 && r[2] == 6 && r[3] == 0);
    /* reset XCR0 enables 18:17 (like every supported component) */
    TEST_CHECK((ax_xcr0(&a) & 0x60000) == 0x60000);
    /* 0DH.0:EBX / 0DH.1:EBX follow XCR0 (standard / compacted, 64-byte aligned AMX) */
    TEST_CHECK(ax_xsetbv(&a, AX_XCR0) == -1);
    ax_cpuid(&a, 0xd, 0, r);
    TEST_CHECK(r[1] == 0x2b00);
    ax_cpuid(&a, 0xd, 1, r);
    TEST_CHECK(r[1] == 0x2380);                      /* 240h + AVX 100h, TILECFG 340h, TILEDATA 380h */
    TEST_MSG("0DH.1:EBX = %x", r[1]);
    ax_cpuid(&a, 0xd, 0, r);
    if (r[0] & 0x200) {                               /* PKRU (8 bytes) then aligned TILECFG */
        TEST_CHECK(ax_xsetbv(&a, AX_XCR0 | 0x200) == -1);
        ax_cpuid(&a, 0xd, 1, r);
        TEST_CHECK(r[1] == 0x23c0);                  /* PKRU 340h, TILECFG 380h, TILEDATA 3C0h */
        TEST_MSG("0DH.1:EBX = %x", r[1]);
    }
    TEST_CHECK(ax_run(&a, tilezero0, sizeof(tilezero0)) == 6);   /* not configured */
    ax_close(&a);

    /* TILE only: no TMUL bits, 1EH.1:EAX = 0 */
    ax_open(&a, UC_MODE_64, UC_X86_AMX_TILE, NULL, 0);
    ax_cpuid(&a, 7, 0, r);
    TEST_CHECK((r[3] & ((1u << 22) | (1u << 24) | (1u << 25))) == (1u << 24));
    ax_cpuid(&a, 7, 1, r);
    TEST_CHECK((r[0] & (1u << 21)) == 0 && (r[3] & (1u << 8)) == 0);
    ax_cpuid(&a, 0x1e, 1, r);
    TEST_CHECK(r[0] == 0);
    ax_close(&a);

    /* INT8 + COMPLEX: aliases bits 0 and 2 */
    ax_open(&a, UC_MODE_64, UC_X86_AMX_INT8 | UC_X86_AMX_COMPLEX, NULL, 0);
    ax_cpuid(&a, 0x1e, 1, r);
    TEST_CHECK(r[0] == 5);
    ax_close(&a);

    /* the i5-13600K leaf 0DH has no AMX components: XCR0 without them, AMX unusable */
    ax_open(&a, UC_MODE_64, UC_X86_AMX_ALL, prof207, 2);
    TEST_CHECK((ax_xcr0(&a) & 0x60000) == 0);
    TEST_CHECK(ax_xsetbv(&a, 0x60207) == 13);
    {
        static const uint8_t rel[] = {0xc4, 0xe2, 0x78, 0x49, 0xc0};
        TEST_CHECK(ax_run(&a, rel, sizeof(rel)) == 6);       /* XCR0[18:17] != 11b */
    }
    ax_close(&a);
}

/* NoVmp U171: XSETBV XCR0[18:17] = 00b or 11b */
static void test_x86_amx_xsetbv(void)
{
    static const struct {
        uint64_t xcr0;
        int ok;
    } t[] = {
        {0x60007, 1}, {0x00007, 1}, {0x20007, 0}, {0x40007, 0}, {0x60003, 1},
        {0x60001, 1}, {0x60005, 0}, {0xe0007, 0}, {0x60007, 1},
    };
    AmxT a;
    size_t i;

    ax_open(&a, UC_MODE_64, UC_X86_AMX_ALL, NULL, 0);
    for (i = 0; i < sizeof(t) / sizeof(t[0]); i++) {
        uint64_t before = ax_xcr0(&a);
        int v = ax_xsetbv(&a, t[i].xcr0);
        TEST_CHECK_(v == (t[i].ok ? -1 : 13), "xsetbv %llx -> %d",
                    (unsigned long long)t[i].xcr0, v);
        TEST_CHECK(ax_xcr0(&a) == (t[i].ok ? t[i].xcr0 : before));
    }
    ax_close(&a);
    ax_open(&a, UC_MODE_32, UC_X86_AMX_ALL, NULL, 0);    /* XSETBV of 18:17 in any mode */
    TEST_CHECK(ax_xsetbv(&a, 0x60007) == -1);
    TEST_CHECK(ax_xsetbv(&a, 0x40007) == 13);
    ax_close(&a);
    ax_open(&a, UC_MODE_64, 0, NULL, 0);                 /* unsupported without the opt-in */
    TEST_CHECK(ax_xsetbv(&a, 0x60007) == 13);
    TEST_CHECK(ax_xsetbv(&a, 0x00007) == -1);
    ax_close(&a);
}

/* NoVmp U174: UC_X86_REG_TILECFG / UC_X86_REG_TMM0..7 */
static void test_x86_amx_api(void)
{
    static const int spec[] = {0, 16, 64, 3, 2, 8, 7, 1, 4, -1};
    uint8_t cfg[64], out[64], tile[1024], back[1024];
    AmxT a;
    int t, i;

    ax_open(&a, UC_MODE_64, UC_X86_AMX_ALL, NULL, 0);
    ax_cfg_get(&a, out);
    TEST_CHECK(m0_all_bytes(out, 0, 64, 0));           /* INIT after reset */
    ax_cfg_make(cfg, 5, spec);
    ax_cfg_set(&a, cfg);
    ax_cfg_get(&a, out);
    TEST_CHECK(memcmp(out, cfg, 64) == 0);
    for (t = 0; t < 8; t++) {
        for (i = 0; i < 1024; i++) {
            tile[i] = (uint8_t)(i * 7 + t * 31 + 1);
        }
        ax_tile_set(&a, t, tile);
    }
    for (t = 0; t < 8; t++) {
        ax_tile_get(&a, t, back);
        for (i = 0; i < 1024; i++) {
            tile[i] = (uint8_t)(i * 7 + t * 31 + 1);
        }
        TEST_CHECK_(memcmp(tile, back, 1024) == 0, "tmm%d round trip", t);
    }
    /* an image LDTILECFG would refuse initialises TILECFG (like XRSTOR); tiles untouched */
    cfg[48] = 17;
    ax_cfg_set(&a, cfg);
    ax_cfg_get(&a, out);
    TEST_CHECK(m0_all_bytes(out, 0, 64, 0));
    ax_tile_get(&a, 0, back);
    TEST_CHECK(back[0] == 1 && back[1023] == (uint8_t)(1023 * 7 + 1));
    /* palette 0 with garbage -> INIT */
    memset(cfg, 0x5a, 64);
    cfg[0] = 0;
    ax_cfg_set(&a, cfg);
    ax_cfg_get(&a, out);
    TEST_CHECK(m0_all_bytes(out, 0, 64, 0));
    /* palette 1 needs XCR0[18:17] = 11b */
    ax_cfg_make(cfg, 0, spec);
    TEST_CHECK(ax_xsetbv(&a, 0x7) == -1);
    ax_cfg_set(&a, cfg);
    ax_cfg_get(&a, out);
    TEST_CHECK(m0_all_bytes(out, 0, 64, 0));
    TEST_CHECK(ax_xsetbv(&a, AX_XCR0) == -1);
    ax_cfg_set(&a, cfg);
    ax_cfg_get(&a, out);
    TEST_CHECK(memcmp(out, cfg, 64) == 0);
    /* uc_reg_read2 size checks */
    {
        size_t sz = 63;
        uc_assert_err(UC_ERR_OVERFLOW, uc_reg_read2(a.uc, UC_X86_REG_TILECFG, out, &sz));
        sz = 1023;
        uc_assert_err(UC_ERR_OVERFLOW, uc_reg_read2(a.uc, UC_X86_REG_TMM3, back, &sz));
        sz = 1024;
        OK(uc_reg_read2(a.uc, UC_X86_REG_TMM3, back, &sz));
        TEST_CHECK(sz == 1024);
    }
    ax_close(&a);
}

/*
 * NoVmp U175: LDTILECFG / STTILECFG / TILERELEASE; the configuration matrix itself is in
 * x86_amx_evecs (ldtilecfg_*). Here: tiles zeroed by LDTILECFG / TILERELEASE, STTILECFG
 * image, start_row kept.
 */
static void test_x86_amx_cfg_insns(void)
{
    static const int spec[] = {0, 16, 64, 5, 3, 12, -1};
    uint8_t cfg[64], out[64], tile[1024], code[16];
    AmxT a;
    int t;
    size_t n;

    ax_open(&a, UC_MODE_64, UC_X86_AMX_ALL, NULL, 0);
    TEST_CHECK(ax_xsetbv(&a, AX_XCR0) == -1);
    memset(tile, 0xee, sizeof(tile));
    for (t = 0; t < 8; t++) {
        ax_tile_set(&a, t, tile);
    }
    ax_cfg_make(cfg, 9, spec);
    OK(uc_mem_write(a.uc, AX_DATA + 0x1000, cfg, 64));
    ax_set(&a, UC_X86_REG_RSI, AX_DATA + 0x1000);
    n = AX_LDTILECFG(code, 6);
    TEST_CHECK(ax_run(&a, code, n) == -1);
    ax_cfg_get(&a, out);
    TEST_CHECK(memcmp(out, cfg, 64) == 0);                    /* start_row 9 loaded too */
    for (t = 0; t < 8; t++) {
        ax_tile_get(&a, t, tile);
        TEST_CHECK_(m0_all_bytes(tile, 0, 1024, 0), "tmm%d zeroed by LDTILECFG", t);
    }
    /* STTILECFG [rdi]: the image (start_row included) */
    memset(out, 0xcc, 64);
    OK(uc_mem_write(a.uc, AX_DATA + 0x2000, out, 64));
    ax_set(&a, UC_X86_REG_RDI, AX_DATA + 0x2000);
    n = AX_STTILECFG(code, 7);
    TEST_CHECK(ax_run(&a, code, n) == -1);
    OK(uc_mem_read(a.uc, AX_DATA + 0x2000, out, 64));
    TEST_CHECK(memcmp(out, cfg, 64) == 0);
    /* TILERELEASE: INIT, tiles zero; STTILECFG then stores 64 zero bytes */
    memset(tile, 0x11, sizeof(tile));
    ax_tile_set(&a, 6, tile);
    TEST_CHECK(ax_run(&a, ax_tilerelease, sizeof(ax_tilerelease)) == -1);
    ax_cfg_get(&a, out);
    TEST_CHECK(m0_all_bytes(out, 0, 64, 0));
    ax_tile_get(&a, 6, tile);
    TEST_CHECK(m0_all_bytes(tile, 0, 1024, 0));
    memset(out, 0xcc, 64);
    OK(uc_mem_write(a.uc, AX_DATA + 0x2000, out, 64));
    TEST_CHECK(ax_run(&a, code, n) == -1);
    OK(uc_mem_read(a.uc, AX_DATA + 0x2000, out, 64));
    TEST_CHECK(m0_all_bytes(out, 0, 64, 0));
    /* LDTILECFG palette 0: INIT and tiles zeroed */
    ax_cfg_set(&a, cfg);
    memset(tile, 0x22, sizeof(tile));
    ax_tile_set(&a, 2, tile);
    memset(out, 0, 64);
    out[5] = 0x77;                                           /* ignored with palette 0 */
    OK(uc_mem_write(a.uc, AX_DATA + 0x1000, out, 64));
    n = AX_LDTILECFG(code, 6);
    TEST_CHECK(ax_run(&a, code, n) == -1);
    ax_cfg_get(&a, out);
    TEST_CHECK(m0_all_bytes(out, 0, 64, 0));
    ax_tile_get(&a, 2, tile);
    TEST_CHECK(m0_all_bytes(tile, 0, 1024, 0));
    /* LDTILECFG #GP: TILECFG and TILEDATA unchanged */
    ax_cfg_set(&a, cfg);
    ax_tile_set(&a, 2, tile);
    memcpy(out, cfg, 64);
    out[49] = 0;                                             /* tile 1: rows 0, colsb 0 ok */
    out[50] = 4;                                             /* tile 2: rows 4, colsb 0 */
    memset(tile, 0x33, sizeof(tile));
    ax_tile_set(&a, 2, tile);
    OK(uc_mem_write(a.uc, AX_DATA + 0x1000, out, 64));
    TEST_CHECK(ax_run(&a, code, n) == 13);
    ax_cfg_get(&a, out);
    TEST_CHECK(memcmp(out, cfg, 64) == 0);
    ax_tile_get(&a, 2, tile);
    TEST_CHECK(tile[0] == 0x33 && tile[1023] == 0x33);
    ax_close(&a);
}

/*
 * NoVmp U176: TILELOADD/TILELOADDT1/TILESTORED/TILEZERO (SDM Vol2B Operation): strided
 * rows, partial rows/columns, zeroing of the rest, no index (stride 0), scale, negative
 * stride, displacement, segment override, 32-bit address size, start_row restart, faults.
 */
static void ax_mem_pattern(AmxT *a, uint64_t addr, size_t len, unsigned salt)
{
    static uint8_t buf[AX_DATA_SIZE];
    size_t i;
    TEST_CHECK(len <= sizeof(buf));
    for (i = 0; i < len; i++) {
        buf[i] = (uint8_t)(i * 13 + salt * 7 + (i >> 8));
    }
    OK(uc_mem_write(a->uc, addr, buf, len));
}

static uint8_t ax_pat(size_t i, unsigned salt)
{
    return (uint8_t)(i * 13 + salt * 7 + (i >> 8));
}

static void test_x86_amx_load_store(void)
{
    static const int spec[] = {0, 16, 64, 1, 5, 24, 2, 3, 4, 3, 16, 64, 4, 7, 12, 5, 16, 8,
                               -1};
    uint8_t cfg[64], tile[1024], code[32], mem[0x800], out[64];
    AmxT a;
    size_t n;
    int r, j;

    ax_open(&a, UC_MODE_64, UC_X86_AMX_ALL, NULL, 0);
    TEST_CHECK(ax_xsetbv(&a, AX_XCR0) == -1);
    ax_cfg_make(cfg, 0, spec);
    ax_cfg_set(&a, cfg);
    ax_mem_pattern(&a, AX_DATA, AX_DATA_SIZE, 1);

    /* tmm1 (5 x 24): rows at RSI + 0x40 + r * 100h (RCX = 100h, scale 0); rest zeroed */
    memset(tile, 0xff, sizeof(tile));
    ax_tile_set(&a, 1, tile);
    ax_set(&a, UC_X86_REG_RSI, AX_DATA + 0x1000);
    ax_set(&a, UC_X86_REG_RCX, 0x100);
    n = AX_TILELOADD(code, 1, 6, 1, 0, 0x40);
    TEST_CHECK(ax_run(&a, code, n) == -1);
    ax_tile_get(&a, 1, tile);
    for (r = 0; r < 16; r++) {
        for (j = 0; j < 64; j++) {
            uint8_t exp = (r < 5 && j < 24) ? ax_pat(0x1040 + r * 0x100 + j, 1) : 0;
            TEST_CHECK_(tile[64 * r + j] == exp, "tmm1 row %d byte %d", r, j);
        }
    }
    ax_cfg_get(&a, out);
    TEST_CHECK(out[1] == 0);
    /* TILELOADDT1 into tmm0 (16 x 64) with R8 index scaled by 8 (stride 8 * 10h) */
    ax_set(&a, UC_X86_REG_R8, 0x10);
    n = AX_TILELOADDT1(code, 0, 6, 8, 3, -0x800);
    TEST_CHECK(ax_run(&a, code, n) == -1);
    ax_tile_get(&a, 0, tile);
    for (r = 0; r < 16; r++) {
        for (j = 0; j < 64; j++) {
            TEST_CHECK_(tile[64 * r + j] == ax_pat(0x800 + r * 0x80 + j, 1), "tmm0 %d/%d", r, j);
        }
    }
    /* no index register: stride 0, every row the same 64 bytes; R13 base needs disp */
    ax_set(&a, UC_X86_REG_R13, AX_DATA + 0x3000);
    n = AX_TILELOADD(code, 3, 13, -1, 0, 0x10);
    TEST_CHECK(ax_run(&a, code, n) == -1);
    ax_tile_get(&a, 3, tile);
    for (r = 0; r < 16; r++) {
        TEST_CHECK(tile[64 * r] == ax_pat(0x3010, 1) && tile[64 * r + 63] == ax_pat(0x304f, 1));
    }
    /* index RSP-encoding with VEX.X = 1 is R12 (a real index); negative stride */
    ax_set(&a, UC_X86_REG_R12, (uint64_t)-0x40);
    ax_set(&a, UC_X86_REG_RBX, AX_DATA + 0x5000);
    n = AX_TILELOADD(code, 4, 3, 12, 0, 0);
    TEST_CHECK(ax_run(&a, code, n) == -1);
    ax_tile_get(&a, 4, tile);
    for (r = 0; r < 16; r++) {
        for (j = 0; j < 64; j++) {
            uint8_t exp = (r < 7 && j < 12) ? ax_pat(0x5000 - r * 0x40 + j, 1) : 0;
            TEST_CHECK_(tile[64 * r + j] == exp, "tmm4 %d/%d", r, j);
        }
    }

    /* TILESTORED tmm1 (5 x 24) to RDI + r * 30h: only colsb bytes of each row written */
    memset(mem, 0xa5, sizeof(mem));
    OK(uc_mem_write(a.uc, AX_DATA + 0x6000, mem, sizeof(mem)));
    ax_set(&a, UC_X86_REG_RDI, AX_DATA + 0x6000);
    ax_set(&a, UC_X86_REG_RDX, 0x30);
    n = AX_TILESTORED(code, 1, 7, 2, 0, 0);
    TEST_CHECK(ax_run(&a, code, n) == -1);
    OK(uc_mem_read(a.uc, AX_DATA + 0x6000, mem, sizeof(mem)));
    for (j = 0; j < 0x200; j++) {
        int row = j / 0x30, col = j % 0x30;
        uint8_t exp = (row < 5 && col < 24) ? ax_pat(0x1040 + row * 0x100 + col, 1) : 0xa5;
        TEST_CHECK_(mem[j] == exp, "store byte %x", j);
    }

    /* start_row restart: start_row = 2 loads rows 2..4 only, rows 0-1 kept, rows >= 2 zeroed */
    memset(tile, 0x5c, sizeof(tile));
    ax_tile_set(&a, 1, tile);
    cfg[1] = 2;
    ax_cfg_set(&a, cfg);
    n = AX_TILELOADD(code, 1, 6, 1, 0, 0x40);
    TEST_CHECK(ax_run(&a, code, n) == -1);
    ax_tile_get(&a, 1, tile);
    for (r = 0; r < 16; r++) {
        for (j = 0; j < 64; j++) {
            uint8_t exp = r < 2 ? 0x5c : (r < 5 && j < 24) ? ax_pat(0x1040 + r * 0x100 + j, 1) : 0;
            TEST_CHECK_(tile[64 * r + j] == exp, "restart tmm1 %d/%d", r, j);
        }
    }
    ax_cfg_get(&a, out);
    TEST_CHECK(out[1] == 0);
    /* TILESTORED from start_row 4 writes only row 4 */
    cfg[1] = 4;
    ax_cfg_set(&a, cfg);
    memset(mem, 0xa5, sizeof(mem));
    OK(uc_mem_write(a.uc, AX_DATA + 0x6000, mem, sizeof(mem)));
    n = AX_TILESTORED(code, 1, 7, 2, 0, 0);
    TEST_CHECK(ax_run(&a, code, n) == -1);
    OK(uc_mem_read(a.uc, AX_DATA + 0x6000, mem, sizeof(mem)));
    TEST_CHECK(mem[0] == 0xa5 && mem[3 * 0x30] == 0xa5);
    TEST_CHECK(mem[4 * 0x30] == ax_pat(0x1040 + 4 * 0x100, 1) && mem[4 * 0x30 + 24] == 0xa5);
    ax_cfg_get(&a, out);
    TEST_CHECK(out[1] == 0);
    cfg[1] = 0;
    ax_cfg_set(&a, cfg);

    /* FS segment override: linear = FS.base + EA */
    {
        uint64_t fsb = 0x1000;
        OK(uc_reg_write(a.uc, UC_X86_REG_FS_BASE, &fsb));
        ax_set(&a, UC_X86_REG_RSI, AX_DATA + 0x1000);
        ax_set(&a, UC_X86_REG_RCX, 0x100);
        code[0] = 0x64;
        n = 1 + AX_TILELOADD(code + 1, 5, 6, 1, 0, 0);
        TEST_CHECK(ax_run(&a, code, n) == -1);
        ax_tile_get(&a, 5, tile);
        TEST_CHECK(tile[0] == ax_pat(0x2000, 1) && tile[64 + 5] == ax_pat(0x2105, 1));
        TEST_CHECK(tile[8] == 0);                            /* colsb 8 */
        fsb = 0;
        OK(uc_reg_write(a.uc, UC_X86_REG_FS_BASE, &fsb));
    }
    /* 67H: 32-bit effective address (upper RSI bits ignored, wraps at 4 GiB) */
    ax_set(&a, UC_X86_REG_RSI, 0xffffffff00000000ULL | (AX_DATA + 0x1000));
    ax_set(&a, UC_X86_REG_RCX, 0x100);
    code[0] = 0x67;
    n = 1 + AX_TILELOADD(code + 1, 2, 6, 1, 0, 0);
    TEST_CHECK(ax_run(&a, code, n) == -1);
    ax_tile_get(&a, 2, tile);
    TEST_CHECK(tile[0] == ax_pat(0x1000, 1) && tile[2 * 64 + 3] == ax_pat(0x1203, 1));

    /* TILEZERO tmm0: all 16 x 64 bytes, start_row := 0 */
    cfg[1] = 7;
    ax_cfg_set(&a, cfg);
    n = ax_enc_tilezero(code, 0);
    TEST_CHECK(ax_run(&a, code, n) == -1);
    ax_tile_get(&a, 0, tile);
    TEST_CHECK(m0_all_bytes(tile, 0, 1024, 0));
    ax_cfg_get(&a, out);
    TEST_CHECK(out[1] == 0);

    /* a fault in the middle: rows from AX_DATA + 0x1F000 + r*0x400 leave the mapping at row 4
       (AX_DATA_SIZE = 0x20000): start_row = 4, rows 0-3 loaded, the rest zeroed */
    memset(tile, 0x77, sizeof(tile));
    ax_tile_set(&a, 3, tile);
    ax_set(&a, UC_X86_REG_RSI, AX_DATA + 0x1f000);
    ax_set(&a, UC_X86_REG_RCX, 0x400);
    n = AX_TILELOADD(code, 3, 6, 1, 0, 0);
    TEST_CHECK(ax_run(&a, code, n) == 1000 + UC_ERR_READ_UNMAPPED);
    ax_cfg_get(&a, out);
    TEST_CHECK(out[1] == 4);
    TEST_MSG("start_row %d", out[1]);
    ax_tile_get(&a, 3, tile);
    TEST_CHECK(tile[0] == ax_pat(0x1f000, 1) && tile[3 * 64] == ax_pat(0x1fc00, 1));
    TEST_CHECK(tile[4 * 64] == 0 && tile[15 * 64 + 63] == 0);
    /* the restart (start_row 4, mapped memory now) loads rows 4.. and keeps 0-3 */
    ax_set(&a, UC_X86_REG_RSI, AX_DATA);
    n = AX_TILELOADD(code, 3, 6, 1, 0, 0);
    TEST_CHECK(ax_run(&a, code, n) == -1);
    ax_tile_get(&a, 3, tile);
    TEST_CHECK(tile[0] == ax_pat(0x1f000, 1) && tile[4 * 64] == ax_pat(0x1000, 1));
    ax_close(&a);
}

/* NoVmp U177-U180: every TMUL vector of ref_amx.py */
static void test_x86_amx_tmul_vectors(void)
{
    size_t i;

    for (i = 0; i < sizeof(x86_amx_tvecs) / sizeof(x86_amx_tvecs[0]); i++) {
        const struct x86_amx_tvec *v = &x86_amx_tvecs[i];
        static uint8_t tiles[8][1024];
        uint8_t cfg[64], out[1024], exp[1024], ocfg[64];
        uint64_t h = 0xCBF29CE484222325ULL;
        AmxT a;
        int t, vec;

        for (t = 0; t < 8; t++) {
            ax_fill_tile(v->seed, t, v->kind[t], tiles[t]);
            h = ax_fnv(tiles[t], 1024, h);
        }
        TEST_CHECK_(h == v->in_hash, "%s: generator hash %016llx", v->name, (unsigned long long)h);
        ax_hex(v->cfg, cfg, 64);
        ax_hex(v->exp, exp, 1024);
        ax_open(&a, UC_MODE_64, v->mask, NULL, 0);
        TEST_CHECK(ax_xsetbv(&a, AX_XCR0) == -1);
        ax_cfg_set(&a, cfg);
        for (t = 0; t < 8; t++) {
            ax_tile_set(&a, t, tiles[t]);
        }
        vec = ax_run(&a, v->code, v->code_len);
        TEST_CHECK_(vec == -1, "%s executes (vector %d)", v->name, vec);
        for (t = 0; t < 8; t++) {
            ax_tile_get(&a, t, out);
            if (t == v->dst) {
                int k;
                for (k = 0; k < 1024; k += 4) {
                    if (memcmp(out + k, exp + k, 4)) {
                        uint32_t g, e;
                        memcpy(&g, out + k, 4);
                        memcpy(&e, exp + k, 4);
                        TEST_CHECK_(0, "%s: row %d dword %d = %08x, expected %08x", v->name,
                                    k / 64, (k % 64) / 4, g, e);
                        break;
                    }
                }
            } else {
                TEST_CHECK_(memcmp(out, tiles[t], 1024) == 0, "%s: tmm%d unchanged", v->name, t);
            }
        }
        ax_cfg_get(&a, ocfg);
        cfg[1] = 0;                                  /* zero_tilecfg_start() */
        TEST_CHECK_(memcmp(ocfg, cfg, 64) == 0, "%s: TILECFG", v->name);
        ax_close(&a);
    }
}

/* NoVmp U175-U180: the exception matrix of ref_amx.py (#UD / #NM / #GP / none) */
static void test_x86_amx_exceptions(void)
{
    size_t i;

    for (i = 0; i < sizeof(x86_amx_evecs) / sizeof(x86_amx_evecs[0]); i++) {
        const struct x86_amx_evec *v = &x86_amx_evecs[i];
        uint8_t cfg[64], mem_cfg[64], post[64], out[64];
        AmxT a;
        int vec;

        ax_hex(v->cfg, cfg, 64);
        ax_hex(v->mem_cfg, mem_cfg, 64);
        ax_hex(v->post_cfg, post, 64);
        ax_open(&a, v->mode64 ? UC_MODE_64 : UC_MODE_32, v->mask, NULL, 0);
        if (v->xcr0) {
            OK(uc_reg_write(a.uc, UC_X86_REG_XCR0, &v->xcr0));
        }
        ax_cfg_set(&a, cfg);
        if (v->xfd) {
            ax_msr_write(&a, 0x1c4, v->xfd);
        }
        if (!v->osxsave) {
            uint64_t cr4 = 0;
            OK(uc_reg_read(a.uc, UC_X86_REG_CR4, &cr4));
            cr4 &= ~(1ULL << 18);
            OK(uc_reg_write(a.uc, UC_X86_REG_CR4, &cr4));
        }
        OK(uc_mem_write(a.uc, AX_DATA + 0x1000, mem_cfg, 64));
        ax_set(&a, UC_X86_REG_RSI, AX_DATA + 0x1000);
        ax_set(&a, UC_X86_REG_RDI, AX_DATA + 0x4000);
        ax_set(&a, UC_X86_REG_RCX, 64);
        if (v->mode64) {
            ax_set(&a, UC_X86_REG_R12, 64);
        }
        vec = ax_run(&a, v->code, v->code_len);
        TEST_CHECK_(vec == v->vec, "%s: vector %d, expected %d", v->name, vec, v->vec);
        if (v->mask) {
            TEST_CHECK_(ax_msr_read(&a, 0x1c5) == v->xfd_err, "%s: IA32_XFD_ERR %llx", v->name,
                        (unsigned long long)ax_msr_read(&a, 0x1c5));
        }
        ax_cfg_get(&a, out);
        TEST_CHECK_(memcmp(out, post, 64) == 0, "%s: TILECFG afterwards", v->name);
        ax_close(&a);
    }
}

/* NoVmp U172/U173: XSAVE family with components 17-18 (+ XFD) */
static void ax_xop(AmxT *a, const char *code, uint64_t rfbm, int expect)
{
    ax_set(a, UC_X86_REG_RSI, AX_DATA);
    ax_set(a, UC_X86_REG_RAX, (uint32_t)rfbm);
    ax_set(a, UC_X86_REG_RDX, (uint32_t)(rfbm >> 32));
    TEST_CHECK_(ax_run(a, (const uint8_t *)code, 3) == expect, "xop %02x %02x %02x rfbm %llx",
                (uint8_t)code[0], (uint8_t)code[1], (uint8_t)code[2], (unsigned long long)rfbm);
}

static void ax_put_state(AmxT *a, const uint8_t cfg[64], unsigned salt)
{
    uint8_t tile[1024];
    int t, i;
    ax_cfg_set(a, cfg);
    for (t = 0; t < 8; t++) {
        for (i = 0; i < 1024; i++) {
            tile[i] = (uint8_t)(i * 3 + t * 17 + salt);
        }
        ax_tile_set(a, t, tile);
    }
}

static int ax_state_is(AmxT *a, const uint8_t cfg[64], unsigned salt, int zero_tiles)
{
    uint8_t tile[1024], out[64];
    int t, i, ok = 1;
    ax_cfg_get(a, out);
    ok &= memcmp(out, cfg, 64) == 0;
    for (t = 0; t < 8; t++) {
        ax_tile_get(a, t, tile);
        for (i = 0; i < 1024; i++) {
            ok &= tile[i] == (zero_tiles ? 0 : (uint8_t)(i * 3 + t * 17 + salt));
        }
    }
    return ok;
}

static void test_x86_amx_xsave(void)
{
    static const int spec[] = {0, 16, 64, 6, 3, 20, -1};
    static uint8_t area[0x3000];
    uint8_t cfg[64], zero64[64] = {0};
    AmxT a;
    uint64_t bv;
    int t, i;

    ax_open(&a, UC_MODE_64, UC_X86_AMX_ALL, NULL, 0);
    TEST_CHECK(ax_xsetbv(&a, AX_XCR0) == -1);
    ax_cfg_make(cfg, 3, spec);
    ax_put_state(&a, cfg, 0x40);

    /* XSAVE: TILECFG at AC0h (the STTILECFG image), all 8 KB of TILEDATA at B00h */
    memset(area, 0xcc, sizeof(area));
    memset(area + 512, 0, 64);
    OK(uc_mem_write(a.uc, AX_DATA, area, sizeof(area)));
    ax_xop(&a, M0_XSAVE, ~0ULL, -1);
    OK(uc_mem_read(a.uc, AX_DATA, area, sizeof(area)));
    TEST_CHECK((m0_rd64(area, 512) & 0x60000) == 0x60000);
    TEST_CHECK(memcmp(area + 0xac0, cfg, 64) == 0);
    for (t = 0; t < 8; t++) {
        for (i = 0; i < 1024; i++) {
            if (area[0xb00 + 1024 * t + i] != (uint8_t)(i * 3 + t * 17 + 0x40)) {
                TEST_CHECK_(0, "XSAVE tmm%d byte %d", t, i);
                t = 8;
                break;
            }
        }
    }
    TEST_CHECK(m0_all_bytes(area, 0x2b00, 0x100, 0xcc));
    /* scribble, XRSTOR: TILECFG (start_row too) and every tile byte back */
    ax_put_state(&a, zero64, 0x99);
    ax_xop(&a, M0_XRSTOR, ~0ULL, -1);
    TEST_CHECK(ax_state_is(&a, cfg, 0x40, 0));
    /* XSTATE_BV[18:17] = 0: both initialised */
    bv = m0_rd64(area, 512) & ~0x60000ULL;
    OK(uc_mem_write(a.uc, AX_DATA + 512, &bv, 8));
    ax_xop(&a, M0_XRSTOR, ~0ULL, -1);
    TEST_CHECK(ax_state_is(&a, zero64, 0, 1));
    /* RFBM = TILEDATA only: tiles loaded whatever TILECFG is (INIT here), TILECFG untouched */
    bv |= 0x60000;
    OK(uc_mem_write(a.uc, AX_DATA + 512, &bv, 8));
    ax_xop(&a, M0_XRSTOR, 0x40000, -1);
    TEST_CHECK(ax_state_is(&a, zero64, 0x40, 0));
    /* RFBM = TILECFG only: configuration loaded, TILEDATA not touched */
    ax_put_state(&a, zero64, 0x99);
    ax_xop(&a, M0_XRSTOR, 0x20000, -1);
    TEST_CHECK(ax_state_is(&a, cfg, 0x99, 0));
    /* an image LDTILECFG would refuse (rows 17) is loaded as INIT, without #GP */
    area[0xac0 + 48] = 17;
    OK(uc_mem_write(a.uc, AX_DATA + 0xac0, area + 0xac0, 64));
    ax_put_state(&a, cfg, 0x99);
    ax_xop(&a, M0_XRSTOR, ~0ULL, -1);
    TEST_CHECK(ax_state_is(&a, zero64, 0x40, 0));
    area[0xac0 + 48] = 16;
    OK(uc_mem_write(a.uc, AX_DATA + 0xac0, area + 0xac0, 64));

    /* XINUSE (value-based): INIT TILECFG and zero tiles -> 18:17 not in use */
    ax_put_state(&a, zero64, 0);
    {
        uint8_t z[1024] = {0};
        for (t = 0; t < 8; t++) {
            ax_tile_set(&a, t, z);
        }
        TEST_CHECK((ax_xgetbv1(&a) & 0x60000) == 0);
        ax_cfg_set(&a, cfg);
        TEST_CHECK((ax_xgetbv1(&a) & 0x60000) == 0x20000);
        z[1023] = 1;
        ax_tile_set(&a, 7, z);
        TEST_CHECK((ax_xgetbv1(&a) & 0x60000) == 0x60000);
        ax_cfg_set(&a, zero64);
        TEST_CHECK((ax_xgetbv1(&a) & 0x60000) == 0x40000);
        z[1023] = 0;
        ax_tile_set(&a, 7, z);
    }
    /* XSAVE of the INIT state: XSTATE_BV[18:17] = 0, sections still written (zeros) */
    memset(area, 0xcc, sizeof(area));
    memset(area + 512, 0, 64);
    OK(uc_mem_write(a.uc, AX_DATA, area, sizeof(area)));
    ax_xop(&a, M0_XSAVE, ~0ULL, -1);
    OK(uc_mem_read(a.uc, AX_DATA, area, sizeof(area)));
    TEST_CHECK((m0_rd64(area, 512) & 0x60000) == 0);
    TEST_CHECK(m0_all_bytes(area, 0xac0, 0x2040, 0));
    /* XSAVEOPT leaves the sections of unused components alone */
    memset(area, 0xcc, sizeof(area));
    memset(area + 512, 0, 64);
    OK(uc_mem_write(a.uc, AX_DATA, area, sizeof(area)));
    ax_xop(&a, M0_XSAVEOPT, ~0ULL, -1);
    OK(uc_mem_read(a.uc, AX_DATA, area, sizeof(area)));
    TEST_CHECK(m0_all_bytes(area, 0xac0, 0x2040, 0xcc));

    /* XSAVEC (XCR0 60007h): TILECFG at 340h, TILEDATA at 380h, XCOMP_BV 8000...60007h */
    ax_put_state(&a, cfg, 0x21);
    memset(area, 0xcc, sizeof(area));
    memset(area + 512, 0, 64);
    OK(uc_mem_write(a.uc, AX_DATA, area, sizeof(area)));
    ax_xop(&a, M0_XSAVEC, ~0ULL, -1);
    OK(uc_mem_read(a.uc, AX_DATA, area, sizeof(area)));
    TEST_CHECK(m0_rd64(area, 520) == 0x8000000000060007ULL);
    TEST_CHECK(memcmp(area + 0x340, cfg, 64) == 0);
    TEST_CHECK(area[0x380] == (uint8_t)0x21 && area[0x380 + 0x1fff] == (uint8_t)(1023 * 3 + 7 * 17 + 0x21));
    TEST_CHECK(m0_all_bytes(area, 0x2380, 0x80, 0xcc));
    ax_put_state(&a, zero64, 0x99);
    ax_xop(&a, M0_XRSTOR, ~0ULL, -1);                       /* compacted XRSTOR */
    TEST_CHECK(ax_state_is(&a, cfg, 0x21, 0));
    /* with PKRU (8 bytes) in XCR0 the AMX components move to the next 64-byte boundary */
    {
        uint32_t r[4];
        ax_cpuid(&a, 0xd, 0, r);
        if (r[0] & 0x200) {
            TEST_CHECK(ax_xsetbv(&a, AX_XCR0 | 0x200) == -1);
            ax_put_state(&a, cfg, 0x31);
            memset(area, 0xcc, sizeof(area));
            memset(area + 512, 0, 64);
            OK(uc_mem_write(a.uc, AX_DATA, area, sizeof(area)));
            ax_xop(&a, M0_XSAVEC, ~0ULL, -1);
            OK(uc_mem_read(a.uc, AX_DATA, area, sizeof(area)));
            TEST_CHECK(memcmp(area + 0x380, cfg, 64) == 0);
            TEST_CHECK(area[0x3c0] == (uint8_t)0x31);
            TEST_CHECK(m0_all_bytes(area, 0x348, 0x38, 0xcc));      /* the alignment gap */
            ax_put_state(&a, zero64, 0x99);
            ax_xop(&a, M0_XRSTOR, ~0ULL, -1);
            TEST_CHECK(ax_state_is(&a, cfg, 0x31, 0));
            TEST_CHECK(ax_xsetbv(&a, AX_XCR0) == -1);
        }
    }
    ax_close(&a);

    /* 32-bit mode: the XSAVE feature set manages AMX state in any mode */
    ax_open(&a, UC_MODE_32, UC_X86_AMX_ALL, NULL, 0);
    TEST_CHECK(ax_xsetbv(&a, AX_XCR0) == -1);
    ax_put_state(&a, cfg, 0x55);
    memset(area, 0xcc, sizeof(area));
    memset(area + 512, 0, 64);
    OK(uc_mem_write(a.uc, AX_DATA, area, sizeof(area)));
    ax_xop(&a, M0_XSAVE, ~0ULL, -1);
    OK(uc_mem_read(a.uc, AX_DATA, area, sizeof(area)));
    TEST_CHECK(memcmp(area + 0xac0, cfg, 64) == 0 && area[0xb00] == 0x55);
    ax_put_state(&a, zero64, 0x99);
    ax_xop(&a, M0_XRSTOR, ~0ULL, -1);
    TEST_CHECK(ax_state_is(&a, cfg, 0x55, 0));
    {
        static const uint8_t rel[] = {0xc4, 0xe2, 0x78, 0x49, 0xc0};
        TEST_CHECK(ax_run(&a, rel, sizeof(rel)) == 6);       /* AMX itself: 64-bit only */
    }
    ax_close(&a);
}

/* NoVmp U173: IA32_XFD / IA32_XFD_ERR, #NM, XSAVE/XRSTOR with XFD armed */
static void test_x86_amx_xfd(void)
{
    static const int spec[] = {0, 16, 64, 1, 16, 64, -1};
    static uint8_t area[0x3000];
    uint8_t cfg[64], code[16], out[64], tile[1024];
    AmxT a;
    size_t n;
    uint64_t bv;

    ax_open(&a, UC_MODE_64, UC_X86_AMX_ALL, NULL, 0);
    TEST_CHECK(ax_xsetbv(&a, AX_XCR0) == -1);
    /* only bit 18 is supported (CPUID.(0DH,18):ECX[2]); the reset values are 0 */
    TEST_CHECK(ax_rdmsr(&a, 0x1c4) == 0 && ax_rdmsr(&a, 0x1c5) == 0);
    TEST_CHECK(ax_wrmsr(&a, 0x1c4, 0x20000) == 13);
    TEST_CHECK(ax_wrmsr(&a, 0x1c4, 0x60000) == 13);
    TEST_CHECK(ax_wrmsr(&a, 0x1c4, 1) == 13);
    TEST_CHECK(ax_wrmsr(&a, 0x1c5, 0x80000) == 13);
    TEST_CHECK(ax_rdmsr(&a, 0x1c4) == 0);
    TEST_CHECK(ax_wrmsr(&a, 0x1c5, 0x40000) == -1);
    TEST_CHECK(ax_rdmsr(&a, 0x1c5) == 0x40000);
    TEST_CHECK(ax_wrmsr(&a, 0x1c5, 0) == -1);
    TEST_CHECK(ax_wrmsr(&a, 0x1c4, 0x40000) == -1);
    TEST_CHECK(ax_rdmsr(&a, 0x1c4) == 0x40000);

    ax_cfg_make(cfg, 0, spec);
    ax_put_state(&a, cfg, 0x10);
    /* TILELOADD #NM, IA32_XFD_ERR = 40000h, nothing changed */
    ax_set(&a, UC_X86_REG_RSI, AX_DATA);
    ax_set(&a, UC_X86_REG_RCX, 64);
    n = AX_TILELOADD(code, 1, 6, 1, 0, 0);
    TEST_CHECK(ax_run(&a, code, n) == 7);
    TEST_CHECK(ax_rdmsr(&a, 0x1c5) == 0x40000);
    TEST_CHECK(ax_state_is(&a, cfg, 0x10, 0));
    TEST_CHECK(ax_wrmsr(&a, 0x1c5, 0) == -1);
    /* STTILECFG does not use TILEDATA: no #NM */
    ax_set(&a, UC_X86_REG_RDI, AX_DATA + 0x4000);
    n = AX_STTILECFG(code, 7);
    TEST_CHECK(ax_run(&a, code, n) == -1);
    OK(uc_mem_read(a.uc, AX_DATA + 0x4000, out, 64));
    TEST_CHECK(memcmp(out, cfg, 64) == 0);
    TEST_CHECK(ax_rdmsr(&a, 0x1c5) == 0);

    /* XSAVE: XSTATE_BV[18] = 0 and the initial configuration (zeros) for TILEDATA */
    memset(area, 0xcc, sizeof(area));
    memset(area + 512, 0, 64);
    OK(uc_mem_write(a.uc, AX_DATA, area, sizeof(area)));
    ax_xop(&a, M0_XSAVE, ~0ULL, -1);
    OK(uc_mem_read(a.uc, AX_DATA, area, sizeof(area)));
    TEST_CHECK((m0_rd64(area, 512) & 0x60000) == 0x20000);
    TEST_CHECK(memcmp(area + 0xac0, cfg, 64) == 0);
    TEST_CHECK(m0_all_bytes(area, 0xb00, 0x2000, 0));
    /* XSAVEOPT / XSAVEC: TILEDATA not saved */
    memset(area, 0xcc, sizeof(area));
    memset(area + 512, 0, 64);
    OK(uc_mem_write(a.uc, AX_DATA, area, sizeof(area)));
    ax_xop(&a, M0_XSAVEOPT, ~0ULL, -1);
    OK(uc_mem_read(a.uc, AX_DATA, area, sizeof(area)));
    TEST_CHECK((m0_rd64(area, 512) & 0x60000) == 0x20000);
    TEST_CHECK(m0_all_bytes(area, 0xb00, 0x2000, 0xcc));
    memset(area, 0xcc, sizeof(area));
    memset(area + 512, 0, 64);
    OK(uc_mem_write(a.uc, AX_DATA, area, sizeof(area)));
    ax_xop(&a, M0_XSAVEC, ~0ULL, -1);
    OK(uc_mem_read(a.uc, AX_DATA, area, sizeof(area)));
    TEST_CHECK((m0_rd64(area, 512) & 0x60000) == 0x20000);
    TEST_CHECK(m0_all_bytes(area, 0x380, 0x2000, 0xcc));
    /* XGETBV(1) is not affected by XFD */
    TEST_CHECK((ax_xgetbv1(&a) & 0x60000) == 0x60000);

    /* XRSTOR loading TILEDATA (XSTATE_BV[18] = 1): #NM before anything is loaded */
    memset(area, 0, sizeof(area));
    memcpy(area + 0xac0, cfg, 64);
    memset(area + 0xb00, 0x3c, 0x2000);
    bv = 0x60003;
    memcpy(area + 512, &bv, 8);
    OK(uc_mem_write(a.uc, AX_DATA, area, sizeof(area)));
    ax_put_state(&a, cfg, 0x10);
    ax_xop(&a, M0_XRSTOR, ~0ULL, 7);
    TEST_CHECK(ax_rdmsr(&a, 0x1c5) == 0x40000);
    TEST_CHECK(ax_state_is(&a, cfg, 0x10, 0));
    TEST_CHECK(ax_wrmsr(&a, 0x1c5, 0) == -1);
    /* RFBM without 18: no #NM */
    ax_xop(&a, M0_XRSTOR, 0x20003, -1);
    TEST_CHECK(ax_rdmsr(&a, 0x1c5) == 0);
    /* XSTATE_BV[18] = 0: TILEDATA initialised, no #NM */
    bv = 0x20003;
    OK(uc_mem_write(a.uc, AX_DATA + 512, &bv, 8));
    ax_xop(&a, M0_XRSTOR, ~0ULL, -1);
    TEST_CHECK(ax_state_is(&a, cfg, 0, 1));
    TEST_CHECK(ax_rdmsr(&a, 0x1c5) == 0);

    /* LDTILECFG and TILERELEASE initialise TILEDATA without #NM */
    memset(tile, 0x44, sizeof(tile));
    ax_tile_set(&a, 0, tile);
    OK(uc_mem_write(a.uc, AX_DATA + 0x1000, cfg, 64));
    ax_set(&a, UC_X86_REG_RSI, AX_DATA + 0x1000);
    n = AX_LDTILECFG(code, 6);
    TEST_CHECK(ax_run(&a, code, n) == -1);
    ax_tile_get(&a, 0, tile);
    TEST_CHECK(m0_all_bytes(tile, 0, 1024, 0));
    TEST_CHECK(ax_run(&a, ax_tilerelease, sizeof(ax_tilerelease)) == -1);
    TEST_CHECK(ax_rdmsr(&a, 0x1c5) == 0);
    /* XFD[18] has no effect while XCR0[18] = 0 */
    TEST_CHECK(ax_xsetbv(&a, 0x7) == -1);
    ax_xop(&a, M0_XSAVE, ~0ULL, -1);
    TEST_CHECK(ax_xsetbv(&a, AX_XCR0) == -1);
    /* disarmed: TILELOADD runs */
    TEST_CHECK(ax_wrmsr(&a, 0x1c4, 0) == -1);
    ax_cfg_set(&a, cfg);
    ax_set(&a, UC_X86_REG_RSI, AX_DATA);
    n = AX_TILELOADD(code, 1, 6, 1, 0, 0);
    TEST_CHECK(ax_run(&a, code, n) == -1);
    ax_close(&a);

    /* without the opt-in the MSRs keep QEMU's store/return behaviour and have no effect */
    ax_open(&a, UC_MODE_64, 0, NULL, 0);
    TEST_CHECK(ax_wrmsr(&a, 0x1c4, 0x12345678abcdef00ULL) == -1);
    TEST_CHECK(ax_rdmsr(&a, 0x1c4) == 0x12345678abcdef00ULL);
    memset(area, 0xcc, sizeof(area));
    memset(area + 512, 0, 64);
    OK(uc_mem_write(a.uc, AX_DATA, area, sizeof(area)));
    ax_xop(&a, M0_XSAVE, ~0ULL, -1);
    OK(uc_mem_read(a.uc, AX_DATA, area, sizeof(area)));
    /* IA32_XFD bit 9 is not an XFD component: PKRU is still saved as in use */
    if (ax_xcr0(&a) & 0x200) {
        TEST_CHECK(m0_rd64(area, 512) & 0x200);
    }
    ax_close(&a);
}

/*
 * ---- NoVmp U141-U153: EVEX (milestone M1) ----
 * Decoding (62h EVEX vs BOUND, fields outside 64-bit mode), state/CPUID #UD and #NM,
 * masked memory (fault suppression, Unicorn memory hooks). Instruction results are
 * covered by Emulator/data/cases_evex_m1.txt (ref_evex_m1.py, independent SDM model).
 */
#define EV_DATA 0x200000
#define EV_ALL (UC_X86_AVX512_F | UC_X86_AVX512_DQ | UC_X86_AVX512_BW | UC_X86_AVX512_VL)

typedef struct EvCtx {
    uc_engine *uc;
    uc_mode mode;
    X86IntrCapture cap;
    uc_hook hook;
    uint64_t pc;
} EvCtx;

static void ev_open(EvCtx *c, uc_mode mode, int avx512)
{
    memset(c, 0, sizeof(*c));
    c->mode = mode;
    c->pc = code_start;
    OK(uc_open(UC_ARCH_X86, mode, &c->uc));
    OK(uc_ctl_set_cpu_model(c->uc, UC_CPU_X86_MAX));
    if (avx512) {
        OK(uc_ctl_set_x86_avx512(c->uc, avx512));
    }
    OK(uc_mem_map(c->uc, code_start, code_len, UC_PROT_ALL));
    OK(uc_mem_map(c->uc, EV_DATA, 0x4000, UC_PROT_ALL));
    OK(uc_hook_add(c->uc, &c->hook, UC_HOOK_INTR, test_x86_intr_capture_cb, &c->cap, 1, 0));
}

/* run one snippet from a fresh address; returns the vector (6 #UD, 7 #NM, 13 #GP) or -1,
   or -2 - uc_err for an emulation error (unmapped memory) */
static int ev_run(EvCtx *c, const char *code, size_t len)
{
    uint64_t pc = c->pc;
    uc_err err;

    c->pc += 0x40;
    TEST_CHECK(len <= 0x40 && c->pc <= code_start + code_len);
    c->cap.count = 0;
    OK(uc_mem_write(c->uc, pc, code, len));
    err = uc_emu_start(c->uc, pc, pc + len, 0, 0);
    if (err == UC_ERR_INSN_INVALID) {
        return 6;
    }
    if (err != UC_ERR_OK) {
        return -2 - (int)err;
    }
    return c->cap.count ? (int)c->cap.intno : -1;
}

static void ev_set(EvCtx *c, int reg, uint64_t v)
{
    if (c->mode == UC_MODE_64) {
        OK(uc_reg_write(c->uc, reg, &v));
    } else {
        uint32_t v32 = (uint32_t)v;
        OK(uc_reg_write(c->uc, reg, &v32));
    }
}

static uint64_t ev_get(EvCtx *c, int reg)
{
    uint64_t v = 0;
    if (c->mode == UC_MODE_64) {
        OK(uc_reg_read(c->uc, reg, &v));
    } else {
        uint32_t v32 = 0;
        OK(uc_reg_read(c->uc, reg, &v32));
        v = v32;
    }
    return v;
}

/* ZMMn = dwords base + 16 * n + i (n = salt) */
static void ev_put_zmm(EvCtx *c, int n, uint32_t base)
{
    uint32_t z[16];
    int i;
    for (i = 0; i < 16; i++) {
        z[i] = base + 0x01010101u * (uint32_t)i;
    }
    OK(uc_reg_write(c->uc, UC_X86_REG_ZMM0 + n, z));
}

static void ev_get_zmm(EvCtx *c, int n, uint32_t z[16])
{
    OK(uc_reg_read(c->uc, UC_X86_REG_ZMM0 + n, z));
}

/* ZMMd == ZMMa + ZMMb (dwords, 512 bits) */
static int ev_is_sum(EvCtx *c, int d, int a, int b)
{
    uint32_t zd[16], za[16], zb[16];
    int i;
    ev_get_zmm(c, d, zd);
    ev_get_zmm(c, a, za);
    ev_get_zmm(c, b, zb);
    for (i = 0; i < 16; i++) {
        if (zd[i] != za[i] + zb[i]) {
            return 0;
        }
    }
    return 1;
}

#define EV_VPADDD_ZMM1_2_3 "\x62\xf1\x6d\x48\xfe\xcb"

/* 62h: EVEX with AVX-512, otherwise exactly the old BOUND / #UD (SDM Vol2A Table 2-40) */
static void test_x86_evex_routing(void)
{
    /* BOUND eax, [EV_DATA] (P0 = ModRM 05: bits 7:6 = 00b, so never EVEX) */
    static const char bound[] = "\x62\x05\x00\x00\x20\x00";
    static const uint32_t bounds[2] = { 10, 100 };
    EvCtx c;
    int avx;

    for (avx = 0; avx < 2; avx++) {
        ev_open(&c, UC_MODE_32, avx ? EV_ALL : 0);
        OK(uc_mem_write(c.uc, EV_DATA, bounds, sizeof(bounds)));
        ev_set(&c, UC_X86_REG_EAX, 50);
        TEST_CHECK(ev_run(&c, bound, 6) == -1);
        ev_set(&c, UC_X86_REG_EAX, 150);
        TEST_CHECK(ev_run(&c, bound, 6) == 5);              /* #BR */
        ev_put_zmm(&c, 1, 0x11111111);
        ev_put_zmm(&c, 2, 0x10203040);
        ev_put_zmm(&c, 3, 0x01020304);
        if (avx) {
            TEST_CHECK(ev_run(&c, EV_VPADDD_ZMM1_2_3, 6) == -1);
            TEST_CHECK(ev_is_sum(&c, 1, 2, 3));
        } else {
            /* BOUND with ModRM.mod = 11b */
            TEST_CHECK(ev_run(&c, EV_VPADDD_ZMM1_2_3, 6) == 6);
        }
        OK(uc_close(c.uc));
        /* 64-bit mode: EVEX, or #UD (BOUND is invalid) */
        ev_open(&c, UC_MODE_64, avx ? EV_ALL : 0);
        ev_put_zmm(&c, 2, 0x10203040);
        ev_put_zmm(&c, 3, 0x01020304);
        TEST_CHECK(ev_run(&c, EV_VPADDD_ZMM1_2_3, 6) == (avx ? -1 : 6));
        if (avx) {
            TEST_CHECK(ev_is_sum(&c, 1, 2, 3));
        }
        OK(uc_close(c.uc));
    }
}

/* EVEX fields outside 64-bit mode (SDM Vol2A Tables 2-34, 2-41) */
static void test_x86_evex_32bit_fields(void)
{
    EvCtx c;
    uint32_t z[16];
    int i, ok;

    ev_open(&c, UC_MODE_32, EV_ALL);
    OK(uc_mem_map(c.uc, 0x8000, 0x1000, UC_PROT_ALL));
    for (i = 0; i < 8; i++) {
        ev_put_zmm(&c, i, 0x01000000u * (uint32_t)(i + 1));
    }
    /* EVEX.B = 1 (r/m "zmm11") is ignored: zmm3 */
    TEST_CHECK(ev_run(&c, "\x62\xd1\x6d\x48\xfe\xcb", 6) == -1);
    TEST_CHECK(ev_is_sum(&c, 1, 2, 3));
    /* EVEX.R' = 1 (reg "zmm17") is ignored: zmm1 */
    ev_put_zmm(&c, 1, 0);
    TEST_CHECK(ev_run(&c, "\x62\xe1\x6d\x48\xfe\xcb", 6) == -1);
    TEST_CHECK(ev_is_sum(&c, 1, 2, 3));
    /* vvvv = 1101b: vvvv[3] (P[14]) is ignored: zmm5 */
    TEST_CHECK(ev_run(&c, "\x62\xf1\x15\x48\xfe\xcb", 6) == -1);
    TEST_CHECK(ev_is_sum(&c, 1, 5, 3));
    /* EVEX.V' = 0 (stored 0): #UD outside 64-bit mode */
    TEST_CHECK(ev_run(&c, "\x62\xf1\x6d\x40\xfe\xcb", 6) == 6);
    /* VPBROADCASTQ zmm1, r64 with EVEX.W1: W is ignored, VPBROADCASTD zmm1, eax */
    ev_set(&c, UC_X86_REG_EAX, 0x89abcdef);
    TEST_CHECK(ev_run(&c, "\x62\xf2\xfd\x48\x7c\xc8", 6) == -1);
    ev_get_zmm(&c, 1, z);
    for (ok = 1, i = 0; i < 16; i++) {
        ok &= z[i] == 0x89abcdef;
    }
    TEST_CHECK(ok);
    /* disp8*N with 32-bit addressing: VMOVDQU32 zmm1, [esi + 1*64] */
    {
        uint32_t m[16];
        for (i = 0; i < 16; i++) {
            m[i] = 0xc0de0000u + (uint32_t)i;
        }
        OK(uc_mem_write(c.uc, EV_DATA + 0x40, m, sizeof(m)));
        ev_set(&c, UC_X86_REG_ESI, EV_DATA);
        TEST_CHECK(ev_run(&c, "\x62\xf1\x7e\x48\x6f\x4e\x01", 7) == -1);
        ev_get_zmm(&c, 1, z);
        TEST_CHECK(memcmp(z, m, sizeof(m)) == 0);
        /* and with 16-bit addressing (67h): VMOVDQU32 zmm1, [di + 1*64] */
        OK(uc_mem_write(c.uc, 0x8040, m, sizeof(m)));
        ev_put_zmm(&c, 1, 0);
        ev_set(&c, UC_X86_REG_EDI, 0x8000);
        TEST_CHECK(ev_run(&c, "\x67\x62\xf1\x7e\x48\x6f\x4d\x01", 8) == -1);
        ev_get_zmm(&c, 1, z);
        TEST_CHECK(memcmp(z, m, sizeof(m)) == 0);
    }
    OK(uc_close(c.uc));
}

/* Table 2-39 state, CPUID (AVX512VL), #NM after every #UD condition */
static void test_x86_evex_state(void)
{
    static const uc_x86_cpuid prof_noavx512[] = {
        {0x0, 0, 0xd, 0x756e6547, 0x6c65746e, 0x49656e69},
        {0x1, 0, 0x906a0, 0, 0x1c000000, 0x06000000},       /* AVX OSXSAVE XSAVE, SSE SSE2 */
        {0x7, 0, 0, 0x00000020, 0, 0},                      /* AVX2, no AVX512F */
        {0xd, 0, 0xe7, 0xa80, 0xa80, 0},
    };
    EvCtx c;
    uint64_t v;

    ev_open(&c, UC_MODE_64, EV_ALL);
    TEST_CHECK(ev_run(&c, EV_VPADDD_ZMM1_2_3, 6) == -1);
    /* XCR0 without the opmask/ZMM components: #UD */
    v = 7;
    OK(uc_reg_write(c.uc, UC_X86_REG_XCR0, &v));
    TEST_CHECK(ev_run(&c, EV_VPADDD_ZMM1_2_3, 6) == 6);
    /* ... also with CR0.TS = 1 (#UD before #NM) */
    OK(uc_reg_read(c.uc, UC_X86_REG_CR0, &v));
    v |= 8;
    OK(uc_reg_write(c.uc, UC_X86_REG_CR0, &v));
    TEST_CHECK(ev_run(&c, EV_VPADDD_ZMM1_2_3, 6) == 6);
    v = 0xe7;
    OK(uc_reg_write(c.uc, UC_X86_REG_XCR0, &v));
    TEST_CHECK(ev_run(&c, EV_VPADDD_ZMM1_2_3, 6) == 7);    /* #NM */
    /* a wrong EVEX.W is #UD, not #NM */
    TEST_CHECK(ev_run(&c, "\x62\xf1\xed\x48\xfe\xcb", 6) == 6);
    OK(uc_reg_read(c.uc, UC_X86_REG_CR0, &v));
    v &= ~8ULL;
    OK(uc_reg_write(c.uc, UC_X86_REG_CR0, &v));
    /* CR4.OSXSAVE = 0: #UD */
    OK(uc_reg_read(c.uc, UC_X86_REG_CR4, &v));
    v &= ~(1ULL << 18);
    OK(uc_reg_write(c.uc, UC_X86_REG_CR4, &v));
    TEST_CHECK(ev_run(&c, EV_VPADDD_ZMM1_2_3, 6) == 6);
    OK(uc_close(c.uc));

    /* AVX512F without AVX512VL: EVEX.512 runs, EVEX.256 is #UD; {er} on a register form is
       512-bit whatever L'L holds, so it needs no AVX512VL */
    ev_open(&c, UC_MODE_64, UC_X86_AVX512_F);
    TEST_CHECK(ev_run(&c, EV_VPADDD_ZMM1_2_3, 6) == -1);
    TEST_CHECK(ev_run(&c, "\x62\xf1\x6d\x28\xfe\xcb", 6) == 6);
    TEST_CHECK(ev_run(&c, "\x62\xf1\x6c\x78\x58\xcb", 6) == -1);   /* vaddps {rz-sae} */
    TEST_CHECK(ev_run(&c, "\x62\xf1\x6c\x18\x58\xcb", 6) == -1);   /* L'L = 00b, b = 1 */
    OK(uc_close(c.uc));

    /* a CPUID profile without AVX512F: strict by default (U435) -> #UD; explicitly
       non-strict: the CPU still has it; explicitly strict: #UD */
    ev_open(&c, UC_MODE_64, EV_ALL);
    OK(uc_ctl_set_x86_cpuid(c.uc, prof_noavx512, 4));
    v = 0xe7;
    OK(uc_reg_write(c.uc, UC_X86_REG_XCR0, &v));
    TEST_CHECK(ev_run(&c, EV_VPADDD_ZMM1_2_3, 6) == 6);
    OK(uc_ctl_set_x86_cpuid_strict(c.uc, 0));
    TEST_CHECK(ev_run(&c, EV_VPADDD_ZMM1_2_3, 6) == -1);
    OK(uc_ctl_set_x86_cpuid_strict(c.uc, 1));
    TEST_CHECK(ev_run(&c, EV_VPADDD_ZMM1_2_3, 6) == 6);
    OK(uc_close(c.uc));
}

typedef struct EvMemLog {
    int n;
    uint64_t addr[64];
    int size[64];
} EvMemLog;

static void ev_mem_cb(uc_engine *uc, uc_mem_type type, uint64_t addr, int size,
                      int64_t value, void *user)
{
    EvMemLog *l = (EvMemLog *)user;
    if (l->n < 64) {
        l->addr[l->n] = addr;
        l->size[l->n] = size;
    }
    l->n++;
}

static bool ev_map_cb(uc_engine *uc, uc_mem_type type, uint64_t addr, int size,
                      int64_t value, void *user)
{
    (*(int *)user)++;
    return uc_mem_map(uc, addr & ~0xfffULL, 0x1000, UC_PROT_ALL) == UC_ERR_OK;
}

/* masked memory: masked-off elements are never accessed; a faulting store writes nothing */
static void test_x86_evex_masked_memory(void)
{
    /* VMOVDQU32 [rsi]{k1}, zmm1 / VMOVDQU32 zmm1{k1}{z}, [rsi] / VMOVDQA32 [rsi+4]{k1}, zmm1 */
    static const char st[] = "\x62\xf1\x7e\x49\x7f\x0e";
    static const char ld[] = "\x62\xf1\x7e\xc9\x6f\x0e";
    static const char sta[] = "\x62\xf1\x7d\x49\x7f\x8e\x04\x00\x00\x00";
    const uint64_t end = EV_DATA + 0x4000, rsi = end - 32;
    uint8_t buf[32], ff[32];
    uint32_t z[16];
    EvCtx c;
    EvMemLog log;
    uc_hook h;
    uint64_t k;
    int i, ok, mapped = 0;

    memset(ff, 0xa5, sizeof(ff));
    ev_open(&c, UC_MODE_64, EV_ALL);
    ev_put_zmm(&c, 1, 0x11223344);
    ev_set(&c, UC_X86_REG_RSI, rsi);
    OK(uc_mem_write(c.uc, rsi, ff, sizeof(ff)));
    /* elements 8-15 lie on the unmapped page: masked off, no access */
    k = 0x00ff;
    OK(uc_reg_write(c.uc, UC_X86_REG_K1, &k));
    TEST_CHECK(ev_run(&c, st, 6) == -1);
    OK(uc_mem_read(c.uc, rsi, buf, sizeof(buf)));
    ev_get_zmm(&c, 1, z);
    TEST_CHECK(memcmp(buf, z, 32) == 0);
    TEST_CHECK(ev_run(&c, ld, 6) == -1);
    /* element 8 active: UC_ERR_WRITE_UNMAPPED and memory unchanged */
    OK(uc_mem_write(c.uc, rsi, ff, sizeof(ff)));
    k = 0x01ff;
    OK(uc_reg_write(c.uc, UC_X86_REG_K1, &k));
    TEST_CHECK(ev_run(&c, st, 6) == -2 - (int)UC_ERR_WRITE_UNMAPPED);
    OK(uc_mem_read(c.uc, rsi, buf, sizeof(buf)));
    TEST_CHECK(memcmp(buf, ff, sizeof(ff)) == 0);
    TEST_CHECK(ev_run(&c, ld, 6) == -2 - (int)UC_ERR_READ_UNMAPPED);
    /* E1: #GP(0) for a misaligned operand even with k1 = 0 */
    k = 0;
    OK(uc_reg_write(c.uc, UC_X86_REG_K1, &k));
    ev_set(&c, UC_X86_REG_RSI, EV_DATA);
    TEST_CHECK(ev_run(&c, sta, 10) == 13);
    OK(uc_close(c.uc));

    /* memory hooks see exactly the active elements */
    ev_open(&c, UC_MODE_64, EV_ALL);
    ev_put_zmm(&c, 1, 0x55667788);
    ev_set(&c, UC_X86_REG_RSI, EV_DATA + 0x100);
    k = 0x8101;
    OK(uc_reg_write(c.uc, UC_X86_REG_K1, &k));
    memset(&log, 0, sizeof(log));
    OK(uc_hook_add(c.uc, &h, UC_HOOK_MEM_READ, ev_mem_cb, &log, 1, 0));
    TEST_CHECK(ev_run(&c, ld, 6) == -1);
    TEST_CHECK(log.n == 3);
    TEST_CHECK(log.addr[0] == EV_DATA + 0x100 && log.size[0] == 4);
    TEST_CHECK(log.addr[1] == EV_DATA + 0x120 && log.size[1] == 4);
    TEST_CHECK(log.addr[2] == EV_DATA + 0x13c && log.size[2] == 4);
    OK(uc_hook_del(c.uc, h));
    memset(&log, 0, sizeof(log));
    OK(uc_hook_add(c.uc, &h, UC_HOOK_MEM_WRITE, ev_mem_cb, &log, 1, 0));
    TEST_CHECK(ev_run(&c, st, 6) == -1);
    TEST_CHECK(log.n == 3);
    TEST_MSG("writes: %d", log.n);
    OK(uc_hook_del(c.uc, h));
    /* an unmapped-write hook that maps the page: the store completes */
    OK(uc_hook_add(c.uc, &h, UC_HOOK_MEM_WRITE_UNMAPPED, ev_map_cb, &mapped, 1, 0));
    ev_set(&c, UC_X86_REG_RSI, rsi);
    k = 0xffff;
    OK(uc_reg_write(c.uc, UC_X86_REG_K1, &k));
    TEST_CHECK(ev_run(&c, st, 6) == -1);
    TEST_CHECK(mapped == 1);
    ev_get_zmm(&c, 1, z);
    {
        uint8_t all[64];
        OK(uc_mem_read(c.uc, rsi, all, sizeof(all)));
        ok = memcmp(all, z, 64) == 0;
        TEST_CHECK(ok);
    }
    OK(uc_close(c.uc));
    (void)i;
}

static void ev_code_cb(uc_engine *uc, uint64_t addr, uint32_t size, void *user)
{
    *(uint32_t *)user = size;
}

/* RIP-relative EVEX memory operand with an imm8 after it; UC_HOOK_CODE size of EVEX */
static void test_x86_evex_riprel(void)
{
    /* VPCMPD k1, zmm2, [rip + disp32], 1 (LT, signed) */
    char code[] = "\x62\xf3\x6d\x48\x1f\x0d\x00\x00\x00\x00\x01";
    int32_t m[16], z[16], disp;
    uint64_t k = 0, want = 0;
    uint32_t size = 0;
    EvCtx c;
    uc_hook h;
    int i;

    ev_open(&c, UC_MODE_64, EV_ALL);
    for (i = 0; i < 16; i++) {
        z[i] = (i & 1) ? -i : i * 3;
        m[i] = (i % 3) ? i : -100;
        if (z[i] < m[i]) {
            want |= 1ULL << i;
        }
    }
    OK(uc_reg_write(c.uc, UC_X86_REG_ZMM2, z));
    OK(uc_mem_write(c.uc, EV_DATA, m, sizeof(m)));
    disp = (int32_t)(EV_DATA - (c.pc + 11));
    memcpy(code + 6, &disp, 4);
    OK(uc_hook_add(c.uc, &h, UC_HOOK_CODE, ev_code_cb, &size, c.pc, c.pc));
    TEST_CHECK(ev_run(&c, code, 11) == -1);
    OK(uc_reg_read(c.uc, UC_X86_REG_K1, &k));
    TEST_CHECK(k == want);
    TEST_MSG("k1 = %llx, want %llx", (unsigned long long)k, (unsigned long long)want);
    TEST_CHECK(size == 11);
    TEST_MSG("UC_HOOK_CODE size %u", size);
    OK(uc_close(c.uc));
}

/*
 * ---- NoVmp U250-U251: EVEX gathers / scatters (exception class E12) ----
 * Restart behaviour with Unicorn memory hooks (completed elements stay written, their k1
 * bits clear, RIP at the instruction), element order, page-straddling elements, segment
 * bases, 32-bit mode (V', 16-bit addressing #UD, 32-bit linear wrap), #UD/#NM order.
 * Instruction results over every form/VL are in Emulator/data/cases_evex_m2_gather.txt
 * (ref_evex_m2_gather.py, independent SDM model).
 */
#define GS_DATA 0x300000
#define GS_ALL (UC_X86_AVX512_F | UC_X86_AVX512_DQ | UC_X86_AVX512_BW | UC_X86_AVX512_VL)

typedef struct GsCtx {
    uc_engine *uc;
    uc_mode mode;
    X86IntrCapture cap;
    uc_hook hook;
    uint64_t pc;
    uint64_t last;      /* address of the last snippet */
} GsCtx;

/* code at code_start, data page GS_DATA (GS_DATA + 0x1000 is left unmapped) */
static void gs_open(GsCtx *c, uc_mode mode)
{
    memset(c, 0, sizeof(*c));
    c->mode = mode;
    c->pc = code_start;
    OK(uc_open(UC_ARCH_X86, mode, &c->uc));
    OK(uc_ctl_set_cpu_model(c->uc, UC_CPU_X86_MAX));
    OK(uc_ctl_set_x86_avx512(c->uc, GS_ALL));
    OK(uc_mem_map(c->uc, code_start, code_len, UC_PROT_ALL));
    OK(uc_mem_map(c->uc, GS_DATA, 0x1000, UC_PROT_ALL));
    OK(uc_hook_add(c->uc, &c->hook, UC_HOOK_INTR, test_x86_intr_capture_cb, &c->cap, 1, 0));
}

/* -1: completed; 6 #UD, 7 #NM, ...; -2 - uc_err for an emulation error (unmapped memory) */
static int gs_result(GsCtx *c, uc_err err)
{
    if (err == UC_ERR_INSN_INVALID) {
        return 6;
    }
    if (err != UC_ERR_OK) {
        return -2 - (int)err;
    }
    return c->cap.count ? (int)c->cap.intno : -1;
}

static int gs_run(GsCtx *c, const char *code, size_t len)
{
    uint64_t pc = c->pc;

    c->pc += 0x40;
    c->last = pc;
    TEST_CHECK(len <= 0x40 && c->pc <= code_start + code_len);
    c->cap.count = 0;
    OK(uc_mem_write(c->uc, pc, code, len));
    return gs_result(c, uc_emu_start(c->uc, pc, pc + len, 0, 0));
}

/* run the last snippet again from its first byte (the restart after a fault) */
static int gs_rerun(GsCtx *c, size_t len)
{
    c->cap.count = 0;
    return gs_result(c, uc_emu_start(c->uc, c->last, c->last + len, 0, 0));
}

static uint64_t gs_rip(GsCtx *c)
{
    uint64_t v = 0;
    if (c->mode == UC_MODE_64) {
        OK(uc_reg_read(c->uc, UC_X86_REG_RIP, &v));
    } else {
        uint32_t v32 = 0;
        OK(uc_reg_read(c->uc, UC_X86_REG_EIP, &v32));
        v = v32;
    }
    return v;
}

static void gs_set(GsCtx *c, int reg, uint64_t v)
{
    if (c->mode == UC_MODE_64) {
        OK(uc_reg_write(c->uc, reg, &v));
    } else {
        uint32_t v32 = (uint32_t)v;
        OK(uc_reg_write(c->uc, reg, &v32));
    }
}

static void gs_setk(GsCtx *c, int n, uint64_t v)
{
    OK(uc_reg_write(c->uc, UC_X86_REG_K0 + n, &v));
}

static uint64_t gs_getk(GsCtx *c, int n)
{
    uint64_t v = 0;
    OK(uc_reg_read(c->uc, UC_X86_REG_K0 + n, &v));
    return v;
}

static uint32_t gs_mem32(GsCtx *c, uint64_t addr)
{
    uint32_t v = 0;
    OK(uc_mem_read(c->uc, addr, &v, 4));
    return v;
}

static void gs_fill(GsCtx *c, uint64_t addr, uint32_t base, int n)
{
    uint32_t v;
    int i;
    for (i = 0; i < n; i++) {
        v = base + (uint32_t)i;
        OK(uc_mem_write(c->uc, addr + 4 * (uint64_t)i, &v, 4));
    }
}

static bool gs_map_cb(uc_engine *uc, uc_mem_type type, uint64_t addr, int size,
                      int64_t value, void *user)
{
    (*(int *)user)++;
    return uc_mem_map(uc, addr & ~0xfffULL, 0x1000, UC_PROT_ALL) == UC_ERR_OK;
}

typedef struct GsLog {
    int n;
    uint64_t addr[32];
    int64_t value[32];
} GsLog;

static void gs_log_cb(uc_engine *uc, uc_mem_type type, uint64_t addr, int size,
                      int64_t value, void *user)
{
    GsLog *l = (GsLog *)user;
    if (l->n < 32) {
        l->addr[l->n] = addr;
        l->value[l->n] = value;
    }
    l->n++;
}

/* VPGATHERDD zmm1{k1}, [rsi + zmm2*4] */
#define GS_GATHER_DD "\x62\xf2\x7d\x49\x90\x0c\x96"
/* VPSCATTERDD [rsi + zmm2*1]{k1}, zmm1 */
#define GS_SCATTER_DD1 "\x62\xf2\x7d\x49\xa0\x0c\x16"

/* a fault in the middle of a gather, then the restart after the page is mapped */
static void test_x86_evex_gather_restart(void)
{
    const uint64_t k_in = 0xABCD00000000FFF5ULL;    /* elements 1 and 3 masked off */
    uint32_t idx[16], z[16], z0[16], want[16];
    uint64_t done = 0;
    GsCtx c;
    int j;

    gs_open(&c, UC_MODE_64);
    gs_fill(&c, GS_DATA, 0xA0000000, 0x400);
    for (j = 0; j < 16; j++) {
        idx[j] = 3 * j;
        z0[j] = 0xEEEE0000 + j;
    }
    idx[6] = 0x402;                                  /* GS_DATA + 0x1008: unmapped */
    OK(uc_reg_write(c.uc, UC_X86_REG_ZMM2, idx));
    OK(uc_reg_write(c.uc, UC_X86_REG_ZMM1, z0));
    gs_set(&c, UC_X86_REG_RSI, GS_DATA);
    gs_setk(&c, 1, k_in);
    TEST_CHECK(gs_run(&c, GS_GATHER_DD, 7) == -2 - (int)UC_ERR_READ_UNMAPPED);
    TEST_CHECK(gs_rip(&c) == c.last);
    TEST_MSG("rip %llx, insn at %llx", (unsigned long long)gs_rip(&c),
             (unsigned long long)c.last);
    OK(uc_reg_read(c.uc, UC_X86_REG_ZMM1, z));
    for (j = 0; j < 16; j++) {
        bool completed = j < 6 && ((k_in >> j) & 1);
        want[j] = completed ? 0xA0000000 + 3 * j : z0[j];
        if (completed) {
            done |= 1ULL << j;
        }
    }
    TEST_CHECK(memcmp(z, want, sizeof(z)) == 0);
    TEST_CHECK(gs_getk(&c, 1) == (k_in & ~done));
    TEST_MSG("k1 = %llx, want %llx", (unsigned long long)gs_getk(&c, 1),
             (unsigned long long)(k_in & ~done));

    /* the completed elements are not read again: change their memory, map the page, restart */
    gs_fill(&c, GS_DATA, 0x55000000, 0x30);
    OK(uc_mem_map(c.uc, GS_DATA + 0x1000, 0x1000, UC_PROT_ALL));
    gs_fill(&c, GS_DATA + 0x1000, 0xB0000000, 0x400);
    TEST_CHECK(gs_rerun(&c, 7) == -1);
    OK(uc_reg_read(c.uc, UC_X86_REG_ZMM1, z));
    for (j = 6; j < 16; j++) {
        if ((k_in >> j) & 1) {
            want[j] = j == 6 ? 0xB0000002 : 0x55000000 + 3 * j;
        }
    }
    TEST_CHECK(memcmp(z, want, sizeof(z)) == 0);
    TEST_CHECK(gs_getk(&c, 1) == 0);
    TEST_CHECK(gs_rip(&c) == c.last + 7);
    OK(uc_close(c.uc));
}

/* a read-unmapped hook that maps the page: one run; the reads come in element order */
static void test_x86_evex_gather_hooks(void)
{
    uint32_t idx[16], z[16];
    uint64_t q[8];
    int mapped = 0, j, ordered = 1;
    GsLog log;
    uc_hook h1, h2;
    GsCtx c;

    gs_open(&c, UC_MODE_64);
    gs_fill(&c, GS_DATA, 0xC0000000, 0x400);
    for (j = 0; j < 16; j++) {
        idx[j] = 0x40 * (15 - j);                    /* descending addresses */
    }
    idx[9] = 0x480;                                  /* GS_DATA + 0x1200 */
    OK(uc_reg_write(c.uc, UC_X86_REG_ZMM2, idx));
    gs_set(&c, UC_X86_REG_RSI, GS_DATA);
    gs_setk(&c, 1, 0xFFFF);
    memset(&log, 0, sizeof(log));
    OK(uc_hook_add(c.uc, &h1, UC_HOOK_MEM_READ_UNMAPPED, gs_map_cb, &mapped, 1, 0));
    OK(uc_hook_add(c.uc, &h2, UC_HOOK_MEM_READ, gs_log_cb, &log, GS_DATA, GS_DATA + 0x1fff));
    TEST_CHECK(gs_run(&c, GS_GATHER_DD, 7) == -1);
    TEST_CHECK(mapped == 1);
    TEST_CHECK(gs_getk(&c, 1) == 0);
    TEST_CHECK(log.n == 16);
    TEST_MSG("reads: %d", log.n);
    for (j = 0; j < 16 && j < log.n; j++) {
        if (log.addr[j] != GS_DATA + 4ULL * idx[j]) {
            ordered = 0;
        }
    }
    TEST_CHECK(ordered);
    OK(uc_reg_read(c.uc, UC_X86_REG_ZMM1, z));
    TEST_CHECK(z[0] == 0xC0000000 + 0x40 * 15 && z[9] == 0 && z[15] == 0xC0000000);
    OK(uc_hook_del(c.uc, h1));
    OK(uc_hook_del(c.uc, h2));

    /* VPGATHERQD xmm1{k2}, [rsi + ymm2*8] (EVEX.256): 4 dwords, DEST[MAXVL-1:128] = 0, k2 = 0 */
    for (j = 0; j < 8; j++) {
        q[j] = j < 4 ? (uint64_t)(int64_t)(-j) : 0x1234;
    }
    OK(uc_reg_write(c.uc, UC_X86_REG_ZMM2, q));
    memset(z, 0xee, sizeof(z));
    OK(uc_reg_write(c.uc, UC_X86_REG_ZMM1, z));
    gs_set(&c, UC_X86_REG_RSI, GS_DATA + 0x100);
    gs_setk(&c, 2, 0xFFFFFFFFFFFFFFF6ULL);           /* elements 1 and 2 */
    TEST_CHECK(gs_run(&c, "\x62\xf2\x7d\x2a\x91\x0c\xd6", 7) == -1);
    OK(uc_reg_read(c.uc, UC_X86_REG_ZMM1, z));
    TEST_CHECK(z[0] == 0xeeeeeeee && z[1] == 0xC0000000 + 0x40 - 2 &&
               z[2] == 0xC0000000 + 0x40 - 4 && z[3] == 0xeeeeeeee);
    for (j = 4; j < 16; j++) {
        TEST_CHECK(z[j] == 0);
    }
    TEST_CHECK(gs_getk(&c, 2) == 0);
    OK(uc_close(c.uc));
}

/* a fault in the middle of a scatter (an element straddling the end of the page) */
static void test_x86_evex_scatter_restart(void)
{
    const uint64_t k_in = 0x00000000FFFFFFDFULL;    /* element 5 masked off */
    uint32_t idx[16], z[16], sentinel = 0x5a5a5a5a;
    GsCtx c;
    int j, ok;

    gs_open(&c, UC_MODE_64);
    for (j = 0; j < 16; j++) {
        idx[j] = 0x100 + 8 * j;
        z[j] = 0xD0000000 + j;
    }
    idx[5] = 0x1000 + 0x40;                          /* masked off: never accessed */
    idx[7] = 0xffe;                                  /* bytes 0xffe-0x1001: straddles */
    OK(uc_reg_write(c.uc, UC_X86_REG_ZMM2, idx));
    OK(uc_reg_write(c.uc, UC_X86_REG_ZMM1, z));
    for (j = 0; j < 0x400; j++) {
        OK(uc_mem_write(c.uc, GS_DATA + 4 * j, &sentinel, 4));
    }
    gs_set(&c, UC_X86_REG_RSI, GS_DATA);
    gs_setk(&c, 1, k_in);
    TEST_CHECK(gs_run(&c, GS_SCATTER_DD1, 7) == -2 - (int)UC_ERR_WRITE_UNMAPPED);
    TEST_CHECK(gs_rip(&c) == c.last);
    for (j = 0; j < 16; j++) {
        uint32_t want = (j < 7 && j != 5) ? z[j] : sentinel;
        if (j == 5 || j == 7) {
            continue;
        }
        TEST_CHECK(gs_mem32(&c, GS_DATA + idx[j]) == want);
        TEST_MSG("element %d", j);
    }
    /* no byte of the faulting element reached the mapped page */
    {
        uint16_t lo = 0;
        OK(uc_mem_read(c.uc, GS_DATA + 0xffe, &lo, 2));
        TEST_CHECK(lo == 0x5a5a);
    }
    TEST_CHECK(gs_getk(&c, 1) == (k_in & ~0x5FULL));
    TEST_MSG("k1 = %llx", (unsigned long long)gs_getk(&c, 1));

    /* restart once the page is there: elements 7-15, k1 = 0 */
    OK(uc_mem_map(c.uc, GS_DATA + 0x1000, 0x1000, UC_PROT_ALL));
    TEST_CHECK(gs_rerun(&c, 7) == -1);
    TEST_CHECK(gs_getk(&c, 1) == 0);
    ok = 1;
    for (j = 8; j < 16; j++) {
        ok &= gs_mem32(&c, GS_DATA + idx[j]) == z[j];
    }
    TEST_CHECK(ok);
    TEST_CHECK(gs_mem32(&c, GS_DATA + 0xffe) == z[7]);
    TEST_CHECK(gs_mem32(&c, GS_DATA + 0x1040) == 0);  /* element 5 never written */
    OK(uc_close(c.uc));
}

/* overlapping scatter indices: every active element is written, in element order */
static void test_x86_evex_scatter_order(void)
{
    uint32_t idx[16], z[16];
    uint64_t ro = 0;
    GsLog log;
    uc_hook h;
    GsCtx c;
    int j, ok = 1;

    gs_open(&c, UC_MODE_64);
    for (j = 0; j < 16; j++) {
        idx[j] = 0x200;
        z[j] = 0xF0000000 + j;
    }
    OK(uc_reg_write(c.uc, UC_X86_REG_ZMM2, idx));
    OK(uc_reg_write(c.uc, UC_X86_REG_ZMM1, z));
    gs_set(&c, UC_X86_REG_RSI, GS_DATA);
    gs_setk(&c, 1, 0x6F7F);
    memset(&log, 0, sizeof(log));
    OK(uc_hook_add(c.uc, &h, UC_HOOK_MEM_WRITE, gs_log_cb, &log, GS_DATA, GS_DATA + 0xfff));
    TEST_CHECK(gs_run(&c, GS_SCATTER_DD1, 7) == -1);
    TEST_CHECK(log.n == 13);
    TEST_MSG("writes: %d", log.n);
    for (j = 0; j < 16 && j < log.n; j++) {
        static const int order[13] = { 0, 1, 2, 3, 4, 5, 6, 8, 9, 10, 11, 13, 14 };
        if (j < 13 && (uint32_t)log.value[j] != z[order[j]]) {
            ok = 0;
        }
    }
    TEST_CHECK(ok);
    TEST_CHECK(gs_mem32(&c, GS_DATA + 0x200) == z[14]);
    TEST_CHECK(gs_getk(&c, 1) == 0);
    OK(uc_hook_del(c.uc, h));

    /* read-only memory (no hook): the scatter stops at that element, nothing of it written */
    OK(uc_mem_map(c.uc, GS_DATA + 0x1000, 0x1000, UC_PROT_READ));
    for (j = 0; j < 16; j++) {
        idx[j] = j == 3 ? 0x1000 : 0x300 + 4 * j;
    }
    OK(uc_reg_write(c.uc, UC_X86_REG_ZMM2, idx));
    gs_setk(&c, 1, 0xFFFF);
    TEST_CHECK(gs_run(&c, GS_SCATTER_DD1, 7) == -2 - (int)UC_ERR_WRITE_PROT);
    TEST_CHECK(gs_getk(&c, 1) == 0xFFF8);
    TEST_CHECK(gs_mem32(&c, GS_DATA + 0x1000) == 0);
    TEST_CHECK(gs_mem32(&c, GS_DATA + 0x308) == z[2]);
    TEST_CHECK(gs_mem32(&c, GS_DATA + 0x310) == 0);
    OK(uc_mem_read(c.uc, GS_DATA + 0x1000, &ro, 8));
    TEST_CHECK(ro == 0);
    OK(uc_close(c.uc));
}

/* segment bases, 32-bit mode, address-size rules, #UD before #NM */
static void test_x86_evex_vsib_modes(void)
{
    uint32_t idx[16], z[16];
    uint64_t v;
    GsCtx c;
    int j;

    /* 64-bit: FS:[rsi + zmm2*4] with FS.base = GS_DATA, rsi = 0x40 */
    gs_open(&c, UC_MODE_64);
    gs_fill(&c, GS_DATA, 0x11110000, 0x400);
    for (j = 0; j < 16; j++) {
        idx[j] = j;
    }
    OK(uc_reg_write(c.uc, UC_X86_REG_ZMM2, idx));
    v = GS_DATA;
    OK(uc_reg_write(c.uc, UC_X86_REG_FS_BASE, &v));
    gs_set(&c, UC_X86_REG_RSI, 0x40);
    gs_setk(&c, 1, 0xFFFF);
    TEST_CHECK(gs_run(&c, "\x64" GS_GATHER_DD, 8) == -1);
    OK(uc_reg_read(c.uc, UC_X86_REG_ZMM1, z));
    TEST_CHECK(z[0] == 0x11110010 && z[15] == 0x1111001f);
    /* 67h: (esi + index*4) wraps at 32 bits before FS.base is added */
    gs_set(&c, UC_X86_REG_RSI, 0x1FFFFFFC0ULL);      /* esi = 0xFFFFFFC0 */
    gs_setk(&c, 1, 0xFFFF);
    for (j = 0; j < 16; j++) {
        idx[j] = 0x10 + j;                           /* 0xFFFFFFC0 + 0x40 + 4j wraps to 4j */
    }
    OK(uc_reg_write(c.uc, UC_X86_REG_ZMM2, idx));
    TEST_CHECK(gs_run(&c, "\x64\x67" GS_GATHER_DD, 9) == -1);
    OK(uc_reg_read(c.uc, UC_X86_REG_ZMM1, z));
    TEST_CHECK(z[0] == 0x11110000 && z[15] == 0x1111000f);
    /* destination == index over 32 registers: VPGATHERDD zmm17{k1}, [rsi + zmm17*4] */
    TEST_CHECK(gs_run(&c, "\x62\xe2\x7d\x41\x90\x0c\x8e", 7) == 6);
    /* ... zmm17 vs index zmm1 (only bit 4 differs) is valid */
    gs_set(&c, UC_X86_REG_RSI, GS_DATA);
    for (j = 0; j < 16; j++) {
        idx[j] = j;
    }
    OK(uc_reg_write(c.uc, UC_X86_REG_ZMM1, idx));
    gs_setk(&c, 1, 0xFFFF);
    TEST_CHECK(gs_run(&c, "\x62\xe2\x7d\x49\x90\x0c\x8e", 7) == -1);
    OK(uc_reg_read(c.uc, UC_X86_REG_ZMM17, z));
    TEST_CHECK(z[3] == 0x11110003);
    /* k0, then CR0.TS = 1: #UD (k0) before #NM; a valid gather is #NM */
    OK(uc_reg_read(c.uc, UC_X86_REG_CR0, &v));
    v |= 8;
    OK(uc_reg_write(c.uc, UC_X86_REG_CR0, &v));
    TEST_CHECK(gs_run(&c, "\x62\xf2\x7d\x48\x90\x0c\x96", 7) == 6);
    TEST_CHECK(gs_run(&c, GS_GATHER_DD, 7) == 7);
    OK(uc_close(c.uc));

    /* 32-bit protected mode */
    gs_open(&c, UC_MODE_32);
    gs_fill(&c, GS_DATA, 0x22220000, 0x400);
    for (j = 0; j < 16; j++) {
        idx[j] = 2 * j;
    }
    OK(uc_reg_write(c.uc, UC_X86_REG_ZMM2, idx));
    gs_set(&c, UC_X86_REG_ESI, GS_DATA);
    gs_setk(&c, 1, 0xFFFF);
    TEST_CHECK(gs_run(&c, GS_GATHER_DD, 7) == -1);
    OK(uc_reg_read(c.uc, UC_X86_REG_ZMM1, z));
    TEST_CHECK(z[0] == 0x22220000 && z[15] == 0x2222001e);
    TEST_CHECK(gs_getk(&c, 1) == 0);
    /* the linear address wraps at 32 bits: esi = 0xFFFFFF00, index * 4 = GS_DATA + 0x100 */
    gs_set(&c, UC_X86_REG_ESI, 0xFFFFFF00);
    for (j = 0; j < 16; j++) {
        idx[j] = (GS_DATA + 0x100) / 4 + j;
    }
    OK(uc_reg_write(c.uc, UC_X86_REG_ZMM2, idx));
    gs_setk(&c, 1, 0xFFFF);
    TEST_CHECK(gs_run(&c, GS_GATHER_DD, 7) == -1);
    OK(uc_reg_read(c.uc, UC_X86_REG_ZMM1, z));
    TEST_CHECK(z[0] == 0x22220000 && z[15] == 0x2222000f);
    /* 67h: 16-bit address size #UD; EVEX.V' = 0 outside 64-bit mode #UD */
    gs_setk(&c, 1, 0xFFFF);
    TEST_CHECK(gs_run(&c, "\x67" GS_GATHER_DD, 8) == 6);
    TEST_CHECK(gs_run(&c, "\x62\xf2\x7d\x41\x90\x0c\x96", 7) == 6);
    TEST_CHECK(gs_getk(&c, 1) == 0xFFFF);
    /* scatter in 32-bit mode: VPSCATTERDD [esi + zmm2*1]{k1}, zmm1 */
    gs_set(&c, UC_X86_REG_ESI, GS_DATA + 0x800);
    for (j = 0; j < 16; j++) {
        idx[j] = 4 * (15 - j);
        z[j] = 0x33330000 + j;
    }
    OK(uc_reg_write(c.uc, UC_X86_REG_ZMM2, idx));
    OK(uc_reg_write(c.uc, UC_X86_REG_ZMM1, z));
    TEST_CHECK(gs_run(&c, GS_SCATTER_DD1, 7) == -1);
    TEST_CHECK(gs_mem32(&c, GS_DATA + 0x800) == 0x3333000f &&
               gs_mem32(&c, GS_DATA + 0x83c) == 0x33330000);
    TEST_CHECK(gs_getk(&c, 1) == 0);
    OK(uc_close(c.uc));
}

/*
 * ---- NoVmp U190-U201: EVEX milestone M2 engine (scalar forms, FMA, compares, blends) ----
 * Unicorn-visible behaviour the expected-value cases cannot show: masked scalar memory
 * operands seen by the memory hooks (k1[0] = 0: no access at all), fault suppression on
 * memory Unicorn has not mapped, and (U)COMISS EFLAGS after a lazily evaluated CMP, also when
 * #XM leaves them unchanged. Results: Emulator/data/cases_evex_m2_engine.txt
 * (ref_evex_m2_engine.py, independent SDM model).
 */
static void eg_set_k1(EvCtx *c, uint64_t k)
{
    OK(uc_reg_write(c->uc, UC_X86_REG_K1, &k));
}

static void eg_log_reset(EvMemLog *l)
{
    memset(l, 0, sizeof(*l));
}

static void test_x86_evex_scalar_memory(void)
{
    /* VMOVSS xmm1{k1}, [rsi] / VMOVSD [rsi]{k1}, xmm1 / VADDSD xmm1{k1}, xmm2, [rsi] */
    static const char ldss[] = "\x62\xf1\x7e\x09\x10\x0e";
    static const char stsd[] = "\x62\xf1\xff\x09\x11\x0e";
    static const char addsd[] = "\x62\xf1\xef\x09\x58\x0e";
    /* VFMADD231PS zmm1{k1}, zmm2, [rsi] */
    static const char fma[] = "\x62\xf2\x6d\x49\xb8\x0e";
    const uint64_t rsi = EV_DATA + 0x100, end = EV_DATA + 0x4000;
    static const float one[16] = { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 };
    uint32_t z[16];
    EvMemLog log;
    EvCtx c;
    uc_hook hr, hw;
    int i, ok;

    ev_open(&c, UC_MODE_64, EV_ALL);
    OK(uc_mem_write(c.uc, rsi, one, sizeof(one)));
    ev_set(&c, UC_X86_REG_RSI, rsi);
    OK(uc_hook_add(c.uc, &hr, UC_HOOK_MEM_READ, ev_mem_cb, &log, 1, 0));
    OK(uc_hook_add(c.uc, &hw, UC_HOOK_MEM_WRITE, ev_mem_cb, &log, 1, 0));

    /* k1[0] = 0: the m32 is not read; DEST[31:0] kept (merging), DEST[MAXVL-1:32] := 0 */
    ev_put_zmm(&c, 1, 0x11223344);
    eg_set_k1(&c, 0xfe);
    eg_log_reset(&log);
    TEST_CHECK(ev_run(&c, ldss, 6) == -1);
    TEST_CHECK(log.n == 0);
    ev_get_zmm(&c, 1, z);
    for (ok = z[0] == 0x11223344, i = 1; i < 16; i++) {
        ok &= z[i] == 0;
    }
    TEST_CHECK(ok);
    /* k1[0] = 1: one 4-byte read */
    eg_set_k1(&c, 1);
    eg_log_reset(&log);
    TEST_CHECK(ev_run(&c, ldss, 6) == -1);
    TEST_CHECK(log.n == 1 && log.addr[0] == rsi && log.size[0] == 4);
    ev_get_zmm(&c, 1, z);
    TEST_CHECK(z[0] == 0x3f800000 && z[1] == 0);

    /* VADDSD: masked-off element 0 reads nothing; active: one 8-byte read */
    eg_set_k1(&c, 0);
    eg_log_reset(&log);
    TEST_CHECK(ev_run(&c, addsd, 6) == -1);
    TEST_CHECK(log.n == 0);
    eg_set_k1(&c, 1);
    eg_log_reset(&log);
    TEST_CHECK(ev_run(&c, addsd, 6) == -1);
    TEST_CHECK(log.n == 1 && log.addr[0] == rsi && log.size[0] == 8);

    /* VMOVSD store: k1[0] = 0 writes nothing (k1[1] is not looked at), k1[0] = 1 one qword */
    eg_set_k1(&c, 2);
    eg_log_reset(&log);
    TEST_CHECK(ev_run(&c, stsd, 6) == -1);
    TEST_CHECK(log.n == 0);
    eg_set_k1(&c, 1);
    eg_log_reset(&log);
    TEST_CHECK(ev_run(&c, stsd, 6) == -1);
    TEST_CHECK(log.n == 1 && log.addr[0] == rsi && log.size[0] == 8);

    /* packed FMA with a memory source: exactly the active elements are read */
    eg_set_k1(&c, 0x8101);
    eg_log_reset(&log);
    TEST_CHECK(ev_run(&c, fma, 6) == -1);
    TEST_CHECK(log.n == 3);
    TEST_CHECK(log.addr[0] == rsi && log.addr[1] == rsi + 0x20 && log.addr[2] == rsi + 0x3c);
    OK(uc_hook_del(c.uc, hr));
    OK(uc_hook_del(c.uc, hw));

    /* fault suppression on memory Unicorn has not mapped (EV_DATA + 0x4000) */
    ev_set(&c, UC_X86_REG_RSI, end);
    eg_set_k1(&c, 0);
    TEST_CHECK(ev_run(&c, ldss, 6) == -1);
    TEST_CHECK(ev_run(&c, stsd, 6) == -1);
    TEST_CHECK(ev_run(&c, addsd, 6) == -1);
    eg_set_k1(&c, 1);
    TEST_CHECK(ev_run(&c, ldss, 6) == -2 - (int)UC_ERR_READ_UNMAPPED);
    TEST_CHECK(ev_run(&c, stsd, 6) == -2 - (int)UC_ERR_WRITE_UNMAPPED);
    OK(uc_close(c.uc));
}

static void test_x86_evex_comis_eflags(void)
{
    /* mov eax, 1; cmp eax, eax; VCOMISS xmm1, xmm2 / VUCOMISS xmm1, xmm2 {sae} */
    static const char comis[] = "\xb8\x01\x00\x00\x00\x39\xc0\x62\xf1\x7c\x08\x2f\xca";
    static const char ucomis_sae[] = "\xb8\x01\x00\x00\x00\x39\xc0\x62\xf1\x7c\x18\x2e\xca";
    const uint64_t arith = 0x8d5;           /* OF SF ZF AF PF CF */
    uint32_t x1[4] = { 0x3f800000, 0, 0, 0 }, x2[4] = { 0x40000000, 0, 0, 0 };
    uint64_t fl, cr4;
    uint32_t mxcsr;
    EvCtx c;

    ev_open(&c, UC_MODE_64, EV_ALL);
    OK(uc_reg_read(c.uc, UC_X86_REG_CR4, &cr4));
    cr4 |= 1ULL << 10;                      /* CR4.OSXMMEXCPT: #XM, not #UD */
    OK(uc_reg_write(c.uc, UC_X86_REG_CR4, &cr4));
    OK(uc_reg_write(c.uc, UC_X86_REG_XMM1, x1));
    OK(uc_reg_write(c.uc, UC_X86_REG_XMM2, x2));
    /* 1.0 < 2.0: CF = 1, ZF = PF = 0, OF SF AF cleared (the CMP's ZF and PF replaced) */
    TEST_CHECK(ev_run(&c, comis, 13) == -1);
    OK(uc_reg_read(c.uc, UC_X86_REG_EFLAGS, &fl));
    TEST_CHECK((fl & arith) == 0x1);
    TEST_MSG("eflags %llx", (unsigned long long)fl);
    /* SNaN with IM = 0: #XM, EFLAGS still those of the CMP (ZF PF), MXCSR.IE set */
    x2[0] = 0x7fa00000;
    OK(uc_reg_write(c.uc, UC_X86_REG_XMM2, x2));
    mxcsr = 0x1f00;
    OK(uc_reg_write(c.uc, UC_X86_REG_MXCSR, &mxcsr));
    TEST_CHECK(ev_run(&c, comis, 13) == 19);
    OK(uc_reg_read(c.uc, UC_X86_REG_EFLAGS, &fl));
    TEST_CHECK((fl & arith) == 0x44);
    TEST_MSG("eflags %llx", (unsigned long long)fl);
    OK(uc_reg_read(c.uc, UC_X86_REG_MXCSR, &mxcsr));
    TEST_CHECK(mxcsr == 0x1f01);
    /* {sae}: no #XM, no MXCSR flag; unordered: ZF PF CF */
    mxcsr = 0x1f00;
    OK(uc_reg_write(c.uc, UC_X86_REG_MXCSR, &mxcsr));
    TEST_CHECK(ev_run(&c, ucomis_sae, 13) == -1);
    OK(uc_reg_read(c.uc, UC_X86_REG_EFLAGS, &fl));
    TEST_CHECK((fl & arith) == 0x45);
    OK(uc_reg_read(c.uc, UC_X86_REG_MXCSR, &mxcsr));
    TEST_CHECK(mxcsr == 0x1f00);
    OK(uc_close(c.uc));
}

/*
 * ---- NoVmp U210-U215: EVEX M2 permutes / moves (pm_) ----
 * Memory accesses of the narrowing stores, compress/expand and Tuple4 broadcasts (only the
 * selected elements, seen through Unicorn memory hooks), the feature gates (AVX512VL only
 * for forms with a 512-bit length, AVX512DQ forms) and EVEX.W outside 64-bit mode.
 * Instruction results: Emulator/data/cases_evex_m2_perm.txt (ref_evex_m2_perm.py).
 */
static int pm_log_has(EvMemLog *l, uint64_t addr, int size)
{
    int i;
    for (i = 0; i < l->n && i < 64; i++) {
        if (l->addr[i] == addr && l->size[i] == size) {
            return 1;
        }
    }
    return 0;
}

/* memory hooks see only the elements the mask / popcount selects */
static void test_x86_evex_m2_memory(void)
{
    /* VPCOMPRESSD [rsi]{k1}, zmm1 / VPEXPANDD zmm2{k1}{z}, [rsi] */
    static const char cmp[] = "\x62\xf2\x7d\x49\x8b\x0e";
    static const char exp[] = "\x62\xf2\x7d\xc9\x89\x16";
    /* VPMOVQB [rsi]{k1}, zmm1 / VBROADCASTF32X4 zmm2{k1}, [rsi] */
    static const char pmov[] = "\x62\xf2\x7e\x49\x32\x0e";
    static const char bc4[] = "\x62\xf2\x7d\x49\x1a\x16";
    const uint64_t end = EV_DATA + 0x4000;
    uint32_t z[16], z2[16], m[4] = { 0x11111111, 0x22222222, 0x33333333, 0x44444444 };
    uint8_t buf[16];
    EvMemLog log;
    EvCtx c;
    uc_hook h;
    uint64_t k;
    int i, ok, mapped = 0;

    ev_open(&c, UC_MODE_64, EV_ALL);
    ev_put_zmm(&c, 1, 0x10203040);
    ev_get_zmm(&c, 1, z);
    ev_set(&c, UC_X86_REG_RSI, EV_DATA + 0x200);
    /* compress: 3 selected elements -> 3 contiguous dword writes */
    k = 0x8401;
    OK(uc_reg_write(c.uc, UC_X86_REG_K1, &k));
    memset(&log, 0, sizeof(log));
    OK(uc_hook_add(c.uc, &h, UC_HOOK_MEM_WRITE, ev_mem_cb, &log, 1, 0));
    TEST_CHECK(ev_run(&c, cmp, 6) == -1);
    TEST_CHECK(log.n == 3);
    TEST_MSG("compress writes: %d", log.n);
    TEST_CHECK(pm_log_has(&log, EV_DATA + 0x200, 4) && pm_log_has(&log, EV_DATA + 0x204, 4) &&
               pm_log_has(&log, EV_DATA + 0x208, 4));
    OK(uc_mem_read(c.uc, EV_DATA + 0x200, buf, 12));
    TEST_CHECK(memcmp(buf, &z[0], 4) == 0 && memcmp(buf + 4, &z[10], 4) == 0 &&
               memcmp(buf + 8, &z[15], 4) == 0);
    OK(uc_hook_del(c.uc, h));
    /* expand: 3 contiguous dword reads */
    memset(&log, 0, sizeof(log));
    OK(uc_hook_add(c.uc, &h, UC_HOOK_MEM_READ, ev_mem_cb, &log, 1, 0));
    TEST_CHECK(ev_run(&c, exp, 6) == -1);
    TEST_CHECK(log.n == 3);
    TEST_MSG("expand reads: %d", log.n);
    ev_get_zmm(&c, 2, z2);
    for (ok = 1, i = 0; i < 16; i++) {
        uint32_t want = i == 0 ? z[0] : i == 10 ? z[10] : i == 15 ? z[15] : 0;
        ok &= z2[i] == want;
    }
    TEST_CHECK(ok);
    OK(uc_hook_del(c.uc, h));
    /* VPMOVQB: bytes 0 and 7 only */
    k = 0x81;
    OK(uc_reg_write(c.uc, UC_X86_REG_K1, &k));
    memset(&log, 0, sizeof(log));
    OK(uc_hook_add(c.uc, &h, UC_HOOK_MEM_WRITE, ev_mem_cb, &log, 1, 0));
    TEST_CHECK(ev_run(&c, pmov, 6) == -1);
    TEST_CHECK(log.n == 2 && pm_log_has(&log, EV_DATA + 0x200, 1) &&
               pm_log_has(&log, EV_DATA + 0x207, 1));
    TEST_MSG("vpmovqb writes: %d", log.n);
    OK(uc_hook_del(c.uc, h));
    /* VBROADCASTF32X4 with lanes 1 and 6 active: group dwords 1 and 2 are read */
    OK(uc_mem_write(c.uc, EV_DATA + 0x300, m, sizeof(m)));
    ev_set(&c, UC_X86_REG_RSI, EV_DATA + 0x300);
    k = 0x42;
    OK(uc_reg_write(c.uc, UC_X86_REG_K1, &k));
    ev_put_zmm(&c, 2, 0);
    memset(&log, 0, sizeof(log));
    OK(uc_hook_add(c.uc, &h, UC_HOOK_MEM_READ, ev_mem_cb, &log, 1, 0));
    TEST_CHECK(ev_run(&c, bc4, 6) == -1);
    TEST_CHECK(log.n == 2 && pm_log_has(&log, EV_DATA + 0x304, 4) &&
               pm_log_has(&log, EV_DATA + 0x308, 4));
    TEST_MSG("vbroadcastf32x4 reads: %d", log.n);
    ev_get_zmm(&c, 2, z2);
    TEST_CHECK(z2[1] == m[1] && z2[6] == m[2] && z2[0] == 0 && z2[5] == 0x05050505 &&
               z2[2] == 0x02020202);
    OK(uc_hook_del(c.uc, h));
    /* compress next to the unmapped page: 4 selected dwords fit, the full vector would not */
    ev_set(&c, UC_X86_REG_RSI, end - 16);
    k = 0xf000;
    OK(uc_reg_write(c.uc, UC_X86_REG_K1, &k));
    TEST_CHECK(ev_run(&c, cmp, 6) == -1);
    OK(uc_mem_read(c.uc, end - 16, buf, 16));
    TEST_CHECK(memcmp(buf, &z[12], 16) == 0);
    /* one more: UC_ERR_WRITE_UNMAPPED, nothing written */
    memset(buf, 0x5a, sizeof(buf));
    OK(uc_mem_write(c.uc, end - 16, buf, 16));
    k = 0xf800;
    OK(uc_reg_write(c.uc, UC_X86_REG_K1, &k));
    TEST_CHECK(ev_run(&c, cmp, 6) == -2 - (int)UC_ERR_WRITE_UNMAPPED);
    OK(uc_mem_read(c.uc, end - 16, buf, 16));
    for (ok = 1, i = 0; i < 16; i++) {
        ok &= buf[i] == 0x5a;
    }
    TEST_CHECK(ok);
    /* a hook that maps the page: the compress store completes */
    OK(uc_hook_add(c.uc, &h, UC_HOOK_MEM_WRITE_UNMAPPED, ev_map_cb, &mapped, 1, 0));
    TEST_CHECK(ev_run(&c, cmp, 6) == -1);
    TEST_CHECK(mapped == 1);
    OK(uc_close(c.uc));
}

/* AVX512VL gates only forms with a 512-bit length; DQ forms; EVEX.W outside 64-bit mode */
static void test_x86_evex_m2_features(void)
{
    static const char vmovd[] = "\x62\xf1\x7d\x08\x6e\xc8";         /* vmovd xmm1, eax */
    static const char vpermd256[] = "\x62\xf2\x6d\x28\x36\xcb";     /* vpermd ymm1, ymm2, ymm3 */
    static const char vpermd512[] = "\x62\xf2\x6d\x48\x36\xcb";
    static const char vpextrd[] = "\x62\xf3\x7d\x08\x16\xd0\x03";   /* vpextrd eax, xmm2, 3 */
    static const char vpextrq[] = "\x62\xf3\xfd\x08\x16\xd0\x01";   /* vpextrq rax, xmm2, 1 */
    static const char vmovq7e[] = "\x62\xf1\xfd\x08\x7e\xd0";       /* vmovq rax, xmm2 */
    static const char vins32x4[] = "\x62\xf3\x6d\x48\x18\xcb\x01";  /* vinsertf32x4 zmm1,zmm2,xmm3,1 */
    static const char vins64x2[] = "\x62\xf3\xed\x48\x18\xcb\x01";  /* vinsertf64x2 */
    uint32_t z[16];
    EvCtx c;

    /* AVX512F only */
    ev_open(&c, UC_MODE_64, UC_X86_AVX512_F);
    ev_put_zmm(&c, 2, 0x10203040);
    ev_set(&c, UC_X86_REG_RAX, 0x1122334455667788ULL);
    TEST_CHECK(ev_run(&c, vmovd, 6) == -1);
    ev_get_zmm(&c, 1, z);
    TEST_CHECK(z[0] == 0x55667788 && z[1] == 0 && z[15] == 0);
    TEST_CHECK(ev_run(&c, vpermd256, 6) == 6);
    TEST_CHECK(ev_run(&c, vpermd512, 6) == -1);
    TEST_CHECK(ev_run(&c, vpextrd, 7) == 6);                    /* AVX512DQ */
    TEST_CHECK(ev_run(&c, vins32x4, 7) == -1);
    TEST_CHECK(ev_run(&c, vins64x2, 7) == 6);                   /* AVX512DQ */
    OK(uc_close(c.uc));

    /* 32-bit mode: EVEX.W1 of VPEXTRQ / VMOVQ r64 is ignored (VPEXTRD / VMOVD) */
    ev_open(&c, UC_MODE_32, EV_ALL);
    ev_put_zmm(&c, 2, 0x10203040);
    ev_get_zmm(&c, 2, z);
    ev_set(&c, UC_X86_REG_EAX, 0);
    TEST_CHECK(ev_run(&c, vpextrq, 7) == -1);
    TEST_CHECK(ev_get(&c, UC_X86_REG_EAX) == z[1]);
    TEST_CHECK(ev_run(&c, vmovq7e, 6) == -1);
    TEST_CHECK(ev_get(&c, UC_X86_REG_EAX) == z[0]);
    OK(uc_close(c.uc));
}

/*
 * ---- NoVmp U260-U269: AVX512BW (milestone M3, byte/word elements) ----
 * CPUID gating (AVX512BW, AVX512VL below 512 bits, none for the EVEX.128-only E9NF forms),
 * byte-granular masked memory (Unicorn memory hooks see only the active bytes; fault
 * suppression per byte), 64-bit opmask from 64 byte compares. Instruction results are
 * covered by Emulator/data/cases_evex_m3_bw.txt (ref_evex_m3_bw.py, independent SDM model).
 */
/* VPADDB zmm1, zmm2, zmm3 / VPADDB xmm1, xmm2, xmm3 */
#define BW_VPADDB_Z "\x62\xf1\x6d\x48\xfc\xcb"
#define BW_VPADDB_X "\x62\xf1\x6d\x08\xfc\xcb"
/* VPINSRB xmm1, xmm2, eax, 3 / VPEXTRB ecx, xmm1, 3 / VPEXTRW edx, xmm1, 1 (0F C5) */
#define BW_VPINSRB "\x62\xf3\x6d\x08\x20\xc8\x03"
#define BW_VPEXTRB "\x62\xf3\x7d\x08\x14\xc9\x03"
#define BW_VPEXTRW_C5 "\x62\xf1\x7d\x08\xc5\xd1\x01"
/* VPINSRB with L'L = 01b: #UD (Table 2-45 note 4) */
#define BW_VPINSRB_L1 "\x62\xf3\x6d\x28\x20\xc8\x03"

static void test_x86_evex_bw_cpuid(void)
{
    static const struct {
        int avx512;
        int z, x, ins, ext, ins_l1;
    } t[] = {
        {UC_X86_AVX512_F, 6, 6, 6, 6, 6},                                   /* no AVX512BW */
        {UC_X86_AVX512_F | UC_X86_AVX512_VL, 6, 6, 6, 6, 6},
        {UC_X86_AVX512_F | UC_X86_AVX512_BW, -1, 6, -1, -1, 6},              /* no AVX512VL */
        {EV_ALL, -1, -1, -1, -1, 6},
    };
    EvCtx c;
    size_t i;

    for (i = 0; i < sizeof(t) / sizeof(t[0]); i++) {
        ev_open(&c, UC_MODE_64, t[i].avx512);
        ev_set(&c, UC_X86_REG_RAX, 0x5a);
        TEST_CHECK(ev_run(&c, BW_VPADDB_Z, 6) == t[i].z);
        TEST_MSG("avx512 mask %x: VPADDB zmm", t[i].avx512);
        TEST_CHECK(ev_run(&c, BW_VPADDB_X, 6) == t[i].x);
        TEST_MSG("avx512 mask %x: VPADDB xmm", t[i].avx512);
        TEST_CHECK(ev_run(&c, BW_VPINSRB, 7) == t[i].ins);
        TEST_MSG("avx512 mask %x: VPINSRB", t[i].avx512);
        TEST_CHECK(ev_run(&c, BW_VPEXTRB, 7) == t[i].ext);
        TEST_CHECK(ev_run(&c, BW_VPINSRB_L1, 7) == t[i].ins_l1);
        if (t[i].ext == -1) {
            TEST_CHECK(ev_get(&c, UC_X86_REG_RCX) == 0x5a);
            TEST_CHECK(ev_run(&c, BW_VPEXTRW_C5, 7) == -1);
            TEST_CHECK(ev_get(&c, UC_X86_REG_RDX) == 0x5a00);
        }
        OK(uc_close(c.uc));
    }
}

/* byte elements: the hooks see one 1-byte access per active element, nothing else */
static void test_x86_evex_bw_masked_bytes(void)
{
    /* VMOVDQU8 zmm1{k1}{z}, [rsi] / VMOVDQU8 [rsi]{k1}, zmm1 */
    static const char ld[] = "\x62\xf1\x7f\xc9\x6f\x0e";
    static const char st[] = "\x62\xf1\x7f\x49\x7f\x0e";
    /* VPMOVWB [rsi]{k1}, zmm2 (32 bytes) */
    static const char wb[] = "\x62\xf2\x7e\x49\x30\x16";
    const uint64_t end = EV_DATA + 0x4000;
    uint8_t m[64], z[64], buf[64];
    uint16_t w[32];
    EvCtx c;
    EvMemLog log;
    uc_hook h;
    uint64_t k;
    int i, ok;

    ev_open(&c, UC_MODE_64, EV_ALL);
    for (i = 0; i < 64; i++) {
        m[i] = (uint8_t)(0x40 + i);
    }
    OK(uc_mem_write(c.uc, EV_DATA + 0x200, m, sizeof(m)));
    ev_set(&c, UC_X86_REG_RSI, EV_DATA + 0x200);
    k = 0x8000000100000005ULL;              /* bytes 0, 2, 32, 63 */
    OK(uc_reg_write(c.uc, UC_X86_REG_K1, &k));
    memset(&log, 0, sizeof(log));
    OK(uc_hook_add(c.uc, &h, UC_HOOK_MEM_READ, ev_mem_cb, &log, 1, 0));
    TEST_CHECK(ev_run(&c, ld, 6) == -1);
    TEST_CHECK(log.n == 4);
    TEST_MSG("reads: %d", log.n);
    TEST_CHECK(log.addr[0] == EV_DATA + 0x200 && log.size[0] == 1);
    TEST_CHECK(log.addr[1] == EV_DATA + 0x202 && log.size[1] == 1);
    TEST_CHECK(log.addr[2] == EV_DATA + 0x220 && log.size[2] == 1);
    TEST_CHECK(log.addr[3] == EV_DATA + 0x23f && log.size[3] == 1);
    OK(uc_hook_del(c.uc, h));
    OK(uc_reg_read(c.uc, UC_X86_REG_ZMM1, z));
    for (ok = 1, i = 0; i < 64; i++) {
        ok &= z[i] == (((k >> i) & 1) ? m[i] : 0);
    }
    TEST_CHECK(ok);
    /* store: 4 one-byte writes */
    memset(&log, 0, sizeof(log));
    OK(uc_hook_add(c.uc, &h, UC_HOOK_MEM_WRITE, ev_mem_cb, &log, 1, 0));
    ev_set(&c, UC_X86_REG_RSI, EV_DATA + 0x300);
    TEST_CHECK(ev_run(&c, st, 6) == -1);
    TEST_CHECK(log.n == 4 && log.size[0] == 1 && log.addr[3] == EV_DATA + 0x33f);
    TEST_MSG("writes: %d", log.n);
    OK(uc_hook_del(c.uc, h));
    /* fault suppression per byte: the last mapped byte is active, the next is not */
    ev_set(&c, UC_X86_REG_RSI, end - 32);
    k = 1ULL << 31;
    OK(uc_reg_write(c.uc, UC_X86_REG_K1, &k));
    TEST_CHECK(ev_run(&c, ld, 6) == -1);
    TEST_CHECK(ev_run(&c, st, 6) == -1);
    k = 1ULL << 32;
    OK(uc_reg_write(c.uc, UC_X86_REG_K1, &k));
    TEST_CHECK(ev_run(&c, ld, 6) == -2 - (int)UC_ERR_READ_UNMAPPED);
    /* narrowing store: byte j of the 32-byte destination under k1[j] */
    for (i = 0; i < 32; i++) {
        w[i] = (uint16_t)(0x1100 + i);
    }
    OK(uc_reg_write(c.uc, UC_X86_REG_ZMM2, w));
    memset(buf, 0xee, sizeof(buf));
    OK(uc_mem_write(c.uc, end - 16, buf, 16));
    ev_set(&c, UC_X86_REG_RSI, end - 16);
    k = 0xffff;
    OK(uc_reg_write(c.uc, UC_X86_REG_K1, &k));
    TEST_CHECK(ev_run(&c, wb, 6) == -1);
    OK(uc_mem_read(c.uc, end - 16, buf, 16));
    for (ok = 1, i = 0; i < 16; i++) {
        ok &= buf[i] == (uint8_t)i;
    }
    TEST_CHECK(ok);
    k = 0x10000;                            /* byte 16 lies on the unmapped page */
    OK(uc_reg_write(c.uc, UC_X86_REG_K1, &k));
    TEST_CHECK(ev_run(&c, wb, 6) == -2 - (int)UC_ERR_WRITE_UNMAPPED);
    OK(uc_close(c.uc));
}

/* 64 byte compares into a 64-bit opmask; KMOVQ / KORTESTQ see all 64 bits */
static void test_x86_evex_bw_kmask64(void)
{
    /* VPCMPEQB k2, zmm1, zmm2; KMOVQ rax, k2; VPMOVM2B zmm3, k2 */
    static const char code[] = "\x62\xf1\x75\x48\x74\xd2" "\xc4\xe1\xfb\x93\xc2"
                               "\x62\xf2\x7e\x48\x28\xda";
    uint8_t a[64], b[64], z[64];
    uint64_t want = 0, k;
    EvCtx c;
    int i, ok;

    for (i = 0; i < 64; i++) {
        a[i] = (uint8_t)(i * 7);
        b[i] = (i % 3) ? a[i] : (uint8_t)~a[i];
        if (a[i] == b[i]) {
            want |= 1ULL << i;
        }
    }
    ev_open(&c, UC_MODE_64, EV_ALL);
    OK(uc_reg_write(c.uc, UC_X86_REG_ZMM1, a));
    OK(uc_reg_write(c.uc, UC_X86_REG_ZMM2, b));
    TEST_CHECK(ev_run(&c, code, sizeof(code) - 1) == -1);
    TEST_CHECK(ev_get(&c, UC_X86_REG_RAX) == want);
    OK(uc_reg_read(c.uc, UC_X86_REG_K2, &k));
    TEST_CHECK(k == want);
    OK(uc_reg_read(c.uc, UC_X86_REG_ZMM3, z));
    for (ok = 1, i = 0; i < 64; i++) {
        ok &= z[i] == (((want >> i) & 1) ? 0xff : 0);
    }
    TEST_CHECK(ok);
    OK(uc_close(c.uc));
}

/*
 * ---- NoVmp U290-U296: AVX512DQ (milestone M3) ----
 * CPUID gating of the AVX512DQ EVEX forms (UC_X86_AVX512_DQ; AVX512VL for EVEX.128/256,
 * not for the LIG scalar forms) and value checks of VPMULLQ, VFPCLASSPD and VRANGESD.
 * Results in detail: Emulator/data/cases_evex_m3_dq.txt (ref_evex_m3_dq.py, SDM model).
 */
#define DQ_VPMULLQ_Z    "\x62\xf2\xed\x48\x40\xcb"          /* vpmullq zmm1, zmm2, zmm3 */
#define DQ_VPMULLD_Z    "\x62\xf2\x6d\x48\x40\xcb"          /* vpmulld zmm1, zmm2, zmm3 */
#define DQ_VPMULLQ_X    "\x62\xf2\xed\x08\x40\xcb"          /* vpmullq xmm1, xmm2, xmm3 */
#define DQ_VANDPS_Z     "\x62\xf1\x6c\x48\x54\xcb"          /* vandps zmm1, zmm2, zmm3 */
#define DQ_VFPCLASSPS_Z "\x62\xf3\x7d\x48\x66\xcb\x81"      /* vfpclassps k1, zmm3, 0x81 */
#define DQ_VFPCLASSPS_X "\x62\xf3\x7d\x08\x66\xcb\x81"      /* vfpclassps k1, xmm3, 0x81 */
#define DQ_VFPCLASSPD_Z "\x62\xf3\xfd\x48\x66\xcb\x81"      /* vfpclasspd k1, zmm3, 0x81 */
#define DQ_VFPCLASSSS   "\x62\xf3\x7d\x08\x67\xcb\x81"      /* vfpclassss k1, xmm3, 0x81 */
#define DQ_VRANGEPS_Z   "\x62\xf3\x6d\x48\x50\xcb\x05"      /* vrangeps zmm1, zmm2, zmm3, 5 */
#define DQ_VREDUCEPS_Z  "\x62\xf3\x7d\x48\x56\xcb\x00"      /* vreduceps zmm1, zmm3, 0 */
#define DQ_VRANGESD     "\x62\xf3\xed\x08\x51\xcb\x05"      /* vrangesd xmm1, xmm2, xmm3, 5 */

/* run one snippet on a fresh engine with the given UC_X86_AVX512_* features */
static int dq_run1(int features, const char *code, size_t len)
{
    EvCtx c;
    int r;

    ev_open(&c, UC_MODE_64, features);
    r = ev_run(&c, code, len);
    OK(uc_close(c.uc));
    return r;
}

static void test_x86_avx512dq_gating(void)
{
    const int fvl = UC_X86_AVX512_F | UC_X86_AVX512_VL;
    const int fdq = UC_X86_AVX512_F | UC_X86_AVX512_DQ;

    /* no AVX512DQ: every DQ form #UD; VPMULLD (same opcode, W0, AVX512F) runs */
    TEST_CHECK(dq_run1(fvl, DQ_VPMULLQ_Z, 6) == 6);
    TEST_CHECK(dq_run1(fvl, DQ_VPMULLD_Z, 6) == -1);
    TEST_CHECK(dq_run1(fvl, DQ_VANDPS_Z, 6) == 6);
    TEST_CHECK(dq_run1(fvl, DQ_VFPCLASSPS_Z, 7) == 6);
    TEST_CHECK(dq_run1(fvl, DQ_VFPCLASSSS, 7) == 6);
    TEST_CHECK(dq_run1(fvl, DQ_VRANGEPS_Z, 7) == 6);
    TEST_CHECK(dq_run1(fvl, DQ_VREDUCEPS_Z, 7) == 6);
    TEST_CHECK(dq_run1(fvl, DQ_VRANGESD, 7) == 6);
    /* AVX512DQ without AVX512VL: EVEX.512 and the scalar (LIG) forms only */
    TEST_CHECK(dq_run1(fdq, DQ_VPMULLQ_Z, 6) == -1);
    TEST_CHECK(dq_run1(fdq, DQ_VPMULLQ_X, 6) == 6);
    TEST_CHECK(dq_run1(fdq, DQ_VFPCLASSPS_X, 7) == 6);
    TEST_CHECK(dq_run1(fdq, DQ_VFPCLASSSS, 7) == -1);
    TEST_CHECK(dq_run1(fdq, DQ_VRANGESD, 7) == -1);
    /* everything */
    TEST_CHECK(dq_run1(EV_ALL, DQ_VPMULLQ_X, 6) == -1);
    TEST_CHECK(dq_run1(EV_ALL, DQ_VFPCLASSPS_X, 7) == -1);
    TEST_CHECK(dq_run1(EV_ALL, DQ_VANDPS_Z, 6) == -1);
}

static void test_x86_avx512dq_values(void)
{
    uint64_t a[8], b[8], r[8], k = 0;
    EvCtx c;
    int i, ok = 1;

    ev_open(&c, UC_MODE_64, EV_ALL);
    /* VPMULLQ: low 64 bits of every product */
    for (i = 0; i < 8; i++) {
        a[i] = 0x9E3779B97F4A7C15ULL * (uint64_t)(i + 1);
        b[i] = 0xC2B2AE3D27D4EB4FULL ^ ((uint64_t)i << 60);
    }
    OK(uc_reg_write(c.uc, UC_X86_REG_ZMM2, a));
    OK(uc_reg_write(c.uc, UC_X86_REG_ZMM3, b));
    TEST_CHECK(ev_run(&c, DQ_VPMULLQ_Z, 6) == -1);
    OK(uc_reg_read(c.uc, UC_X86_REG_ZMM1, r));
    for (i = 0; i < 8; i++) {
        ok &= r[i] == a[i] * b[i];
    }
    TEST_CHECK(ok);
    /* VFPCLASSPD k1, zmm3, 0x81: QNaN or SNaN -> lanes 0, 1, 4 */
    memset(b, 0, sizeof(b));
    b[0] = 0x7FF8000000000000ULL;       /* QNaN */
    b[1] = 0x7FF4000000000000ULL;       /* SNaN */
    b[2] = 0x3FF0000000000000ULL;       /* 1.0 */
    b[3] = 0xFFF0000000000000ULL;       /* -INF */
    b[4] = 0xFFF8000000000001ULL;       /* negative QNaN */
    b[5] = 0x0000000000000001ULL;       /* denormal */
    OK(uc_reg_write(c.uc, UC_X86_REG_ZMM3, b));
    TEST_CHECK(ev_run(&c, DQ_VFPCLASSPD_Z, 7) == -1);
    OK(uc_reg_read(c.uc, UC_X86_REG_K1, &k));
    TEST_CHECK(k == 0x13);
    TEST_MSG("k1 = %llx", (unsigned long long)k);
    /* VRANGESD xmm1, xmm2, xmm3, 5: MAX(-3.0, 2.0) = 2.0, bits 127:64 from xmm2, 511:128 = 0 */
    memset(a, 0xA5, sizeof(a));
    a[0] = 0xC008000000000000ULL;
    a[1] = 0x1122334455667788ULL;
    b[0] = 0x4000000000000000ULL;
    OK(uc_reg_write(c.uc, UC_X86_REG_ZMM2, a));
    OK(uc_reg_write(c.uc, UC_X86_REG_ZMM3, b));
    TEST_CHECK(ev_run(&c, DQ_VRANGESD, 7) == -1);
    OK(uc_reg_read(c.uc, UC_X86_REG_ZMM1, r));
    TEST_CHECK(r[0] == 0x4000000000000000ULL && r[1] == 0x1122334455667788ULL);
    ok = 1;
    for (i = 2; i < 8; i++) {
        ok &= r[i] == 0;
    }
    TEST_CHECK(ok);
    OK(uc_close(c.uc));
}

/*
 * ---- NoVmp U320-U329: AVX512CD and further UC_CTL_X86_AVX512 feature bits ----
 * Mask read-back, CPUID.(EAX=7,ECX=0) bits, strict CPUID profiles hiding / showing a bit.
 * Instruction results: Emulator/data/cases_evex_m3_cd.txt (ref_evex_m3_cd.py).
 */
#define CDX_DATA 0x200000
#define CDX_BASE (UC_X86_AVX512_F | UC_X86_AVX512_DQ | UC_X86_AVX512_BW | UC_X86_AVX512_VL)
#define CDX_EBX_CD (1u << 28)

typedef struct CdxCtx {
    uc_engine *uc;
    X86IntrCapture cap;
    uc_hook hook;
    uint64_t pc;
} CdxCtx;

static void cdx_open(CdxCtx *c, int avx512, const uc_x86_cpuid *prof, size_t nprof, int strict)
{
    memset(c, 0, sizeof(*c));
    c->pc = code_start;
    OK(uc_open(UC_ARCH_X86, UC_MODE_64, &c->uc));
    OK(uc_ctl_set_cpu_model(c->uc, UC_CPU_X86_MAX));
    if (avx512) {
        OK(uc_ctl_set_x86_avx512(c->uc, avx512));
    }
    if (nprof) {
        uint64_t xcr0 = 0xe7;
        OK(uc_ctl_set_x86_cpuid(c->uc, prof, nprof));
        OK(uc_reg_write(c->uc, UC_X86_REG_XCR0, &xcr0));
        /* explicit (U435: a profile alone is strict), so strict 0 stays non-strict */
        OK(uc_ctl_set_x86_cpuid_strict(c->uc, strict));
    }
    OK(uc_mem_map(c->uc, code_start, code_len, UC_PROT_ALL));
    OK(uc_mem_map(c->uc, CDX_DATA, 0x4000, UC_PROT_ALL));
    OK(uc_hook_add(c->uc, &c->hook, UC_HOOK_INTR, test_x86_intr_capture_cb, &c->cap, 1, 0));
}

/* one snippet from a fresh address: the vector (6 #UD) or -1 */
static int cdx_run(CdxCtx *c, const char *code, size_t len)
{
    uint64_t pc = c->pc;
    uc_err err;

    c->pc += 0x40;
    TEST_CHECK(len <= 0x40 && c->pc <= code_start + code_len);
    c->cap.count = 0;
    OK(uc_mem_write(c->uc, pc, code, len));
    err = uc_emu_start(c->uc, pc, pc + len, 0, 0);
    if (err == UC_ERR_INSN_INVALID) {
        return 6;
    }
    OK(err);
    return c->cap.count ? (int)c->cap.intno : -1;
}

static void cdx_cpuid7(CdxCtx *c, uint32_t r[4])
{
    uint64_t v = 7;
    OK(uc_reg_write(c->uc, UC_X86_REG_RAX, &v));
    v = 0;
    OK(uc_reg_write(c->uc, UC_X86_REG_RCX, &v));
    TEST_CHECK(cdx_run(c, "\x0f\xa2", 2) == -1);
    OK(uc_reg_read(c->uc, UC_X86_REG_EAX, &r[0]));
    OK(uc_reg_read(c->uc, UC_X86_REG_EBX, &r[1]));
    OK(uc_reg_read(c->uc, UC_X86_REG_ECX, &r[2]));
    OK(uc_reg_read(c->uc, UC_X86_REG_EDX, &r[3]));
}

/* U320: UC_X86_AVX512_CD (16): read-back, CPUID.(7,0):EBX.AVX512CD[28], default off */
static void test_x86_avx512cd_optin(void)
{
    static const struct {
        int set, get, cd;
    } m[] = {
        {0, 0, 0},
        {UC_X86_AVX512_F, UC_X86_AVX512_F, 0},
        {CDX_BASE, CDX_BASE, 0},
        {UC_X86_AVX512_CD, UC_X86_AVX512_F | UC_X86_AVX512_CD, 1},
        {CDX_BASE | UC_X86_AVX512_CD, CDX_BASE | UC_X86_AVX512_CD, 1},
    };
    /* a profile without AVX512CD (CPUID.7.0:EBX = AVX512F|DQ|BW|VL ...) */
    static const uc_x86_cpuid prof_nocd[] = {
        {0x0, 0, 0xd, 0x756e6547, 0x6c65746e, 0x49656e69},
        {0x1, 0, 0x906a0, 0, 0x1c000000, 0x06000000},
        {0x7, 0, 0, 0xc0030020, 0, 0},
        {0xd, 0, 0xe7, 0xa80, 0xa80, 0},
    };
    CdxCtx c;
    uint32_t r[4];
    size_t i;
    int on = -1, strict;

    for (i = 0; i < sizeof(m) / sizeof(m[0]); i++) {
        cdx_open(&c, m[i].set, NULL, 0, 0);
        OK(uc_ctl_get_x86_avx512(c.uc, &on));
        TEST_CHECK_(on == m[i].get, "mask %d: read back %d", m[i].set, on);
        cdx_cpuid7(&c, r);
        TEST_CHECK_(!!(r[1] & CDX_EBX_CD) == m[i].cd, "mask %d: CPUID.7.0:EBX = %08x",
                    m[i].set, r[1]);
        OK(uc_close(c.uc));
    }
    /* the default (UC_CPU_X86_MAX, no opt-in) has no AVX512CD */
    OK(uc_open(UC_ARCH_X86, UC_MODE_64, &c.uc));
    OK(uc_ctl_get_x86_avx512(c.uc, &on));
    TEST_CHECK(on == 0);
    OK(uc_close(c.uc));
    /* a CPUID profile without the bit hides it from CPUID, strict or not */
    for (strict = 0; strict < 2; strict++) {
        cdx_open(&c, CDX_BASE | UC_X86_AVX512_CD, prof_nocd, 4, strict);
        cdx_cpuid7(&c, r);
        TEST_CHECK_(r[1] == 0xc0030020, "profile, strict %d: CPUID.7.0:EBX = %08x", strict, r[1]);
        OK(uc_close(c.uc));
    }
}

/* VPLZCNTD zmm1, zmm2 (EVEX.512.66.0F38.W0 44 /r): runs and gives the leading-zero counts */
#define CDX_VPLZCNTD_512 "\x62\xf2\x7d\x48\x44\xca"
#define CDX_VPLZCNTD_256 "\x62\xf2\x7d\x28\x44\xca"
/* VPBROADCASTMW2D zmm1, k2 (EVEX.512.F3.0F38.W0 3A /r) */
#define CDX_VPBCSTMW2D_512 "\x62\xf2\x7e\x48\x3a\xca"

static int cdx_lzcnt_ok(CdxCtx *c)
{
    uint32_t z[16], r[16];
    int i;

    for (i = 0; i < 16; i++) {
        z[i] = i == 0 ? 0 : 0x80000000u >> (i * 2 - 1);
    }
    OK(uc_reg_write(c->uc, UC_X86_REG_ZMM2, z));
    if (cdx_run(c, CDX_VPLZCNTD_512, 6) != -1) {
        return 0;
    }
    OK(uc_reg_read(c->uc, UC_X86_REG_ZMM1, r));
    for (i = 0; i < 16; i++) {
        if (r[i] != (i == 0 ? 32u : (uint32_t)(i * 2 - 1))) {
            return 0;
        }
    }
    return 1;
}

/*
 * U321: AVX512CD gating (SDM Vol2C CPUID columns "AVX512CD" / "(AVX512VL AND AVX512CD)"):
 * the mask bit, AVX512VL for EVEX.256, a strict CPUID profile hiding / showing AVX512CD;
 * VPBROADCASTMW2D with the register operand only.
 */
static void test_x86_avx512cd_gating(void)
{
    static const uc_x86_cpuid prof_nocd[] = {
        {0x0, 0, 0xd, 0x756e6547, 0x6c65746e, 0x49656e69},
        {0x1, 0, 0x906a0, 0, 0x1c000000, 0x06000000},
        {0x7, 0, 0, 0xc0030020, 0, 0},
        {0xd, 0, 0xe7, 0xa80, 0xa80, 0},
    };
    static const uc_x86_cpuid prof_cd[] = {
        {0x0, 0, 0xd, 0x756e6547, 0x6c65746e, 0x49656e69},
        {0x1, 0, 0x906a0, 0, 0x1c000000, 0x06000000},
        {0x7, 0, 0, 0xd0030020, 0, 0},
        {0xd, 0, 0xe7, 0xa80, 0xa80, 0},
    };
    CdxCtx c;
    uint32_t z[16];
    uint64_t k;
    int i, ok;

    /* without UC_X86_AVX512_CD: #UD */
    cdx_open(&c, CDX_BASE, NULL, 0, 0);
    TEST_CHECK(cdx_run(&c, CDX_VPLZCNTD_512, 6) == 6);
    TEST_CHECK(cdx_run(&c, CDX_VPBCSTMW2D_512, 6) == 6);
    OK(uc_close(c.uc));
    /* with it */
    cdx_open(&c, CDX_BASE | UC_X86_AVX512_CD, NULL, 0, 0);
    TEST_CHECK(cdx_lzcnt_ok(&c));
    TEST_CHECK(cdx_run(&c, CDX_VPLZCNTD_256, 6) == -1);
    k = 0x123456789abcdef0ULL;
    OK(uc_reg_write(c.uc, UC_X86_REG_K2, &k));
    TEST_CHECK(cdx_run(&c, CDX_VPBCSTMW2D_512, 6) == -1);
    OK(uc_reg_read(c.uc, UC_X86_REG_ZMM1, z));
    for (ok = 1, i = 0; i < 16; i++) {
        ok &= z[i] == 0xdef0;
    }
    TEST_CHECK(ok);
    /* register-only: VPBROADCASTMW2D zmm1, [rsi] (mod = 00b) #UD; with aaa = 001b #UD */
    TEST_CHECK(cdx_run(&c, "\x62\xf2\x7e\x48\x3a\x0e", 6) == 6);
    TEST_CHECK(cdx_run(&c, "\x62\xf2\x7e\x49\x3a\xca", 6) == 6);
    OK(uc_close(c.uc));
    /* AVX512CD without AVX512VL: EVEX.512 runs, EVEX.256 #UD */
    cdx_open(&c, UC_X86_AVX512_F | UC_X86_AVX512_CD, NULL, 0, 0);
    TEST_CHECK(cdx_lzcnt_ok(&c));
    TEST_CHECK(cdx_run(&c, CDX_VPLZCNTD_256, 6) == 6);
    OK(uc_close(c.uc));
    /* a profile without AVX512CD: hidden (#UD) only when strict */
    cdx_open(&c, CDX_BASE | UC_X86_AVX512_CD, prof_nocd, 4, 0);
    TEST_CHECK(cdx_lzcnt_ok(&c));
    OK(uc_close(c.uc));
    cdx_open(&c, CDX_BASE | UC_X86_AVX512_CD, prof_nocd, 4, 1);
    TEST_CHECK(cdx_run(&c, CDX_VPLZCNTD_512, 6) == 6);
    OK(uc_close(c.uc));
    /* a strict profile with AVX512CD shows it */
    cdx_open(&c, CDX_BASE | UC_X86_AVX512_CD, prof_cd, 4, 1);
    TEST_CHECK(cdx_lzcnt_ok(&c));
    TEST_CHECK(cdx_run(&c, CDX_VPLZCNTD_256, 6) == -1);
    OK(uc_close(c.uc));
    /* ... but not without the opt-in: the profile bit alone does not add the instructions */
    cdx_open(&c, CDX_BASE, prof_cd, 4, 1);
    TEST_CHECK(cdx_run(&c, CDX_VPLZCNTD_512, 6) == 6);
    OK(uc_close(c.uc));
}

/*
 * U322-U325: further UC_CTL_X86_AVX512 feature bits: read-back, the CPUID.(7,0) bit only
 * with the opt-in, and a first instruction of the extension #UD without / running with it.
 */
static const struct {
    int bit;
    int reg;            /* CPUID.(7,0) register: 1 = EBX, 2 = ECX */
    uint32_t cpuid;
    const char *code;   /* an EVEX.512 form of the extension (zmm registers only) */
} cdx_bits[] = {
    /* U322 AVX512_IFMA: VPMADD52LUQ zmm1, zmm2, zmm3 */
    {UC_X86_AVX512_IFMA, 1, 1u << 21, "\x62\xf2\xed\x48\xb4\xcb"},
    /* U323 AVX512_VPOPCNTDQ: VPOPCNTD zmm1, zmm3 */
    {UC_X86_AVX512_VPOPCNTDQ, 2, 1u << 14, "\x62\xf2\x7d\x48\x55\xcb"},
    /* U324 AVX512_BITALG: VPOPCNTB zmm1, zmm3 */
    {UC_X86_AVX512_BITALG, 2, 1u << 12, "\x62\xf2\x7d\x48\x54\xcb"},
    /* U325 AVX512_VBMI: VPERMB zmm1, zmm2, zmm3 */
    {UC_X86_AVX512_VBMI, 2, 1u << 1, "\x62\xf2\x6d\x48\x8d\xcb"},
};

static void test_x86_avx512_m4_bits(void)
{
    CdxCtx c;
    uint32_t r[4];
    size_t i;
    int on;

    for (i = 0; i < sizeof(cdx_bits) / sizeof(cdx_bits[0]); i++) {
        cdx_open(&c, CDX_BASE, NULL, 0, 0);
        cdx_cpuid7(&c, r);
        TEST_CHECK_(!(r[cdx_bits[i].reg] & cdx_bits[i].cpuid), "bit %d off: %08x", cdx_bits[i].bit,
                    r[cdx_bits[i].reg]);
        TEST_CHECK_(cdx_run(&c, cdx_bits[i].code, 6) == 6, "bit %d off: #UD", cdx_bits[i].bit);
        OK(uc_close(c.uc));
        cdx_open(&c, CDX_BASE | cdx_bits[i].bit, NULL, 0, 0);
        OK(uc_ctl_get_x86_avx512(c.uc, &on));
        TEST_CHECK(on == (CDX_BASE | cdx_bits[i].bit));
        cdx_cpuid7(&c, r);
        TEST_CHECK_((r[cdx_bits[i].reg] & cdx_bits[i].cpuid) != 0, "bit %d on: %08x",
                    cdx_bits[i].bit, r[cdx_bits[i].reg]);
        TEST_CHECK_(cdx_run(&c, cdx_bits[i].code, 6) == -1, "bit %d on: runs", cdx_bits[i].bit);
        OK(uc_close(c.uc));
        /* alone: implies AVX512F, EVEX.512 needs no AVX512VL */
        cdx_open(&c, cdx_bits[i].bit, NULL, 0, 0);
        OK(uc_ctl_get_x86_avx512(c.uc, &on));
        TEST_CHECK(on == (UC_X86_AVX512_F | cdx_bits[i].bit));
        TEST_CHECK_(cdx_run(&c, cdx_bits[i].code, 6) == -1, "bit %d alone: runs", cdx_bits[i].bit);
        OK(uc_close(c.uc));
    }
}

/*
 * ---- NoVmp U230-U241: EVEX milestone M2 conversions / FP specials (prefix cv_) ----
 * Engine paths around them (U230: opmask per destination element when source and
 * destination element sizes differ, destination narrower than VL, scalar merge under a
 * mask; masked 16-bit stores; EVEX.W of GPR forms outside 64-bit mode; #XM). Values are
 * covered by Emulator/data/cases_evex_m2_cvt.txt (ref_evex_m2_cvt.py, independent model).
 */
static uint32_t cv_f32(float v)
{
    uint32_t u;
    memcpy(&u, &v, 4);
    return u;
}

static uint64_t cv_f64(double v)
{
    uint64_t u;
    memcpy(&u, &v, 8);
    return u;
}

/* VCVTPD2PS ymm1{k1}{z}, zmm2: one opmask bit per dword result, bits 511:256 zeroed */
static void test_x86_evex_cvt_narrow(void)
{
    static const char merge[] = "\x62\xf1\xfd\x49\x5a\xca";
    static const char zero[] = "\x62\xf1\xfd\xc9\x5a\xca";
    uint64_t src[8], k = 0xA5;
    uint32_t init[16], z[16];
    EvCtx c;
    int i, pass, ok;

    for (pass = 0; pass < 2; pass++) {
        ev_open(&c, UC_MODE_64, EV_ALL);
        for (i = 0; i < 8; i++) {
            src[i] = cv_f64((double)(i + 1));
        }
        for (i = 0; i < 16; i++) {
            init[i] = 0x11111111u;
        }
        OK(uc_reg_write(c.uc, UC_X86_REG_ZMM2, src));
        OK(uc_reg_write(c.uc, UC_X86_REG_ZMM1, init));
        OK(uc_reg_write(c.uc, UC_X86_REG_K1, &k));
        TEST_CHECK(ev_run(&c, pass ? zero : merge, 6) == -1);
        ev_get_zmm(&c, 1, z);
        ok = 1;
        for (i = 0; i < 16; i++) {
            uint32_t want = i >= 8 ? 0 : ((k >> i) & 1) ? cv_f32((float)(i + 1))
                                                        : (pass ? 0 : 0x11111111u);
            ok &= z[i] == want;
        }
        TEST_CHECK(ok);
        TEST_MSG("pass %d: z[0..3] = %08x %08x %08x %08x z[8] = %08x", pass, z[0], z[1], z[2],
                 z[3], z[8]);
        OK(uc_close(c.uc));
    }
}

/* VCVTPS2PH [rax]{k1}, zmm2, 0: 16-bit elements stored per mask bit, fault suppression */
static void test_x86_evex_cvt_ph_store(void)
{
    static const char code[] = "\x62\xf3\x7d\x49\x1d\x10\x00";
    static const uint16_t half[8] = { 0x3C00, 0x4000, 0x4200, 0x4400, 0x4500, 0x4600, 0x4700,
                                      0x4800 };
    uint64_t base = EV_DATA + 0x4000 - 16, k;
    uint32_t src[16];
    uint16_t m[8], fill[8];
    EvCtx c;
    int i, r;

    ev_open(&c, UC_MODE_64, EV_ALL);
    for (i = 0; i < 16; i++) {
        src[i] = cv_f32((float)(i + 1));
    }
    for (i = 0; i < 8; i++) {
        fill[i] = 0xEEEE;
    }
    OK(uc_reg_write(c.uc, UC_X86_REG_ZMM2, src));
    ev_set(&c, UC_X86_REG_RAX, base);
    /* elements 0..7 mapped, 8..15 on the unmapped page: masked off, no fault */
    OK(uc_mem_write(c.uc, base, fill, sizeof(fill)));
    k = 0xB5;
    OK(uc_reg_write(c.uc, UC_X86_REG_K1, &k));
    TEST_CHECK(ev_run(&c, code, 7) == -1);
    OK(uc_mem_read(c.uc, base, m, sizeof(m)));
    for (i = 0; i < 8; i++) {
        TEST_CHECK(m[i] == (((k >> i) & 1) ? half[i] : 0xEEEE));
        TEST_MSG("word %d = %04x", i, m[i]);
    }
    /* an active element on the unmapped page: fault, nothing written */
    OK(uc_mem_write(c.uc, base, fill, sizeof(fill)));
    k = 0x1FF;
    OK(uc_reg_write(c.uc, UC_X86_REG_K1, &k));
    r = ev_run(&c, code, 7);
    TEST_CHECK(r <= -2);
    TEST_MSG("run = %d", r);
    OK(uc_mem_read(c.uc, base, m, sizeof(m)));
    TEST_CHECK(memcmp(m, fill, sizeof(m)) == 0);
    OK(uc_close(c.uc));
}

/* VCVTSS2SD xmm1{k1}{z}, xmm2, xmm3: bits 127:64 from xmm2 also under the mask */
static void test_x86_evex_cvt_scalar_merge(void)
{
    static const char code[] = "\x62\xf1\x6e\x89\x5a\xcb";
    uint64_t z[8], k;
    uint32_t s3[16];
    EvCtx c;
    int i, pass;

    for (pass = 0; pass < 2; pass++) {
        ev_open(&c, UC_MODE_64, EV_ALL);
        ev_put_zmm(&c, 1, 0x11111111);
        ev_put_zmm(&c, 2, 0x22222222);
        for (i = 0; i < 16; i++) {
            s3[i] = cv_f32(1.5f);
        }
        OK(uc_reg_write(c.uc, UC_X86_REG_ZMM3, s3));
        k = pass;
        OK(uc_reg_write(c.uc, UC_X86_REG_K1, &k));
        TEST_CHECK(ev_run(&c, code, 6) == -1);
        OK(uc_reg_read(c.uc, UC_X86_REG_ZMM1, z));
        TEST_CHECK(z[0] == (pass ? cv_f64(1.5) : 0));
        TEST_CHECK(z[1] == 0x2525252524242424ull);      /* dwords 2, 3 of ev_put_zmm(2) */
        for (i = 2; i < 8; i++) {
            TEST_CHECK(z[i] == 0);
        }
        TEST_MSG("pass %d: %016llx %016llx", pass, (unsigned long long)z[0],
                 (unsigned long long)z[1]);
        OK(uc_close(c.uc));
    }
}

/* EVEX.W of the GPR scalar conversions is ignored outside 64-bit mode */
static void test_x86_evex_cvt_gpr_w(void)
{
    static const char ss2si_w1[] = "\x62\xf1\xfe\x08\x2d\xc2";   /* vcvtss2si eax/rax, xmm2 */
    static const char si2sd_w1[] = "\x62\xf1\xef\x08\x2a\xc8";   /* vcvtsi2sd xmm1, xmm2, eax/rax */
    static const char si2sd_w1_b[] = "\x62\xf1\xef\x18\x2a\xc8"; /* the same with EVEX.b */
    uint32_t s2[16];
    uint64_t z[8];
    EvCtx c;
    int i, mode, ax_reg;

    for (i = 0; i < 16; i++) {
        s2[i] = cv_f32(1099511627776.0f);                      /* 2^40 */
    }
    for (mode = 0; mode < 2; mode++) {
        ev_open(&c, mode ? UC_MODE_64 : UC_MODE_32, EV_ALL);
        OK(uc_reg_write(c.uc, UC_X86_REG_ZMM2, s2));
        ax_reg = mode ? UC_X86_REG_RAX : UC_X86_REG_EAX;
        ev_set(&c, ax_reg, 0x1234);
        TEST_CHECK(ev_run(&c, ss2si_w1, 6) == -1);
        /* 32-bit: r32, 2^40 out of range -> integer indefinite; 64-bit: r64 = 2^40 */
        TEST_CHECK(ev_get(&c, ax_reg) == (mode ? 0x10000000000ull : 0x80000000ull));
        TEST_MSG("mode %d: eax = %llx", mode, (unsigned long long)ev_get(&c, ax_reg));
        ev_set(&c, ax_reg, mode ? 0xFFFFFFFF00000005ull : 0xFFFFFFFBu);
        TEST_CHECK(ev_run(&c, si2sd_w1, 6) == -1);
        OK(uc_reg_read(c.uc, UC_X86_REG_ZMM1, z));
        TEST_CHECK(z[0] == (mode ? cv_f64(-4294967291.0) : cv_f64(-5.0)));
        /* EVEX.b: {er} only for the W1 (r/m64) form, which does not exist outside 64-bit mode */
        TEST_CHECK(ev_run(&c, si2sd_w1_b, 6) == (mode ? -1 : 6));
        OK(uc_close(c.uc));
    }
}

/* VCVTPS2DQ zmm1, zmm2 with MXCSR.IM = 0: #XM for an active SNaN lane, none when masked off */
static void test_x86_evex_cvt_xm(void)
{
    static const char plain[] = "\x62\xf1\x7d\x48\x5b\xca";
    static const char masked[] = "\x62\xf1\x7d\x49\x5b\xca";    /* {k1} */
    uint32_t s2[16], z[16], mxcsr = 0x1F00, mx = 0;
    uint64_t k = ~(1ull << 3);
    EvCtx c;
    int i;

    ev_open(&c, UC_MODE_64, EV_ALL);
    for (i = 0; i < 16; i++) {
        s2[i] = cv_f32((float)i);
    }
    s2[3] = 0x7FA00000u;                                         /* SNaN */
    OK(uc_reg_write(c.uc, UC_X86_REG_ZMM2, s2));
    ev_put_zmm(&c, 1, 0x11111111);
    OK(uc_reg_write(c.uc, UC_X86_REG_MXCSR, &mxcsr));
    TEST_CHECK(ev_run(&c, plain, 6) == 19);
    ev_get_zmm(&c, 1, z);
    TEST_CHECK(z[0] == 0x11111111u && z[15] == 0x11111111u + 15 * 0x01010101u);
    OK(uc_reg_read(c.uc, UC_X86_REG_MXCSR, &mx));
    TEST_CHECK((mx & 0x3F) == 1);
    TEST_MSG("mxcsr = %x", mx);
    OK(uc_reg_write(c.uc, UC_X86_REG_MXCSR, &mxcsr));
    OK(uc_reg_write(c.uc, UC_X86_REG_K1, &k));
    TEST_CHECK(ev_run(&c, masked, 6) == -1);
    ev_get_zmm(&c, 1, z);
    TEST_CHECK(z[2] == 2 && z[3] == 0x11111111u + 3 * 0x01010101u && z[15] == 15);
    OK(uc_reg_read(c.uc, UC_X86_REG_MXCSR, &mx));
    TEST_CHECK((mx & 0x3F) == 0);
    OK(uc_close(c.uc));
}

/* ---- U440-U442: F16C VCVTPS2PH FTZ / underflow / precision (prefix hc_) ---- */
typedef struct {
    int count;
    uint32_t intno;
} hc_intr_t;

static void hc_hook_intr(uc_engine *uc, uint32_t intno, void *user_data)
{
    hc_intr_t *r = (hc_intr_t *)user_data;

    if (r->count++ == 0) {
        r->intno = intno;
    }
    uc_emu_stop(uc);
}

/*
 * vcvtps2ph xmm0, xmm1, imm8 (VEX.128.66.0F3A.W0 1D C8 ib) with lane 0 = v,
 * lanes 1..3 = 1.0, XMM0 preloaded with 0xA5 bytes. Returns the fault vector
 * (-1 = none); *lo = XMM0 bits 63:0, *mx = MXCSR afterwards. Without a
 * fault MXCSR comes from a following stmxcsr [rip+0xf3] (= code_start +
 * 0x100): the API register read does not fold the pending softfloat flags.
 */
static int hc_run(uint32_t v, uint8_t imm, uint32_t mxcsr, uint64_t *lo, uint32_t *mx)
{
    uint8_t code[13] = {0xc4, 0xe3, 0x79, 0x1d, 0xc8, 0,
                        0x0f, 0xae, 0x1d, 0xf3, 0x00, 0x00, 0x00};
    uint32_t src[4] = {v, 0x3f800000, 0x3f800000, 0x3f800000};
    uint8_t junk[16];
    uint64_t x[2];
    uint64_t cr4, xcr0 = 7;
    hc_intr_t intr = {0, 0};
    uc_engine *uc;
    uc_hook h;
    uc_err e;

    code[5] = imm;
    memset(junk, 0xa5, sizeof(junk));
    OK(uc_open(UC_ARCH_X86, UC_MODE_64, &uc));
    OK(uc_ctl_set_cpu_model(uc, UC_CPU_X86_MAX));
    OK(uc_mem_map(uc, code_start, code_len, UC_PROT_ALL));
    OK(uc_mem_write(uc, code_start, code, sizeof(code)));
    OK(uc_hook_add(uc, &h, UC_HOOK_INTR, hc_hook_intr, &intr, 1, 0));
    OK(uc_reg_read(uc, UC_X86_REG_CR4, &cr4));
    cr4 |= 1ULL << 18; /* OSXSAVE */
    OK(uc_reg_write(uc, UC_X86_REG_CR4, &cr4));
    OK(uc_reg_write(uc, UC_X86_REG_XCR0, &xcr0));
    OK(uc_reg_write(uc, UC_X86_REG_MXCSR, &mxcsr));
    OK(uc_reg_write(uc, UC_X86_REG_XMM1, src));
    OK(uc_reg_write(uc, UC_X86_REG_XMM0, junk));
    e = uc_emu_start(uc, code_start, code_start + sizeof(code), 0, 0);
    OK(e);
    OK(uc_reg_read(uc, UC_X86_REG_XMM0, x));
    if (intr.count) {
        OK(uc_reg_read(uc, UC_X86_REG_MXCSR, mx));
    } else {
        OK(uc_mem_read(uc, code_start + 0x100, mx, 4));
    }
    *lo = x[0];
    OK(uc_close(uc));
    return intr.count ? (int)intr.intno : -1;
}

static void test_x86_f16c_vcvtps2ph_ftz(void)
{
    static const struct {
        const char *name;
        uint32_t v;
        uint8_t imm;
        uint32_t mxcsr_in;
        int fault;          /* -1 none, 19 = #XM */
        uint16_t lane0;     /* result lane 0 when no fault */
        uint32_t mxcsr_out;
    } t[] = {
        /* U440: FTZ ignored, tiny results stay FP16 denormals */
        {"2^-20 exact, FTZ", 0x35800000, 0, 0x9f80, -1, 0x0010, 0x9f80},
        {"2^-25+ulp RNE, FTZ", 0x33000001, 0, 0x9f80, -1, 0x0001, 0x9fb0},
        {"2^-25+ulp RNE, FTZ via MXCSR.RC", 0x33000001, 4, 0x9f80, -1, 0x0001, 0x9fb0},
        {"-(2^-20+x) up, FTZ", 0xb5801000, 2, 0x9f80, -1, 0x8010, 0x9fb0},
        {"fp32 denormal up, FTZ", 0x00000001, 2, 0x9f80, -1, 0x0001, 0x9fb2},
        {"fp32 denormal up, FTZ+DAZ", 0x00000001, 2, 0x9fc0, -1, 0x0000, 0x9fc0},
        {"below 2^-14 RNE (not tiny after rounding)", 0x387fffff, 0, 0x9f80, -1, 0x0400, 0x9fa0},
        /* U441: unmasked #U on an exact tiny result */
        {"2^-20 exact, UM=0", 0x35800000, 0, 0x1780, 19, 0, 0x1790},
        {"2^-24 exact, UM=0, FTZ", 0x33800000, 0, 0x9780, 19, 0, 0x9790},
        {"2^-20 exact, UM=1", 0x35800000, 0, 0x1f80, -1, 0x0010, 0x1f80},
        /* U442: unmasked #O/#U: PE from the unbounded-exponent rounding */
        {"1.5*2^-24 tie, UM=0", 0x33c00000, 0, 0x1780, 19, 0, 0x1790},
        {"2^-25+ulp, UM=0", 0x33000001, 0, 0x1780, 19, 0, 0x17b0},
        {"fp32 denormal, UM=0", 0x00000001, 0, 0x1780, 19, 0, 0x17b2},
        {"65536, OM=0", 0x47800000, 0, 0x1b80, 19, 0, 0x1b88},
        {"65520, OM=0", 0x477ff000, 0, 0x1b80, 19, 0, 0x1ba8},
        {"65536, OM=1", 0x47800000, 0, 0x1f80, -1, 0x7c00, 0x1fa8},
    };
    size_t i;

    for (i = 0; i < sizeof(t) / sizeof(t[0]); i++) {
        uint64_t lo;
        uint32_t mx;
        int f = hc_run(t[i].v, t[i].imm, t[i].mxcsr_in, &lo, &mx);

        TEST_CHECK(f == t[i].fault);
        TEST_MSG("%s: fault %d, expected %d", t[i].name, f, t[i].fault);
        TEST_CHECK(mx == t[i].mxcsr_out);
        TEST_MSG("%s: mxcsr %04x, expected %04x", t[i].name, mx, t[i].mxcsr_out);
        if (t[i].fault < 0) {
            uint64_t want = 0x3c003c003c000000ULL | t[i].lane0;
            TEST_CHECK(lo == want);
            TEST_MSG("%s: xmm0 %016llx, expected %016llx", t[i].name,
                     (unsigned long long)lo, (unsigned long long)want);
        } else {
            TEST_CHECK(lo == 0xa5a5a5a5a5a5a5a5ULL);
            TEST_MSG("%s: destination written on #XM: %016llx", t[i].name,
                     (unsigned long long)lo);
        }
    }
}
/* ---- end U440-U442 (hc_) ---- */
/* ---- qk_ block begin (NoVmp U98/U99/U430-U439, U531-U539: SDM results where the i5-13600K deviates) ---- */
/*
 * NoVmp U539: the SDM result of every behaviour where the i5-13600K deviates from the
 * SDM (docs/quirks.md; the former UC_CTL_X86_HW_QUIRKS bits, removed in U531-U538).
 * Each qk_ case runs one snippet on a fresh engine.
 * Data at QK_DATA (qwords): 2.0, -1.0, 1.0, -2.0 (doubles), FCW 037Eh (IM = 0),
 * QNaN; RAX = QK_DATA.
 */
#define QK_DATA 0x200000

static uc_engine *qk_run(const char *code, size_t len, uc_err want)
{
    static const uint64_t data[6] = {0x4000000000000000ULL, 0xBFF0000000000000ULL,
                                     0x3FF0000000000000ULL, 0xC000000000000000ULL,
                                     0x037E, 0x7FF8000000000001ULL};
    uint64_t rax = QK_DATA;
    uc_engine *uc;

    OK(uc_open(UC_ARCH_X86, UC_MODE_64, &uc));
    OK(uc_mem_map(uc, code_start, code_len, UC_PROT_ALL));
    OK(uc_mem_write(uc, code_start, code, len));
    OK(uc_mem_map(uc, QK_DATA, 0x1000, UC_PROT_READ | UC_PROT_WRITE));
    OK(uc_mem_write(uc, QK_DATA, data, sizeof(data)));
    OK(uc_reg_write(uc, UC_X86_REG_RAX, &rax));
    uc_assert_err(want, uc_emu_start(uc, code_start, code_start + len, 0, 0));
    return uc;
}

static uint16_t qk_fsw(uc_engine *uc)
{
    uint16_t fsw = 0;
    OK(uc_reg_read(uc, UC_X86_REG_FPSW, &fsw));
    return fsw;
}

/* bit 0: fninit; fld [rax] (2.0); fld [rax+8] (-1.0); fxam (C1 = 1); fcomi st0, st1 */
static void qk_fcomi(int want_c1)
{
    static const char code[] = "\xdb\xe3\xdd\x00\xdd\x40\x08\xd9\xe5\xdb\xf1";
    uc_engine *uc = qk_run(code, sizeof(code) - 1, UC_ERR_OK);
    uint16_t fsw = qk_fsw(uc);
    TEST_CHECK(((fsw >> 9) & 1) == want_c1);
    TEST_MSG("fsw %04x, C1 want %d", fsw, want_c1);
    OK(uc_close(uc));
}

/* bit 1: fninit; fld1 (TOP 7); cvtpi2ps xmm0, qword [rax] (SDM: x87 -> MMX, TOP 0) */
static void qk_cvtpi2ps(int want_top)
{
    static const char code[] = "\xdb\xe3\xd9\xe8\x0f\x2a\x00";
    uc_engine *uc = qk_run(code, sizeof(code) - 1, UC_ERR_OK);
    uint16_t fsw = qk_fsw(uc);
    TEST_CHECK(((fsw >> 11) & 7) == want_top);
    TEST_MSG("fsw %04x, TOP want %d", fsw, want_top);
    OK(uc_close(uc));
}

/* bit 2: fninit; fld [rax+16] (y = 1.0) or fldz; fld [rax+24] (x = -2.0); fyl2xp1 */
static void qk_fyl2xp1(int yzero, uint16_t want_sexp,
                       uint64_t want_mant, uint16_t want_flag)
{
    static const char code1[] = "\xdb\xe3\xdd\x40\x10\xdd\x40\x18\xd9\xf9";
    static const char code0[] = "\xdb\xe3\xd9\xee\x90\xdd\x40\x18\xd9\xf9";
    uc_engine *uc =
        qk_run(yzero ? code0 : code1, sizeof(code1) - 1, UC_ERR_OK);
    uint8_t st0[10];
    uint64_t mant;
    uint16_t sexp, fsw = qk_fsw(uc);
    OK(uc_reg_read(uc, UC_X86_REG_ST0, st0));
    memcpy(&mant, st0, 8);
    memcpy(&sexp, st0 + 8, 2);
    TEST_CHECK(sexp == want_sexp && mant == want_mant);
    TEST_CHECK(want_flag == 0 ? (fsw & 0x3F) == 0 : (fsw & want_flag) != 0);
    TEST_MSG("st0 %04x:%016llx fsw %04x", sexp,
             (unsigned long long)mant, fsw);
    OK(uc_close(uc));
}

/* bit 3: ptwrite dword [rax] (CPUID.14.0:EBX[4] = 0) */
static void qk_ptwrite(uc_err want)
{
    static const char code[] = "\xf3\x0f\xae\x20";
    OK(uc_close(qk_run(code, sizeof(code) - 1, want)));
}

/* bit 4: dppd xmm0, xmm1, 0x33 with two NaN products (xmm0 = QNaN a0, QNaN a1) */
static void qk_dppd(uint64_t want1)
{
    static const char code[] = "\x66\x0f\x3a\x41\xc1\x33";
    uint64_t x0[2] = {0x7FF8000000000A01ULL, 0x7FF8000000000A02ULL};
    uint64_t x1[2] = {0x3FF0000000000000ULL, 0x3FF0000000000000ULL};
    uint64_t r[2];
    uc_engine *uc;

    OK(uc_open(UC_ARCH_X86, UC_MODE_64, &uc));
    OK(uc_mem_map(uc, code_start, code_len, UC_PROT_ALL));
    OK(uc_mem_write(uc, code_start, code, sizeof(code) - 1));
    OK(uc_reg_write(uc, UC_X86_REG_XMM0, x0));
    OK(uc_reg_write(uc, UC_X86_REG_XMM1, x1));
    OK(uc_emu_start(uc, code_start, code_start + sizeof(code) - 1, 0, 0));
    OK(uc_reg_read(uc, UC_X86_REG_XMM0, r));
    TEST_CHECK(r[0] == x0[0] && r[1] == want1);
    TEST_MSG("xmm0 %016llx %016llx", (unsigned long long)r[0],
             (unsigned long long)r[1]);
    OK(uc_close(uc));
}

/* bit 5: rep movsb with 67h, ECX = 0, upper register halves set */
static void qk_rep_zero(int zx)
{
    static const char code[] = "\x67\xf3\xa4";
    uint64_t rcx = 0xFFFFFFFF00000000ULL, rsi = 0x1111111100003000ULL;
    uint64_t rdi = 0x2222222200004000ULL;
    uc_engine *uc;

    OK(uc_open(UC_ARCH_X86, UC_MODE_64, &uc));
    OK(uc_mem_map(uc, code_start, code_len, UC_PROT_ALL));
    OK(uc_mem_write(uc, code_start, code, sizeof(code) - 1));
    OK(uc_reg_write(uc, UC_X86_REG_RCX, &rcx));
    OK(uc_reg_write(uc, UC_X86_REG_RSI, &rsi));
    OK(uc_reg_write(uc, UC_X86_REG_RDI, &rdi));
    OK(uc_emu_start(uc, code_start, code_start + sizeof(code) - 1, 0, 0));
    OK(uc_reg_read(uc, UC_X86_REG_RCX, &rcx));
    OK(uc_reg_read(uc, UC_X86_REG_RSI, &rsi));
    OK(uc_reg_read(uc, UC_X86_REG_RDI, &rdi));
    if (zx) {
        TEST_CHECK(rcx == 0 && rsi == 0x3000 && rdi == 0x4000);
    } else {
        TEST_CHECK(rcx == 0xFFFFFFFF00000000ULL && rsi == 0x1111111100003000ULL &&
                   rdi == 0x2222222200004000ULL);
    }
    TEST_MSG("rcx %llx rsi %llx rdi %llx", (unsigned long long)rcx,
             (unsigned long long)rsi, (unsigned long long)rdi);
    OK(uc_close(uc));
}

/*
 * bit 6: fninit; fldcw [rax+32] (IM = 0); fld [rax+16] (1.0); fld [rax+40] (QNaN);
 * then fcom st(1) / fcomi st(0), st(1) / ftst. #IA unmasked.
 */
static void qk_x87_cmp(char insn, uint16_t want_cc, uint64_t want_zpc)
{
    char code[] = "\xdb\xe3\xd9\x68\x20\xdd\x40\x10\xdd\x40\x28\x90\x90";
    uint64_t rflags = 0;
    uint16_t fsw;
    uc_engine *uc;

    if (insn == 'c') {          /* fcom st(1) */
        code[11] = '\xd8', code[12] = '\xd1';
    } else if (insn == 'i') {   /* fcomi st(0), st(1) */
        code[11] = '\xdb', code[12] = '\xf1';
    } else {                    /* ftst */
        code[11] = '\xd9', code[12] = '\xe4';
    }
    uc = qk_run(code, sizeof(code) - 1, UC_ERR_OK);
    fsw = qk_fsw(uc);
    OK(uc_reg_read(uc, UC_X86_REG_EFLAGS, &rflags));
    TEST_CHECK((fsw & 0x0001) != 0);
    TEST_CHECK((fsw & 0x4500) == want_cc);
    TEST_CHECK((rflags & 0x45) == want_zpc);
    TEST_MSG("insn %c: fsw %04x rflags %llx", insn, fsw,
             (unsigned long long)rflags);
    OK(uc_close(uc));
}

/*
 * U433 (not a quirk): the fork's x87 always behaves as FDP_EXCPTN_ONLY and
 * ZERO_FCS_FDS (U64), so CPUID.(EAX=07H,ECX=0):EBX[6] and [13] are reported.
 */
static void qk_cpuid_fdp(void)
{
    static const char code[] = "\xb8\x07\x00\x00\x00\x31\xc9\x0f\xa2";
    uint64_t rbx = 0;
    uc_engine *uc = qk_run(code, sizeof(code) - 1, UC_ERR_OK);
    OK(uc_reg_read(uc, UC_X86_REG_RBX, &rbx));
    TEST_CHECK((rbx & ((1u << 6) | (1u << 13))) == ((1u << 6) | (1u << 13)));
    TEST_MSG("CPUID.7.0:EBX %llx", (unsigned long long)rbx);
    OK(uc_close(uc));
}

static void test_x86_sdm_documented_deviations(void)
{
    /* FCOMI/FUCOMI C1 (U531, quirk removed): SDM C1 = 0 (the i5-13600K keeps C1 = 1,
       docs/quirks.md) */
    qk_fcomi(0);
    /* CVTPI2PS m64 x87 transition (U532, quirk removed): SDM TOP = 0 (the i5-13600K
       keeps TOP = 7, docs/quirks.md) */
    qk_cvtpi2ps(0);
    /* FYL2XP1 below -1 (U533, quirk removed): SDM #IA (masked: indefinite, IE), also
       with y = +0 (U432); the i5-13600K gives ST0 = x with PE / -0 (docs/quirks.md) */
    qk_fyl2xp1(0, 0xFFFF, 0xC000000000000000ULL, 0x0001);
    qk_fyl2xp1(1, 0xFFFF, 0xC000000000000000ULL, 0x0001);
    /* PTWRITE without PT (U534, quirk removed): SDM #UD (the i5-13600K reads the
       operand and goes on, docs/quirks.md) */
    qk_ptwrite(UC_ERR_INSN_INVALID);
    /* DPPD two NaN products (U535, quirk removed): SDM p0 + p1 in both elements (the
       i5-13600K gives p1 + p0 in element 1, docs/quirks.md) */
    qk_dppd(0x7FF8000000000A01ULL);
    /* REP 67h ECX=0 zero-extension (U536, quirk removed): SDM writes nothing (the
       i5-13600K zero-extends RCX/RSI/RDI, docs/quirks.md) */
    qk_rep_zero(0);
    /* x87 compare unmasked #IA condition codes (U537, quirk removed): SDM keeps C3/C2/C0
       (ZF/PF/CF) (the i5-13600K sets 111, docs/quirks.md) */
    qk_x87_cmp('c', 0x0000, 0);
    qk_x87_cmp('i', 0x0000, 0);
    /* FTST has no IM condition in the SDM: "unordered" */
    qk_x87_cmp('t', 0x4500, 0);
    qk_cpuid_fdp();
}
/* ---- qk_ block end ---- */

/* ===== NoVmp U330-U369 (AVX512-FP16) unit tests: helper prefix fh_ ===== */
#define FH_DATA 0x200000
#define FH_ALL (UC_X86_AVX512_F | UC_X86_AVX512_DQ | UC_X86_AVX512_BW | UC_X86_AVX512_VL | \
                UC_X86_AVX512_FP16)
#define FH_CPUID_7_0_EDX_AVX512_FP16 (1U << 23)
#define FH_CPUID_7_0_EBX_AVX512BW (1U << 30)

typedef struct FhCtx {
    uc_engine *uc;
    X86IntrCapture cap;
    uc_hook hook;
    uint64_t pc;
} FhCtx;

static void fh_open(FhCtx *c, uc_mode mode, int avx512)
{
    memset(c, 0, sizeof(*c));
    c->pc = code_start;
    OK(uc_open(UC_ARCH_X86, mode, &c->uc));
    OK(uc_ctl_set_cpu_model(c->uc, UC_CPU_X86_MAX));
    if (avx512) {
        OK(uc_ctl_set_x86_avx512(c->uc, avx512));
    }
    OK(uc_mem_map(c->uc, code_start, code_len, UC_PROT_ALL));
    OK(uc_mem_map(c->uc, FH_DATA, 0x4000, UC_PROT_ALL));
    OK(uc_hook_add(c->uc, &c->hook, UC_HOOK_INTR, test_x86_intr_capture_cb, &c->cap, 1, 0));
}

/* run one snippet from a fresh address: the exception vector (6 #UD, 19 #XM) or -1 */
static int fh_run(FhCtx *c, const char *code, size_t len)
{
    uint64_t pc = c->pc;
    uc_err err;

    c->pc += 0x40;
    TEST_CHECK(len <= 0x40 && c->pc <= code_start + code_len);
    c->cap.count = 0;
    OK(uc_mem_write(c->uc, pc, code, len));
    err = uc_emu_start(c->uc, pc, pc + len, 0, 0);
    if (err == UC_ERR_INSN_INVALID) {
        return 6;
    }
    if (err != UC_ERR_OK) {
        return -2 - (int)err;
    }
    return c->cap.count ? (int)c->cap.intno : -1;
}

static void fh_cpuid7(FhCtx *c, uint32_t r[4])
{
    uint64_t v;
    v = 7;
    OK(uc_reg_write(c->uc, UC_X86_REG_RAX, &v));
    v = 0;
    OK(uc_reg_write(c->uc, UC_X86_REG_RCX, &v));
    TEST_CHECK(fh_run(c, "\x0f\xa2", 2) == -1);
    OK(uc_reg_read(c->uc, UC_X86_REG_RAX, &v));
    r[0] = (uint32_t)v;
    OK(uc_reg_read(c->uc, UC_X86_REG_RBX, &v));
    r[1] = (uint32_t)v;
    OK(uc_reg_read(c->uc, UC_X86_REG_RCX, &v));
    r[2] = (uint32_t)v;
    OK(uc_reg_read(c->uc, UC_X86_REG_RDX, &v));
    r[3] = (uint32_t)v;
}

/* U330: UC_X86_AVX512_FP16 (0x200) opt-in: CPUID.7.0:EDX[23], implies BW (and F); default off */
static void test_x86_fp16_optin(void)
{
    uc_engine *uc;
    uint32_t r[4];
    int on = -1;
    FhCtx c;

    OK(uc_open(UC_ARCH_X86, UC_MODE_64, &uc));
    OK(uc_ctl_set_x86_avx512(uc, UC_X86_AVX512_FP16));
    OK(uc_ctl_get_x86_avx512(uc, &on));
    TEST_CHECK(on == (UC_X86_AVX512_FP16 | UC_X86_AVX512_BW | UC_X86_AVX512_F));
    TEST_MSG("mask %d", on);
    uc_assert_err(UC_ERR_ARG, uc_ctl_set_x86_avx512(uc, 0x400));
    OK(uc_close(uc));

    /* default (no opt-in) and AVX-512 without FP16: no AVX512_FP16 bit */
    fh_open(&c, UC_MODE_64, 0);
    fh_cpuid7(&c, r);
    TEST_CHECK((r[3] & FH_CPUID_7_0_EDX_AVX512_FP16) == 0);
    OK(uc_close(c.uc));
    fh_open(&c, UC_MODE_64, FH_ALL & ~UC_X86_AVX512_FP16);
    fh_cpuid7(&c, r);
    TEST_CHECK((r[3] & FH_CPUID_7_0_EDX_AVX512_FP16) == 0);
    OK(uc_close(c.uc));
    /* FP16 alone: FP16 and BW */
    fh_open(&c, UC_MODE_64, UC_X86_AVX512_FP16);
    fh_cpuid7(&c, r);
    TEST_CHECK((r[3] & FH_CPUID_7_0_EDX_AVX512_FP16) != 0);
    TEST_CHECK((r[1] & FH_CPUID_7_0_EBX_AVX512BW) != 0);
    TEST_MSG("CPUID.7.0: EBX %08x EDX %08x", r[1], r[3]);
    OK(uc_close(c.uc));
}
static void fh_put(FhCtx *c, int n, uint16_t v)
{
    uint16_t z[32];
    int i;
    for (i = 0; i < 32; i++) {
        z[i] = v;
    }
    OK(uc_reg_write(c->uc, UC_X86_REG_ZMM0 + n, z));
}

static int fh_all(FhCtx *c, int n, uint16_t v)
{
    uint16_t z[32];
    int i;
    OK(uc_reg_read(c->uc, UC_X86_REG_ZMM0 + n, z));
    for (i = 0; i < 32; i++) {
        if (z[i] != v) {
            return 0;
        }
    }
    return 1;
}

/* U333: map 5 decodes only with UC_X86_AVX512_FP16; VADDPH in 64- and 32-bit mode */
static void test_x86_fp16_gating(void)
{
    /* VADDPH zmm1, zmm2, zmm3 */
    static const char vaddph[] = "\x62\xf5\x6c\x48\x58\xcb";
    FhCtx c;

    fh_open(&c, UC_MODE_64, FH_ALL & ~UC_X86_AVX512_FP16);
    TEST_CHECK(fh_run(&c, vaddph, 6) == 6);
    OK(uc_close(c.uc));
    fh_open(&c, UC_MODE_64, FH_ALL);
    fh_put(&c, 2, 0x3c00);                              /* 1.0 */
    fh_put(&c, 3, 0x4000);                              /* 2.0 */
    TEST_CHECK(fh_run(&c, vaddph, 6) == -1);
    TEST_CHECK(fh_all(&c, 1, 0x4200));                  /* 3.0 */
    OK(uc_close(c.uc));
    fh_open(&c, UC_MODE_32, FH_ALL);
    fh_put(&c, 2, 0xbc00);                              /* -1.0 */
    fh_put(&c, 3, 0x3c00);
    fh_put(&c, 1, 0x1234);
    TEST_CHECK(fh_run(&c, vaddph, 6) == -1);
    TEST_CHECK(fh_all(&c, 1, 0x0000));                  /* +0.0 (RNE) */
    OK(uc_close(c.uc));
}

/* U331/U333: VDIVPH by zero with MXCSR.ZM = 0: #XM, ZMM1 unchanged, ZE set */
static void test_x86_fp16_xm(void)
{
    static const char vdivph[] = "\x62\xf5\x6c\x48\x5e\xcb";
    uint32_t mxcsr = 0x1f80 & ~0x200;
    FhCtx c;

    fh_open(&c, UC_MODE_64, FH_ALL);
    fh_put(&c, 1, 0x5555);
    fh_put(&c, 2, 0x3c00);
    fh_put(&c, 3, 0x0000);
    OK(uc_reg_write(c.uc, UC_X86_REG_MXCSR, &mxcsr));
    TEST_CHECK(fh_run(&c, vdivph, 6) == 19);
    TEST_CHECK(fh_all(&c, 1, 0x5555));
    OK(uc_reg_read(c.uc, UC_X86_REG_MXCSR, &mxcsr));
    TEST_CHECK((mxcsr & 0x3f) == 0x04);
    TEST_MSG("mxcsr %08x", mxcsr);
    /* masked: +INF, ZE */
    mxcsr = 0x1f80;
    OK(uc_reg_write(c.uc, UC_X86_REG_MXCSR, &mxcsr));
    TEST_CHECK(fh_run(&c, vdivph, 6) == -1);
    TEST_CHECK(fh_all(&c, 1, 0x7c00));
    OK(uc_close(c.uc));
}

/* U338: VFMULCPH with the destination equal to a source is #UD; otherwise (1+2i)(3+4i) */
static void test_x86_fp16_complex(void)
{
    /* VFMULCPH zmm1, zmm1, zmm2 (#UD) and VFMULCPH zmm1, zmm2, zmm3 */
    static const char ud[] = "\x62\xf6\x76\x48\xd6\xca";
    static const char ok[] = "\x62\xf6\x6e\x48\xd6\xcb";
    uint16_t z[32];
    FhCtx c;
    int i, good = 1;

    fh_open(&c, UC_MODE_64, FH_ALL);
    TEST_CHECK(fh_run(&c, ud, 6) == 6);
    for (i = 0; i < 32; i++) {
        z[i] = (i & 1) ? 0x4000 : 0x3c00;               /* 1 + 2i */
    }
    OK(uc_reg_write(c.uc, UC_X86_REG_ZMM2, z));
    for (i = 0; i < 32; i++) {
        z[i] = (i & 1) ? 0x4400 : 0x4200;               /* 3 + 4i */
    }
    OK(uc_reg_write(c.uc, UC_X86_REG_ZMM3, z));
    TEST_CHECK(fh_run(&c, ok, 6) == -1);
    OK(uc_reg_read(c.uc, UC_X86_REG_ZMM1, z));
    for (i = 0; i < 32; i++) {
        good &= z[i] == ((i & 1) ? 0x4900 : 0xc500);    /* -5 + 10i */
    }
    TEST_CHECK(good);
    OK(uc_close(c.uc));
}

/* U335: VCVTSH2SI rax, xmm1 (W1) and VCVTSI2SH xmm1, xmm2, rax round trip */
static void test_x86_fp16_cvt_gpr(void)
{
    static const char sh2si[] = "\x62\xf5\xfe\x08\x2d\xc1";
    uint64_t rax = 0;
    FhCtx c;

    fh_open(&c, UC_MODE_64, FH_ALL);
    fh_put(&c, 1, 0xc580);                              /* -5.5 -> -6 (RNE) */
    TEST_CHECK(fh_run(&c, sh2si, 6) == -1);
    OK(uc_reg_read(c.uc, UC_X86_REG_RAX, &rax));
    TEST_CHECK(rax == (uint64_t)-6);
    TEST_MSG("rax %llx", (unsigned long long)rax);
    OK(uc_close(c.uc));
}
/* ===== end of NoVmp U330-U369 (AVX512-FP16) unit tests ===== */

/*
 * ---- NoVmp U370-U376: Intel AVX10 enumeration and gating, AVX10.2 decoding ----
 * UC_CTL_X86_AVX10 (CPUID.(7,1):EDX[19], leaf 24H, XSAVE components 5-7, reset XCR0) and the
 * AVX10.1 rule "AVX512x OR AVX10.1": AVX10 alone runs the EVEX AVX-512 forms at every vector
 * length and the VEX opmask instructions; neither -> #UD. AVX10.2 spec 361050-007 3.1.
 */
#define A10_DATA 0x200000

typedef struct A10Ctx {
    uc_engine *uc;
    X86IntrCapture cap;
    uc_hook hook;
    uint64_t pc;
} A10Ctx;

static void a10_open(A10Ctx *c, uc_mode mode, int avx512, int avx10, const uc_x86_cpuid *prof,
                     size_t nprof, int strict)
{
    memset(c, 0, sizeof(*c));
    c->pc = code_start;
    OK(uc_open(UC_ARCH_X86, mode, &c->uc));
    OK(uc_ctl_set_cpu_model(c->uc, UC_CPU_X86_MAX));
    if (avx512) {
        OK(uc_ctl_set_x86_avx512(c->uc, avx512));
    }
    if (avx10) {
        OK(uc_ctl_set_x86_avx10(c->uc, avx10));
    }
    if (nprof) {
        OK(uc_ctl_set_x86_cpuid(c->uc, prof, nprof));
        /* U435: a profile is strict by default, so "not strict" must be written explicitly */
        OK(uc_ctl_set_x86_cpuid_strict(c->uc, strict ? 1 : 0));
    } else if (strict) {
        OK(uc_ctl_set_x86_cpuid_strict(c->uc, 1));
    }
    OK(uc_mem_map(c->uc, code_start, code_len, UC_PROT_ALL));
    OK(uc_mem_map(c->uc, A10_DATA, 0x4000, UC_PROT_ALL));
    OK(uc_hook_add(c->uc, &c->hook, UC_HOOK_INTR, test_x86_intr_capture_cb, &c->cap, 1, 0));
}

/* run one snippet from a fresh address: the exception vector (6 #UD, 13 #GP), or -1 */
static int a10_run(A10Ctx *c, const char *code, size_t len)
{
    uint64_t pc = c->pc;
    uc_err err;

    c->pc += 0x40;
    TEST_CHECK(len <= 0x40 && c->pc <= code_start + code_len);
    c->cap.count = 0;
    OK(uc_mem_write(c->uc, pc, code, len));
    err = uc_emu_start(c->uc, pc, pc + len, 0, 0);
    if (err == UC_ERR_INSN_INVALID) {
        return 6;
    }
    TEST_CHECK(err == UC_ERR_OK);
    return c->cap.count ? (int)c->cap.intno : -1;
}

static void a10_cpuid(A10Ctx *c, uint32_t leaf, uint32_t sub, uint32_t r[4])
{
    uint64_t v;

    v = leaf;
    OK(uc_reg_write(c->uc, UC_X86_REG_RAX, &v));
    v = sub;
    OK(uc_reg_write(c->uc, UC_X86_REG_RCX, &v));
    TEST_CHECK(a10_run(c, "\x0f\xa2", 2) == -1);
    OK(uc_reg_read(c->uc, UC_X86_REG_RAX, &v));
    r[0] = (uint32_t)v;
    OK(uc_reg_read(c->uc, UC_X86_REG_RBX, &v));
    r[1] = (uint32_t)v;
    OK(uc_reg_read(c->uc, UC_X86_REG_RCX, &v));
    r[2] = (uint32_t)v;
    OK(uc_reg_read(c->uc, UC_X86_REG_RDX, &v));
    r[3] = (uint32_t)v;
}

static uint64_t a10_xcr0(A10Ctx *c)
{
    uint64_t v = 0;
    OK(uc_reg_read(c->uc, UC_X86_REG_XCR0, &v));
    return v;
}

/* VPADDD zmm1/ymm1/xmm1, 2, 3 (EVEX.512/256/128.66.0F.W0 FE) and KANDW/KANDB/KANDD k1, k2, k3 */
#define A10_VPADDD_Z "\x62\xf1\x6d\x48\xfe\xcb"
#define A10_VPADDD_Y "\x62\xf1\x6d\x28\xfe\xcb"
#define A10_VPADDD_X "\x62\xf1\x6d\x08\xfe\xcb"
#define A10_VPADDD_U0 "\x62\xf1\x69\x48\xfe\xcb"     /* EVEX.U (P1 bit 2) = 0 */
#define A10_KANDW "\xc5\xec\x41\xcb"
#define A10_KANDB "\xc5\xed\x41\xcb"
#define A10_KANDD "\xc4\xe1\xed\x41\xcb"

/* zmm1 = zmm2 + zmm3 over vl bytes, upper bytes zero (EVEX writes zero MAXVL-1:VL) */
static int a10_vpaddd_ok(A10Ctx *c, const char *code, int vl)
{
    uint32_t a[16], b[16], d[16];
    int i;

    for (i = 0; i < 16; i++) {
        a[i] = 0x10000000u + 0x01010101u * (uint32_t)i;
        b[i] = 0x00300000u + (uint32_t)i * 7u;
        d[i] = 0xdeadbeefu;
    }
    OK(uc_reg_write(c->uc, UC_X86_REG_ZMM2, a));
    OK(uc_reg_write(c->uc, UC_X86_REG_ZMM3, b));
    OK(uc_reg_write(c->uc, UC_X86_REG_ZMM1, d));
    if (a10_run(c, code, 6) != -1) {
        return 0;
    }
    OK(uc_reg_read(c->uc, UC_X86_REG_ZMM1, d));
    for (i = 0; i < 16; i++) {
        if (d[i] != (i < vl / 4 ? a[i] + b[i] : 0)) {
            return 0;
        }
    }
    return 1;
}

static void test_x86_avx10_optin(void)
{
    A10Ctx c;
    uint32_t r[4];
    int v = -1;

    /* default: off - no CPUID.(7,1):EDX.AVX10, AVX-512 state not enabled */
    a10_open(&c, UC_MODE_64, 0, 0, NULL, 0, 0);
    OK(uc_ctl_get_x86_avx10(c.uc, &v));
    TEST_CHECK(v == 0);
    a10_cpuid(&c, 7, 1, r);
    TEST_CHECK((r[3] & (1u << 19)) == 0);
    TEST_CHECK((a10_xcr0(&c) & 0xe0) == 0);
    TEST_CHECK(a10_run(&c, A10_VPADDD_Z, 6) == 6);
    uc_assert_err(UC_ERR_ARG, uc_ctl_set_x86_avx10(c.uc, UC_X86_AVX10_1)); /* after init */
    OK(uc_close(c.uc));

    /* values: version 1 or 2, optionally | V1_AUX; anything else refused */
    OK(uc_open(UC_ARCH_X86, UC_MODE_64, &c.uc));
    uc_assert_err(UC_ERR_ARG, uc_ctl_set_x86_avx10(c.uc, 3));
    uc_assert_err(UC_ERR_ARG, uc_ctl_set_x86_avx10(c.uc, UC_X86_AVX10_V1_AUX));
    uc_assert_err(UC_ERR_ARG, uc_ctl_set_x86_avx10(c.uc, 0x200 | UC_X86_AVX10_1));
    uc_assert_err(UC_ERR_ARG, uc_ctl_set_x86_avx10(c.uc, -1));
    OK(uc_ctl_set_x86_avx10(c.uc, UC_X86_AVX10_1 | UC_X86_AVX10_V1_AUX));
    OK(uc_ctl_get_x86_avx10(c.uc, &v));
    TEST_CHECK(v == (UC_X86_AVX10_1 | UC_X86_AVX10_V1_AUX));
    OK(uc_ctl_set_x86_avx10(c.uc, 0));
    OK(uc_ctl_get_x86_avx10(c.uc, &v));
    TEST_CHECK(v == 0);
    OK(uc_close(c.uc));

    /* AVX10.1: 7.1:EDX[19], 24H.0 = (0, 70001h), 24H.1 = 0, state 5-7 in 0DH and reset XCR0 */
    a10_open(&c, UC_MODE_64, 0, UC_X86_AVX10_1, NULL, 0, 0);
    a10_cpuid(&c, 0, 0, r);
    TEST_CHECK(r[0] >= 0x24);
    a10_cpuid(&c, 7, 0, r);
    TEST_CHECK(r[0] >= 1);
    TEST_CHECK((r[1] & ((1u << 16) | (1u << 31))) == 0); /* AVX512F/VL bits stay as set */
    a10_cpuid(&c, 7, 1, r);
    TEST_CHECK(r[3] & (1u << 19));
    a10_cpuid(&c, 0x24, 0, r);
    TEST_CHECK(r[0] == 0 && r[1] == 0x70001 && r[2] == 0 && r[3] == 0);
    TEST_MSG("24H.0 = %x %x %x %x", r[0], r[1], r[2], r[3]);
    a10_cpuid(&c, 0x24, 1, r);
    TEST_CHECK(r[0] == 0 && r[1] == 0 && r[2] == 0 && r[3] == 0);
    a10_cpuid(&c, 0xd, 0, r);
    TEST_CHECK((r[0] & 0xe7) == 0xe7);
    a10_cpuid(&c, 0xd, 6, r);
    TEST_CHECK(r[0] == 0x200 && r[1] == 0x480);          /* ZMM_Hi256: 200h @ 480h */
    TEST_CHECK((a10_xcr0(&c) & 0xe7) == 0xe7);
    OK(uc_close(c.uc));

    /* AVX10.2: 24H.0 = (1, 70002h), 24H.1:ECX.AVX10_V1_AUX[2] */
    a10_open(&c, UC_MODE_64, 0, UC_X86_AVX10_2, NULL, 0, 0);
    a10_cpuid(&c, 0x24, 0, r);
    TEST_CHECK(r[0] == 1 && r[1] == 0x70002 && r[2] == 0 && r[3] == 0);
    a10_cpuid(&c, 0x24, 1, r);
    TEST_CHECK(r[0] == 0 && r[1] == 0 && r[2] == 4 && r[3] == 0);
    a10_cpuid(&c, 0x24, 2, r);
    TEST_CHECK(r[0] == 0 && r[1] == 0 && r[2] == 0 && r[3] == 0);
    OK(uc_close(c.uc));

    /* AVX10.1 + V1_AUX */
    a10_open(&c, UC_MODE_64, 0, UC_X86_AVX10_1 | UC_X86_AVX10_V1_AUX, NULL, 0, 0);
    a10_cpuid(&c, 0x24, 0, r);
    TEST_CHECK(r[0] == 1 && r[1] == 0x70001);
    a10_cpuid(&c, 0x24, 1, r);
    TEST_CHECK(r[2] == 4);
    OK(uc_close(c.uc));
}

static void test_x86_avx10_gating(void)
{
    /* a strict profile with AVX10.1 (no AVX512* bits), and the same without 7.1:EDX[19] */
    static const uc_x86_cpuid prof_on[] = {
        {0x0, 0, 0x24, 0x756e6547, 0x6c65746e, 0x49656e69},
        {0x1, 0, 0x000b0671, 0, (1u << 26) | (1u << 28), 0},
        {0x7, 0, 1, 0, 0, 0},
        {0x7, 1, 0, 0, 0, 1u << 19},
        {0xd, 0, 0xe7, 0, 0, 0},
        {0x24, 0, 0, 0x70001, 0, 0},
    };
    static const uc_x86_cpuid prof_off[] = {
        {0x0, 0, 0x24, 0x756e6547, 0x6c65746e, 0x49656e69},
        {0x1, 0, 0x000b0671, 0, (1u << 26) | (1u << 28), 0},
        {0x7, 0, 1, 0, 0, 0},
        {0x7, 1, 0, 0, 0, 0},
        {0xd, 0, 0xe7, 0, 0, 0},
    };
    A10Ctx c;

    /* neither AVX-512 nor AVX10: EVEX and the opmask instructions #UD */
    a10_open(&c, UC_MODE_64, 0, 0, NULL, 0, 0);
    TEST_CHECK(a10_run(&c, A10_VPADDD_Z, 6) == 6);
    TEST_CHECK(a10_run(&c, A10_VPADDD_X, 6) == 6);
    TEST_CHECK(a10_run(&c, A10_KANDW, 4) == 6);
    OK(uc_close(c.uc));

    /* AVX-512 off, AVX10.1 on: AVX512F at 512/256/128 bits (no AVX512VL bit), opmask W/B/D */
    a10_open(&c, UC_MODE_64, 0, UC_X86_AVX10_1, NULL, 0, 0);
    TEST_CHECK(a10_vpaddd_ok(&c, A10_VPADDD_Z, 64));
    TEST_CHECK(a10_vpaddd_ok(&c, A10_VPADDD_Y, 32));
    TEST_CHECK(a10_vpaddd_ok(&c, A10_VPADDD_X, 16));
    TEST_CHECK(a10_run(&c, A10_KANDW, 4) == -1);
    TEST_CHECK(a10_run(&c, A10_KANDB, 4) == -1);       /* AVX512DQ OR AVX10.1 */
    TEST_CHECK(a10_run(&c, A10_KANDD, 5) == -1);       /* AVX512BW OR AVX10.1 */
    /* EVEX.U = 0 stays #UD (AVX10.2 rev 4.0 removed the YMM embedded-rounding use) */
    TEST_CHECK(a10_run(&c, A10_VPADDD_U0, 6) == 6);
    OK(uc_close(c.uc));

    /* AVX10.2 in 32-bit protected mode: EVEX (P0[7:6] = 11b) instead of BOUND */
    a10_open(&c, UC_MODE_32, 0, UC_X86_AVX10_2, NULL, 0, 0);
    TEST_CHECK(a10_vpaddd_ok(&c, A10_VPADDD_Z, 64));
    OK(uc_close(c.uc));

    /* AVX-512 behaviour unchanged without AVX10: AVX512F alone has no EVEX.128/256 */
    a10_open(&c, UC_MODE_64, UC_X86_AVX512_F, 0, NULL, 0, 0);
    TEST_CHECK(a10_vpaddd_ok(&c, A10_VPADDD_Z, 64));
    TEST_CHECK(a10_run(&c, A10_VPADDD_X, 6) == 6);
    TEST_CHECK(a10_run(&c, A10_KANDB, 4) == 6);        /* no AVX512DQ */
    OK(uc_close(c.uc));

    /* strict profiles: AVX10 needs 7.1:EDX[19] in the profile; then its version applies */
    a10_open(&c, UC_MODE_64, 0, UC_X86_AVX10_1, prof_on, 6, 1);
    TEST_CHECK((a10_xcr0(&c) & 0xe7) == 0xe7);
    TEST_CHECK(a10_vpaddd_ok(&c, A10_VPADDD_X, 16));
    TEST_CHECK(a10_run(&c, A10_KANDD, 5) == -1);
    OK(uc_close(c.uc));
    a10_open(&c, UC_MODE_64, 0, UC_X86_AVX10_1, prof_off, 5, 1);
    TEST_CHECK(a10_run(&c, A10_VPADDD_Z, 6) == 6);
    TEST_CHECK(a10_run(&c, A10_KANDW, 4) == 6);
    OK(uc_close(c.uc));
    /* the same profile without strict mode: only the CPUID bit is hidden */
    a10_open(&c, UC_MODE_64, 0, UC_X86_AVX10_1, prof_off, 5, 0);
    TEST_CHECK(a10_vpaddd_ok(&c, A10_VPADDD_Z, 64));
    OK(uc_close(c.uc));
}

/*
 * AVX10.2 instructions (U372-U376) need CPUID.(24H,0):EBX[7:0] >= 2: #UD with AVX10.1 or with
 * every AVX-512 bit but no AVX10; EVEX maps 5/6 decode (empty slots #UD), EVEX.W1 on a W0
 * form #UD. Values: Emulator/data/cases_avx10_a.txt (ref_avx10_a.py).
 */
#define A10_VADDBF16_Z "\x62\xf5\x6d\x48\x58\xcb"       /* VADDBF16 zmm1, zmm2, zmm3 */
#define A10_VADDBF16_W1 "\x62\xf5\xed\x48\x58\xcb"
#define A10_VMINMAXPS_Z "\x62\xf3\x6d\x48\x52\xcb\x00"  /* VMINMAXPS zmm1, zmm2, zmm3, 0 */
#define A10_VCOMXSS "\x62\xf1\x7e\x08\x2f\xcb"          /* VCOMXSS xmm1, xmm3 */
#define A10_MAP5_00 "\x62\xf5\x6d\x48\x00\xcb"          /* EVEX map 5, empty slot 00h */

static void test_x86_avx10_2_gating(void)
{
    uint16_t a[32], b[32], d[32];
    A10Ctx c;
    int i;

    a10_open(&c, UC_MODE_64, 0, UC_X86_AVX10_1, NULL, 0, 0);
    TEST_CHECK(a10_run(&c, A10_VADDBF16_Z, 6) == 6);
    TEST_CHECK(a10_run(&c, A10_VMINMAXPS_Z, 7) == 6);
    TEST_CHECK(a10_run(&c, A10_VCOMXSS, 6) == 6);
    OK(uc_close(c.uc));

    a10_open(&c, UC_MODE_64,
             UC_X86_AVX512_F | UC_X86_AVX512_DQ | UC_X86_AVX512_BW | UC_X86_AVX512_VL, 0,
             NULL, 0, 0);
    TEST_CHECK(a10_run(&c, A10_VADDBF16_Z, 6) == 6);
    OK(uc_close(c.uc));

    /* AVX10.2: 1.0 + 2.0 = 3.0 in every BF16 lane */
    a10_open(&c, UC_MODE_64, 0, UC_X86_AVX10_2, NULL, 0, 0);
    for (i = 0; i < 32; i++) {
        a[i] = 0x3f80;
        b[i] = 0x4000;
    }
    OK(uc_reg_write(c.uc, UC_X86_REG_ZMM2, a));
    OK(uc_reg_write(c.uc, UC_X86_REG_ZMM3, b));
    TEST_CHECK(a10_run(&c, A10_VADDBF16_Z, 6) == -1);
    OK(uc_reg_read(c.uc, UC_X86_REG_ZMM1, d));
    for (i = 0; i < 32; i++) {
        TEST_CHECK(d[i] == 0x4040);
    }
    TEST_CHECK(a10_run(&c, A10_VADDBF16_W1, 6) == 6);
    TEST_CHECK(a10_run(&c, A10_MAP5_00, 6) == 6);
    TEST_CHECK(a10_run(&c, A10_VMINMAXPS_Z, 7) == -1);
    TEST_CHECK(a10_run(&c, A10_VCOMXSS, 6) == -1);
    OK(uc_close(c.uc));
}
/* ---- end NoVmp U370-U376 (a10_) ---- */

/*
 * ---- NoVmp U400-U412: AVX10.2 (avx10_b) ----
 * UC_CTL_X86_AVX10 (U370, the a10_ block has the enumeration details), the AVX10.2 /
 * AVX10_V1_AUX / "AVX10 and MOVRS" / "AVX10 AND SM4" gates, EVEX map 5 routing, a few
 * result spot checks (FP8, VNNI, VMOVW) and the
 * E4 / E4NF memory behaviour. Full results: Emulator/data/cases_avx10_b.txt (ref_avx10_b.py).
 */
static void xb_open(EvCtx *c, uc_mode mode, int avx10)
{
    memset(c, 0, sizeof(*c));
    c->mode = mode;
    c->pc = code_start;
    OK(uc_open(UC_ARCH_X86, mode, &c->uc));
    OK(uc_ctl_set_cpu_model(c->uc, UC_CPU_X86_MAX));
    if (avx10 < 0) {
        OK(uc_ctl_set_x86_avx512(c->uc, EV_ALL));
    } else if (avx10 > 0) {
        OK(uc_ctl_set_x86_avx10(c->uc, avx10));
    }
    OK(uc_mem_map(c->uc, code_start, code_len, UC_PROT_ALL));
    OK(uc_mem_map(c->uc, EV_DATA, 0x4000, UC_PROT_ALL));
    OK(uc_hook_add(c->uc, &c->hook, UC_HOOK_INTR, test_x86_intr_capture_cb, &c->cap, 1, 0));
}

/* VCVT2PH2HF8 zmm1, zmm2, zmm3 / VPDPBSSD zmm1, zmm2, zmm3 / VMOVRSD zmm1, [rsi] / VMOVRSD
   zmm1, [esi] (32-bit) / VMOVW xmm1, xmm2 / VCVTHF82PH zmm1{k1}, [rsi] / VCVTPH2BF8 ymm1{k1},
   [rsi] */
#define XB_VCVT2PH2HF8 "\x62\xf5\x6f\x48\x18\xcb"
#define XB_VPDPBSSD "\x62\xf2\x6f\x48\x50\xcb"
#define XB_VMOVRSD "\x62\xf5\x7e\x48\x6f\x0e"
#define XB_VMOVW "\x62\xf5\x7e\x08\x6e\xca"
#define XB_VCVTHF82PH_M "\x62\xf5\x7f\x49\x1e\x0e"
#define XB_VCVTPH2BF8_M "\x62\xf2\x7e\x49\x74\x0e"
#define XB_VSM4KEY4 "\x62\xf2\x6e\x48\xda\xcb"

static void test_x86_avx10b_ctl(void)
{
    uc_engine *uc;
    int v = -1, mask = 0;
    EvCtx c;
    uint64_t rax, rcx, rdx;

    OK(uc_open(UC_ARCH_X86, UC_MODE_64, &uc));
    OK(uc_ctl_get_x86_avx10(uc, &v));
    TEST_CHECK(v == 0);
    TEST_CHECK(uc_ctl_set_x86_avx10(uc, 3) == UC_ERR_ARG);
    TEST_CHECK(uc_ctl_set_x86_avx10(uc, -1) == UC_ERR_ARG);
    OK(uc_ctl_set_x86_avx10(uc, 2));
    OK(uc_ctl_get_x86_avx10(uc, &v));
    TEST_CHECK(v == 2);
    /* U370: AVX10 leaves UC_CTL_X86_AVX512 (and the AVX512* CPUID bits) alone */
    OK(uc_ctl_get_x86_avx512(uc, &mask));
    TEST_CHECK(mask == 0);
    OK(uc_close(uc));

    /* CPUID.(EAX=7,ECX=1):EDX.AVX10[19] follows the opt-in; fixed once the CPU exists */
    xb_open(&c, UC_MODE_64, 2);
    ev_set(&c, UC_X86_REG_RAX, 7);
    ev_set(&c, UC_X86_REG_RCX, 1);
    TEST_CHECK(ev_run(&c, "\x0f\xa2", 2) == -1);
    rax = ev_get(&c, UC_X86_REG_RAX);
    rcx = ev_get(&c, UC_X86_REG_RCX);
    rdx = ev_get(&c, UC_X86_REG_RDX);
    TEST_CHECK((rdx >> 19) & 1);
    TEST_MSG("CPUID.7.1: eax %llx ecx %llx edx %llx", (unsigned long long)rax,
             (unsigned long long)rcx, (unsigned long long)rdx);
    TEST_CHECK(uc_ctl_set_x86_avx10(c.uc, 1) == UC_ERR_ARG);
    OK(uc_close(c.uc));
    xb_open(&c, UC_MODE_64, -1);
    ev_set(&c, UC_X86_REG_RAX, 7);
    ev_set(&c, UC_X86_REG_RCX, 1);
    TEST_CHECK(ev_run(&c, "\x0f\xa2", 2) == -1);
    TEST_CHECK(!((ev_get(&c, UC_X86_REG_RDX) >> 19) & 1));
    OK(uc_close(c.uc));
}

/* AVX10.2 forms need version 2; VMOVRS* needs AVX10 (any version) and MOVRS, 64-bit mode;
   the EVEX VSM4KEY4 needs AVX10 (any version) and SM4 */
static void test_x86_avx10b_gating(void)
{
    static const int vers[3] = { -1, 1, 2 };
    EvCtx c;
    int i;

    for (i = 0; i < 3; i++) {
        int v = vers[i];
        xb_open(&c, UC_MODE_64, v);
        ev_set(&c, UC_X86_REG_RSI, EV_DATA);
        TEST_CHECK(ev_run(&c, XB_VCVT2PH2HF8, 6) == (v == 2 ? -1 : 6));
        TEST_CHECK(ev_run(&c, XB_VPDPBSSD, 6) == (v == 2 ? -1 : 6));
        TEST_CHECK(ev_run(&c, XB_VMOVW, 6) == (v == 2 ? -1 : 6));
        TEST_CHECK(ev_run(&c, XB_VMOVRSD, 6) == (v >= 1 ? -1 : 6));
        TEST_CHECK(ev_run(&c, XB_VSM4KEY4, 6) == (v >= 1 ? -1 : 6));
        TEST_MSG("avx10 version %d", v);
        OK(uc_close(c.uc));
    }
    /* VMOVRSD is N.E. outside 64-bit mode (EVEX in 32-bit protected mode: P0[7:6] = 11b) */
    xb_open(&c, UC_MODE_32, 2);
    ev_set(&c, UC_X86_REG_ESI, EV_DATA);
    TEST_CHECK(ev_run(&c, XB_VMOVRSD, 6) == 6);
    TEST_CHECK(ev_run(&c, XB_VCVT2PH2HF8, 6) == -1);
    OK(uc_close(c.uc));
    /* empty map 5 / map 6 slots #UD; NP map 5 58 is VADDPH (AVX512-FP16, U333), which AVX10.1
       includes (U371) */
    xb_open(&c, UC_MODE_64, 2);
    TEST_CHECK(ev_run(&c, "\x62\xf6\x7c\x48\x58\xcb", 6) == 6);
    TEST_CHECK(ev_run(&c, "\x62\xf5\x7c\x48\x00\xcb", 6) == 6);
    TEST_CHECK(ev_run(&c, "\x62\xf5\x7c\x48\x58\xcb", 6) == -1);
    OK(uc_close(c.uc));
}

/* spot values: FP8 RNE / saturation (spec Table 3.5/3.6), VNNI signed bytes, VMOVW */
static void test_x86_avx10b_values(void)
{
    uint16_t a[32], b[32];
    uint8_t r[64], want[64];
    uint32_t z1[16], z2[16], z3[16];
    uint16_t w[32];
    EvCtx c;
    int i;

    xb_open(&c, UC_MODE_64, 2);
    for (i = 0; i < 32; i++) {
        a[i] = 0x3C00;                          /* 1.0 -> E4M3 38h */
        b[i] = 0x3C00;
    }
    b[0] = 0x5F40;                              /* 464: tie -> 448 = 7Eh */
    b[1] = 0x5F41;                              /* 465: overflow -> NaN 7Fh */
    b[2] = 0xFC00;                              /* -Inf -> FFh */
    b[3] = 0x1800;                              /* 2^-9 = HF8 min denormal 01h */
    b[4] = 0x8000;                              /* -0 */
    a[0] = 0x7E00;                              /* NaN -> 7Fh (upper half, byte 32) */
    OK(uc_reg_write(c.uc, UC_X86_REG_ZMM2, a));
    OK(uc_reg_write(c.uc, UC_X86_REG_ZMM3, b));
    TEST_CHECK(ev_run(&c, XB_VCVT2PH2HF8, 6) == -1);
    OK(uc_reg_read(c.uc, UC_X86_REG_ZMM1, r));
    memset(want, 0x38, 64);
    want[0] = 0x7E;
    want[1] = 0x7F;
    want[2] = 0xFF;
    want[3] = 0x01;
    want[4] = 0x80;
    want[32] = 0x7F;
    TEST_CHECK(memcmp(r, want, 64) == 0);
    /* VPDPBSSD: dword i += sum of 4 signed byte products */
    for (i = 0; i < 16; i++) {
        z1[i] = 0x7FFFFFF0u;
        z2[i] = 0x80FF7F01u;                    /* bytes 01, 7F, FF, 80 */
        z3[i] = 0x80808080u;                    /* -128 each */
    }
    OK(uc_reg_write(c.uc, UC_X86_REG_ZMM1, z1));
    OK(uc_reg_write(c.uc, UC_X86_REG_ZMM2, z2));
    OK(uc_reg_write(c.uc, UC_X86_REG_ZMM3, z3));
    TEST_CHECK(ev_run(&c, XB_VPDPBSSD, 6) == -1);
    OK(uc_reg_read(c.uc, UC_X86_REG_ZMM1, z1));
    /* (1 + 127 - 1 - 128) * -128 = 128, wraps: 7FFFFFF0h + 80h */
    TEST_CHECK(z1[0] == 0x80000070u && z1[15] == 0x80000070u);
    TEST_MSG("vpdpbssd dword 0 = %08x", z1[0]);
    /* VMOVW xmm1, xmm2: word 0, everything else of ZMM1 zero */
    for (i = 0; i < 32; i++) {
        w[i] = (uint16_t)(0x1111 * (i + 1));
    }
    OK(uc_reg_write(c.uc, UC_X86_REG_ZMM2, w));
    OK(uc_reg_write(c.uc, UC_X86_REG_ZMM1, z3));
    TEST_CHECK(ev_run(&c, XB_VMOVW, 6) == -1);
    OK(uc_reg_read(c.uc, UC_X86_REG_ZMM1, w));
    TEST_CHECK(w[0] == 0x1111);
    for (i = 1; i < 32; i++) {
        TEST_CHECK(w[i] == 0);
    }
    OK(uc_close(c.uc));
}

/* E4 (VCVTHF82PH): masked-off elements never touch memory; E4NF (VCVTPH2BF8): the whole
   operand is read whatever the mask (spec 4.2.5 / 4.2.6) */
static void test_x86_avx10b_fault_suppression(void)
{
    uint64_t k0 = 0, k1 = 0xFFFF;
    uint8_t z[64];
    EvCtx c;
    int i;

    xb_open(&c, UC_MODE_64, 2);
    OK(uc_reg_write(c.uc, UC_X86_REG_K1, &k0));
    /* zmm source of 32 bytes ends 16 bytes into the unmapped page after EV_DATA + 0x4000 */
    ev_set(&c, UC_X86_REG_RSI, EV_DATA + 0x4000 - 16);
    memset(z, 0xAB, 64);
    OK(uc_reg_write(c.uc, UC_X86_REG_ZMM1, z));
    TEST_CHECK(ev_run(&c, XB_VCVTHF82PH_M, 6) == -1);       /* k1 = 0: no access */
    OK(uc_reg_read(c.uc, UC_X86_REG_ZMM1, z));
    for (i = 0; i < 64; i++) {
        TEST_CHECK(z[i] == 0xAB);
    }
    OK(uc_reg_write(c.uc, UC_X86_REG_K1, &k1));
    TEST_CHECK(ev_run(&c, XB_VCVTHF82PH_M, 6) == -1);       /* bytes 0-15 only: mapped */
    k1 = 0x1FFFF;
    OK(uc_reg_write(c.uc, UC_X86_REG_K1, &k1));
    TEST_CHECK(ev_run(&c, XB_VCVTHF82PH_M, 6) == -2 - (int)UC_ERR_READ_UNMAPPED);
    OK(uc_close(c.uc));

    xb_open(&c, UC_MODE_64, 2);
    OK(uc_reg_write(c.uc, UC_X86_REG_K1, &k0));
    ev_set(&c, UC_X86_REG_RSI, EV_DATA + 0x4000 - 32);
    TEST_CHECK(ev_run(&c, XB_VCVTPH2BF8_M, 6) == -2 - (int)UC_ERR_READ_UNMAPPED);
    ev_set(&c, UC_X86_REG_RSI, EV_DATA + 0x4000 - 64);
    TEST_CHECK(ev_run(&c, XB_VCVTPH2BF8_M, 6) == -1);
    OK(uc_close(c.uc));
}

/* ---- U445-U447: SSE/AVX/FMA unmasked #O/#U, DPPS steps, MXCSR API (prefix sx_) ---- */
typedef struct {
    int count;
    uint32_t intno;
} sx_intr_t;

static void sx_hook_intr(uc_engine *uc, uint32_t intno, void *user_data)
{
    sx_intr_t *r = (sx_intr_t *)user_data;

    if (r->count++ == 0) {
        r->intno = intno;
    }
    uc_emu_stop(uc);
}

typedef struct {
    const char *name;
    uint8_t code[8];
    uint8_t len;
    uint32_t x0[4], x1[4], x2[4];   /* XMM0..2 inputs (lane 0 first) */
    uint32_t mxcsr_in;
    int fault;                      /* -1 none, 19 = #XM */
    uint32_t mxcsr_out;             /* uc_reg_read(UC_X86_REG_MXCSR) afterwards */
    int dst;                        /* destination register 0 or 1 */
    uint32_t lane0;                 /* destination lane 0 when no fault */
} sx_case;

/*
 * Runs one instruction with XMM0..2 preset; MXCSR afterwards comes from the API
 * register read (U447 folds the pending flags). Returns the fault vector (-1 =
 * none); *d = the destination register afterwards.
 */
static int sx_run(const sx_case *t, uint32_t d[4], uint32_t *mx)
{
    uint64_t cr4, xcr0 = 7;
    sx_intr_t intr = {0, 0};
    uc_engine *uc;
    uc_hook h;

    OK(uc_open(UC_ARCH_X86, UC_MODE_64, &uc));
    OK(uc_ctl_set_cpu_model(uc, UC_CPU_X86_MAX));
    OK(uc_mem_map(uc, code_start, code_len, UC_PROT_ALL));
    OK(uc_mem_write(uc, code_start, t->code, t->len));
    OK(uc_hook_add(uc, &h, UC_HOOK_INTR, sx_hook_intr, &intr, 1, 0));
    OK(uc_reg_read(uc, UC_X86_REG_CR4, &cr4));
    cr4 |= 1ULL << 18; /* OSXSAVE */
    OK(uc_reg_write(uc, UC_X86_REG_CR4, &cr4));
    OK(uc_reg_write(uc, UC_X86_REG_XCR0, &xcr0));
    OK(uc_reg_write(uc, UC_X86_REG_MXCSR, &t->mxcsr_in));
    OK(uc_reg_write(uc, UC_X86_REG_XMM0, t->x0));
    OK(uc_reg_write(uc, UC_X86_REG_XMM1, t->x1));
    OK(uc_reg_write(uc, UC_X86_REG_XMM2, t->x2));
    OK(uc_emu_start(uc, code_start, code_start + t->len, 0, 0));
    OK(uc_reg_read(uc, t->dst ? UC_X86_REG_XMM1 : UC_X86_REG_XMM0, d));
    OK(uc_reg_read(uc, UC_X86_REG_MXCSR, mx));
    OK(uc_close(uc));
    return intr.count ? (int)intr.intno : -1;
}

static void sx_check(const sx_case *t, size_t n)
{
    size_t i;

    for (i = 0; i < n; i++) {
        uint32_t d[4], mx;
        const uint32_t *in = t[i].dst ? t[i].x1 : t[i].x0;
        int f = sx_run(&t[i], d, &mx);

        TEST_CHECK(f == t[i].fault);
        TEST_MSG("%s: fault %d, expected %d", t[i].name, f, t[i].fault);
        TEST_CHECK(mx == t[i].mxcsr_out);
        TEST_MSG("%s: mxcsr %04x, expected %04x", t[i].name, mx, t[i].mxcsr_out);
        if (t[i].fault < 0) {
            TEST_CHECK(d[0] == t[i].lane0);
            TEST_MSG("%s: lane 0 %08x, expected %08x", t[i].name, d[0], t[i].lane0);
        } else {
            TEST_CHECK(memcmp(d, in, 16) == 0);
            TEST_MSG("%s: destination written on #XM: %08x", t[i].name, d[0]);
        }
    }
}

#define SX_MULSS  {0xf3, 0x0f, 0x59, 0xca}, 4             /* mulss xmm1, xmm2 */
#define SX_VMULSS {0xc5, 0xf2, 0x59, 0xc2}, 4             /* vmulss xmm0, xmm1, xmm2 */
#define SX_FMA231 {0xc4, 0xe2, 0x71, 0xb9, 0xc2}, 5       /* vfmadd231ss xmm0, xmm1, xmm2 */
#define SX_CVTSD2SS {0xf2, 0x0f, 0x5a, 0xca}, 4           /* cvtsd2ss xmm1, xmm2 */
#define SX_ADDSS  {0xf3, 0x0f, 0x58, 0xca}, 4             /* addss xmm1, xmm2 */
#define SX_DPPS   {0x66, 0x0f, 0x3a, 0x40, 0xca, 0xf1}, 6 /* dpps xmm1, xmm2, 0xf1 */
#define SX_ONE    0x3f800000
#define SX_J      {0xa5a5a5a5, 0xa5a5a5a5, 0xa5a5a5a5, 0xa5a5a5a5}

/* U445: unmasked #U on exact tiny results, FTZ ignored, PE from the unbounded-exponent rounding */
static void test_x86_sse_unmasked_ou(void)
{
    static const sx_case t[] = {
        {"mulss 2^-100*2^-30 UM=0", SX_MULSS, SX_J, {0x0d800000, SX_ONE}, {0x30800000, SX_ONE},
         0x1780, 19, 0x1790, 1, 0},
        {"mulss 2^-100*2^-30 UM=0 FTZ", SX_MULSS, SX_J, {0x0d800000, SX_ONE}, {0x30800000, SX_ONE},
         0x9780, 19, 0x9790, 1, 0},
        {"mulss 2^-100*2^-30 masked", SX_MULSS, SX_J, {0x0d800000, SX_ONE}, {0x30800000, SX_ONE},
         0x1f80, -1, 0x1f80, 1, 0x00080000},
        {"mulss 2^-100*2^-30 masked FTZ", SX_MULSS, SX_J, {0x0d800000, SX_ONE}, {0x30800000, SX_ONE},
         0x9f80, -1, 0x9fb0, 1, 0},
        {"mulss tiny unbounded-exact UM=0", SX_MULSS, SX_J, {0x0d800001, SX_ONE}, {0x30800000, SX_ONE},
         0x1780, 19, 0x1790, 1, 0},
        {"mulss tiny unbounded-exact masked", SX_MULSS, SX_J, {0x0d800001, SX_ONE}, {0x30800000, SX_ONE},
         0x1f80, -1, 0x1fb0, 1, 0x00080000},
        {"mulss tiny inexact UM=0", SX_MULSS, SX_J, {0x0d800001, SX_ONE}, {0x30800001, SX_ONE},
         0x1780, 19, 0x17b0, 1, 0},
        {"mulss 2^127*2 OM=0", SX_MULSS, SX_J, {0x7f000000, SX_ONE}, {0x40000000, SX_ONE},
         0x1b80, 19, 0x1b88, 1, 0},
        {"mulss MAX*MAX OM=0", SX_MULSS, SX_J, {0x7f7fffff, SX_ONE}, {0x7f7fffff, SX_ONE},
         0x1b80, 19, 0x1ba8, 1, 0},
        {"mulss 2^127*2 masked", SX_MULSS, SX_J, {0x7f000000, SX_ONE}, {0x40000000, SX_ONE},
         0x1f80, -1, 0x1fa8, 1, 0x7f800000},
        {"mulss 2^127*2 RZ OM=0", SX_MULSS, SX_J, {0x7f000000, SX_ONE}, {0x40000000, SX_ONE},
         0x7b80, 19, 0x7b88, 1, 0},
        /* tininess after rounding: (1+u)minN*(1-u) rounds to minN at RN, not at RZ */
        {"mulss rounds to minN RN", SX_MULSS, SX_J, {0x00800001, SX_ONE}, {0x3f7ffffe, SX_ONE},
         0x1f80, -1, 0x1fa0, 1, 0x00800000},
        {"mulss rounds to minN RN UM=0", SX_MULSS, SX_J, {0x00800001, SX_ONE}, {0x3f7ffffe, SX_ONE},
         0x1780, -1, 0x17a0, 1, 0x00800000},
        {"mulss tiny at RZ", SX_MULSS, SX_J, {0x00800001, SX_ONE}, {0x3f7ffffe, SX_ONE},
         0x7f80, -1, 0x7fb0, 1, 0x007fffff},
        {"vmulss 2^-100*2^-30 UM=0", SX_VMULSS, SX_J, {0x0d800000, SX_ONE}, {0x30800000, SX_ONE},
         0x1780, 19, 0x1790, 0, 0},
        {"vfmadd231ss 2^-100*2^-30+0 UM=0", SX_FMA231, {0, SX_ONE}, {0x0d800000, SX_ONE},
         {0x30800000, SX_ONE}, 0x1780, 19, 0x1790, 0, 0},
        {"vfmadd231ss MAX*2-MAX OM=0 (no overflow)", SX_FMA231, {0xff7fffff, SX_ONE},
         {0x7f7fffff, SX_ONE}, {0x40000000, SX_ONE}, 0x1b80, -1, 0x1b80, 0, 0x7f7fffff},
        {"cvtsd2ss 2^-130 UM=0", SX_CVTSD2SS, SX_J, SX_J, {0x00000000, 0x37d00000}, 0x1780,
         19, 0x1790, 1, 0},
        {"cvtsd2ss 2^128 OM=0", SX_CVTSD2SS, SX_J, SX_J, {0x00000000, 0x47f00000}, 0x1b80,
         19, 0x1b88, 1, 0},
    };

    sx_check(t, sizeof(t) / sizeof(t[0]));
}

/* U446/U538: DPPS steps in the SDM order (the i5-13600K's grouping is a documented
   deviation, docs/quirks.md "DPPS exception step grouping") */
static void test_x86_sse_dpps_steps(void)
{
    static const sx_case t[] = {
        /* Temp2 = 1.5minN - minN exact tiny (#U), Temp3 = 1 + 2^-24 inexact */
        {"dpps Temp2 tiny UM=0 (SDM)", SX_DPPS, SX_J, {0x00c00000, 0x80800000, SX_ONE, 0x33800000},
         {SX_ONE, SX_ONE, SX_ONE, SX_ONE}, 0x1780, 19, 0x1790, 1, 0},
        /* products MAX*2 and -MAX*2 overflow (#O): stop before inf - inf */
        {"dpps product overflow OM=0", SX_DPPS, SX_J, {0x7f7fffff, 0xff7fffff, 0, 0},
         {0x40000000, 0x40000000, 0, 0}, 0x1b80, 19, 0x1b88, 1, 0},
        {"dpps product overflow masked", SX_DPPS, SX_J, {0x7f7fffff, 0xff7fffff, 0, 0},
         {0x40000000, 0x40000000, 0, 0}, 0x1f80, -1, 0x1fa9, 1, 0xffc00000},
        /* tiny inexact product (masked U, P), then the add of that denormal with DM = 0 */
        {"dpps denormal intermediate DM=0", SX_DPPS, SX_J, {0x0d800001, 0, 0, 0},
         {0x30800001, 0, 0, 0}, 0x1e80, 19, 0x1eb2, 1, 0},
    };

    sx_check(t, sizeof(t) / sizeof(t[0]));
}

/* U447: uc_reg_read(MXCSR) folds pending flags; uc_reg_write(MXCSR) sets RC, FTZ, DAZ, flags */
static void test_x86_mxcsr_api(void)
{
    static const sx_case t[] = {
        {"addss 1+2^-30 PE visible", SX_ADDSS, SX_J, {SX_ONE, SX_ONE}, {0x30800000, SX_ONE},
         0x1f80, -1, 0x1fa0, 1, SX_ONE},
        {"addss 1+2^-30 RU", SX_ADDSS, SX_J, {SX_ONE, SX_ONE}, {0x30800000, SX_ONE},
         0x5f80, -1, 0x5fa0, 1, 0x3f800001},
        {"addss 1+2^-30 RD", SX_ADDSS, SX_J, {SX_ONE, SX_ONE}, {0x30800000, SX_ONE},
         0x3f80, -1, 0x3fa0, 1, SX_ONE},
        {"addss flags preset stay", SX_ADDSS, SX_J, {SX_ONE, SX_ONE}, {SX_ONE, SX_ONE},
         0x1f81, -1, 0x1f81, 1, 0x40000000},
        {"addss minD+0 DAZ", SX_ADDSS, SX_J, {0x00000001, SX_ONE}, {0, SX_ONE},
         0x1fc0, -1, 0x1fc0, 1, 0},
        {"addss minD+0 no DAZ", SX_ADDSS, SX_J, {0x00000001, SX_ONE}, {0, SX_ONE},
         0x1f80, -1, 0x1f82, 1, 0x00000001},
        {"mulss tiny FTZ", SX_MULSS, SX_J, {0x0d800001, SX_ONE}, {0x30800001, SX_ONE},
         0x9f80, -1, 0x9fb0, 1, 0},
        {"addss two QNaNs -> src1 (U96)", SX_ADDSS, SX_J, {0x7fc11111, SX_ONE}, {0x7fc22222, SX_ONE},
         0x1f80, -1, 0x1f80, 1, 0x7fc11111},
    };
    uc_engine *uc;
    uint32_t mx;

    sx_check(t, sizeof(t) / sizeof(t[0]));

    /* uc_reg_write clears the flags in sse_status too: the next read shows only new ones */
    OK(uc_open(UC_ARCH_X86, UC_MODE_64, &uc));
    OK(uc_ctl_set_cpu_model(uc, UC_CPU_X86_MAX));
    mx = 0x1fbf;
    OK(uc_reg_write(uc, UC_X86_REG_MXCSR, &mx));
    mx = 0x1f80;
    OK(uc_reg_write(uc, UC_X86_REG_MXCSR, &mx));
    mx = 0;
    OK(uc_reg_read(uc, UC_X86_REG_MXCSR, &mx));
    TEST_CHECK(mx == 0x1f80);
    TEST_MSG("MXCSR after clearing write: %04x", mx);
    OK(uc_close(uc));
}
/* ---- end U445-U447 (sx_) ---- */

/* ---- sd_ block begin (NoVmp U435-U439: strict CPUID default, DPPS NaN order bit) ---- */
/*
 * NoVmp U435: UC_CTL_X86_CPUID_STRICT defaults to ON while a UC_CTL_X86_CPUID profile is
 * installed, OFF without one; an explicit 0/1 wins in either order, a negative value
 * returns to the default. sd_prof: the i5-13600K leaves 0, 1, 7.0, 7.1, 0DH.1
 * (Emulator/data/cpuid_i5-13600k.txt) with leaf 0DH.0 widened to the AVX-512 components so
 * XCR0 = E7h is allowed: AVX512F (7.0:EBX[16]) and SHA512 (7.1:EAX[0]) stay hidden, AVX2 is
 * shown. The model is UC_CPU_X86_MAX with the AVX512F opt-in (it has both features).
 */
static const uc_x86_cpuid sd_prof[] = {
    {0x0, 0, 0x20, 0x756E6547, 0x6C65746E, 0x49656E69},
    {0x1, 0, 0x000B0671, 0x05040800, 0x7FFA3223, 0x1F8BFBFF},
    {0x7, 0, 0x2, 0x219C27EB, 0x9840078C, 0xBC004410},
    {0x7, 1, 0x810, 0, 0, 0},
    {0xd, 0, 0xe7, 0xa80, 0xa80, 0},
    {0xd, 1, 0xf, 0x350, 0x1800, 0},
};

#define sd_NOT_WRITTEN (-100)

typedef struct SdCtx {
    uc_engine *uc;
    uint64_t pc;
} SdCtx;

static void sd_open(SdCtx *c)
{
    memset(c, 0, sizeof(*c));
    c->pc = code_start;
    OK(uc_open(UC_ARCH_X86, UC_MODE_64, &c->uc));
    OK(uc_ctl_set_cpu_model(c->uc, UC_CPU_X86_MAX));
    OK(uc_ctl_set_x86_avx512(c->uc, UC_X86_AVX512_F));
    OK(uc_mem_map(c->uc, code_start, code_len, UC_PROT_ALL));
}

static void sd_profile(SdCtx *c, int on)
{
    if (on) {
        OK(uc_ctl_set_x86_cpuid(c->uc, sd_prof, sizeof(sd_prof) / sizeof(sd_prof[0])));
    } else {
        OK(uc_ctl_set_x86_cpuid(c->uc, NULL, 0));
    }
}

static void sd_strict(SdCtx *c, int v)
{
    if (v != sd_NOT_WRITTEN) {
        OK(uc_ctl_set_x86_cpuid_strict(c->uc, v));
    }
}

/* one snippet at a fresh address: 6 for #UD, -1 when it runs */
static int sd_run(SdCtx *c, const char *code, size_t len)
{
    uint64_t pc = c->pc, xcr0 = 0xe7;
    uc_err err;

    c->pc += 0x40;
    TEST_CHECK(c->pc <= code_start + code_len);
    OK(uc_reg_write(c->uc, UC_X86_REG_XCR0, &xcr0));
    OK(uc_mem_write(c->uc, pc, code, len));
    err = uc_emu_start(c->uc, pc, pc + len, 0, 0);
    if (err == UC_ERR_INSN_INVALID) {
        return 6;
    }
    OK(err);
    return -1;
}

/* reads back 'strict'; AVX2 always runs, AVX-512 / SHA512 are #UD exactly when 'hidden' */
static void sd_check(SdCtx *c, const char *what, int strict, int hidden)
{
    int on = -1, r;

    OK(uc_ctl_get_x86_cpuid_strict(c->uc, &on));
    TEST_CHECK_(on == strict, "%s: UC_CTL_X86_CPUID_STRICT reads %d, want %d", what, on, strict);
    r = sd_run(c, "\xc5\xed\xfe\xcb", 4);                 /* vpaddd ymm1, ymm2, ymm3 */
    TEST_CHECK_(r == -1, "%s: AVX2 vpaddd ymm -> %d", what, r);
    r = sd_run(c, "\x62\xf1\x6d\x48\xfe\xcb", 6);         /* vpaddd zmm1, zmm2, zmm3 */
    TEST_CHECK_(r == (hidden ? 6 : -1), "%s: AVX-512 vpaddd zmm -> %d", what, r);
    r = sd_run(c, "\xc4\xe2\x7f\xcc\xc1", 5);             /* vsha512msg1 ymm0, xmm1 */
    TEST_CHECK_(r == (hidden ? 6 : -1), "%s: SHA512 vsha512msg1 -> %d", what, r);
}

static void test_x86_cpuid_strict_default(void)
{
    /* strict written before the profile / after it; sd_NOT_WRITTEN = never written */
    static const struct {
        const char *what;
        int before, profile, after, strict, hidden;
    } t[] = {
        {"no profile, not written", sd_NOT_WRITTEN, 0, sd_NOT_WRITTEN, 0, 0},
        {"profile, not written (default on)", sd_NOT_WRITTEN, 1, sd_NOT_WRITTEN, 1, 1},
        {"0 before the profile", 0, 1, sd_NOT_WRITTEN, 0, 0},
        {"0 after the profile", sd_NOT_WRITTEN, 1, 0, 0, 0},
        {"1 before the profile", 1, 1, sd_NOT_WRITTEN, 1, 1},
        {"1 after the profile", sd_NOT_WRITTEN, 1, 1, 1, 1},
        {"1 without a profile (nothing hidden)", 1, 0, sd_NOT_WRITTEN, 1, 0},
        {"0 then -1 (default again), profile", 0, 1, -1, 1, 1},
        {"profile, 0, then -1", sd_NOT_WRITTEN, 1, -1, 1, 1},
        {"-1 without a profile", -1, 0, sd_NOT_WRITTEN, 0, 0},
    };
    SdCtx c;
    size_t i;

    for (i = 0; i < sizeof(t) / sizeof(t[0]); i++) {
        sd_open(&c);
        sd_strict(&c, t[i].before);
        if (t[i].profile) {
            sd_profile(&c, 1);
        }
        if (i == 8) {
            sd_strict(&c, 0);
        }
        sd_strict(&c, t[i].after);
        sd_check(&c, t[i].what, t[i].strict, t[i].hidden);
        OK(uc_close(c.uc));
    }

    /* not written: removing the profile turns strict off, installing it again on */
    sd_open(&c);
    sd_profile(&c, 1);
    sd_check(&c, "profile installed", 1, 1);
    sd_profile(&c, 0);
    sd_check(&c, "profile removed", 0, 0);
    sd_profile(&c, 1);
    sd_check(&c, "profile installed again", 1, 1);
    /* after init: an explicit 0 takes effect at once (translation cache flushed) ... */
    sd_strict(&c, 0);
    sd_check(&c, "explicit 0 after a run", 0, 0);
    /* ... and survives replacing / removing / reinstalling the profile */
    sd_profile(&c, 0);
    sd_check(&c, "explicit 0, profile removed", 0, 0);
    sd_profile(&c, 1);
    sd_check(&c, "explicit 0, profile reinstalled", 0, 0);
    sd_strict(&c, 1);
    sd_check(&c, "explicit 1 after a run", 1, 1);
    sd_profile(&c, 0);
    sd_check(&c, "explicit 1, profile removed", 1, 0);
    sd_strict(&c, -1);
    sd_check(&c, "-1, no profile", 0, 0);
    sd_profile(&c, 1);
    sd_check(&c, "-1, profile installed", 1, 1);
    OK(uc_close(c.uc));
}

/* ---- sd_ block end ---- */

/*
 * ---- NoVmp U475-U499 (tb2_): Tier-2/3 upstream QEMU backports, PUSHF RF/VM, LFENCE ----
 * Helpers reuse the M0 engine (m0_open/m0_run/m0_set/m0_get, data at M0_DATA).
 */
static void tb2_mem(M0 *m, uint64_t addr, void *buf, size_t len)
{
    OK(uc_mem_read(m->uc, addr, buf, len));
}

/* U476 (backport 7653b44534): SGDT/SIDT store the whole base for every operand size */
static void test_x86_bp_sgdt_sidt_base(void)
{
    /* o16 sgdt [0x200000]; o16 sidt [0x200010] (32-bit code) */
    static const char c32[] = "\x66\x0f\x01\x05\x00\x00\x20\x00"
                              "\x66\x0f\x01\x0d\x10\x00\x20\x00";
    /* o16 sgdt [0x200000]; o16 sidt [0x200010] (64-bit code, absolute disp32) */
    static const char c64[] = "\x66\x0f\x01\x04\x25\x00\x00\x20\x00"
                              "\x66\x0f\x01\x0c\x25\x10\x00\x20\x00";
    uc_x86_mmr gdtr = {0, 0x12345678, 0x1234, 0};
    uc_x86_mmr idtr = {0, 0x9abcdef0, 0x0567, 0};
    uint8_t b[32];
    M0 m;
    int i;

    for (i = 0; i < 2; i++) {
        m0_open(&m, i ? UC_MODE_64 : UC_MODE_32, 0, NULL, 0);
        if (i) {
            gdtr.base = 0xfffff80012345678ULL;
            idtr.base = 0xfffff8009abcdef0ULL;
        }
        OK(uc_reg_write(m.uc, UC_X86_REG_GDTR, &gdtr));
        OK(uc_reg_write(m.uc, UC_X86_REG_IDTR, &idtr));
        memset(b, 0xa5, sizeof(b));
        OK(uc_mem_write(m.uc, M0_DATA, b, sizeof(b)));
        TEST_CHECK(m0_run(&m, i ? c64 : c32, i ? sizeof(c64) - 1 : sizeof(c32) - 1) == -1);
        tb2_mem(&m, M0_DATA, b, sizeof(b));
        TEST_CHECK(b[0] == 0x34 && b[1] == 0x12);
        TEST_CHECK(b[0x10] == 0x67 && b[0x11] == 0x05);
        if (!i) {
            /* legacy mode: 2 + 4 bytes, base bits 31:24 included */
            TEST_CHECK(memcmp(b + 2, "\x78\x56\x34\x12\xa5", 5) == 0);
            TEST_CHECK(memcmp(b + 0x12, "\xf0\xde\xbc\x9a\xa5", 5) == 0);
        } else {
            /* 64-bit mode: 2 + 8 bytes whatever the operand size */
            TEST_CHECK(memcmp(b + 2, "\x78\x56\x34\x12\x00\xf8\xff\xff\xa5", 9) == 0);
            TEST_CHECK(memcmp(b + 0x12, "\xf0\xde\xbc\x9a\x00\xf8\xff\xff\xa5", 9) == 0);
        }
        TEST_MSG("mode %d: %02x%02x%02x%02x%02x%02x%02x%02x", i ? 64 : 32, b[2], b[3], b[4],
                 b[5], b[6], b[7], b[8], b[9]);
        m0_close(&m);
    }
}

/*
 * System-level setup (64-bit mode, CPL0): GDT at TB2_SYS with
 *   0x08 64-bit code DPL0, 0x10 data DPL0, 0x18 data DPL3, 0x20 64-bit code DPL3,
 *   0x28 32-bit code DPL3, 0x30 data DPL0 (base 0)
 * data at TB2_SYS + 0x4000, stacks below TB2_SYS + 0x8000 (CPL0) and + 0xC000 (CPL3).
 */
#define TB2_SYS 0x60000000ULL
#define TB2_SYS_SIZE 0x10000
#define TB2_DATA (TB2_SYS + 0x4000)
#define TB2_KSTACK (TB2_SYS + 0x8000)
#define TB2_USTACK (TB2_SYS + 0xC000)

static uc_engine *tb2_sys_open(const char *code, size_t len, nk_intr_t *intr)
{
    static const uint64_t gdt[7] = {0, 0x00AF9A000000FFFFULL, 0x00CF92000000FFFFULL,
                                    0x00CFF2000000FFFFULL, 0x00AFFA000000FFFFULL,
                                    0x00CFFA000000FFFFULL, 0x00CF92000000FFFFULL};
    uc_x86_mmr gdtr = {0, TB2_SYS, sizeof(gdt) - 1, 0};
    uc_engine *uc;
    uc_hook h;
    uint64_t rsp = TB2_KSTACK;

    OK(uc_open(UC_ARCH_X86, UC_MODE_64, &uc));
    OK(uc_ctl_set_cpu_model(uc, UC_CPU_X86_MAX));
    OK(uc_mem_map(uc, code_start, code_len, UC_PROT_ALL));
    OK(uc_mem_write(uc, code_start, code, len));
    OK(uc_mem_map(uc, TB2_SYS, TB2_SYS_SIZE, UC_PROT_ALL));
    OK(uc_mem_write(uc, TB2_SYS, gdt, sizeof(gdt)));
    OK(uc_reg_write(uc, UC_X86_REG_GDTR, &gdtr));
    OK(uc_reg_write(uc, UC_X86_REG_RSP, &rsp));
    memset(intr, 0, sizeof(*intr));
    OK(uc_hook_add(uc, &h, UC_HOOK_INTR, nk_hook_intr, intr, 1, 0));
    return uc;
}

/* IRETQ frame on the CPL0 stack: to 'rip' at CPL3 (CS 0x23, SS 0x1B, RSP TB2_USTACK) */
static void tb2_iretq_frame(uc_engine *uc, uint64_t rip, uint64_t rflags)
{
    uint64_t frame[5] = {rip, 0x23, rflags, TB2_USTACK, 0x1B};

    OK(uc_mem_write(uc, TB2_KSTACK, frame, sizeof(frame)));
}

/*
 * U477 (backport c2ba0515f2): IRET to an outer level makes ES/DS/FS/GS null when their DPL is
 * below the new CPL, but only the selector: the cached base and limit stay (SDM Vol2A IRET:
 * "the segment register is loaded with a NULL segment selector"). FS keeps its base, so
 * fs:[0] still reads TB2_DATA after the return to CPL3.
 */
static void test_x86_bp_iret_null_seg_keeps_base(void)
{
    /* iretq; mov rax, fs:[0] */
    static const char code[] = "\x48\xcf\x64\x48\x8b\x04\x25\x00\x00\x00\x00";
    nk_intr_t intr;
    uc_engine *uc = tb2_sys_open(code, sizeof(code) - 1, &intr);
    uint64_t marker = 0x1122334455667788ULL;

    tb2_iretq_frame(uc, code_start + 2, 0x202);
    OK(uc_mem_write(uc, TB2_DATA, &marker, 8));
    nk_setreg(uc, UC_X86_REG_DS, 0x10);
    nk_setreg(uc, UC_X86_REG_FS, 0x10);
    nk_setreg(uc, UC_X86_REG_FS_BASE, TB2_DATA);
    OK(uc_emu_start(uc, code_start, code_start + sizeof(code) - 1, 0, 0));
    TEST_CHECK(intr.count == 0);
    TEST_CHECK((nk_reg(uc, UC_X86_REG_CS) & 0xffff) == 0x23);
    TEST_CHECK((nk_reg(uc, UC_X86_REG_DS) & 0xffff) == 0);
    TEST_CHECK((nk_reg(uc, UC_X86_REG_FS) & 0xffff) == 0);
    TEST_CHECK(nk_reg(uc, UC_X86_REG_FS_BASE) == TB2_DATA);
    TEST_CHECK(nk_reg(uc, UC_X86_REG_RAX) == marker);
    TEST_MSG("rax %016" PRIx64 " fs.base %016" PRIx64, nk_reg(uc, UC_X86_REG_RAX),
             nk_reg(uc, UC_X86_REG_FS_BASE));
    OK(uc_close(uc));
}

/* runs 'code' from slot 'slot' (64 bytes each) of the code region; intno or -1 */
static int tb2_exec(uc_engine *uc, nk_intr_t *intr, int slot, const char *code, size_t len)
{
    uint64_t pc = code_start + 0x400 + (uint64_t)slot * 0x40;
    uc_err err;

    TEST_CHECK(len <= 0x40 && pc + 0x40 <= code_start + code_len);
    memset(intr, 0, sizeof(*intr));
    OK(uc_mem_write(uc, pc, code, len));
    err = uc_emu_start(uc, pc, pc + len, 0, 0);
    if (err == UC_ERR_INSN_INVALID) {
        return 6;
    }
    OK(err);
    return intr->count ? (int)intr->intno : -1;
}

/*
 * 4-level paging over tb2_sys_open's layout (tables at TB2_PT, physical = linear):
 *   code pages (code_start .. +code_len): user, R/W
 *   TB2_SYS pages: GDT and CPL0 stack supervisor R/W, CPL3 stack user R/W,
 *   the data page TB2_DATA with 'data_flags' (P/RW/US/PK... bits of the PTE).
 */
#define TB2_PT 0x400000ULL

static void tb2_st64(uc_engine *uc, uint64_t a, uint64_t v)
{
    OK(uc_mem_write(uc, a, &v, 8));
}

static void tb2_set_data_pte(uc_engine *uc, uint64_t data_flags)
{
    tb2_st64(uc, TB2_PT + 0x5000 + ((TB2_DATA - TB2_SYS) >> 12) * 8, TB2_DATA | data_flags);
}

static void tb2_paging(uc_engine *uc, uint64_t data_flags)
{
    uc_x86_msr efer = {0xc0000080, 0};
    uint64_t a, cr0, cr4;

    OK(uc_mem_map(uc, TB2_PT, 0x6000, UC_PROT_ALL));
    tb2_st64(uc, TB2_PT + 0x0000, (TB2_PT + 0x1000) | 7);           /* PML4[0] */
    tb2_st64(uc, TB2_PT + 0x1000, (TB2_PT + 0x2000) | 7);           /* PDPT[0]: 0-1 GB */
    tb2_st64(uc, TB2_PT + 0x1008, (TB2_PT + 0x3000) | 7);           /* PDPT[1]: 1-2 GB */
    tb2_st64(uc, TB2_PT + 0x2000, (TB2_PT + 0x4000) | 7);           /* PD0[0]: 0-2 MB */
    tb2_st64(uc, TB2_PT + 0x3000 + ((TB2_SYS - 0x40000000ULL) >> 21) * 8,
             (TB2_PT + 0x5000) | 7);                                  /* PD1: TB2_SYS */
    for (a = code_start; a < code_start + code_len; a += 0x1000) {
        tb2_st64(uc, TB2_PT + 0x4000 + (a >> 12) * 8, a | 7);
    }
    for (a = TB2_SYS; a < TB2_SYS + TB2_SYS_SIZE; a += 0x1000) {
        tb2_st64(uc, TB2_PT + 0x5000 + ((a - TB2_SYS) >> 12) * 8,
                 a | (a >= TB2_SYS + 0x9000 ? 7 : 3));
    }
    tb2_set_data_pte(uc, data_flags);
    a = TB2_PT;
    OK(uc_reg_write(uc, UC_X86_REG_CR3, &a));
    OK(uc_reg_read(uc, UC_X86_REG_CR4, &cr4));
    cr4 |= 1u << 5;                                                   /* PAE */
    OK(uc_reg_write(uc, UC_X86_REG_CR4, &cr4));
    OK(uc_reg_read(uc, UC_X86_REG_MSR, &efer));
    efer.value |= 1u << 8;                                            /* LME */
    OK(uc_reg_write(uc, UC_X86_REG_MSR, &efer));
    OK(uc_reg_read(uc, UC_X86_REG_CR0, &cr0));
    cr0 |= 0x80010000ull;                                             /* PG, WP */
    OK(uc_reg_write(uc, UC_X86_REG_CR0, &cr0));
}

static void tb2_wrmsr(uc_engine *uc, uint32_t idx, uint64_t v)
{
    uc_x86_msr msr = {idx, v};

    OK(uc_reg_write(uc, UC_X86_REG_MSR, &msr));
}

/*
 * U479 (backport e7e7bdabab): PKS. SDM Vol3A 5.6.2: with CR4.PKS = 1 the IA32_PKRS MSR (6E1H)
 * controls data accesses to supervisor-mode addresses by protection key (PTE bits 62:59):
 * ADi = 1 -> no data access; WDi = 1 -> no write if CR0.WP = 1. Supervisor page TB2_DATA has
 * key 5 (PKRS bit 10 = AD5, bit 11 = WD5).
 */
static void test_x86_bp_pks(void)
{
    /* mov rax, [TB2_DATA] / mov [TB2_DATA], rax (absolute disp32) */
    static const char rd[] = "\x48\x8b\x04\x25\x00\x40\x00\x60";
    static const char wr[] = "\x48\x89\x04\x25\x00\x40\x00\x60";
    const uint64_t key5 = 5ULL << 59;
    nk_intr_t intr;
    uc_engine *uc = tb2_sys_open("\x90", 1, &intr);
    uint64_t cr4, cr0;
    int slot = 0;

    tb2_paging(uc, key5 | 3);
    cr4 = nk_reg(uc, UC_X86_REG_CR4);
    nk_setreg(uc, UC_X86_REG_CR4, cr4 | (1u << 24));                  /* CR4.PKS */
    TEST_CHECK(nk_reg(uc, UC_X86_REG_CR4) & (1u << 24));
    /* AD5: read and write fault (#PF, CR2 = address) */
    tb2_wrmsr(uc, 0x6e1, 1u << 10);
    TEST_CHECK(tb2_exec(uc, &intr, slot++, rd, sizeof(rd) - 1) == 14);
    TEST_CHECK(nk_reg(uc, UC_X86_REG_CR2) == TB2_DATA);
    TEST_CHECK(tb2_exec(uc, &intr, slot++, wr, sizeof(wr) - 1) == 14);
    /* AD of another key: no effect */
    tb2_wrmsr(uc, 0x6e1, 1u << 8);
    TEST_CHECK(tb2_exec(uc, &intr, slot++, rd, sizeof(rd) - 1) == -1);
    TEST_CHECK(tb2_exec(uc, &intr, slot++, wr, sizeof(wr) - 1) == -1);
    /* WD5 with CR0.WP = 1: read allowed, write faults */
    tb2_wrmsr(uc, 0x6e1, 1u << 11);
    TEST_CHECK(tb2_exec(uc, &intr, slot++, rd, sizeof(rd) - 1) == -1);
    TEST_CHECK(tb2_exec(uc, &intr, slot++, wr, sizeof(wr) - 1) == 14);
    /* WD5 with CR0.WP = 0: write allowed */
    cr0 = nk_reg(uc, UC_X86_REG_CR0);
    nk_setreg(uc, UC_X86_REG_CR0, cr0 & ~(1ULL << 16));
    TEST_CHECK(tb2_exec(uc, &intr, slot++, wr, sizeof(wr) - 1) == -1);
    nk_setreg(uc, UC_X86_REG_CR0, cr0);
    /* CR4.PKS = 0: IA32_PKRS is ignored */
    tb2_wrmsr(uc, 0x6e1, 1u << 10);
    nk_setreg(uc, UC_X86_REG_CR4, cr4);
    TEST_CHECK(tb2_exec(uc, &intr, slot++, rd, sizeof(rd) - 1) == -1);
    /* a user page with key 5: IA32_PKRS does not apply (PKRU = 0, SMAP off) */
    nk_setreg(uc, UC_X86_REG_CR4, cr4 | (1u << 24));
    tb2_set_data_pte(uc, key5 | 7);
    tb2_wrmsr(uc, 0x6e1, 1u << 10);
    TEST_CHECK(tb2_exec(uc, &intr, slot++, rd, sizeof(rd) - 1) == -1);
    OK(uc_close(uc));
}

/*
 * U480 (backport d3e8b648ab / 4526f58a27 / X86Access series, done by probing): with 4-level
 * paging and the page after TB2_DATA not present, FNSAVE / FNSTENV / FXSAVE / XSAVE across
 * the page end take #PF and store nothing, FRSTOR / FLDENV / FXRSTOR / XRSTOR load nothing
 * (i5-13600K, cases_backport_t2). FSTP m80 / FBSTP store the low qword first and fault on the
 * last two bytes, as the i5-13600K does (the SDM does not say); TOP stays unchanged.
 */
static void test_x86_bp_x87_no_partial(void)
{
    /* each snippet: rsi = TB2_DATA (set below) */
    static const struct {
        const char *code;
        size_t len;
        uint32_t off;          /* operand offset in TB2_DATA's page */
        int stored;            /* bytes expected written (low qword of FSTP/FBSTP) */
    } t[] = {
        {"\xdd\xb6\xc0\x0f\x00\x00", 6, 0xfc0, 0},                       /* fnsave [rsi+0xfc0] */
        {"\xd9\xb6\xf0\x0f\x00\x00", 6, 0xff0, 0},                       /* fnstenv [rsi+0xff0] */
        {"\x0f\xae\x86\x00\x0f\x00\x00", 7, 0xf00, 0},                   /* fxsave [rsi+0xf00] */
        {"\xb8\x03\x00\x00\x00\x31\xd2\x0f\xae\xa6\x00\x0f\x00\x00", 14, 0xf00, 0}, /* xsave */
        {"\xd9\xe8\xdb\xbe\xf8\x0f\x00\x00", 8, 0xff8, 8},               /* fld1; fstp m80 */
        {"\xd9\xe8\xdb\xbe\xfa\x0f\x00\x00", 8, 0xffa, 0},               /* qword crosses */
        {"\xd9\xe8\xdf\xb6\xf8\x0f\x00\x00", 8, 0xff8, 8},               /* fld1; fbstp m80 */
    };
    static const char frstor[] = "\xdd\xa6\xc0\x0f\x00\x00";          /* frstor [rsi+0xfc0] */
    static const char fldenv[] = "\xd9\xa6\xf0\x0f\x00\x00";          /* fldenv [rsi+0xff0] */
    static const char fxrstor[] = "\x0f\xae\x8e\x00\x0f\x00\x00";     /* fxrstor [rsi+0xf00] */
    static const char xrstor[] = "\xb8\x03\x00\x00\x00\x31\xd2\x0f\xae\xae\xc0\x0d\x00\x00";
    nk_intr_t intr;
    uc_engine *uc = tb2_sys_open("\x90", 1, &intr);
    uint8_t page[0x1000], img[0x1000];
    uint64_t cr4;
    int slot = 0, j;
    size_t i;

    tb2_paging(uc, 3);
    /* the page after TB2_DATA: mapped in Unicorn, not present in the page tables */
    tb2_st64(uc, TB2_PT + 0x5000 + ((TB2_DATA + 0x1000 - TB2_SYS) >> 12) * 8, 0);
    cr4 = nk_reg(uc, UC_X86_REG_CR4);
    nk_setreg(uc, UC_X86_REG_CR4, cr4 | (1u << 9) | (1u << 18));      /* OSFXSR, OSXSAVE */
    for (i = 0; i < sizeof(t) / sizeof(t[0]); i++) {
        memset(page, 0xa5, sizeof(page));
        OK(uc_mem_write(uc, TB2_DATA, page, sizeof(page)));
        nk_setreg(uc, UC_X86_REG_RSI, TB2_DATA);
        nk_setreg(uc, UC_X86_REG_FPSW, 0);
        nk_setreg(uc, UC_X86_REG_FPTAG, 0xffff);
        TEST_CHECK(tb2_exec(uc, &intr, slot++, t[i].code, t[i].len) == 14);
        TEST_CHECK(nk_reg(uc, UC_X86_REG_CR2) == TB2_DATA + 0x1000);
        OK(uc_mem_read(uc, TB2_DATA, img, sizeof(img)));
        for (j = 0; j < 0x1000; j++) {
            int in_store = j >= (int)t[i].off && j < (int)t[i].off + t[i].stored;
            if (!in_store && img[j] != 0xa5) {
                break;
            }
        }
        TEST_CHECK(j == 0x1000);
        TEST_MSG("case %d: byte %03x changed", (int)i, j);
        if (t[i].stored) {
            /* low qword of 1.0 (FSTP) / BCD 1 (FBSTP); TOP = 7 (FLD1 done, no pop) */
            TEST_CHECK(img[t[i].off] == (t[i].code[3] == '\xbe' ? 0x00 : 0x01));
            TEST_CHECK(((nk_reg(uc, UC_X86_REG_FPSW) >> 11) & 7) == 7);
        }
    }
    /* loads: an image with FCW = 027Fh in the present page; nothing may be loaded */
    memset(page, 0, sizeof(page));
    page[0xfc0] = 0x7f; page[0xfc1] = 0x02;                           /* FRSTOR FCW */
    page[0xff0] = 0x7f; page[0xff1] = 0x02;                           /* FLDENV FCW */
    page[0xf00] = 0x7f; page[0xf01] = 0x02;                           /* FXRSTOR FCW */
    page[0xf18] = 0x80; page[0xf19] = 0x1f;                           /* MXCSR */
    page[0xdc0] = 0x7f; page[0xdc1] = 0x02;                           /* XRSTOR FCW */
    page[0xdd8] = 0x80; page[0xdd9] = 0x1f;
    OK(uc_mem_write(uc, TB2_DATA, page, sizeof(page)));
    nk_setreg(uc, UC_X86_REG_FPCW, 0x37f);
    TEST_CHECK(tb2_exec(uc, &intr, slot++, frstor, sizeof(frstor) - 1) == 14);
    TEST_CHECK(nk_reg(uc, UC_X86_REG_FPCW) == 0x37f);
    TEST_CHECK(tb2_exec(uc, &intr, slot++, fldenv, sizeof(fldenv) - 1) == 14);
    TEST_CHECK(nk_reg(uc, UC_X86_REG_FPCW) == 0x37f);
    TEST_CHECK(tb2_exec(uc, &intr, slot++, fxrstor, sizeof(fxrstor) - 1) == 14);
    TEST_CHECK(nk_reg(uc, UC_X86_REG_FPCW) == 0x37f);
    /* XRSTOR at +0xdc0: legacy area and header (XSTATE_BV = 3 at +0xfc0) present, the
       AVX area (not requested) on the absent page: no fault, the image loads */
    tb2_st64(uc, TB2_DATA + 0xfc0, 3);
    TEST_CHECK(tb2_exec(uc, &intr, slot++, xrstor, sizeof(xrstor) - 1) == -1);
    TEST_CHECK(nk_reg(uc, UC_X86_REG_FPCW) == 0x27f);
    OK(uc_close(uc));
}

/*
 * U481 (backport 0bd385e7e3 + e136648c5c): with CR4.SMAP = 1, RETF at CPL3 pops from the
 * user stack and a far CALL at CPL3 pushes onto it as CPL3 data accesses, not supervisor
 * ones (which SMAP would refuse on a user page).
 */
static void test_x86_bp_far_ret_call_cpl3_smap(void)
{
    /*
     * iretq (to CPL3); push 0x23; lea rax, [rip+3]; push rax; retfq; nop;
     * call far qword [TB2_DATA]; nop (the call's target)
     */
    static const char code[] = "\x48\xcf\x6a\x23\x48\x8d\x05\x03\x00\x00\x00\x50\x48\xcb\x90"
                               "\x48\xff\x1c\x25\x00\x40\x00\x60\x90";
    nk_intr_t intr;
    uc_engine *uc = tb2_sys_open(code, sizeof(code) - 1, &intr);
    uint64_t fptr[2] = {code_start + 23, 0x23}, st[2] = {0, 0};

    tb2_paging(uc, 7);
    OK(uc_mem_write(uc, TB2_DATA, fptr, 10));
    tb2_iretq_frame(uc, code_start + 2, 0x202);
    nk_setreg(uc, UC_X86_REG_CR4, nk_reg(uc, UC_X86_REG_CR4) | (1u << 21));    /* SMAP */
    TEST_CHECK(nk_reg(uc, UC_X86_REG_CR4) & (1u << 21));
    OK(uc_emu_start(uc, code_start, code_start + sizeof(code) - 1, 0, 0));
    TEST_CHECK(intr.count == 0);
    TEST_MSG("intr %u at rip %" PRIx64, intr.intno, nk_reg(uc, UC_X86_REG_RIP));
    TEST_CHECK((nk_reg(uc, UC_X86_REG_CS) & 0xffff) == 0x23);
    TEST_CHECK(nk_reg(uc, UC_X86_REG_RSP) == TB2_USTACK - 16);
    OK(uc_mem_read(uc, TB2_USTACK - 16, st, sizeof(st)));
    TEST_CHECK(st[0] == code_start + 23 && st[1] == 0x23);
    OK(uc_close(uc));
}

/*
 * U482 (backport 0d82d9e846): RF for REP string instructions. SDM Vol3B 20.3.1.1: the
 * processor sets RF in the EFLAGS image of a trap or interrupt taken between iterations of a
 * repeated string instruction, so an instruction breakpoint does not hit again on resume; RF
 * is cleared when the instruction completes. Single-step (TF) over REP MOVSB, RCX = 3: the
 * #DB after the first iteration sees RF = 1; resuming with TF = 0 finishes with RF = 0.
 */
static void test_x86_bp_rep_string_rf(void)
{
    static const char code[] = "\xf3\xa4";                      /* rep movsb */
    M0 m;
    uint64_t pc, fl;
    uc_err err;

    m0_open(&m, UC_MODE_64, 0, NULL, 0);
    pc = m.pc;
    OK(uc_mem_write(m.uc, pc, code, sizeof(code) - 1));
    m0_set(&m, UC_X86_REG_RCX, 3);
    m0_set(&m, UC_X86_REG_RSI, M0_DATA);
    m0_set(&m, UC_X86_REG_RDI, M0_DATA + 0x100);
    m0_set(&m, UC_X86_REG_RFLAGS, 0x302);                       /* TF */
    m.cap.count = 0;
    err = uc_emu_start(m.uc, pc, pc + sizeof(code) - 1, 0, 0);
    OK(err);
    TEST_CHECK(m.cap.count == 1 && m.cap.intno == 1);
    TEST_CHECK(m0_get(&m, UC_X86_REG_RCX) == 2);
    TEST_CHECK(m0_get(&m, UC_X86_REG_RIP) == pc);
    fl = m0_get(&m, UC_X86_REG_RFLAGS);
    TEST_CHECK(fl & 0x10000);
    TEST_MSG("rflags after the first iteration %" PRIx64, fl);
    m0_set(&m, UC_X86_REG_RFLAGS, fl & ~0x100ULL);
    m.cap.count = 0;
    OK(uc_emu_start(m.uc, pc, pc + sizeof(code) - 1, 0, 0));
    TEST_CHECK(m.cap.count == 0);
    TEST_CHECK(m0_get(&m, UC_X86_REG_RCX) == 0);
    fl = m0_get(&m, UC_X86_REG_RFLAGS);
    TEST_CHECK(!(fl & 0x10000));
    TEST_MSG("rflags at the end %" PRIx64, fl);
    m0_close(&m);
}

/*
 * U483 (backport 51aa3f3e05): SYSRETQ with a non-canonical RCX is #GP(0) on Intel (SDM Vol2B
 * SYSRET) before any state changes: RFLAGS is not loaded from R11 and CS stays.
 */
static void test_x86_bp_sysret_canonical(void)
{
    static const char sysretq[] = "\x48\x0f\x07";
    M0 m;
    uc_x86_msr efer = {0xc0000080, 0}, star = {0xc0000081, 0x0013000800000000ULL};

    m0_open(&m, UC_MODE_64, 0, NULL, 0);
    OK(uc_reg_read(m.uc, UC_X86_REG_MSR, &efer));
    efer.value |= 1;                                            /* SCE */
    OK(uc_reg_write(m.uc, UC_X86_REG_MSR, &efer));
    OK(uc_reg_write(m.uc, UC_X86_REG_MSR, &star));
    m0_set(&m, UC_X86_REG_R11, 0x246 | 0x400);                  /* DF in the new RFLAGS */
    m0_set(&m, UC_X86_REG_RCX, 0x0000800000000000ULL);          /* non-canonical */
    TEST_CHECK(m0_run(&m, sysretq, 3) == 13);
    TEST_CHECK(!(m0_get(&m, UC_X86_REG_RFLAGS) & 0x400));
    TEST_CHECK((m0_get(&m, UC_X86_REG_CS) & 3) == 0);
    m0_set(&m, UC_X86_REG_RCX, m.pc + 3);                       /* canonical: returns there */
    TEST_CHECK(m0_run(&m, sysretq, 3) == -1);
    TEST_CHECK((m0_get(&m, UC_X86_REG_CS) & 0xffff) == 0x23);   /* STAR[63:48] + 16, RPL 3 */
    TEST_CHECK(m0_get(&m, UC_X86_REG_RFLAGS) & 0x400);
    m0_close(&m);
}

/*
 * U484 (backport ed88bdcfbd): VEX in 16-bit protected mode (CS.D = 0, PE = 1). C5 F9 EF C0
 * is VPXOR xmm0, xmm0, xmm0 there (SDM Vol2A 2.5: VEX is #UD only in real and virtual-8086
 * mode, where C4/C5 are LES/LDS); before, it decoded as LDS with a register operand (#UD).
 */
static void test_x86_bp_vex_16bit_pm(void)
{
    static const uint64_t gdt[3] = {0, 0x00009A000000FFFFULL, 0x00CF92000000FFFFULL};
    static const char vpxor[] = "\xc5\xf9\xef\xc0";
    uc_x86_mmr gdtr = {0, M0_DATA + 0x1000, sizeof(gdt) - 1, 0};
    uint64_t x[2] = {0x1111111111111111ULL, 0x2222222222222222ULL};
    M0 m;

    m0_open(&m, UC_MODE_32, 0, NULL, 0);
    OK(uc_mem_write(m.uc, M0_DATA + 0x1000, gdt, sizeof(gdt)));
    OK(uc_reg_write(m.uc, UC_X86_REG_GDTR, &gdtr));
    m0_set(&m, UC_X86_REG_CS, 0x08);                    /* 16-bit code segment, base 0 */
    OK(uc_reg_write(m.uc, UC_X86_REG_XMM0, x));
    TEST_CHECK(m0_run(&m, vpxor, sizeof(vpxor) - 1) == -1);
    OK(uc_reg_read(m.uc, UC_X86_REG_XMM0, x));
    TEST_CHECK(x[0] == 0 && x[1] == 0);
    m0_close(&m);
}

/*
 * U485 (backport 3fabbe0b7d, UD0/UD1 only): UD0 (0F FF /r) and UD1 (0F B9 /r) take a ModRM
 * byte (SDM Vol2B UD; note 1: a processor decoding UD0 without it "would deliver an invalid-
 * opcode exception instead of a fault on instruction fetch"). With the disp32 on the unmapped
 * page after the code, the fetch fault wins; fully mapped, #UD.
 */
static void test_x86_bp_ud0_ud1_modrm(void)
{
    static const char ud1[] = "\x0f\xb9\x80\x00\x00\x00\x00";
    static const char ud0[] = "\x0f\xff\x80\x00\x00\x00\x00";
    const char *c[2] = {ud1, ud0};
    uint64_t at = code_start + code_len - 3;
    M0 m;
    int i;

    for (i = 0; i < 2; i++) {
        uc_err err;

        m0_open(&m, UC_MODE_64, 0, NULL, 0);
        TEST_CHECK(m0_run(&m, c[i], 7) == 6);
        OK(uc_mem_write(m.uc, at, c[i], 3));
        m.cap.count = 0;
        err = uc_emu_start(m.uc, at, at + 7, 0, 0);
        TEST_CHECK(err == UC_ERR_FETCH_UNMAPPED);
        TEST_CHECK(m.cap.count == 0);
        TEST_MSG("%s: err %d (%s), intr count %u", i ? "UD0" : "UD1", err, uc_strerror(err),
                 m.cap.count);
        m0_close(&m);
    }
}

/*
 * U486 (backport 183e6679e3): the VEX.L / VEX.W #UD conditions of an entry come before
 * CR0.TS (#NM): VLDMXCSR/VSTMXCSR with VEX.L = 1 (SDM Vol2A LDMXCSR/STMXCSR: #UD) and
 * VPERMILPS with VEX.W = 1 (W0) are #UD with CR0.TS = 1; the valid forms still #NM.
 */
static void test_x86_bp_vex_ud_before_nm(void)
{
    static const char vld_l1[] = "\xc5\xfc\xae\x10";          /* vldmxcsr [rax], VEX.L = 1 */
    static const char vst_l1[] = "\xc5\xfc\xae\x18";          /* vstmxcsr [rax], VEX.L = 1 */
    static const char vld_l0[] = "\xc5\xf8\xae\x10";          /* vldmxcsr [rax] */
    static const char perm_w1[] = "\xc4\xe2\xf9\x0c\xc1";     /* vpermilps xmm0, xmm0, xmm1, W1 */
    static const char perm_w0[] = "\xc4\xe2\x79\x0c\xc1";     /* the same with W0 */
    M0 m;

    m0_open(&m, UC_MODE_64, 0, NULL, 0);
    m0_set(&m, UC_X86_REG_RAX, M0_DATA);
    /* TS = 0: L = 1 / W = 1 #UD, the valid forms run */
    TEST_CHECK(m0_run(&m, vld_l1, 4) == 6);
    TEST_CHECK(m0_run(&m, perm_w1, 5) == 6);
    TEST_CHECK(m0_run(&m, perm_w0, 5) == -1);
    m0_set(&m, UC_X86_REG_CR0, m0_get(&m, UC_X86_REG_CR0) | 8);           /* CR0.TS */
    TEST_CHECK(m0_run(&m, vld_l1, 4) == 6);
    TEST_CHECK(m0_run(&m, vst_l1, 4) == 6);
    TEST_CHECK(m0_run(&m, perm_w1, 5) == 6);
    TEST_CHECK(m0_run(&m, vld_l0, 4) == 7);
    TEST_CHECK(m0_run(&m, perm_w0, 5) == 7);
    m0_close(&m);
}

/*
 * U487 (ours, the U70 check): VGF2P8MULB is VEX.W0 only (SDM Vol2A GF2P8MULB: VEX.128.66.0F38.W0
 * CF /r); VEX.W = 1 is #UD before CR0.TS (#NM), as the upstream W0/W1 checks (U486).
 */
static void test_x86_bp_vex_w_ud_before_nm(void)
{
    static const char w1[] = "\xc4\xe2\xf9\xcf\xc1";          /* vgf2p8mulb xmm0, xmm0, xmm1, W1 */
    static const char w0[] = "\xc4\xe2\x79\xcf\xc1";          /* the same with W0 */
    M0 m;

    m0_open(&m, UC_MODE_64, 0, NULL, 0);
    TEST_CHECK(m0_run(&m, w1, 5) == 6);
    TEST_CHECK(m0_run(&m, w0, 5) == -1);
    m0_set(&m, UC_X86_REG_CR0, m0_get(&m, UC_X86_REG_CR0) | 8);           /* CR0.TS */
    TEST_CHECK(m0_run(&m, w1, 5) == 6);
    TEST_CHECK(m0_run(&m, w0, 5) == 7);
    m0_close(&m);
}
/* ---- end U475-U499 (tb2_) ---- */

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
    {"test_x86_ptwrite_ud", test_x86_ptwrite_ud},
    {"test_x86_vsha512", test_x86_vsha512},
    {"test_x86_vsm3", test_x86_vsm3},
    {"test_x86_vsm4", test_x86_vsm4},
    {"test_x86_tsx_rtm_always_abort", test_x86_tsx_rtm_always_abort},
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
    {"test_x86_movrs_prefetchrst2", test_x86_movrs_prefetchrst2},
    {"test_x86_user_msr", test_x86_user_msr},
    {"test_x86_uintr_uif", test_x86_uintr_uif},
    {"test_x86_uintr_senduipi", test_x86_uintr_senduipi},
    {"test_x86_uintr_delivery", test_x86_uintr_delivery},
    {"test_x86_waitpkg", test_x86_waitpkg},
    {"test_x86_enqcmd", test_x86_enqcmd},
    {"test_x86_smx_pconfig_sgx", test_x86_smx_pconfig_sgx},
    {"test_x86_cet_shadow_stack", test_x86_cet_shadow_stack},
    {"test_x86_cet_shadow_stack_32", test_x86_cet_shadow_stack_32},
    {"test_x86_cet_call_ret", test_x86_cet_call_ret},
    {"test_x86_cet_ibt", test_x86_cet_ibt},
    {"test_x86_cet_shadow_stack_paging", test_x86_cet_shadow_stack_paging},
    {"test_x86_opmask_optin", test_x86_opmask_optin},
    {"test_x86_opmask_vectors", test_x86_opmask_vectors},
    {"test_x86_opmask_ud", test_x86_opmask_ud},
    {"test_x86_opmask_features", test_x86_opmask_features},
    {"test_x86_opmask_state", test_x86_opmask_state},
    {"test_x86_lock_hint_nops", test_x86_lock_hint_nops},
    {"test_x86_mpx_modrm_length", test_x86_mpx_modrm_length},
    {"test_x86_vex_b_32", test_x86_vex_b_32},
    {"test_x86_mem_write_invalidates_tb", test_x86_mem_write_invalidates_tb},
    {"test_x86_amx_optin", test_x86_amx_optin},
    {"test_x86_amx_xsetbv", test_x86_amx_xsetbv},
    {"test_x86_amx_api", test_x86_amx_api},
    {"test_x86_amx_cfg_insns", test_x86_amx_cfg_insns},
    {"test_x86_amx_load_store", test_x86_amx_load_store},
    {"test_x86_amx_tmul_vectors", test_x86_amx_tmul_vectors},
    {"test_x86_amx_exceptions", test_x86_amx_exceptions},
    {"test_x86_amx_xsave", test_x86_amx_xsave},
    {"test_x86_amx_xfd", test_x86_amx_xfd},
    {"test_x86_evex_routing", test_x86_evex_routing},
    {"test_x86_evex_32bit_fields", test_x86_evex_32bit_fields},
    {"test_x86_evex_state", test_x86_evex_state},
    {"test_x86_evex_masked_memory", test_x86_evex_masked_memory},
    {"test_x86_evex_riprel", test_x86_evex_riprel},
    {"test_x86_evex_gather_restart", test_x86_evex_gather_restart},
    {"test_x86_evex_gather_hooks", test_x86_evex_gather_hooks},
    {"test_x86_evex_scatter_restart", test_x86_evex_scatter_restart},
    {"test_x86_evex_scatter_order", test_x86_evex_scatter_order},
    {"test_x86_evex_vsib_modes", test_x86_evex_vsib_modes},
    {"test_x86_evex_scalar_memory", test_x86_evex_scalar_memory},
    {"test_x86_evex_comis_eflags", test_x86_evex_comis_eflags},
    {"test_x86_evex_m2_memory", test_x86_evex_m2_memory},
    {"test_x86_evex_m2_features", test_x86_evex_m2_features},
    {"test_x86_evex_bw_cpuid", test_x86_evex_bw_cpuid},
    {"test_x86_evex_bw_masked_bytes", test_x86_evex_bw_masked_bytes},
    {"test_x86_evex_bw_kmask64", test_x86_evex_bw_kmask64},
    {"test_x86_avx512dq_gating", test_x86_avx512dq_gating},
    {"test_x86_avx512dq_values", test_x86_avx512dq_values},
    {"test_x86_avx512cd_optin", test_x86_avx512cd_optin},
    {"test_x86_avx512cd_gating", test_x86_avx512cd_gating},
    {"test_x86_avx512_m4_bits", test_x86_avx512_m4_bits},
    {"test_x86_evex_cvt_narrow", test_x86_evex_cvt_narrow},
    {"test_x86_evex_cvt_ph_store", test_x86_evex_cvt_ph_store},
    {"test_x86_evex_cvt_scalar_merge", test_x86_evex_cvt_scalar_merge},
    {"test_x86_evex_cvt_gpr_w", test_x86_evex_cvt_gpr_w},
    {"test_x86_evex_cvt_xm", test_x86_evex_cvt_xm},
    {"test_x86_f16c_vcvtps2ph_ftz", test_x86_f16c_vcvtps2ph_ftz},
    {"test_x86_sdm_documented_deviations", test_x86_sdm_documented_deviations},
    {"test_x86_fp16_optin", test_x86_fp16_optin},
    {"test_x86_fp16_gating", test_x86_fp16_gating},
    {"test_x86_fp16_xm", test_x86_fp16_xm},
    {"test_x86_fp16_complex", test_x86_fp16_complex},
    {"test_x86_fp16_cvt_gpr", test_x86_fp16_cvt_gpr},
    {"test_x86_avx10_optin", test_x86_avx10_optin},
    {"test_x86_avx10_gating", test_x86_avx10_gating},
    {"test_x86_avx10_2_gating", test_x86_avx10_2_gating},
    {"test_x86_avx10b_ctl", test_x86_avx10b_ctl},
    {"test_x86_avx10b_gating", test_x86_avx10b_gating},
    {"test_x86_avx10b_values", test_x86_avx10b_values},
    {"test_x86_avx10b_fault_suppression", test_x86_avx10b_fault_suppression},
    {"test_x86_sse_unmasked_ou", test_x86_sse_unmasked_ou},
    {"test_x86_sse_dpps_steps", test_x86_sse_dpps_steps},
    {"test_x86_mxcsr_api", test_x86_mxcsr_api},
    {"test_x86_cpuid_strict_default", test_x86_cpuid_strict_default},
    {"test_x86_bp_sgdt_sidt_base", test_x86_bp_sgdt_sidt_base},
    {"test_x86_bp_iret_null_seg_keeps_base", test_x86_bp_iret_null_seg_keeps_base},
    {"test_x86_bp_pks", test_x86_bp_pks},
    {"test_x86_bp_x87_no_partial", test_x86_bp_x87_no_partial},
    {"test_x86_bp_far_ret_call_cpl3_smap", test_x86_bp_far_ret_call_cpl3_smap},
    {"test_x86_bp_rep_string_rf", test_x86_bp_rep_string_rf},
    {"test_x86_bp_sysret_canonical", test_x86_bp_sysret_canonical},
    {"test_x86_bp_vex_16bit_pm", test_x86_bp_vex_16bit_pm},
    {"test_x86_bp_ud0_ud1_modrm", test_x86_bp_ud0_ud1_modrm},
    {"test_x86_bp_vex_ud_before_nm", test_x86_bp_vex_ud_before_nm},
    {"test_x86_bp_vex_w_ud_before_nm", test_x86_bp_vex_w_ud_before_nm},
    {NULL, NULL}};
