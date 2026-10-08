DEF_HELPER_4(uc_tracecode, void, i32, i32, ptr, i64)
DEF_HELPER_6(uc_traceopcode, void, ptr, i64, i64, i32, ptr, i64)

DEF_HELPER_FLAGS_4(cc_compute_all, TCG_CALL_NO_RWG_SE, tl, tl, tl, tl, int)
DEF_HELPER_FLAGS_4(cc_compute_c, TCG_CALL_NO_RWG_SE, tl, tl, tl, tl, int)

DEF_HELPER_3(write_eflags, void, env, tl, i32)
DEF_HELPER_1(read_eflags, tl, env)
DEF_HELPER_2(divb_AL, void, env, tl)
DEF_HELPER_2(idivb_AL, void, env, tl)
DEF_HELPER_2(divw_AX, void, env, tl)
DEF_HELPER_2(idivw_AX, void, env, tl)
DEF_HELPER_2(divl_EAX, void, env, tl)
DEF_HELPER_2(idivl_EAX, void, env, tl)
#ifdef TARGET_X86_64
DEF_HELPER_2(divq_EAX, void, env, tl)
DEF_HELPER_2(idivq_EAX, void, env, tl)
#endif
DEF_HELPER_FLAGS_2(cr4_testbit, TCG_CALL_NO_WG, void, env, i32)

DEF_HELPER_FLAGS_2(bndck, TCG_CALL_NO_WG, void, env, i32)
DEF_HELPER_FLAGS_3(bndldx32, TCG_CALL_NO_WG, i64, env, tl, tl)
DEF_HELPER_FLAGS_3(bndldx64, TCG_CALL_NO_WG, i64, env, tl, tl)
DEF_HELPER_FLAGS_5(bndstx32, TCG_CALL_NO_WG, void, env, tl, tl, i64, i64)
DEF_HELPER_FLAGS_5(bndstx64, TCG_CALL_NO_WG, void, env, tl, tl, i64, i64)
DEF_HELPER_1(bnd_jmp, void, env)

DEF_HELPER_2(aam, void, env, int)
DEF_HELPER_2(aad, void, env, int)
DEF_HELPER_1(aaa, void, env)
DEF_HELPER_1(aas, void, env)
DEF_HELPER_1(daa, void, env)
DEF_HELPER_1(das, void, env)

DEF_HELPER_2(lsl, tl, env, tl)
DEF_HELPER_2(lar, tl, env, tl)
DEF_HELPER_2(verr, void, env, tl)
DEF_HELPER_2(verw, void, env, tl)
DEF_HELPER_2(lldt, void, env, int)
DEF_HELPER_2(ltr, void, env, int)
DEF_HELPER_3(load_seg, void, env, int, int)
DEF_HELPER_4(ljmp_protected, void, env, int, tl, tl)
DEF_HELPER_5(lcall_real, void, env, int, tl, int, int)
DEF_HELPER_5(lcall_protected, void, env, int, tl, int, tl)
DEF_HELPER_2(iret_real, void, env, int)
DEF_HELPER_3(iret_protected, void, env, int, int)
DEF_HELPER_3(lret_protected, void, env, int, int)
DEF_HELPER_2(read_crN, tl, env, int)
DEF_HELPER_3(write_crN, void, env, int, tl)
DEF_HELPER_2(lmsw, void, env, tl)
DEF_HELPER_1(clts, void, env)
DEF_HELPER_FLAGS_3(set_dr, TCG_CALL_NO_WG, void, env, int, tl)
DEF_HELPER_FLAGS_2(get_dr, TCG_CALL_NO_WG, tl, env, int)
DEF_HELPER_2(invlpg, void, env, tl)

