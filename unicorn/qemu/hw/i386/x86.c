/*
 * QEMU PC System Emulator
 *
 * Copyright (c) 2003-2004 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
/* Modified for Unicorn Engine by Nguyen Anh Quynh, 2015 */
/* Modified for Unicorn Engine by Chen Huitao<chenhuitao@hfmrit.com>, 2020 */

#include "qemu/compiler.h"
#include "sysemu/sysemu.h"
#include "target/i386/cpu.h"
#if __Use_Original_Qemu != 1 /* ours (U2090) */
#include "uc_priv.h"
#endif /* __Use_Original_Qemu (U2090) */


/* TSC handling */
uint64_t cpu_get_tsc(CPUX86State *env)
{
#if __Use_Original_Qemu != 1 /* ours (U2090) */
    /*
     * NoVmp (ledger U2090, plan 3.M6 "one virtual time base"): UC_CTL_X86_VCLOCK virtual mode - the time base
     * advances with the instructions started (and the environment's waits), never with the host clock; the host
     * mode (default) is the stock path below.
     */
    if (env->uc->vclock_mode == UC_X86_VCLOCK_VIRTUAL) {
        return uc_vclock_base(env->uc);
    }
#endif /* __Use_Original_Qemu (U2090) */
    return cpu_get_ticks();
}

