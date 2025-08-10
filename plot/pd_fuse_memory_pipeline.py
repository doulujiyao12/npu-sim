import matplotlib.pyplot as plt
import numpy as np
import matplotlib.ticker as ticker
from matplotlib.patches import Patch
from matplotlib.lines import Line2D
import matplotlib.patches as mpatches
from matplotlib.backends.backend_pdf import PdfPages


plt.rcParams.update({
    "font.size": 12,
    "axes.labelsize": 14,
    "axes.titlesize": 16,
    "legend.fontsize": 12,
    "xtick.labelsize": 12,
    "ytick.labelsize": 12,
    "font.family": "serif"
})

# 数据（单位 ns）
# 数据说明："36*1"代表一共36个pipeline stage，每一个核做模型的1层（模型喂qwen3_8B，一共36层）。数组前的数字代表seq len。数组中的三个元素分别代表sram大小64M、128M、192M
data = {
    "36*1": {
        128: [1796017910880, 687403293688, 688662057688],
        256: [2322221284080, 628269102080, 632254102080],
        512: [2810994266544, 863553844904, 863620580904],
    },
    "18*2": {
        128: [2891482754160, 1945658692624, 463798649720],
        256: [2889831267728, 2221779819336, 512751347096],
        512: [3079334848952, 2260389561256, 623328783936],
    },
    "12*3": {
        128: [2821980493480, 2441872172160, 610391173656],
        256: [2909145252680, 2643844301432, 1448136494840],
        512: [3071953209496, 2714839199544, 1630140030968],
    }
}

pp_groups = list(data.keys())
seq_lens = [128, 256, 512]
mem_labels = ['64M', '128M', '192M']
hatches = ['--', '//', 'xx']  # 花纹代表内存

# 改进颜色方案（明亮有区分度）
bar_colors = ['#7E8CAD', '#7A9273', '#a28cc2', '#B37070']  # pp对应柱子颜色
line_colors = ['#ff7f0e', '#A58778']  # 折线颜色

bar_width = 0.2
bar_positions = np.arange(len(seq_lens))
offsets = [-1.5, -0.5, 0.5, 1.5]

fig, ax1 = plt.subplots()
ax2 = ax1.twinx()

# 记录加速比数据
speedup_128M = {128: [], 256: [], 512: []}
speedup_192M = {128: [], 256: [], 512: []}

# 用于标记128M柱子的中心位置
x_128M_center = {128: [], 256: [], 512: []}

# 柱状图
for idx, (pp, offset) in enumerate(zip(pp_groups, offsets)):
    for mem_idx, hatch in enumerate(hatches):
        y_vals = [data[pp][sl][mem_idx] / 1e6 for sl in seq_lens]  # ms
        for sl_idx, sl in enumerate(seq_lens):
            x_val = bar_positions[sl_idx] + offset * bar_width + mem_idx * (bar_width / 3)
            ax1.bar(x_val, y_vals[sl_idx],
                    width=bar_width / 3,
                    color=bar_colors[idx],
                    hatch=hatch,
                    edgecolor='black',
                    alpha=0.8)

            if mem_idx == 1:  # 128M中心点
                x_128M_center[sl].append(x_val)

    # 记录加速比
    for sl in seq_lens:
        base = data[pp][sl][0]
        speedup_128M[sl].append(base / data[pp][sl][1])
        speedup_192M[sl].append(base / data[pp][sl][2])

# 折线图
for sl in seq_lens:
    ax2.plot(x_128M_center[sl],
             speedup_128M[sl],
             color=line_colors[0],
             marker='o',
             linewidth=2,
             label='Speedup 128M/64M' if sl == 128 else None)
    
    ax2.plot(x_128M_center[sl],
             speedup_192M[sl],
             color=line_colors[1],
             marker='s',
             linewidth=2,
             label='Speedup 192M/64M' if sl == 128 else None)

# 设置坐标轴
ax1.set_xlabel("Input Sequence Length", fontsize=20)
ax1.set_ylabel("Latency (ms)", fontsize=20)
ax2.set_ylabel("Speedup (vs. 64M)", fontsize=20)

ax1.set_xticks(bar_positions)
ax1.set_xticklabels([str(sl) for sl in seq_lens])
ax1.grid(True, linestyle='--', alpha=0.5)
ax1.tick_params(axis='x', labelsize=16)
ax1.tick_params(axis='y', labelsize=16)
ax2.tick_params(axis='y', labelsize=16)
# ax.ticklabel_format(style='scientific', axis='y', scilimits=(0,0))
ax1.yaxis.get_offset_text().set_fontsize(15)
# 构造图例：只保留内存大小和折线图图例
legend_elements = [
    Patch(facecolor='white', edgecolor='black', hatch=hatches[0], label='64M'),
    Patch(facecolor='white', edgecolor='black', hatch=hatches[1], label='128M'),
    Patch(facecolor='white', edgecolor='black', hatch=hatches[2], label='192M'),
    Line2D([0], [0], color=line_colors[0], marker='o', label='Speedup ratio 128M/64M'),
    Line2D([0], [0], color=line_colors[1], marker='s', label='Speedup ratio 192M/64M'),
    Patch(facecolor=bar_colors[0], label='pipeline stage=36'),
    Patch(facecolor=bar_colors[1], label='pipeline stage=18'),
    Patch(facecolor=bar_colors[2], label='pipeline stage=12'),
]

ax1.legend(handles=legend_elements,
           ncol=3, bbox_to_anchor=(0.5, -0.2), loc='upper center', fontsize=14)

# ax1.set_title("Latency and Memory-based Speedup across Pipeline Configurations")

# plt.tight_layout(pad = 0.1)
# plt.show()
fig.set_size_inches(12,6)
plt.subplots_adjust(bottom=0.33, left=0.08, right=0.93, top=0.95)
fig.savefig('pd_fuse_memory_pipeline.pdf', format='pdf')
# output_pdf = 'pd_fuse_memory_pipeline.pdf'
# with PdfPages(output_pdf) as pdf:
#     
#     pdf.savefig(fig)
#     plt.close()