DEF_HELPER_2(sysenter, void, env, int)
DEF_HELPER_2(sysexit, void, env, int)
#ifdef TARGET_X86_64
DEF_HELPER_2(syscall, void, env, int)
DEF_HELPER_2(sysret, void, env, int)
#endif
DEF_HELPER_2(hlt, void, env, int)
DEF_HELPER_2(monitor, void, env, tl)
DEF_HELPER_2(mwait, void, env, int)
DEF_HELPER_2(pause, void, env, int)
DEF_HELPER_1(debug, void, env)
DEF_HELPER_1(reset_rf, void, env)
DEF_HELPER_3(raise_interrupt, void, env, int, int)
DEF_HELPER_2(raise_exception, void, env, int)
DEF_HELPER_FLAGS_1(icebp, TCG_CALL_NO_WG, noreturn, env)
DEF_HELPER_1(cli, void, env)
DEF_HELPER_1(sti, void, env)
DEF_HELPER_1(clac, void, env)
DEF_HELPER_1(stac, void, env)
DEF_HELPER_3(boundw, void, env, tl, int)
DEF_HELPER_3(boundl, void, env, tl, int)
DEF_HELPER_1(rsm, void, env)
DEF_HELPER_2(into, void, env, int)
DEF_HELPER_2(cmpxchg8b_unlocked, void, env, tl)
DEF_HELPER_2(cmpxchg8b, void, env, tl)
#ifdef TARGET_X86_64
DEF_HELPER_2(cmpxchg16b_unlocked, void, env, tl)
DEF_HELPER_2(cmpxchg16b, void, env, tl)
#if __Use_Original_Qemu != 1 /* ours (U73) */
DEF_HELPER_3(movdir64b, void, env, tl, tl)
#endif /* __Use_Original_Qemu (U73) */
#if __Use_Original_Qemu != 1 /* ours (U80) */
DEF_HELPER_1(ptwrite, void, env)
#endif /* __Use_Original_Qemu (U80) */
#endif
#if __Use_Original_Qemu != 1 /* ours (U101) */
DEF_HELPER_FLAGS_4(rao, TCG_CALL_NO_WG, void, env, tl, tl, i32)
#endif /* __Use_Original_Qemu (U101) */
#if __Use_Original_Qemu != 1 /* ours (U110) */
DEF_HELPER_2(xbegin_check, void, env, tl)
#endif /* __Use_Original_Qemu (U110) */
#if __Use_Original_Qemu != 1 /* ours (U111) */
DEF_HELPER_2(waitpkg, void, env, i32)
#endif /* __Use_Original_Qemu (U111) */
#if __Use_Original_Qemu != 1 /* ours (U112) */
DEF_HELPER_4(enqcmd, void, env, tl, tl, i32)
#endif /* __Use_Original_Qemu (U112) */
#if __Use_Original_Qemu != 1 /* ours (U113) */
DEF_HELPER_1(getsec, void, env)
DEF_HELPER_1(pconfig, void, env)
#endif /* __Use_Original_Qemu (U113) */
#if __Use_Original_Qemu != 1 /* ours (U175) */
DEF_HELPER_2(amx_ldtilecfg, void, env, tl)
DEF_HELPER_2(amx_sttilecfg, void, env, tl)
DEF_HELPER_1(amx_tilerelease, void, env)
#endif /* __Use_Original_Qemu (U175) */
#if __Use_Original_Qemu != 1 /* ours (U176) */
DEF_HELPER_5(amx_tileload, void, env, tl, tl, tl, i32)
DEF_HELPER_5(amx_tilestore, void, env, tl, tl, tl, i32)
DEF_HELPER_2(amx_tilezero, void, env, i32)
#endif /* __Use_Original_Qemu (U176) */
#if __Use_Original_Qemu != 1 /* ours (U177) */
DEF_HELPER_2(amx_tmul, void, env, i32)
#endif /* __Use_Original_Qemu (U177) */
#if __Use_Original_Qemu != 1 /* ours (U114) */
DEF_HELPER_3(incssp, void, env, tl, i32)
DEF_HELPER_1(saveprevssp, void, env)
DEF_HELPER_2(rstorssp, void, env, tl)
DEF_HELPER_5(wrss, void, env, tl, tl, i32, i32)
DEF_HELPER_1(setssbsy, void, env)
DEF_HELPER_2(clrssbsy, void, env, tl)
#endif /* __Use_Original_Qemu (U114) */
#if __Use_Original_Qemu != 1 /* ours (U115) */
DEF_HELPER_2(ss_call, void, env, tl)
DEF_HELPER_2(ss_ret, void, env, tl)
#endif /* __Use_Original_Qemu (U115) */
#if __Use_Original_Qemu != 1 /* ours (U116) */
DEF_HELPER_2(ibt_branch, void, env, i32)
DEF_HELPER_1(ibt_far, void, env)
DEF_HELPER_1(ibt_idle, void, env)
DEF_HELPER_3(ibt_check, void, env, tl, i32)
#endif /* __Use_Original_Qemu (U116) */
DEF_HELPER_1(single_step, void, env)
DEF_HELPER_1(rechecking_single_step, void, env)
DEF_HELPER_1(cpuid, void, env)
DEF_HELPER_FLAGS_1(rdpid, TCG_CALL_NO_WG, tl, env)
DEF_HELPER_1(rdtsc, void, env)
DEF_HELPER_1(rdtscp, void, env)
DEF_HELPER_1(rdpmc, void, env)
DEF_HELPER_1(rdmsr, void, env)
DEF_HELPER_1(wrmsr, void, env)

