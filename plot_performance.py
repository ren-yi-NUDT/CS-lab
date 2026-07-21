import matplotlib.pyplot as plt
import matplotlib as mpl

mpl.rcParams['font.sans-serif'] = ['Noto Sans CJK JP', 'WenQuanYi Zen Hei', 'DejaVu Sans']
mpl.rcParams['axes.unicode_minus'] = False
mpl.rcParams['axes.grid'] = True
mpl.rcParams['grid.alpha'] = 0.3
mpl.rcParams['axes.spines.top'] = False
mpl.rcParams['axes.spines.right'] = False

threads = [4, 8, 16, 24, 32, 64]
real    = [0.237, 0.165, 0.129, 0.109, 0.114, 0.110]
user    = [0.926, 1.201, 1.544, 2.001, 2.045, 2.253]
sys_    = [0.002, 0.002, 0.006, 0.009, 0.013, 0.017]
util    = [u / r for u, r in zip(user, real)]
speedup = [real[0] / r for r in real]

baseline_single = 13.0
baseline_speedup = baseline_single / real[3]

fig, axes = plt.subplots(2, 2, figsize=(12, 8))

# 1) real time vs threads
ax = axes[0, 0]
ax.plot(threads, real, 'o-', color='#d62728', lw=2, ms=8)
ax.plot(24, 0.109, '*', color='#d62728', ms=20, zorder=5, label='最优 (24 线程)')
ax.set_xlabel('线程数')
ax.set_ylabel('real (s) 墙钟时间')
ax.set_title('墙钟时间 vs 线程数')
ax.set_xscale('log', base=2)
ax.set_xticks(threads)
ax.set_xticklabels(threads)
ax.legend()

# 2) speedup vs threads (log-log)
ax = axes[0, 1]
ax.plot(threads, speedup, 's-', color='#1f77b4', lw=2, ms=8)
ax.plot([4, 64], [1, 16], '--', color='gray', alpha=0.6, label='理想线性加速')
ax.set_xlabel('线程数')
ax.set_ylabel('加速比 (相对 4 线程)')
ax.set_title('加速比 vs 线程数')
ax.set_xscale('log', base=2)
ax.set_xticks(threads)
ax.set_xticklabels(threads)
ax.legend()

# 3) CPU utilization (user/real)
ax = axes[1, 0]
bars = ax.bar([str(t) for t in threads], util,
              color=['#2ca02c' if t != 24 else '#ff7f0e' for t in threads])
ax.axhline(8, color='red', ls='--', alpha=0.7, label='vCPU 数 (8)')
ax.set_xlabel('线程数')
ax.set_ylabel('user/real (实际并行度)')
ax.set_title('实际并行度 vs 线程数 (超线性加速)')
for bar, v in zip(bars, util):
    ax.text(bar.get_x() + bar.get_width()/2, v + 0.3, f'{v:.1f}',
            ha='center', fontsize=9)
ax.legend()

# 4) baseline vs best
ax = axes[1, 1]
labels = ['单线程\n基线', '24 线程\n实验组']
times = [baseline_single, real[3]]
bars = ax.bar(labels, times, color=['#7f7f7f', '#ff7f0e'])
ax.set_ylabel('耗时 (s)')
ax.set_title(f'基线 vs 实验组 (总加速比 ~{baseline_speedup:.0f}×)')
ax.set_yscale('log')
for bar, v in zip(bars, times):
    ax.text(bar.get_x() + bar.get_width()/2, v * 1.05, f'{v:.3f} s',
            ha='center', fontsize=10)

fig.suptitle('多线程 AVX2 密码穷举性能实验', fontsize=14, fontweight='bold')
plt.tight_layout()
plt.savefig('/home/ren/Desktop/CS/lab12/performance.png', dpi=150, bbox_inches='tight')
plt.savefig('/home/ren/Desktop/CS/lab12/performance.pdf', bbox_inches='tight')
print('Saved: performance.png / performance.pdf')
