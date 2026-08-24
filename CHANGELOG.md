# 更新日志

## [2026-08-24] clone3 完整支持（KernelSU / KernelSU_2.6GHz / main / main_2.6GHz / development）

从 rd-stuffs/msm-4.14 backport 完整 clone3 系列，全部 5 个分支已同步：

- **`sys_clone3`（编号 435）**：`struct kernel_clone_args` fork 重构（copy_process / _do_fork 转 args 风格），全部 arch syscall 表接线，`kernel/sys_ni.c` stub
- **CLONE_CLEAR_SIGHAND**：glibc 2.34+ 依赖的 clone 标志
- **set_tid 支持**：clone3 可指定新进程 PID（容器/CRIU 场景）
- **CAP_CHECKPOINT_RESTORE**：新增 capability，set_tid 权限校验改用 `checkpoint_restore_ns_capable()`
- 前置清理：`_do_fork` early-return 重构、clone_args 的 `__ASSEMBLY__` guard、kernel-doc

> 注：rd-stuffs 的 `copy_process(): don't use ksys_close()` 无需移植 —— 本树 4.14.336 的 copy_process 天然无 ksys_close 路径。
> 兼容性：32 位 compat 直接复用 `sys_clone3`（与 rd-stuffs 上游一致，Android 64 位不受影响）。

## [2026-08-23] mt6893 4.19/5.10 backport 系列（全部 5 分支）

应用 xiaomi-mt6893-dev/kernel_xiaomi_mt6893 (lineage-23.2) 的两个 backport 系列，为 Android 15/17 用户空间提供支持：

- **4.19 系列**（1181 commits）+ **5.10 系列**（1142 commits）
- **bpf**：verifier/JIT/helpers backport（netd/netbpfload 需求），permissive log-full 处理，MTK tracepoint 的 `__TRACE_NO_BPF_PROBE` guard（>12 参数）
- **cgroup**：v2 freezer、psi、cgroup bpf query
- **binder/binderfs**、erofs、net/fs 各类 backport
- **sched**：`sched_set_fifo` / `sched_setattr_nocheck` 及头文件声明
- **cpuset affinity 恢复**：`cpus_requested` 字段维护（sched_setaffinity / cpuset 迁移）
- **per-comm uname fake**：对 bpfloader/netbpfload/netd 等进程报告 "5.10.253"，绕过新版 Android 的内核版本检查
- 冲突全部针对 MT6853T 树手工解决，clang-13（cannon_defconfig）编译验证通过

## [2026-08-22~23] A17 QPR0 引导问题诊断（结论与修复方向）

- 诊断工具链：电源键 panic + ramoops 任务 dump + 用户态 fp 栈回溯，定位 A17 卡 logo 根因
- **根因**：A17（HyperOS 定制）SurfaceFlinger 默认 **Vulkan RenderEngine**，在 MT6853T 的 Mali valhall r32p1 驱动上崩溃（render 子进程异常 → vsync 定时器回调访问损坏的 variant → `bad_variant_access` → SIGABRT → `onrestart restart zygote` 连锁循环）
- **修复方案**：强制 GL backend（`debug.renderengine.backend=skiaglthreaded`，KernelSU 模块或 vendor build.prop）
- 调试代码（KDEBUG 追踪、binder mmap 追踪、电源键 panic）已全部清除，`CONFIG_CONSOLE_LOGLEVEL_DEFAULT` 恢复默认

## 之前的更新（历史摘要）

- **0f13bfea** pstore: enhance crash logging（ramoops 详细头部）
- **bpf 前置系列**（11 commits）：lpm_trie 修复、BPF JIT 可执行内存、map_name、vendor hooks
- KernelSU 集成、PD PPS/QC 快充支持（mt6360）、2.6GHz 超频变体
