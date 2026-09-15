#include "linux/printk.h"
#include <linux/spinlock.h>
#include <linux/kprobes.h>
#include <linux/tracepoint.h>
#include <asm/syscall.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <linux/task_work.h>
#include <trace/events/syscalls.h>

#include <linux/version.h>
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 7, 0)
#include <linux/compat.h>
#include <linux/sched/task_stack.h>
#endif

#include "arch.h"
#include "klog.h" // IWYU pragma: keep
#include "hook/syscall_hook_manager.h"
#include "hook/tp_marker.h"
#include "feature/sucompat.h"
#include "hook/setuid_hook.h"
#include "hook/syscall_hook.h"
#include "hook/syscall_event_bridge.h"
#include "policy/allowlist.h"

static bool syscall_hook_manager_initialized;

#if defined(CONFIG_KSU_SAMSUNG_RKP) && defined(CONFIG_KRETPROBES) && defined(__aarch64__)
struct ksu_setresuid_task_work {
    struct callback_head callback;
    uid_t old_uid;
    uid_t new_uid;
};

static bool setresuid_kretprobe_registered;
static bool samsung_sucompat_kprobes_registered;

#define SAMSUNG_SUCOMPAT_BYPASS_NR (-2)

static bool samsung_sucompat_should_redirect(int syscall_nr)
{
    struct pt_regs *syscall_regs = task_pt_regs(current);

    if (unlikely(syscall_regs->syscallno == SAMSUNG_SUCOMPAT_BYPASS_NR)) {
        syscall_regs->syscallno = syscall_nr;
        return false;
    }

    return ksu_su_compat_enabled &&
           ksu_is_allow_uid_for_current(current_uid().val);
}

/* One kprobe per syscall entry. The pre-handler redirects execution to the
 * matching thunk, which re-enters the shared dispatcher with the bypass
 * sentinel set, so the nested syscall-table call cannot recurse.
 *
 * Thunk, pre-handler and kprobe only differ in the syscall number and the
 * dispatcher entry they call, so they are generated from a single definition
 * that keeps the register/number mapping in one place.
 */
#define SAMSUNG_SUCOMPAT_ENTRY(name, nr, hook)					\
	static long __nocfi samsung_sucompat_##name(const struct pt_regs *regs)	\
	{									\
		struct pt_regs *syscall_regs = (struct pt_regs *)regs;		\
		int saved_nr = syscall_regs->syscallno;				\
		long ret;							\
										\
		syscall_regs->syscallno = SAMSUNG_SUCOMPAT_BYPASS_NR;		\
		ret = hook(nr, regs);						\
		syscall_regs->syscallno = saved_nr;				\
		return ret;							\
	}									\
										\
	static int samsung_sucompat_##name##_pre_handler(struct kprobe *probe,	\
							 struct pt_regs *regs)	\
	{									\
		(void)probe;							\
		if (!samsung_sucompat_should_redirect(nr))			\
			return 0;						\
										\
		instruction_pointer_set(regs,					\
					(unsigned long)samsung_sucompat_##name);	\
		return 1;							\
	}									\
										\
	static struct kprobe samsung_sucompat_##name##_kprobe = {		\
		.pre_handler = samsung_sucompat_##name##_pre_handler,		\
	}

SAMSUNG_SUCOMPAT_ENTRY(execve, __NR_execve, ksu_hook_execve);
SAMSUNG_SUCOMPAT_ENTRY(execveat, __NR_execveat, ksu_hook_execveat);
SAMSUNG_SUCOMPAT_ENTRY(newfstatat, __NR_newfstatat, ksu_hook_newfstatat);
SAMSUNG_SUCOMPAT_ENTRY(faccessat, __NR_faccessat, ksu_hook_faccessat);
/* statx(2) passes the pathname in x1, exactly like newfstatat(2). */
SAMSUNG_SUCOMPAT_ENTRY(statx, __NR_statx, ksu_hook_newfstatat);
/* faccessat2(2) passes the pathname in x1 as well. */
SAMSUNG_SUCOMPAT_ENTRY(faccessat2, __NR_faccessat2, ksu_hook_faccessat);

#undef SAMSUNG_SUCOMPAT_ENTRY

struct samsung_sucompat_probe {
	int nr;
	struct kprobe *kp;
};

static struct samsung_sucompat_probe samsung_sucompat_probes[] = {
	{ .nr = __NR_execve, .kp = &samsung_sucompat_execve_kprobe },
	{ .nr = __NR_execveat, .kp = &samsung_sucompat_execveat_kprobe },
	{ .nr = __NR_newfstatat, .kp = &samsung_sucompat_newfstatat_kprobe },
	{ .nr = __NR_faccessat, .kp = &samsung_sucompat_faccessat_kprobe },
	{ .nr = __NR_statx, .kp = &samsung_sucompat_statx_kprobe },
	{ .nr = __NR_faccessat2, .kp = &samsung_sucompat_faccessat2_kprobe },
};

