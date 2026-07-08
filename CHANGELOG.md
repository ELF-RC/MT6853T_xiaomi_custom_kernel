# 更新日志

## ZSTD 压缩库升级
- 将内核内嵌的 zstd 从 v1.3.x 升级至 **v1.5.2**（来自上游 Linux 6.1）
- 压缩比提升约 5-8%，压缩/解压速度提升 10-25%
- 更新了所有消费者驱动：`crypto/zstd.c`、`btrfs`、`f2fs`、`squashfs`
- 修复 zstd_deps.h 中内存分配器，使其在 kernel 环境下正常工作

## 内存管理（MADV_COLD / MADV_PAGEOUT）
- 从 Linux v5.4/v5.5 backport **MADV_COLD** 和 **MADV_PAGEOUT** 两个 madvise 操作
- 新增 `deactivate_page()` 函数支持页面去激活
- Android 12+ 的 lmkd 可利用此机制主动回收后台应用内存，减少杀进程概率

## UFS 存储优化
- 开启 **UFS 时钟缩放 (CLK_SCALING)**，负载低时自动降频省电
- 开启 **Hibern8 + 时钟门控**，空闲时链路进入最深省电状态

## 网络与移动数据省电

### TCP 默认值调优
| 参数 | 原值 | 新值 |
|------|------|------|
| FIN_TIMEOUT | 60s | **30s** |
| KEEPALIVE_TIME | 2h | **30min** |
| KEEPALIVE_PROBES | 9 | **3** |
| KEEPALIVE_INTVL | 75s | **30s** |
| SYN_RETRIES | 6 | **3** |
| RETR2 | 15 | **8** |

- 修复 `tcp_output.c` 写缓冲计算，恢复 `tcp_limit_output_bytes` 作为上限限制

### Modem 驱动调优
- CCMNI: GRO flush timer 2ms → **10ms**，减少 AP 唤醒频率
- CCMNI: 新增 `module_param` 支持运行时调整 GRO 参数
- CCMNI: wakelock 超时 1s → **250ms**，允许更快进入休眠
- ECCCI: 开启低电量 Modem 发射功率限制

## defconfig 配置优化
- **ARM_CPUIDLE**：开启更深 CPU 空闲状态
- **HZ 250→100**：减少定时器中断频率
- **BBR 拥塞控制**：设为默认 TCP 算法，移动网络下更省电
- **WiFi 省电**：默认开启 PS（Power Save）
- **DEVFREQ_GOV_POWERSAVE**：DDR 空闲时降频
- **F2FS 文件压缩**：启用 zstd 后端，减少存储 I/O
- **MTK_LOWMEM_HINT**：低内存时通知 userspace 提前回收

## 其他优化
- **VM dirty ratio**：脏页阈值从 10/20 降至 5/10，减少写延迟尖峰
- **KernelSU**：从 v32514 升级至 **v32522**（backslashxx 仓库）