DEF_HELPER_2(check_iob, void, env, i32)
DEF_HELPER_2(check_iow, void, env, i32)
DEF_HELPER_2(check_iol, void, env, i32)
DEF_HELPER_FLAGS_3(check_io, TCG_CALL_NO_WG, void, env, i32, i32)
DEF_HELPER_3(outb, void, env, i32, i32)
DEF_HELPER_2(inb, tl, env, i32)
DEF_HELPER_3(outw, void, env, i32, i32)
DEF_HELPER_2(inw, tl, env, i32)
DEF_HELPER_3(outl, void, env, i32, i32)
DEF_HELPER_2(inl, tl, env, i32)
DEF_HELPER_FLAGS_4(bpt_io, TCG_CALL_NO_WG, void, env, i32, i32, tl)

DEF_HELPER_3(svm_check_intercept_param, void, env, i32, i64)
DEF_HELPER_2(svm_check_intercept, void, env, i32)
DEF_HELPER_4(svm_check_io, void, env, i32, i32, i32)
DEF_HELPER_3(vmrun, void, env, int, int)
DEF_HELPER_1(vmmcall, void, env)
DEF_HELPER_2(vmload, void, env, int)
DEF_HELPER_2(vmsave, void, env, int)
DEF_HELPER_1(stgi, void, env)
DEF_HELPER_1(clgi, void, env)
DEF_HELPER_1(skinit, void, env)
DEF_HELPER_2(invlpga, void, env, int)
DEF_HELPER_FLAGS_2(flush_page, TCG_CALL_NO_RWG, void, env, tl)

/* x86 FPU */

