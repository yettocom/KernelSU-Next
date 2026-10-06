# KernelSU-Next Samsung v3.4.0 patch set

Base: `1a879d6a866f80b1fa1c1009a2ffa747873cbb5e` (official tag `v3.4.0`)
Version code: `33294` (`30000 + 3294`)
Version name: `3.4.0`

Confirmed still the newest official release on 2026-10-06 (only the `legacy`
line has its own `v3.4.0-legacy`). Rebase watch for the next release: the
`compat` patch touches `kernel/Kbuild`, `kernel/feature/sucompat.c`,
`kernel/hook/syscall_hook.h` and `kernel/hook/syscall_hook_manager.c`, all four
of which upstream has already changed on `dev` (`4bc78d77`, "kernel: Add
Android riscv64 support with LKM"). `selinux_hide` and `lateload` still apply
to the current `dev` unchanged.

Location: the same files are stored in two places, byte for byte identical -
the working copy at `D:\root\patch\ksu-next\`, and the fork
`yettocom/KernelSU-Next` at `patches/samsung/v3.4.0/` on branch `samsung`.
That branch also carries the content in its tree (tip `8ce9981e`, "kernel: add
Samsung KDP/RKP/DEFEX late-load support for v3.4.0"), so a release build can
either apply these patches to a clean `v3.4.0` tree or check out the branch.

The fork keeps exactly two branches: `dev` follows `upstream/dev`, and
`samsung` is this line. The retired v3.3.0 line is no longer a branch; its
content lives in `D:\root\_trash-20261006-1715\ksu-next\`.

Three patches, one responsibility each. Apply order is for review only because
the file sets are disjoint.

1. `KernelSU-Next-v3.4.0-samsung-compat.patch`
   - Samsung KDP credential installation and native PGD path
   - Samsung DEFEX credential synchronization
   - RKP-safe kprobe syscall hooks
   - execveat coverage and the setresuid kretprobe
   - Samsung Kconfig/Kbuild wiring
   - x86_64 syscall hook API alignment

2. `KernelSU-Next-v3.4.0-samsung-selinux_hide.patch`
   - Kernel-side SELinux status hiding through kprobes, with no direct kernel
     text or rodata patching
   - the kprobe route is compiled when `CONFIG_KSU_SAMSUNG_RKP` or
     `CONFIG_KSU_SAMSUNG_NO_PATCH_TEXT` is set, so an RKP target cannot fall
     back onto the text-patching route by missing one flag; `CONFIG_KPROBES` is
     required too, because the `register_kprobe()` stubs return `-ENOSYS`
     otherwise and the feature would only fail at runtime
   - the text-patching code is otherwise untouched, and the patch is additive
     only: reversing it restores the upstream file byte for byte
   - re-entry is handled the way KernelSU-Next PR #1507 does it: `context` and
     `access` are diverted only for uids in the app range, where the replacement
     never calls the original back, so they carry no guard at all; `status` and
     `selinux_setprocattr` do call it back and record the current task in a hash
     table for the duration of that call, using a stack-allocated entry, so a
     pre-handler allocates nothing and no entry can outlive the call
   - the `status` entry keeps upstream's lazy fake-page initialization, which
     PR #1507 drops; a non-late-load build therefore still initializes on first
     open instead of silently serving the real status
   - `context`, `access` and `setprocattr` also check `ksu_selinux_hide_enabled`
     and fall through while the feature is off (PR #1507 does not, which would
     reach a replacement that needs `backup_sepolicy` after it has been dropped)
   - a failed probe registration reports the underlying errno instead of
     masking every failure as `-ENOSYS`

3. `KernelSU-Next-v3.4.0-samsung-lateload.patch`
   - late-load staging through `/data/local/tmp/.ksud-stage`
   - `rename()` staging into `/data/adb/ksud`
   - no daemonize in the Samsung late-load path
   - release version overrides for Manager and ksud
   - explicit `KSU_VERSION_CODE`/`KSU_VERSION_NAME` are honored even when
     git metadata is unavailable

Build flags:

```text
CONFIG_KSU=m
CONFIG_KSU_SAMSUNG_KDP=y
CONFIG_KSU_SAMSUNG_RKP=y
CONFIG_KSU_SAMSUNG_DEFEX=y
CONFIG_KSU_SAMSUNG_NO_PATCH_TEXT=y
```

Release environment:

```text
KSU_VERSION_CODE=33294
KSU_VERSION_NAME=3.4.0
```

Patch SHA256:

```text
AF63315869605480A3EC278926449D033B1EBB0F438CFE229E8BB4692F346F3C  compat
E60F41EF093E275569B1D64382B0F0CAAB0718EF85C83756B101693D81DFDFEC  selinux_hide
F1E12705FB5EDD4C72B3FF96FB5FE9FD85676BF66A087F1960E9DD5AEE4CDFA9  lateload
```

Verification performed against the clean official v3.4.0 tag:

- all three patches `git apply --check`: pass
- apply all three: pass
- reverse apply in reverse order: clean official v3.4.0 tree restored
- `selinux_hide` is additive only (`git diff --numstat`: 327 insertions, 0
  deletions), and reversing it restores `kernel/feature/selinux_hide.c` to
  blob `fbb6c44f` byte for byte
- preprocessor check over four combinations: `RKP+KPROBES` and
  `NO_PATCH_TEXT+KPROBES` select the kprobe route, `RKP` without `CONFIG_KPROBES`
  and "no flags" fall back to the text route (no kprobe code), all balanced
- kernel module compiled with the three DDK images after the `selinux_hide`
  rework: Android 13-5.15, Android 14-6.1 and Android 15-6.6, 0 warnings and
  0 errors each, `kernelsu.ko` linked and BTF emitted; logs in
  `D:\root\ksu-next\build\ko-compile-check\`
- negative control: with both gates off, the built `selinux_hide.o` contains no
  kprobe code, while the kprobe build contains the bypass symbols
- the `lateload` patch's ksud side was not rebuilt by this rework; the
  `selinux_hide` change is kernel-only
- the module unload drain of the previous revision was dropped with the xarray
  guard: it can only be implemented over a per-task record, and PR #1507 ships
  without one. Unloading this module is not supported anyway (Samsung loads it
  late and keeps it until reboot)

## Source audit of the kprobe route

Checked against `kernel/feature/selinux_hide.c` at the release tag:

- every ip-redirect target matches the signature the kernel calls the probed
  slot with: `write_op_fn` (`ssize_t (*)(struct file *, char *, size_t)`),
  `sel_open_handle_status_fn` (`int (*)(struct inode *, struct file *)`) and
  `setprocattr_fn` (`int (*)(const char *, void *, size_t)`)
- the replacements reach their original in exactly four places, which is what
  decides the guard placement:
  - `orig_context_write` and `orig_access_write` are called only under
    `current_uid().val < 10000`, and the pre-handlers divert only for uids
    outside that range, so those two entries cannot re-enter
  - `selinux_setprocattr_hook.original` is called for `uid < 10000` **or**
    `strcmp(name, "current") != 0`, both of which the wrapper covers
  - `orig_sel_open_handle_status` is called on the fall-through path, so the
    status entry carries the wrapper too
- the `ksu_selinux_hide_enabled` checks are load-bearing, not defensive
  padding: `on_boot_completed()` (`kernel/runtime/boot_event.c`) calls
  `ksu_selinux_hide_drop_backup_if_unused()` on every boot when the feature is
  not enabled, which frees `backup_sepolicy` (and leaves `fake_state.policy`
  dangling on pre-6.6). Diverting on uid alone — as PR #1507 does — would run
  the replacement against that freed policy
- `CONFIG_KSU_SAMSUNG_NO_PATCH_TEXT` does not depend on `KPROBES` in the compat
  patch while `CONFIG_KSU_SAMSUNG_RKP` does, so the gate checks `CONFIG_KPROBES`
  itself
- hot-path order (2026-10-06): the pre-handlers test `ksu_selinux_hide_enabled`
  and the uid before the bypass table, so a status open that will not be diverted
  costs no lock; the `likely()` hint sits on the uid comparison, which is the
  test that actually short-circuits

This patch set is fork-local. No upstream submission is intended.
