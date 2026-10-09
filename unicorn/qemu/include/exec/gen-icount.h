#ifndef GEN_ICOUNT_H
#define GEN_ICOUNT_H

#include "qemu/timer.h"

/* Helpers for instruction counting code generation.  */

static inline void gen_io_start(TCGContext *tcg_ctx)
{
    TCGv_i32 tmp = tcg_const_i32(tcg_ctx, 1);
    tcg_gen_st_i32(tcg_ctx, tmp, tcg_ctx->cpu_env,
                   offsetof(ArchCPU, parent_obj.can_do_io) -
                   offsetof(ArchCPU, env));
    tcg_temp_free_i32(tcg_ctx, tmp);
}

/*
 * cpu->can_do_io is cleared automatically at the beginning of
 * each translation block.  The cost is minimal and only paid
 * for -icount, plus it would be very easy to forget doing it
 * in the translator.  Therefore, backends only need to call
 * gen_io_start.
 */
static inline void gen_io_end(TCGContext *tcg_ctx)
{
    TCGv_i32 tmp = tcg_const_i32(tcg_ctx, 0);
    tcg_gen_st_i32(tcg_ctx, tmp, tcg_ctx->cpu_env,
                   offsetof(ArchCPU, parent_obj.can_do_io) -
                   offsetof(ArchCPU, env));
    tcg_temp_free_i32(tcg_ctx, tmp);
}

static inline void gen_tb_start(TCGContext *tcg_ctx, TranslationBlock *tb)
{
#if __Use_Original_Qemu != 1 /* ours (U502) */
    /*
     * helper_check_exit_request only acts when icount_decr.u32 < 0. Test that
     * inline, as QEMU's own gen_tb_start does, and branch to an out-of-line
     * stub after the TB (gen_tb_end) only then: a load and a branch per TB
     * instead of a helper call. Nothing is live at TB start, and the stub
     * uses only fresh constants and a TCG_CALL_NO_RWG helper. Targets with a
     * delay-slot flag keep the original call.
     */
    tcg_ctx->uc_exitreq_label = NULL;
    if (tcg_ctx->delay_slot_flag == NULL) {
        TCGv_i32 count = tcg_temp_new_i32(tcg_ctx);

        tcg_ctx->uc_exitreq_label = gen_new_label(tcg_ctx);
        tcg_gen_ld_i32(tcg_ctx, count, tcg_ctx->cpu_env,
                       offsetof(ArchCPU, neg.icount_decr.u32) -
                       offsetof(ArchCPU, env));
        tcg_gen_brcondi_i32(tcg_ctx, TCG_COND_LT, count, 0,
                            tcg_ctx->uc_exitreq_label);
        tcg_temp_free_i32(tcg_ctx, count);
        return;
    }
#endif /* __Use_Original_Qemu (U502) */
    TCGv_ptr puc = tcg_const_ptr(tcg_ctx, tcg_ctx->uc);
    TCGv_i32 tmp = tcg_const_i32(tcg_ctx, 0);
    // Unicorn:
    //    We CANT'T use brcondi_i32 here or we will fail liveness analysis
    //    because it marks the end of BB
    if (tcg_ctx->delay_slot_flag != NULL) {
        tcg_gen_mov_i32(tcg_ctx, tmp, tcg_ctx->delay_slot_flag);
    }
    gen_helper_check_exit_request(tcg_ctx, puc, tmp);
    tcg_temp_free_i32(tcg_ctx, tmp);
    tcg_temp_free_ptr(tcg_ctx, puc);
}

static inline void gen_tb_end(TCGContext *tcg_ctx, TranslationBlock *tb, int num_insns)
{
    if (tcg_ctx->delay_slot_flag != NULL){
        tcg_temp_free_i32(tcg_ctx, tcg_ctx->delay_slot_flag);
    }
    tcg_ctx->delay_slot_flag = NULL;
#if __Use_Original_Qemu != 1 /* ours (U502) */
    if (tcg_ctx->uc_exitreq_label != NULL) {
        TCGv_ptr puc, ptb;

        gen_set_label(tcg_ctx, tcg_ctx->uc_exitreq_label);
        tcg_ctx->uc_exitreq_label = NULL;
        puc = tcg_const_ptr(tcg_ctx, tcg_ctx->uc);
        ptb = tcg_const_ptr(tcg_ctx, tb);
        gen_helper_check_exit_request_tb_start(tcg_ctx, puc, ptb);
        tcg_temp_free_ptr(tcg_ctx, ptb);
        tcg_temp_free_ptr(tcg_ctx, puc);
    }
#endif /* __Use_Original_Qemu (U502) */
    if (tb_cflags(tb) & CF_USE_ICOUNT) {
        /* Update the num_insn immediate parameter now that we know
         * the actual insn count.  */
        tcg_set_insn_param(tcg_ctx->icount_start_insn, 1, num_insns);
    }

    tcg_gen_exit_tb(tcg_ctx, tb, TB_EXIT_REQUESTED);
}

#endif
