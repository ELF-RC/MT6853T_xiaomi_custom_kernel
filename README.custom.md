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
- Proactive compaction sysctl defaults to **off** (`vm.compaction_proactiveness=0`). Values `1..100` are aggressiveness, not raw jiffies.
- TCP Fast Open defaults to **client only**.

## CI

GitHub Actions builds `Image.gz-dtb`, packages AnyKernel3 for `cannon`/`cannong`, and asserts defconfig/source consistency after `cannon_defconfig`.

## Security notice

KernelSU / Resukisu / SUSFS intentionally expand privilege and (on Resukisu) spoof visibility. Treat those builds as **user-debug / research** images, not stock integrity.

`development` keeps SELinux non-enforcing for bring-up and module testing.
