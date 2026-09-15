#ifndef __KSU_H_EXEC_ARGS
#define __KSU_H_EXEC_ARGS

#include <asm/ptrace.h>

#include "arch.h"

/*
 * Register view of an execve(2)/execveat(2) call. Arm64 layout:
 *
 *   execve:   x0 = filename, x1 = argv, x2 = envp
 *   execveat: x0 = dirfd,    x1 = filename, x2 = argv, x3 = envp, x4 = flags
 *
 * Every exec hook uses this helper so the register mapping lives in exactly
 * one place instead of being spelled out at each call site.
 */
struct exec_args {
	const char __user *filename;
	const char __user *const __user *argv;
	unsigned long envp;
};

static inline struct exec_args ksu_exec_args_from_regs(const struct pt_regs *regs,
						      bool execveat)
{
	struct exec_args args;

	if (execveat) {
		args.filename = (const char __user *)PT_REGS_PARM2(regs);
		args.argv = (const char __user *const __user *)PT_REGS_PARM3(regs);
		args.envp = PT_REGS_SYSCALL_PARM4(regs);
	} else {
		args.filename = (const char __user *)PT_REGS_PARM1(regs);
		args.argv = (const char __user *const __user *)PT_REGS_PARM2(regs);
		args.envp = PT_REGS_PARM3(regs);
	}

	return args;
}

/*
 * Writable slot of the envp register, for consumers that replace the envp
 * pointer before the syscall runs (e.g. LD_PRELOAD injection).
 *
 * struct pt_regs stores the registers as u64 on arm64, so the slot is cast to
 * the type the callers write through (same width and alignment on LP64).
 */
static inline unsigned long *ksu_exec_envp_slot(struct pt_regs *regs, bool execveat)
{
	if (execveat)
		return (unsigned long *)&PT_REGS_SYSCALL_PARM4(regs);

	return (unsigned long *)&PT_REGS_PARM3(regs);
}

#endif /* __KSU_H_EXEC_ARGS */
