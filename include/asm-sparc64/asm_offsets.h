/* Automatically generated. Do not edit. */
#ifndef __ASM_OFFSETS_H__
#define __ASM_OFFSETS_H__

#ifndef __SMP__

#define AOFF_task_state	0x00000000
#define ASIZ_task_state	0x00000008
#define AOFF_task_flags	0x00000008
#define ASIZ_task_flags	0x00000008
#define AOFF_task_sigpending	0x00000010
#define ASIZ_task_sigpending	0x00000004
#define AOFF_task_addr_limit	0x00000018
#define ASIZ_task_addr_limit	0x00000008
#define AOFF_task_exec_domain	0x00000020
#define ASIZ_task_exec_domain	0x00000008
#define AOFF_task_debugreg	0x00000028
#define ASIZ_task_debugreg	0x00000040
#define AOFF_task_counter	0x00000068
#define ASIZ_task_counter	0x00000008
#define AOFF_task_priority	0x00000070
#define ASIZ_task_priority	0x00000008
#define AOFF_task_binfmt	0x00000078
#define ASIZ_task_binfmt	0x00000008
#define AOFF_task_next_task	0x00000080
#define ASIZ_task_next_task	0x00000008
#define AOFF_task_prev_task	0x00000088
#define ASIZ_task_prev_task	0x00000008
#define AOFF_task_next_run	0x00000090
#define ASIZ_task_next_run	0x00000008
#define AOFF_task_prev_run	0x00000098
#define ASIZ_task_prev_run	0x00000008
#define AOFF_task_exit_code	0x000000a0
#define ASIZ_task_exit_code	0x00000004
#define AOFF_task_exit_signal	0x000000a4
#define ASIZ_task_exit_signal	0x00000004
#define AOFF_task_pdeath_signal	0x000000a8
#define ASIZ_task_pdeath_signal	0x00000004
#define AOFF_task_personality	0x000000b0
#define ASIZ_task_personality	0x00000008
#define AOFF_task_pid	0x000000bc
#define ASIZ_task_pid	0x00000004
#define AOFF_task_pgrp	0x000000c0
#define ASIZ_task_pgrp	0x00000004
#define AOFF_task_tty_old_pgrp	0x000000c4
#define ASIZ_task_tty_old_pgrp	0x00000004
#define AOFF_task_session	0x000000c8
#define ASIZ_task_session	0x00000004
#define AOFF_task_leader	0x000000cc
#define ASIZ_task_leader	0x00000004
#define AOFF_task_p_opptr	0x000000d0
#define ASIZ_task_p_opptr	0x00000008
#define AOFF_task_p_pptr	0x000000d8
#define ASIZ_task_p_pptr	0x00000008
#define AOFF_task_p_cptr	0x000000e0
#define ASIZ_task_p_cptr	0x00000008
#define AOFF_task_p_ysptr	0x000000e8
#define ASIZ_task_p_ysptr	0x00000008
#define AOFF_task_p_osptr	0x000000f0
#define ASIZ_task_p_osptr	0x00000008
#define AOFF_task_pidhash_next	0x000000f8
#define ASIZ_task_pidhash_next	0x00000008
#define AOFF_task_pidhash_pprev	0x00000100
#define ASIZ_task_pidhash_pprev	0x00000008
#define AOFF_task_tarray_ptr	0x00000108
#define ASIZ_task_tarray_ptr	0x00000008
#define AOFF_task_wait_chldexit	0x00000110
#define ASIZ_task_wait_chldexit	0x00000008
#define AOFF_task_timeout	0x00000118
#define ASIZ_task_timeout	0x00000008
#define AOFF_task_policy	0x00000120
#define ASIZ_task_policy	0x00000008
#define AOFF_task_rt_priority	0x00000128
#define ASIZ_task_rt_priority	0x00000008
#define AOFF_task_it_real_value	0x00000130
#define ASIZ_task_it_real_value	0x00000008
#define AOFF_task_it_prof_value	0x00000138
#define ASIZ_task_it_prof_value	0x00000008
#define AOFF_task_it_virt_value	0x00000140
#define ASIZ_task_it_virt_value	0x00000008
#define AOFF_task_it_real_incr	0x00000148
#define ASIZ_task_it_real_incr	0x00000008
#define AOFF_task_it_prof_incr	0x00000150
#define ASIZ_task_it_prof_incr	0x00000008
#define AOFF_task_it_virt_incr	0x00000158
#define ASIZ_task_it_virt_incr	0x00000008
#define AOFF_task_real_timer	0x00000160
#define ASIZ_task_real_timer	0x00000028
#define AOFF_task_times	0x00000188
#define ASIZ_task_times	0x00000020
#define AOFF_task_start_time	0x000001a8
#define ASIZ_task_start_time	0x00000008
#define AOFF_task_per_cpu_utime	0x000001b0
#define ASIZ_task_per_cpu_utime	0x00000008
#define AOFF_task_min_flt	0x000001c0
#define ASIZ_task_min_flt	0x00000008
#define AOFF_task_maj_flt	0x000001c8
#define ASIZ_task_maj_flt	0x00000008
#define AOFF_task_nswap	0x000001d0
#define ASIZ_task_nswap	0x00000008
#define AOFF_task_cmin_flt	0x000001d8
#define ASIZ_task_cmin_flt	0x00000008
#define AOFF_task_cmaj_flt	0x000001e0
#define ASIZ_task_cmaj_flt	0x00000008
#define AOFF_task_cnswap	0x000001e8
#define ASIZ_task_cnswap	0x00000008
#define AOFF_task_swap_address	0x000001f8
#define ASIZ_task_swap_address	0x00000008
#define AOFF_task_old_maj_flt	0x00000200
#define ASIZ_task_old_maj_flt	0x00000008
#define AOFF_task_dec_flt	0x00000208
#define ASIZ_task_dec_flt	0x00000008
#define AOFF_task_swap_cnt	0x00000210
#define ASIZ_task_swap_cnt	0x00000008
#define AOFF_task_uid	0x00000218
#define ASIZ_task_uid	0x00000004
#define AOFF_task_euid	0x0000021c
#define ASIZ_task_euid	0x00000004
#define AOFF_task_suid	0x00000220
#define ASIZ_task_suid	0x00000004
#define AOFF_task_fsuid	0x00000224
#define ASIZ_task_fsuid	0x00000004
#define AOFF_task_gid	0x00000228
#define ASIZ_task_gid	0x00000004
#define AOFF_task_egid	0x0000022c
#define ASIZ_task_egid	0x00000004
#define AOFF_task_sgid	0x00000230
#define ASIZ_task_sgid	0x00000004
#define AOFF_task_fsgid	0x00000234
#define ASIZ_task_fsgid	0x00000004
#define AOFF_task_ngroups	0x00000238
#define ASIZ_task_ngroups	0x00000004
#define AOFF_task_groups	0x0000023c
#define ASIZ_task_groups	0x00000080
#define AOFF_task_cap_effective	0x000002bc
#define ASIZ_task_cap_effective	0x00000004
#define AOFF_task_cap_inheritable	0x000002c0
#define ASIZ_task_cap_inheritable	0x00000004
#define AOFF_task_cap_permitted	0x000002c4
#define ASIZ_task_cap_permitted	0x00000004
#define AOFF_task_rlim	0x000002c8
#define ASIZ_task_rlim	0x000000a0
#define AOFF_task_used_math	0x00000368
#define ASIZ_task_used_math	0x00000002
#define AOFF_task_comm	0x0000036a
#define ASIZ_task_comm	0x00000010
#define AOFF_task_link_count	0x0000037c
#define ASIZ_task_link_count	0x00000004
#define AOFF_task_tty	0x00000380
#define ASIZ_task_tty	0x00000008
#define AOFF_task_semundo	0x00000388
#define ASIZ_task_semundo	0x00000008
#define AOFF_task_semsleeping	0x00000390
#define ASIZ_task_semsleeping	0x00000008
#define AOFF_task_tss	0x000003a0
#define ASIZ_task_tss	0x00000490
#define AOFF_task_fs	0x00000830
#define ASIZ_task_fs	0x00000008
#define AOFF_task_files	0x00000838
#define ASIZ_task_files	0x00000008
#define AOFF_task_mm	0x00000840
#define ASIZ_task_mm	0x00000008
#define AOFF_task_sig	0x00000848
#define ASIZ_task_sig	0x00000008
#define AOFF_task_signal	0x00000850
#define ASIZ_task_signal	0x00000008
#define AOFF_task_blocked	0x00000858
#define ASIZ_task_blocked	0x00000008
#define AOFF_task_sigqueue	0x00000860
#define ASIZ_task_sigqueue	0x00000008
#define AOFF_task_sigqueue_tail	0x00000868
#define ASIZ_task_sigqueue_tail	0x00000008
#define AOFF_task_has_cpu	0x00000870
#define ASIZ_task_has_cpu	0x00000004
#define AOFF_task_processor	0x00000874
#define ASIZ_task_processor	0x00000004
#define AOFF_task_last_processor	0x00000878
#define ASIZ_task_last_processor	0x00000004
#define AOFF_task_lock_depth	0x0000087c
#define ASIZ_task_lock_depth	0x00000004
#define AOFF_task_sigmask_lock	0x00000880
#define ASIZ_task_sigmask_lock	0x00000000
#define AOFF_mm_mmap	0x00000000
#define ASIZ_mm_mmap	0x00000008
#define AOFF_mm_mmap_cache	0x00000008
#define ASIZ_mm_mmap_cache	0x00000008
#define AOFF_mm_pgd	0x00000010
#define ASIZ_mm_pgd	0x00000008
#define AOFF_mm_count	0x00000018
#define ASIZ_mm_count	0x00000004
#define AOFF_mm_map_count	0x0000001c
#define ASIZ_mm_map_count	0x00000004
#define AOFF_mm_mmap_sem	0x00000020
#define ASIZ_mm_mmap_sem	0x00000010
#define AOFF_mm_context	0x00000030
#define ASIZ_mm_context	0x00000008
#define AOFF_mm_start_code	0x00000038
#define ASIZ_mm_start_code	0x00000008
#define AOFF_mm_end_code	0x00000040
#define ASIZ_mm_end_code	0x00000008
#define AOFF_mm_start_data	0x00000048
#define ASIZ_mm_start_data	0x00000008
#define AOFF_mm_end_data	0x00000050
#define ASIZ_mm_end_data	0x00000008
#define AOFF_mm_start_brk	0x00000058
#define ASIZ_mm_start_brk	0x00000008
#define AOFF_mm_brk	0x00000060
#define ASIZ_mm_brk	0x00000008
#define AOFF_mm_start_stack	0x00000068
#define ASIZ_mm_start_stack	0x00000008
#define AOFF_mm_arg_start	0x00000070
#define ASIZ_mm_arg_start	0x00000008
#define AOFF_mm_arg_end	0x00000078
#define ASIZ_mm_arg_end	0x00000008
#define AOFF_mm_env_start	0x00000080
#define ASIZ_mm_env_start	0x00000008
#define AOFF_mm_env_end	0x00000088
#define ASIZ_mm_env_end	0x00000008
#define AOFF_mm_rss	0x00000090
#define ASIZ_mm_rss	0x00000008
#define AOFF_mm_total_vm	0x00000098
#define ASIZ_mm_total_vm	0x00000008
#define AOFF_mm_locked_vm	0x000000a0
#define ASIZ_mm_locked_vm	0x00000008
#define AOFF_mm_def_flags	0x000000a8
#define ASIZ_mm_def_flags	0x00000008
#define AOFF_mm_cpu_vm_mask	0x000000b0
#define ASIZ_mm_cpu_vm_mask	0x00000008
#define AOFF_mm_segments	0x000000b8
#define ASIZ_mm_segments	0x00000008
#define AOFF_thread_ksp	0x00000000
#define ASIZ_thread_ksp	0x00000008
#define AOFF_thread_kpc	0x00000008
#define ASIZ_thread_kpc	0x00000004
#define AOFF_thread_wstate	0x0000000c
#define ASIZ_thread_wstate	0x00000002
#define AOFF_thread_cwp	0x0000000e
#define ASIZ_thread_cwp	0x00000002
#define AOFF_thread_flags	0x00000010
#define ASIZ_thread_flags	0x00000002
#define AOFF_thread_ctx	0x00000012
#define ASIZ_thread_ctx	0x00000002
#define AOFF_thread_w_saved	0x00000014
#define ASIZ_thread_w_saved	0x00000002
#define AOFF_thread_new_signal	0x00000016
#define ASIZ_thread_new_signal	0x00000002
#define AOFF_thread_current_ds	0x00000018
#define ASIZ_thread_current_ds	0x00000008
#define AOFF_thread_kregs	0x00000020
#define ASIZ_thread_kregs	0x00000008
#define AOFF_thread_utraps	0x00000028
#define ASIZ_thread_utraps	0x00000008
#define AOFF_thread_reg_window	0x00000030
#define ASIZ_thread_reg_window	0x00000400
#define AOFF_thread_rwbuf_stkptrs	0x00000430
#define ASIZ_thread_rwbuf_stkptrs	0x00000040
#define AOFF_thread_sig_address	0x00000470
#define ASIZ_thread_sig_address	0x00000008
#define AOFF_thread_sig_desc	0x00000478
#define ASIZ_thread_sig_desc	0x00000008
#define AOFF_thread_sstk_info	0x00000480
#define ASIZ_thread_sstk_info	0x00000010