static int samsung_sucompat_hook_init(void)
{
	size_t i;
	int ret;

	if (!ksu_syscall_table)
		return -ENOENT;

	for (i = 0; i < ARRAY_SIZE(samsung_sucompat_probes); i++)
		samsung_sucompat_probes[i].kp->addr =
			(kprobe_opcode_t *)READ_ONCE(ksu_syscall_table[samsung_sucompat_probes[i].nr]);

	ksu_sucompat_init();

	for (i = 0; i < ARRAY_SIZE(samsung_sucompat_probes); i++) {
		ret = register_kprobe(samsung_sucompat_probes[i].kp);
		if (ret)
			goto unregister;
	}

	samsung_sucompat_kprobes_registered = true;
	pr_info("hook_manager: Samsung sucompat kprobes registered\n");
	return 0;

unregister:
	while (i > 0) {
		i--;
		unregister_kprobe(samsung_sucompat_probes[i].kp);
	}
	ksu_sucompat_exit();
	return ret;
}

static void samsung_sucompat_hook_exit(void)
{
	size_t i;

	if (!samsung_sucompat_kprobes_registered)
		return;

	for (i = ARRAY_SIZE(samsung_sucompat_probes); i > 0; i--)
		unregister_kprobe(samsung_sucompat_probes[i - 1].kp);

	samsung_sucompat_kprobes_registered = false;
	ksu_sucompat_exit();
}

static void setresuid_task_work_func(struct callback_head *callback)
{
    struct ksu_setresuid_task_work *work = container_of(callback, struct ksu_setresuid_task_work, callback);

    ksu_handle_setresuid(work->old_uid, work->new_uid);
    kfree(work);
}

static int setresuid_entry_handler(struct kretprobe_instance *ri, struct pt_regs *regs)
{
    *(uid_t *)ri->data = current_uid().val;
    return 0;
}

static int setresuid_return_handler(struct kretprobe_instance *ri, struct pt_regs *regs)
{
    struct ksu_setresuid_task_work *work;
    uid_t old_uid = *(uid_t *)ri->data;
    uid_t new_uid;

    if (regs_return_value(regs) < 0)
        return 0;

    new_uid = current_uid().val;
    if (old_uid == new_uid)
        return 0;

    work = kzalloc(sizeof(*work), GFP_ATOMIC);
    if (!work)
        return 0;

    work->old_uid = old_uid;
    work->new_uid = new_uid;
    work->callback.func = setresuid_task_work_func;

    if (task_work_add(current, &work->callback, TWA_RESUME))
        kfree(work);

    return 0;
}

static struct kretprobe setresuid_kretprobe = {
    .kp.symbol_name = "__arm64_sys_setresuid",
    .entry_handler = setresuid_entry_handler,
    .handler = setresuid_return_handler,
    .data_size = sizeof(uid_t),
};

static int samsung_setresuid_hook_init(void)
{
    int ret = register_kretprobe(&setresuid_kretprobe);

    if (ret) {
        pr_err("hook_manager: Samsung setresuid kretprobe failed: %d\n", ret);
        return ret;
    }

    setresuid_kretprobe_registered = true;
    ksu_setuid_hook_init();
    pr_info("hook_manager: Samsung setresuid kretprobe registered\n");
    return 0;
}

static void samsung_setresuid_hook_exit(void)
{
    if (!setresuid_kretprobe_registered)
        return;

    unregister_kretprobe(&setresuid_kretprobe);
    setresuid_kretprobe_registered = false;
    ksu_setuid_hook_exit();
}
#endif

#ifdef CONFIG_KRETPROBES

static struct kretprobe *init_kretprobe(const char *name, kretprobe_handler_t handler)
{
    struct kretprobe *rp = kzalloc(sizeof(struct kretprobe), GFP_KERNEL);
    if (!rp)
        return NULL;
    rp->kp.symbol_name = name;
    rp->handler = handler;
    rp->data_size = 0;
    rp->maxactive = 0;

    int ret = register_kretprobe(rp);
    pr_info("hook_manager: register_%s kretprobe: %d\n", name, ret);
    if (ret) {
        kfree(rp);
        return NULL;
    }

    return rp;
}

static void destroy_kretprobe(struct kretprobe **rp_ptr)
{
    struct kretprobe *rp = *rp_ptr;
    if (!rp)
        return;
    unregister_kretprobe(rp);
    synchronize_rcu();
    kfree(rp);
    *rp_ptr = NULL;
}

static int syscall_regfunc_handler(struct kretprobe_instance *ri, struct pt_regs *regs)
{
    unsigned long flags;
    ksu_tp_marker_lock(&flags);
    if (ksu_tp_marker_reg_count() < 1) {
        // while install our tracepoint, mark our processes
        ksu_mark_running_process_locked();
    } else if (ksu_tp_marker_reg_count() == 1) {
        // while other tracepoint first added, mark all processes
        ksu_mark_all_process();
    }
    ksu_tp_marker_inc_reg_count();
    ksu_tp_marker_unlock(&flags);
    return 0;
}

