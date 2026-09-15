# KernelSU-Next Samsung v3.3.0 patch set

Base: 3b18216f71df189ab3d1b1ce0bdb21be1268e771 (official tag v3.3.0, stable)
Version: 33214

Apply order (split patches):

1. KernelSU-Next-v3.3.0-samsung-compat.patch
2. KernelSU-Next-v3.3.0-samsung-selinux_hide.patch

Or apply the combined atomic patch:

KernelSU-Next-v3.3.0-samsung-compat+hide.patch

The combined patch is byte-identical to applying the two split patches in order.

Build flags:

CONFIG_KSU=m
CONFIG_KSU_SAMSUNG_KDP=y
CONFIG_KSU_SAMSUNG_RKP=y
CONFIG_KSU_SAMSUNG_DEFEX=y
CONFIG_KSU_SAMSUNG_NO_PATCH_TEXT=y

Version overrides for release builds:

KSU_VERSION=33214
KSU_VERSION_CODE=33214
KSU_VERSION_NAME=3.3.0

Runtime contract for the late-load flow:

- The loader is started through the vendor helper, which bind-mounts it over
  /system/bin/logcat before exec'ing it. DEFEX refuses to read that path back,
  so the helper (or the deployment script) must pre-stage the daemon at
  /data/local/tmp/.ksud-stage; ksud prefers that copy and only falls back to
  current_exe() when it is absent.
- With CONFIG_KSU_SAMSUNG_NO_PATCH_TEXT=y all kernel text patching is refused
  on purpose, so the __NR_read/__NR_fstat syscall-table hooks cannot be
  installed and the init.rc injection path is unavailable; ksud reports this
  with a warning instead of failing silently.

This branch is fork-local. Do not open a combined pull request to upstream.