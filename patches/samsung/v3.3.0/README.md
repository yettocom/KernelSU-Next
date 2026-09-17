# KernelSU-Next Samsung v3.3.0 patch set

Base: 3b18216f71df189ab3d1b1ce0bdb21be1268e771 (official tag v3.3.0, stable)
Version: 33214

Three patches, one responsibility each. They touch disjoint files, so the order
below is for review only.

1. KernelSU-Next-v3.3.0-samsung-compat.patch
   Samsung kernel compatibility: KDP credential install, RKP-safe syscall
   hooks, DEFEX credential synchronization, execveat coverage, Kconfig and
   Kbuild wiring. It also keeps the x86_64 syscall-table hook prototype and
   implementation aligned with the arm64/header `int` return contract.

2. KernelSU-Next-v3.3.0-samsung-selinux_hide.patch
   Kernel-side SELinux status hiding through kprobes (no kernel text patching).

3. KernelSU-Next-v3.3.0-samsung-lateload.patch
   ksud adaptation for the runtime (late-load) install path. It intentionally
   does not daemonize this path: the Samsung deployment helper waits for the
   loader and performs a single KernelSU control check, so the loader must stay
   alive until module loading and installation complete. It adds the
   Samsung-compatible staging path:
   `stage_daemon_from("/data/local/tmp/.ksud-stage")` moves the helper's
   pre-staged copy into `/data/adb/ksud` with `rename()`. The ordinary CLI
   install path keeps upstream `stage_daemon()` / `/proc/self/exe` behavior.
   The final Manager restart uses the package name supplied by the caller.
   This patch also carries the release version overrides.

Applying all three reproduces the samsung branch head. The former combined
compat+hide patch is no longer published.

Runtime note for the late-load patch: the loader is executed through a
`/system/bin/logcat` bind mount and cannot read itself back, so the deployment
helper or script must place the loader at `/data/local/tmp/.ksud-stage` before
late-load starts. The file is moved into place with `rename()` and there is no
fallback, so a missing file fails staging. The loader does not daemonize; it
stays alive through module loading and the installation stages so the helper's
single post-exit control check is reliable. The Manager restart at the end
uses the caller-supplied package name. Installs that never use late-load are
unaffected.

The compat patch keeps the six Samsung syscall hooks
(`execve`, `execveat`, `newfstatat`, `faccessat`, `statx`, `faccessat2`) and
the setresuid kretprobe.

Build flags:

CONFIG_KSU=m
CONFIG_KSU_SAMSUNG_KDP=y
CONFIG_KSU_SAMSUNG_RKP=y
CONFIG_KSU_SAMSUNG_DEFEX=y
CONFIG_KSU_SAMSUNG_NO_PATCH_TEXT=y

Version overrides for release builds (implemented by patch 3):

KSU_VERSION=33214
KSU_VERSION_CODE=33214
KSU_VERSION_NAME=3.3.0

Patch SHA256:

1652153153B526A294AB1F70FA71648908332AE8144C9EAB2CF086D85A3BD6AD  compat
9BA2858B98059B499B7659E3782C205CD668300E469AA35B7EE5A5CFEDB4CC60  selinux_hide
E1F4470242BA4C0A064BE36A424384AB66E75FEA1C202249712C8EA280434B70  lateload

This branch is fork-local. Do not open a combined pull request to upstream.
