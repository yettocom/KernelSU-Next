# Device checklist (release gate)

Run this on every target before the product directories (`ksu-next-chc-23u`,
`ksu-next-chc-24u`, `ksu-next-chc-25u`, `ksu-next-tgy-25u`) are refreshed.
Static verification (builds, undefined symbols, patch apply, checkpatch) is
done separately and is not a substitute for this list.

Record for each run: device model, `uname -r`, ko sha256, ksud sha256,
manager version, log file.

## 1. Late load
- Run the package script (`root-*.sh`) end to end.
- `cat /proc/modules | grep kernelsu` must list the module.
- `dmesg` must not contain `Failed to stage ksud`.

## 2. su
- Grant root to a test app in the manager.
- `su -c id` from that app must return `uid=0(root)`.

## 3. sucompat (execve/execveat redirection)
- Execute `su` from a shell of an allowlisted app (not only from the manager).
- The request must reach the manager and root must be granted.
- Repeat with an app that uses `execveat` (e.g. a recent toybox/`sh` path).

## 4. selinux_hide
- Toggle the option in the manager; it must not return `-38`.
- `getenforce` must stay `Enforcing` after the toggle.
- Apps that check `selinux` status must not observe the KSU domain.

## 5. adb_root
- With the feature enabled, restart adbd and confirm the injected
  `LD_PRELOAD=/data/adb/ksu/lib/libadbroot.so` is applied to adbd.

## 6. Kernel log health
- `dmesg | grep -E "BUG:|WARNING:|scheduling while atomic|KASAN|panic"` must be
  empty for the whole session, including module load and teardown.

## 7. Cleanup
- After the run, `/data/local/tmp` must not contain `cve-2026-43499*`, `ksud*`
  or `.ksud-stage`; `/data/adb/ksud.stage` must be gone.
- `/data/adb/ksud` and `/data/adb/ksu/` are expected to remain (runtime state).

## 8. Reboot stability
- Reboot the device, confirm it boots normally, USB debugging stays usable and
  no leftover file re-triggers the root flow.