DEF_HELPER_2(flds_FT0, void, env, i32)
DEF_HELPER_2(fldl_FT0, void, env, i64)
DEF_HELPER_2(fildl_FT0, void, env, s32)
DEF_HELPER_2(flds_ST0, void, env, i32)
DEF_HELPER_2(fldl_ST0, void, env, i64)
DEF_HELPER_2(fildl_ST0, void, env, s32)
DEF_HELPER_2(fildll_ST0, void, env, s64)
#if __Use_Original_Qemu != 1 /* ours (U54) */
DEF_HELPER_3(x87_store, void, env, tl, i32)
#endif /* __Use_Original_Qemu (U54) */
DEF_HELPER_1(fsts_ST0, i32, env)
DEF_HELPER_1(fstl_ST0, i64, env)
DEF_HELPER_1(fist_ST0, s32, env)
DEF_HELPER_1(fistl_ST0, s32, env)
DEF_HELPER_1(fistll_ST0, s64, env)
DEF_HELPER_1(fistt_ST0, s32, env)
DEF_HELPER_1(fisttl_ST0, s32, env)
DEF_HELPER_1(fisttll_ST0, s64, env)
DEF_HELPER_2(fldt_ST0, void, env, tl)
DEF_HELPER_2(fstt_ST0, void, env, tl)
DEF_HELPER_1(fpush, void, env)
#if __Use_Original_Qemu != 1 /* ours (U46) */
DEF_HELPER_3(x87_pre, i32, env, i32, tl)
#if __Use_Original_Qemu != 1 /* ours (U67) */
DEF_HELPER_1(sse_fp_begin, void, env)
DEF_HELPER_3(sse_fp_end, void, env, i32, i32)
#endif /* __Use_Original_Qemu (U67) */
#if __Use_Original_Qemu != 1 /* ours (U146) */
DEF_HELPER_3(evex_align, void, env, tl, i32)
DEF_HELPER_5(evex_mload, void, env, ptr, tl, i64, i32)
DEF_HELPER_5(evex_mstore, void, env, ptr, tl, i64, i32)
DEF_HELPER_5(evex_blend, void, env, ptr, ptr, i64, i32)
DEF_HELPER_5(evex_neutral, void, env, ptr, ptr, i64, i32)
#endif /* __Use_Original_Qemu (U146) */
#if __Use_Original_Qemu != 1 /* ours (U147) */
DEF_HELPER_2(evex_rc_begin, void, env, s32)
DEF_HELPER_1(evex_rc_end, void, env)
#endif /* __Use_Original_Qemu (U147) */
#if __Use_Original_Qemu != 1 /* ours (U152) */
DEF_HELPER_4(evex_pcmp, i64, env, ptr, ptr, i32)
#endif /* __Use_Original_Qemu (U152) */
#if __Use_Original_Qemu != 1 /* ours (U159) */
DEF_HELPER_6(evex_pternlog, void, env, ptr, ptr, ptr, ptr, i32)
#endif /* __Use_Original_Qemu (U159) */
#if __Use_Original_Qemu != 1 /* ours (U250) */
DEF_HELPER_6(evex_gather, void, env, ptr, ptr, tl, tl, i32)
#endif /* __Use_Original_Qemu (U250) */
#if __Use_Original_Qemu != 1 /* ours (U251) */
DEF_HELPER_6(evex_scatter, void, env, ptr, ptr, tl, tl, i32)
#endif /* __Use_Original_Qemu (U251) */
#if __Use_Original_Qemu != 1 /* ours (U197) */
DEF_HELPER_4(evex_fcmp, i64, env, ptr, ptr, i32)
#endif /* __Use_Original_Qemu (U197) */
#if __Use_Original_Qemu != 1 /* ours (U211) */
DEF_HELPER_6(evex_perm, void, env, ptr, ptr, ptr, ptr, i32)
#endif /* __Use_Original_Qemu (U211) */
#if __Use_Original_Qemu != 1 /* ours (U213) */
DEF_HELPER_5(evex_expand, void, env, ptr, ptr, i64, i32)
DEF_HELPER_5(evex_compress, void, env, ptr, ptr, i64, i32)
#endif /* __Use_Original_Qemu (U213) */
#if __Use_Original_Qemu != 1 /* ours (U263) */
DEF_HELPER_5(evex_dbpsadbw, void, env, ptr, ptr, ptr, i32)
#endif /* __Use_Original_Qemu (U263) */
#if __Use_Original_Qemu != 1 /* ours (U265) */
DEF_HELPER_5(evex_pshiftvw, void, env, ptr, ptr, ptr, i32)
#endif /* __Use_Original_Qemu (U265) */
#if __Use_Original_Qemu != 1 /* ours (U266) */
DEF_HELPER_4(evex_movm2v, void, env, ptr, i64, i32)
#endif /* __Use_Original_Qemu (U266) */
#if __Use_Original_Qemu != 1 /* ours (U268) */
DEF_HELPER_4(evex_pmovwb, void, env, ptr, ptr, i32)
#endif /* __Use_Original_Qemu (U268) */
#if __Use_Original_Qemu != 1 /* ours (U269) */
DEF_HELPER_6(evex_vpermw, void, env, ptr, ptr, ptr, ptr, i32)
#endif /* __Use_Original_Qemu (U269) */
#if __Use_Original_Qemu != 1 /* ours (U292) */
DEF_HELPER_3(evex_fpclass, i64, env, ptr, i32)
#endif /* __Use_Original_Qemu (U292) */
#if __Use_Original_Qemu != 1 /* ours (U293) */
DEF_HELPER_6(evex_range, void, env, ptr, ptr, ptr, ptr, i32)
#endif /* __Use_Original_Qemu (U293) */
#if __Use_Original_Qemu != 1 /* ours (U294) */
DEF_HELPER_5(evex_reduce, void, env, ptr, ptr, ptr, i32)
#endif /* __Use_Original_Qemu (U294) */
#if __Use_Original_Qemu != 1 /* ours (U231) */
DEF_HELPER_4(evex_cvt, void, env, ptr, ptr, i32)
#endif /* __Use_Original_Qemu (U231) */
#if __Use_Original_Qemu != 1 /* ours (U232) */
DEF_HELPER_5(evex_cvt_s, void, env, ptr, ptr, ptr, i32)
DEF_HELPER_5(evex_cvt_i2f, void, env, ptr, ptr, i64, i32)
DEF_HELPER_3(evex_cvt_f2i, i64, env, ptr, i32)
#endif /* __Use_Original_Qemu (U232) */
#if __Use_Original_Qemu != 1 /* ours (U234) */
DEF_HELPER_5(evex_pshift, void, env, ptr, ptr, ptr, i32)
#endif /* __Use_Original_Qemu (U234) */
#if __Use_Original_Qemu != 1 /* ours (U235) */
DEF_HELPER_5(evex_prolv, void, env, ptr, ptr, ptr, i32)
#endif /* __Use_Original_Qemu (U235) */
#if __Use_Original_Qemu != 1 /* ours (U66) */
DEF_HELPER_FLAGS_3(xsavec, TCG_CALL_NO_WG, void, env, tl, i64)
#endif /* __Use_Original_Qemu (U66) */
#if __Use_Original_Qemu != 1 /* ours (U64) */
DEF_HELPER_4(x87_ptrs, void, env, i32, tl, i32)
#endif /* __Use_Original_Qemu (U64) */
#endif /* __Use_Original_Qemu (U46) */
DEF_HELPER_1(fpop, void, env)
DEF_HELPER_1(fdecstp, void, env)
DEF_HELPER_1(fincstp, void, env)
DEF_HELPER_2(ffree_STN, void, env, int)
DEF_HELPER_1(fmov_ST0_FT0, void, env)
DEF_HELPER_2(fmov_FT0_STN, void, env, int)
DEF_HELPER_2(fmov_ST0_STN, void, env, int)
DEF_HELPER_2(fmov_STN_ST0, void, env, int)
DEF_HELPER_2(fxchg_ST0_STN, void, env, int)
DEF_HELPER_1(fcom_ST0_FT0, void, env)
DEF_HELPER_1(fucom_ST0_FT0, void, env)
DEF_HELPER_1(fcomi_ST0_FT0, void, env)
DEF_HELPER_1(fucomi_ST0_FT0, void, env)
DEF_HELPER_1(fadd_ST0_FT0, void, env)
DEF_HELPER_1(fmul_ST0_FT0, void, env)
DEF_HELPER_1(fsub_ST0_FT0, void, env)
DEF_HELPER_1(fsubr_ST0_FT0, void, env)
DEF_HELPER_1(fdiv_ST0_FT0, void, env)
DEF_HELPER_1(fdivr_ST0_FT0, void, env)
DEF_HELPER_2(fadd_STN_ST0, void, env, int)
DEF_HELPER_2(fmul_STN_ST0, void, env, int)
DEF_HELPER_2(fsub_STN_ST0, void, env, int)
DEF_HELPER_2(fsubr_STN_ST0, void, env, int)
DEF_HELPER_2(fdiv_STN_ST0, void, env, int)
DEF_HELPER_2(fdivr_STN_ST0, void, env, int)
DEF_HELPER_1(fchs_ST0, void, env)
DEF_HELPER_1(fabs_ST0, void, env)
DEF_HELPER_1(fxam_ST0, void, env)
DEF_HELPER_1(fld1_ST0, void, env)
DEF_HELPER_1(fldl2t_ST0, void, env)
DEF_HELPER_1(fldl2e_ST0, void, env)
DEF_HELPER_1(fldpi_ST0, void, env)
DEF_HELPER_1(fldlg2_ST0, void, env)
DEF_HELPER_1(fldln2_ST0, void, env)
DEF_HELPER_1(fldz_ST0, void, env)
DEF_HELPER_1(fldz_FT0, void, env)
DEF_HELPER_1(fnstsw, i32, env)
DEF_HELPER_1(fnstcw, i32, env)
DEF_HELPER_2(fldcw, void, env, i32)
DEF_HELPER_1(fclex, void, env)
DEF_HELPER_1(fwait, void, env)
#if __Use_Original_Qemu != 1 /* ours (U50/U52) */
DEF_HELPER_2(check_canonical_ip, void, env, tl)
DEF_HELPER_1(cvtpi2ps_m64_fwait, void, env)
#endif /* __Use_Original_Qemu (U50/U52) */
DEF_HELPER_1(fninit, void, env)
DEF_HELPER_2(fbld_ST0, void, env, tl)
DEF_HELPER_2(fbst_ST0, void, env, tl)
DEF_HELPER_1(f2xm1, void, env)
DEF_HELPER_1(fyl2x, void, env)
DEF_HELPER_1(fptan, void, env)
DEF_HELPER_1(fpatan, void, env)
DEF_HELPER_1(fxtract, void, env)
DEF_HELPER_1(fprem1, void, env)
DEF_HELPER_1(fprem, void, env)
DEF_HELPER_1(fyl2xp1, void, env)
DEF_HELPER_1(fsqrt, void, env)
DEF_HELPER_1(fsincos, void, env)
DEF_HELPER_1(frndint, void, env)
DEF_HELPER_1(fscale, void, env)
DEF_HELPER_1(fsin, void, env)
DEF_HELPER_1(fcos, void, env)
DEF_HELPER_3(fstenv, void, env, tl, int)
DEF_HELPER_3(fldenv, void, env, tl, int)
DEF_HELPER_3(fsave, void, env, tl, int)
DEF_HELPER_3(frstor, void, env, tl, int)
DEF_HELPER_FLAGS_2(fxsave, TCG_CALL_NO_WG, void, env, tl)
DEF_HELPER_FLAGS_2(fxrstor, TCG_CALL_NO_WG, void, env, tl)
DEF_HELPER_FLAGS_3(xsave, TCG_CALL_NO_WG, void, env, tl, i64)
DEF_HELPER_FLAGS_3(xsaveopt, TCG_CALL_NO_WG, void, env, tl, i64)
DEF_HELPER_FLAGS_3(xrstor, TCG_CALL_NO_WG, void, env, tl, i64)
DEF_HELPER_FLAGS_2(xgetbv, TCG_CALL_NO_WG, i64, env, i32)
DEF_HELPER_FLAGS_3(xsetbv, TCG_CALL_NO_WG, void, env, i32, i64)
DEF_HELPER_FLAGS_2(rdpkru, TCG_CALL_NO_WG, i64, env, i32)
DEF_HELPER_FLAGS_3(wrpkru, TCG_CALL_NO_WG, void, env, i32, i64)
#if __Use_Original_Qemu != 1 /* ours (U100) */
/* Key Locker (they write CC_SRC) */
DEF_HELPER_3(loadiwkey, void, env, i32, i32)
DEF_HELPER_3(encodekey, tl, env, tl, i32)
DEF_HELPER_4(aeskl, void, env, tl, i32, i32)
#endif /* __Use_Original_Qemu (U100) */
#if __Use_Original_Qemu != 1 /* ours (U103) */
/* URDMSR / UWRMSR (they run helper_rdmsr / helper_wrmsr, which may call hooks) */
DEF_HELPER_2(urdmsr, tl, env, tl)
DEF_HELPER_3(uwrmsr, void, env, tl, tl)
#endif /* __Use_Original_Qemu (U103) */
#if __Use_Original_Qemu != 1 /* ours (U104) */
/* user interrupts */
DEF_HELPER_1(clui, void, env)
DEF_HELPER_1(stui, void, env)
DEF_HELPER_1(testui, void, env)
DEF_HELPER_1(uiret, void, env)
DEF_HELPER_2(senduipi, void, env, tl)
#endif /* __Use_Original_Qemu (U104) */