static int syscall_unregfunc_handler(struct kretprobe_instance *ri, struct pt_regs *regs)
{
    unsigned long flags;
    ksu_tp_marker_lock(&flags);
    ksu_tp_marker_dec_reg_count();
    if (ksu_tp_marker_reg_count() <= 0) {
        // while no tracepoint left, unmark all processes
        ksu_unmark_all_process();
    } else if (ksu_tp_marker_reg_count() == 1) {
        // while just our tracepoint left, unmark disallowed processes
        ksu_mark_running_process_locked();
    }
    ksu_tp_marker_unlock(&flags);
    return 0;
}

static struct kretprobe *syscall_regfunc_rp = NULL;
static struct kretprobe *syscall_unregfunc_rp = NULL;
#endif

#ifdef CONFIG_HAVE_SYSCALL_TRACEPOINTS
// sys_enter handler: redirect hooked syscalls to the dispatcher
static void ksu_sys_enter_handler(void *data, struct pt_regs *regs, long id)
{
#if defined(__x86_64__)
    if (unlikely(in_compat_syscall()))
#elif defined(__aarch64__)
    if (unlikely(is_compat_task()))
#endif
        return;

    if (ksu_dispatcher_nr < 0)
        return;

    if (ksu_has_syscall_hook(id)) {
        struct pt_regs *current_regs = task_pt_regs(current);

#if defined(__x86_64__)
        // Stash the original syscall number in ax.
        // We use ax because it currently just holds -ENOSYS and is safe to overwrite.
        current_regs->ax = id;
        current_regs->orig_ax = ksu_dispatcher_nr;
#elif defined(__aarch64__)
        PT_REGS_ORIG_SYSCALL(current_regs) = id;
        current_regs->syscallno = ksu_dispatcher_nr;
#endif
    }
}
#endif

void __init ksu_syscall_hook_manager_init(void)
{
	int ret;
	pr_info("hook_manager: ksu_hook_manager_init called\n");

	if (ksu_dispatcher_nr < 0) {
		pr_warn("hook_manager: dispatcher unavailable; syscall event hooks disabled\n");
#if defined(CONFIG_KSU_SAMSUNG_RKP) && defined(CONFIG_KRETPROBES) && defined(__aarch64__)
		samsung_setresuid_hook_init();
		ret = samsung_sucompat_hook_init();
		if (ret)
			pr_err("hook_manager: Samsung sucompat hook init failed: %d\n", ret);
#endif
		ksu_avc_spoof_init();
		return;
	}

	syscall_hook_manager_initialized = true;

#ifdef CONFIG_KRETPROBES
    syscall_regfunc_rp = init_kretprobe("syscall_regfunc", syscall_regfunc_handler);
    syscall_unregfunc_rp = init_kretprobe("syscall_unregfunc", syscall_unregfunc_handler);
#endif

    // Register syscall hooks via dispatcher
    ksu_register_syscall_hook(__NR_setresuid, ksu_hook_setresuid);
    ksu_register_syscall_hook(__NR_execve, ksu_hook_execve);
    ksu_register_syscall_hook(__NR_execveat, ksu_hook_execveat);
    ksu_register_syscall_hook(__NR_newfstatat, ksu_hook_newfstatat);
    ksu_register_syscall_hook(__NR_faccessat, ksu_hook_faccessat);

#ifdef CONFIG_HAVE_SYSCALL_TRACEPOINTS
    ret = register_trace_sys_enter(ksu_sys_enter_handler, NULL);
#ifndef CONFIG_KRETPROBES
    ksu_mark_running_process_locked();
#endif
    if (ret) {
        pr_err("hook_manager: failed to register sys_enter tracepoint: %d\n", ret);
    } else {
        pr_info("hook_manager: sys_enter tracepoint registered\n");
    }
#endif

	ksu_setuid_hook_init();
	ksu_sucompat_init();
	ksu_avc_spoof_init();
}

void __exit ksu_syscall_hook_manager_exit(void)
{
	pr_info("hook_manager: ksu_hook_manager_exit called\n");

	if (!syscall_hook_manager_initialized) {
#if defined(CONFIG_KSU_SAMSUNG_RKP) && defined(CONFIG_KRETPROBES) && defined(__aarch64__)
		samsung_sucompat_hook_exit();
		samsung_setresuid_hook_exit();
#endif
		ksu_avc_spoof_exit();
		ksu_syscall_hook_exit();
		return;
	}
#ifdef CONFIG_HAVE_SYSCALL_TRACEPOINTS
    unregister_trace_sys_enter(ksu_sys_enter_handler, NULL);
    tracepoint_synchronize_unregister();
    pr_info("hook_manager: sys_enter tracepoint unregistered\n");
#endif

#ifdef CONFIG_KRETPROBES
    destroy_kretprobe(&syscall_regfunc_rp);
    destroy_kretprobe(&syscall_unregfunc_rp);
#endif

    ksu_unregister_syscall_hook(__NR_setresuid);
    ksu_unregister_syscall_hook(__NR_execve);
    ksu_unregister_syscall_hook(__NR_execveat);
    ksu_unregister_syscall_hook(__NR_newfstatat);
    ksu_unregister_syscall_hook(__NR_faccessat);

    ksu_syscall_hook_exit();

    ksu_sucompat_exit();
    ksu_setuid_hook_exit();
    ksu_avc_spoof_exit();
}
