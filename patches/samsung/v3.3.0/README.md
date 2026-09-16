# KernelSU-Next Samsung v3.3.0 patch set

Base: 3b18216f71df189ab3d1b1ce0bdb21be1268e771 (official tag v3.3.0, stable)
Version: 33214

Three patches, one responsibility each. They touch disjoint files, so the order
below is for review only.

1. KernelSU-Next-v3.3.0-samsung-compat.patch
   Samsung kernel compatibility: KDP credential install, RKP-safe syscall
   hooks, DEFEX credential synchronization, execveat coverage, Kconfig and
   Kbuild wiring.

2. KernelSU-Next-v3.3.0-samsung-selinux_hide.patch
   Kernel-side SELinux status hiding through kprobes (no kernel text patching).

3. KernelSU-Next-v3.3.0-samsung-lateload.patch
   ksud adaptation for the runtime (late-load) install path: stage_daemon_from()
   moves the deployment helper's pre-staged copy into /data/adb/ksud with
   rename(), stage_daemon() keeps the upstream /proc/self/exe copy for the CLI
   install path, and late-load no longer daemonizes or restarts the manager.
   Also carries the release version overrides.

Applying all three reproduces the samsung branch head. The former combined
compat+hide patch is no longer published.

Runtime note for the late-load patch: the loader is executed through a
/system/bin/logcat bind mount and cannot read itself back, so the deployment
helper or script must place the loader at /data/local/tmp/.ksud-stage before
late-load starts. The file is moved into place with rename() and there is no
fallback, so a missing file fails staging. The upstream manager restart no
longer runs inside ksud, so the deployment has to (re)start the manager after
late-load. Installs that never use late-load are unaffected.

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

This branch is fork-local. Do not open a combined pull request to upstream.