DEF_HELPER_FLAGS_2(pdep, TCG_CALL_NO_RWG_SE, tl, tl, tl)
DEF_HELPER_FLAGS_2(pext, TCG_CALL_NO_RWG_SE, tl, tl, tl)

/* MMX/SSE */

DEF_HELPER_2(ldmxcsr, void, env, i32)
DEF_HELPER_1(update_mxcsr, void, env)
DEF_HELPER_1(enter_mmx, void, env)
#if __Use_Original_Qemu != 1 /* ours (U44) */
DEF_HELPER_1(cvtpi2ps_m64_enter_mmx, void, env)
#endif /* __Use_Original_Qemu (U44) */
DEF_HELPER_1(emms, void, env)
DEF_HELPER_3(movq, void, env, ptr, ptr)

#define SHIFT 0
#include "ops_sse_header.h"
#define SHIFT 1
#include "ops_sse_header.h"
#define SHIFT 2
#include "ops_sse_header.h"
#if __Use_Original_Qemu != 1 /* ours (U143) */
#define SHIFT 3
#include "ops_sse_header.h"
#endif /* __Use_Original_Qemu (U143) */

DEF_HELPER_3(rclb, tl, env, tl, tl)
DEF_HELPER_3(rclw, tl, env, tl, tl)
DEF_HELPER_3(rcll, tl, env, tl, tl)
DEF_HELPER_3(rcrb, tl, env, tl, tl)
DEF_HELPER_3(rcrw, tl, env, tl, tl)
DEF_HELPER_3(rcrl, tl, env, tl, tl)
#ifdef TARGET_X86_64
DEF_HELPER_3(rclq, tl, env, tl, tl)
DEF_HELPER_3(rcrq, tl, env, tl, tl)
#endif

DEF_HELPER_1(rdrand, tl, env)
#if __Use_Original_Qemu != 1 /* ours (U321) */
DEF_HELPER_4(evex_elem_unop, void, env, ptr, ptr, i32)
#endif /* __Use_Original_Qemu (U321) */
#if __Use_Original_Qemu != 1 /* ours (U324) */
DEF_HELPER_4(evex_shufbitqmb, i64, env, ptr, ptr, i32)
#endif /* __Use_Original_Qemu (U324) */
#if __Use_Original_Qemu != 1 /* ours (U325) */
DEF_HELPER_6(evex_permb, void, env, ptr, ptr, ptr, ptr, i32)
#endif /* __Use_Original_Qemu (U325) */
