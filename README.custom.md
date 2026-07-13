# MT6853T Xiaomi Custom Kernel

Linux **4.14.336** for **Redmi Note 9 5G / Note 9T** (`cannon` / `cannong`, MT6853).

Fork base: [xiaomi-mt6853-devs/android_kernel_xiaomi_cannon](https://github.com/xiaomi-mt6853-devs/android_kernel_xiaomi_cannon)  
This tree: [ELF-RC/MT6853T_xiaomi_custom_kernel](https://github.com/ELF-RC/MT6853T_xiaomi_custom_kernel)

## Branches

| Branch | Purpose | Root | Notes |
|--------|---------|------|--------|
| `main` | Base image without KernelSU | No | Clean daily driver base |
| `development` | Experimental / permissive SELinux | No KSU sources | **SELinux forced permissive** on purpose — do not remove |
| `KernelSU` | Base + [backslashxx/KernelSU](https://github.com/backslashxx/KernelSU) | Yes | Submodule `KernelSU` → `drivers/kernelsu` |
| `Resukisu` | ReSukiSU + in-tree SUSFS | Yes | Highest integrity / hide surface |

Common correctness and CI fixes land on all product branches. Feature differences stay branch-local.

## Build (local)

Toolchain is the same pin as CI (`ELF-RC/android_kernel4x_tools`).

```bash
make O=out ARCH=arm64 CC=clang LLVM_IAS=1 cannon_defconfig
make O=out ARCH=arm64 CC=clang LLVM_IAS=1 Image.gz-dtb
```

Use a clean source tree for out-of-tree builds (`make mrproper` if configure fails on dirty `include/config`).

## Defconfig policy

- Only enable `CONFIG_KSU` / `CONFIG_MILLET` / `CONFIG_KSU_SUSFS` when matching sources exist on that branch.
- `cannon_defconfig` keeps BBR as an **explicit** device default (`CONFIG_DEFAULT_TCP_CONG="bbr"`); tree-wide Kconfig no longer forces BBR for every defconfig.
- **`CONFIG_NET_SCH_FQ=y`** and default qdisc **`fq`** so BBR has pacing (do not run BBR on pfifo_fast alone).
- Proactive compaction sysctl defaults to **off** (`vm.compaction_proactiveness=0`). Values `1..100` are aggressiveness, not raw jiffies.
- TCP Fast Open defaults to **client only**.
- Idle governor: **TEO** rating preferred over menu on tickless systems.
- Light hardening: `SLAB_FREELIST_RANDOM`, `SLAB_FREELIST_HARDENED`, `FORTIFY_SOURCE` enabled. `init_on_alloc=1` remains a boot param (not default-on).

## Backports / features of note

| Feature | Notes |
|---------|--------|
| ZSTD 1.5.2 + zram multi-comp/recompress | Already in tree |
| MADV_COLD / MADV_PAGEOUT | Already in tree |
| **process_madvise(2)** (`__NR_ 440`) | Backport from 5.10; only `MADV_COLD` / `MADV_PAGEOUT` |
| cgroup v2 freezer | Already in tree |
| TEO cpuidle | Preferred default via governor rating |

### zram multi-comp suggested userspace policy

Kernel already supports multi-comp. A sensible mobile setup (init/rc or sysfs):

```text
# primary: fast; secondary: denser idle recompress
echo lz4 > /sys/block/zram0/comp_algorithm          # if exposed as primary
# or use multi-comp sysfs from zram multi-comp port:
# /sys/block/zram0/recomp_algorithm, recompress, ...
# writeback (optional, needs backing device):
# echo /dev/block/... > /sys/block/zram0/backing_dev
```

Prefer **lz4 (or zstd level low) for fault path** and **idle recompress to zstd** under memory pressure / PSI, instead of only raising `swappiness`.

`process_madvise` is intended for lmkd / system_server helpers with ptrace-equivalent access (pidfd).

## Not done here (too large for 4.14 product tree)

- **MGLRU / folio conversion** — project-sized; needs dedicated port
- EEVDF / sched_ext — conflicts with MTK HMP/EAS
- Full DAMON productization — low mobile value

## CI

GitHub Actions builds `Image.gz-dtb`, packages AnyKernel3 for `cannon`/`cannong`, and asserts defconfig/source consistency after `cannon_defconfig`.

## Security notice

KernelSU / Resukisu / SUSFS intentionally expand privilege and (on Resukisu) spoof visibility. Treat those builds as **user-debug / research** images, not stock integrity.

`development` keeps SELinux non-enforcing for bring-up and module testing.