#else /* __SMP__ */

#define AOFF_task_state	0x00000000
#define ASIZ_task_state	0x00000008
#define AOFF_task_flags	0x00000008
#define ASIZ_task_flags	0x00000008
#define AOFF_task_sigpending	0x00000010
#define ASIZ_task_sigpending	0x00000004
#define AOFF_task_addr_limit	0x00000018
#define ASIZ_task_addr_limit	0x00000008
#define AOFF_task_exec_domain	0x00000020
#define ASIZ_task_exec_domain	0x00000008
#define AOFF_task_debugreg	0x00000028
#define ASIZ_task_debugreg	0x00000040
#define AOFF_task_counter	0x00000068
#define ASIZ_task_counter	0x00000008
#define AOFF_task_priority	0x00000070
#define ASIZ_task_priority	0x00000008
#define AOFF_task_binfmt	0x00000078
#define ASIZ_task_binfmt	0x00000008
#define AOFF_task_next_task	0x00000080
#define ASIZ_task_next_task	0x00000008
#define AOFF_task_prev_task	0x00000088
#define ASIZ_task_prev_task	0x00000008
#define AOFF_task_next_run	0x00000090
#define ASIZ_task_next_run	0x00000008
#define AOFF_task_prev_run	0x00000098
#define ASIZ_task_prev_run	0x00000008
#define AOFF_task_exit_code	0x000000a0
#define ASIZ_task_exit_code	0x00000004
#define AOFF_task_exit_signal	0x000000a4
#define ASIZ_task_exit_signal	0x00000004
#define AOFF_task_pdeath_signal	0x000000a8
#define ASIZ_task_pdeath_signal	0x00000004
#define AOFF_task_personality	0x000000b0
#define ASIZ_task_personality	0x00000008
#define AOFF_task_pid	0x000000bc
#define ASIZ_task_pid	0x00000004
#define AOFF_task_pgrp	0x000000c0
#define ASIZ_task_pgrp	0x00000004
#define AOFF_task_tty_old_pgrp	0x000000c4
#define ASIZ_task_tty_old_pgrp	0x00000004
#define AOFF_task_session	0x000000c8
#define ASIZ_task_session	0x00000004
#define AOFF_task_leader	0x000000cc
#define ASIZ_task_leader	0x00000004
#define AOFF_task_p_opptr	0x000000d0
#define ASIZ_task_p_opptr	0x00000008
#define AOFF_task_p_pptr	0x000000d8
#define ASIZ_task_p_pptr	0x00000008
#define AOFF_task_p_cptr	0x000000e0
#define ASIZ_task_p_cptr	0x00000008
#define AOFF_task_p_ysptr	0x000000e8
#define ASIZ_task_p_ysptr	0x00000008
#define AOFF_task_p_osptr	0x000000f0
#define ASIZ_task_p_osptr	0x00000008
#define AOFF_task_pidhash_next	0x000000f8
#define ASIZ_task_pidhash_next	0x00000008
#define AOFF_task_pidhash_pprev	0x00000100
#define ASIZ_task_pidhash_pprev	0x00000008
#define AOFF_task_tarray_ptr	0x00000108
#define ASIZ_task_tarray_ptr	0x00000008
#define AOFF_task_wait_chldexit	0x00000110
#define ASIZ_task_wait_chldexit	0x00000008
#define AOFF_task_timeout	0x00000118
#define ASIZ_task_timeout	0x00000008
#define AOFF_task_policy	0x00000120
#define ASIZ_task_policy	0x00000008
#define AOFF_task_rt_priority	0x00000128
#define ASIZ_task_rt_priority	0x00000008
#define AOFF_task_it_real_value	0x00000130
#define ASIZ_task_it_real_value	0x00000008
#define AOFF_task_it_prof_value	0x00000138
#define ASIZ_task_it_prof_value	0x00000008
#define AOFF_task_it_virt_value	0x00000140
#define ASIZ_task_it_virt_value	0x00000008
#define AOFF_task_it_real_incr	0x00000148
#define ASIZ_task_it_real_incr	0x00000008
#define AOFF_task_it_prof_incr	0x00000150
#define ASIZ_task_it_prof_incr	0x00000008
#define AOFF_task_it_virt_incr	0x00000158
#define ASIZ_task_it_virt_incr	0x00000008
#define AOFF_task_real_timer	0x00000160
#define ASIZ_task_real_timer	0x00000028
#define AOFF_task_times	0x00000188
#define ASIZ_task_times	0x00000020
#define AOFF_task_start_time	0x000001a8
#define ASIZ_task_start_time	0x00000008
#define AOFF_task_per_cpu_utime	0x000001b0
#define ASIZ_task_per_cpu_utime	0x00000100
#define AOFF_task_min_flt	0x000003b0
#define ASIZ_task_min_flt	0x00000008
#define AOFF_task_maj_flt	0x000003b8
#define ASIZ_task_maj_flt	0x00000008
#define AOFF_task_nswap	0x000003c0
#define ASIZ_task_nswap	0x00000008
#define AOFF_task_cmin_flt	0x000003c8
#define ASIZ_task_cmin_flt	0x00000008
#define AOFF_task_cmaj_flt	0x000003d0
#define ASIZ_task_cmaj_flt	0x00000008
#define AOFF_task_cnswap	0x000003d8
#define ASIZ_task_cnswap	0x00000008
#define AOFF_task_swap_address	0x000003e8
#define ASIZ_task_swap_address	0x00000008
#define AOFF_task_old_maj_flt	0x000003f0
#define ASIZ_task_old_maj_flt	0x00000008
#define AOFF_task_dec_flt	0x000003f8
#define ASIZ_task_dec_flt	0x00000008
#define AOFF_task_swap_cnt	0x00000400
#define ASIZ_task_swap_cnt	0x00000008
#define AOFF_task_uid	0x00000408
#define ASIZ_task_uid	0x00000004
#define AOFF_task_euid	0x0000040c
#define ASIZ_task_euid	0x00000004
#define AOFF_task_suid	0x00000410
#define ASIZ_task_suid	0x00000004
#define AOFF_task_fsuid	0x00000414
#define ASIZ_task_fsuid	0x00000004
#define AOFF_task_gid	0x00000418
#define ASIZ_task_gid	0x00000004
#define AOFF_task_egid	0x0000041c
#define ASIZ_task_egid	0x00000004
#define AOFF_task_sgid	0x00000420
#define ASIZ_task_sgid	0x00000004
#define AOFF_task_fsgid	0x00000424
#define ASIZ_task_fsgid	0x00000004
#define AOFF_task_ngroups	0x00000428
#define ASIZ_task_ngroups	0x00000004
#define AOFF_task_groups	0x0000042c
#define ASIZ_task_groups	0x00000080
#define AOFF_task_cap_effective	0x000004ac
#define ASIZ_task_cap_effective	0x00000004
#define AOFF_task_cap_inheritable	0x000004b0
#define ASIZ_task_cap_inheritable	0x00000004
#define AOFF_task_cap_permitted	0x000004b4
#define ASIZ_task_cap_permitted	0x00000004
#define AOFF_task_rlim	0x000004b8
#define ASIZ_task_rlim	0x000000a0
#define AOFF_task_used_math	0x00000558
#define ASIZ_task_used_math	0x00000002
#define AOFF_task_comm	0x0000055a
#define ASIZ_task_comm	0x00000010
#define AOFF_task_link_count	0x0000056c
#define ASIZ_task_link_count	0x00000004
#define AOFF_task_tty	0x00000570
#define ASIZ_task_tty	0x00000008
#define AOFF_task_semundo	0x00000578
#define ASIZ_task_semundo	0x00000008
#define AOFF_task_semsleeping	0x00000580
#define ASIZ_task_semsleeping	0x00000008
#define AOFF_task_tss	0x00000590
#define ASIZ_task_tss	0x00000490
#define AOFF_task_fs	0x00000a20
#define ASIZ_task_fs	0x00000008
#define AOFF_task_files	0x00000a28
#define ASIZ_task_files	0x00000008
#define AOFF_task_mm	0x00000a30
#define ASIZ_task_mm	0x00000008
#define AOFF_task_sig	0x00000a38
#define ASIZ_task_sig	0x00000008
#define AOFF_task_signal	0x00000a40
#define ASIZ_task_signal	0x00000008
#define AOFF_task_blocked	0x00000a48
#define ASIZ_task_blocked	0x00000008
#define AOFF_task_sigqueue	0x00000a50
#define ASIZ_task_sigqueue	0x00000008
#define AOFF_task_sigqueue_tail	0x00000a58
#define ASIZ_task_sigqueue_tail	0x00000008
#define AOFF_task_has_cpu	0x00000a60
#define ASIZ_task_has_cpu	0x00000004
#define AOFF_task_processor	0x00000a64
#define ASIZ_task_processor	0x00000004
#define AOFF_task_last_processor	0x00000a68
#define ASIZ_task_last_processor	0x00000004
#define AOFF_task_lock_depth	0x00000a6c
#define ASIZ_task_lock_depth	0x00000004
#define AOFF_task_sigmask_lock	0x00000a70
#define ASIZ_task_sigmask_lock	0x00000001
#define AOFF_mm_mmap	0x00000000
#define ASIZ_mm_mmap	0x00000008
#define AOFF_mm_mmap_cache	0x00000008
#define ASIZ_mm_mmap_cache	0x00000008
#define AOFF_mm_pgd	0x00000010
#define ASIZ_mm_pgd	0x00000008
#define AOFF_mm_count	0x00000018
#define ASIZ_mm_count	0x00000004
#define AOFF_mm_map_count	0x0000001c
#define ASIZ_mm_map_count	0x00000004
#define AOFF_mm_mmap_sem	0x00000020
#define ASIZ_mm_mmap_sem	0x00000010
#define AOFF_mm_context	0x00000030
#define ASIZ_mm_context	0x00000008
#define AOFF_mm_start_code	0x00000038
#define ASIZ_mm_start_code	0x00000008
#define AOFF_mm_end_code	0x00000040
#define ASIZ_mm_end_code	0x00000008
#define AOFF_mm_start_data	0x00000048
#define ASIZ_mm_start_data	0x00000008
#define AOFF_mm_end_data	0x00000050
#define ASIZ_mm_end_data	0x00000008
#define AOFF_mm_start_brk	0x00000058
#define ASIZ_mm_start_brk	0x00000008
#define AOFF_mm_brk	0x00000060
#define ASIZ_mm_brk	0x00000008
#define AOFF_mm_start_stack	0x00000068
#define ASIZ_mm_start_stack	0x00000008
#define AOFF_mm_arg_start	0x00000070
#define ASIZ_mm_arg_start	0x00000008
#define AOFF_mm_arg_end	0x00000078
#define ASIZ_mm_arg_end	0x00000008
#define AOFF_mm_env_start	0x00000080
#define ASIZ_mm_env_start	0x00000008
#define AOFF_mm_env_end	0x00000088
#define ASIZ_mm_env_end	0x00000008
#define AOFF_mm_rss	0x00000090
#define ASIZ_mm_rss	0x00000008
#define AOFF_mm_total_vm	0x00000098
#define ASIZ_mm_total_vm	0x00000008
#define AOFF_mm_locked_vm	0x000000a0
#define ASIZ_mm_locked_vm	0x00000008
#define AOFF_mm_def_flags	0x000000a8
#define ASIZ_mm_def_flags	0x00000008
#define AOFF_mm_cpu_vm_mask	0x000000b0
#define ASIZ_mm_cpu_vm_mask	0x00000008
#define AOFF_mm_segments	0x000000b8
#define ASIZ_mm_segments	0x00000008
#define AOFF_thread_ksp	0x00000000
#define ASIZ_thread_ksp	0x00000008
#define AOFF_thread_kpc	0x00000008
#define ASIZ_thread_kpc	0x00000004
#define AOFF_thread_wstate	0x0000000c
#define ASIZ_thread_wstate	0x00000002
#define AOFF_thread_cwp	0x0000000e
#define ASIZ_thread_cwp	0x00000002
#define AOFF_thread_flags	0x00000010
#define ASIZ_thread_flags	0x00000002
#define AOFF_thread_ctx	0x00000012
#define ASIZ_thread_ctx	0x00000002
#define AOFF_thread_w_saved	0x00000014
#define ASIZ_thread_w_saved	0x00000002
#define AOFF_thread_new_signal	0x00000016
#define ASIZ_thread_new_signal	0x00000002
#define AOFF_thread_current_ds	0x00000018
#define ASIZ_thread_current_ds	0x00000008
#define AOFF_thread_kregs	0x00000020
#define ASIZ_thread_kregs	0x00000008
#define AOFF_thread_utraps	0x00000028
#define ASIZ_thread_utraps	0x00000008
#define AOFF_thread_reg_window	0x00000030
#define ASIZ_thread_reg_window	0x00000400
#define AOFF_thread_rwbuf_stkptrs	0x00000430
#define ASIZ_thread_rwbuf_stkptrs	0x00000040
#define AOFF_thread_sig_address	0x00000470
#define ASIZ_thread_sig_address	0x00000008
#define AOFF_thread_sig_desc	0x00000478
#define ASIZ_thread_sig_desc	0x00000008
#define AOFF_thread_sstk_info	0x00000480
#define ASIZ_thread_sstk_info	0x00000010

#endif /* __SMP__ */

#endif /* __ASM_OFFSETS_H__ */
