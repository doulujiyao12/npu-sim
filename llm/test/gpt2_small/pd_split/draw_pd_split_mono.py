import matplotlib.pyplot as plt
import numpy as np
import re
from collections import defaultdict

# 设置学术风格的字体和参数
plt.rcParams.update({
    'font.size': 12,
    'font.family': 'serif',
    'axes.linewidth': 1.2,
    'axes.grid': True,
    'grid.alpha': 0.3,
    'lines.linewidth': 2,
    'lines.markersize': 8
})

def parse_data_file(file_path):
    """解析数据文件"""
    configs = {}
    current_config = None
    current_request = None
    
    with open(file_path, 'r') as f:
        lines = f.readlines()
    
    for line in lines:
        line = line.strip()
        if line.startswith('*') and line.endswith('*'):
            # 提取配置名称
            config_match = re.search(r'pd_split_(\d+)_(\d+)_', line)
            if config_match:
                p_cores, d_cores = int(config_match.group(1)), int(config_match.group(2))
                current_config = f"{p_cores}_{d_cores}"
                configs[current_config] = {'p_cores': p_cores, 'd_cores': d_cores, 'requests': []}
        
        elif line.startswith('Request'):
            request_match = re.search(r'Request (\d+):', line)
            if request_match and current_config:
                current_request = int(request_match.group(1))
                configs[current_config]['requests'].append([])
        
        elif line.startswith('Token'):
            token_match = re.search(r'Token (\d+): ([\d.]+)', line)
            if token_match and current_config and current_request is not None:
                token_idx = int(token_match.group(1))
                timestamp = float(token_match.group(2))
                configs[current_config]['requests'][current_request].append(timestamp)
    
    return configs

def calculate_metrics(configs):
    """计算性能指标"""
    metrics = {}
    
    for config_name, config_data in configs.items():
        p_cores = config_data['p_cores']
        d_cores = config_data['d_cores']
        requests = config_data['requests']
        
        # 计算TTFT (Time to First Token) - 生成Token 0的时间（从开始到Token 0）
        ttfts = []
        # 计算Time Between Tokens - Token间的平均时间间隔
        tbts = []
        # 计算总延迟 - Token 0到Token 100的时间
        total_latencies = []
        
        # 获取所有请求的开始时间（假设为最早的Token 0时间）
        all_start_times = []
        for request_tokens in requests:
            if len(request_tokens) > 0:
                all_start_times.append(request_tokens[0])
        
        earliest_start = min(all_start_times) if all_start_times else 0
        
        for request_tokens in requests:
            if len(request_tokens) >= 21:  # 确保有足够的token
                # TTFT: 生成Token 0的时间（从系统开始处理到生成第一个token）
                ttft = (request_tokens[0] - earliest_start) / 1e9  # 转换为秒
                ttfts.append(ttft)
                
                # TBT: 所有token间的平均时间间隔
                if len(request_tokens) > 1:
                    decode_times = []
                    for i in range(len(request_tokens)-1):
                        decode_times.append((request_tokens[i+1] - request_tokens[i]) / 1e9)
                    if decode_times:
                        tbts.append(np.mean(decode_times))
                
                # 总延迟：从Token 0到Token 100的时间
                total_latency = (request_tokens[-1] - request_tokens[0]) / 1e9
                total_latencies.append(total_latency)
        
        # 计算吞吐量 (tokens per second)
        if requests and all_start_times:
            # 计算总的token数量
            total_tokens = sum(len(req) for req in requests if len(req) > 0)
            # 计算总的运行时间（从最早开始到最晚结束）
            all_end_times = []
            for request_tokens in requests:
                if len(request_tokens) > 0:
                    all_end_times.append(request_tokens[-1])
            
            if all_end_times:
                latest_end = max(all_end_times)
                total_time = (latest_end - earliest_start) / 1e9  # 转换为秒
                throughput = total_tokens / total_time if total_time > 0 else 0
            else:
                throughput = 0
        else:
            throughput = 0
        
        metrics[config_name] = {
            'p_cores': p_cores,
            'd_cores': d_cores,
            'ttft_mean': np.mean(ttfts) if ttfts else 0,
            'ttft_std': np.std(ttfts) if ttfts else 0,
            'tbt_mean': np.mean(tbts) if tbts else 0,
            'tbt_std': np.std(tbts) if tbts else 0,
            'latency_mean': np.mean(total_latencies) if total_latencies else 0,
            'throughput': throughput,
            'total_cores': p_cores + d_cores
        }
    
    return metrics

# 模拟数据（你需要用实际的parse_data_file函数替换这部分）
# 这里使用符合你描述趋势的模拟数据
a_config = parse_data_file('../../build/token_records_HETE.txt')
metrics = calculate_metrics(a_config)

# 按P核数量排序
sorted_configs = sorted(metrics.items(), key=lambda x: x[1]['p_cores'])
config_names = [item[0] for item in sorted_configs]
config_data = [item[1] for item in sorted_configs]

# 提取数据
p_cores = [data['p_cores'] for data in config_data]
d_cores = [data['d_cores'] for data in config_data]
ttft = [data['ttft_mean'] for data in config_data]
tbt = [data['tbt_mean'] for data in config_data]
latency = [data['latency_mean'] for data in config_data]
throughput = [data['throughput'] for data in config_data]

# 创建图表
fig, ax1 = plt.subplots(figsize=(12, 8))

# 设置x轴
x_pos = np.arange(len(config_names))
config_labels = [f"P{p}/D{d}" for p, d in zip(p_cores, d_cores)]

# 左y轴 - 延迟
color1 = '#2E86C1'  # 蓝色 - TTFT
color2 = '#E74C3C'  # 红色 - 延迟
color4 = '#F39C12'  # 橙色 - TBT

# 绘制平均延迟 (柱状图)
bars = ax1.bar(x_pos, latency, 0.4, label='Avg Latency (s)', color=color2, alpha=0.8, edgecolor='black', linewidth=0.8)

ax1.set_xlabel('Core Allocation Strategy (Prefill/Decode)', fontsize=14, fontweight='bold')
ax1.set_ylabel('Average Latency (seconds)', color=color2, fontsize=14, fontweight='bold')
ax1.set_xticks(x_pos)
ax1.set_xticklabels(config_labels, fontsize=12)
ax1.tick_params(axis='y', labelcolor=color2)
ax1.set_ylim(0, max(latency) * 1.1)

# 右y轴 - TTFT和TBT
ax2 = ax1.twinx()

# 绘制TTFT (折线图)
line1 = ax2.plot(x_pos, ttft, 'o-', color=color1, linewidth=3, markersize=10, 
                 label='TTFT (s)', markerfacecolor='white', markeredgewidth=2)

# 绘制TBT (折线图)
line2 = ax2.plot(x_pos, tbt, '^-', color=color4, linewidth=3, markersize=10,
                 label='TBT (s)', markerfacecolor='white', markeredgewidth=2)

ax2.set_ylabel('TTFT & TBT (seconds)', color='black', fontsize=14, fontweight='bold')
ax2.tick_params(axis='y', labelcolor='black')
ax2.set_ylim(0, max(max(ttft), max(tbt)) * 1.1)

# 添加网格
ax1.grid(True, alpha=0.3, linestyle='-', linewidth=0.5)
ax1.set_axisbelow(True)

# 添加数值标签
# for i, bar in enumerate(bars):
#     # 延迟标签
#     height = bar.get_height()
#     ax1.annotate(f'{height:.1f}', xy=(bar.get_x() + bar.get_width()/2, height),
#                 xytext=(0, 3), textcoords="offset points", ha='center', va='bottom',
#                 fontsize=9, fontweight='bold', color=color2)

# # TTFT标签
# for i, (x, y) in enumerate(zip(x_pos, ttft)):
#     ax2.annotate(f'{y:.3f}', xy=(x, y), xytext=(5, 5), textcoords="offset points",
#                 ha='left', va='bottom', fontsize=9, fontweight='bold', color=color1)

# # TBT标签
# for i, (x, y) in enumerate(zip(x_pos, tbt)):
#     ax2.annotate(f'{y:.3f}', xy=(x, y), xytext=(-5, 5), textcoords="offset points",
#                 ha='right', va='bottom', fontsize=9, fontweight='bold', color=color4)

# 合并图例
lines1, labels1 = ax1.get_legend_handles_labels()
lines2, labels2 = ax2.get_legend_handles_labels()
ax1.legend(lines1 + lines2, labels1 + labels2, loc='upper left', fontsize=11, bbox_to_anchor=(0.02, 0.8),
          framealpha=0.9, edgecolor='black')

# 添加标题和副标题
plt.suptitle('Performance Analysis of P/D Core Allocation Strategies', 
             fontsize=16, fontweight='bold', y=0.95)
ax1.set_title('Impact on TTFT, TBT, and Average Latency', 
              fontsize=12, style='italic', pad=20)

# 调整布局
plt.tight_layout()
plt.subplots_adjust(top=0.88)

# 显示图表
plt.show()

# 保存图表（可选）
# plt.savefig('pd_core_allocation_analysis.pdf', dpi=300, bbox_inches='tight')
plt.savefig('pd_core_allocation_analysis.png', dpi=300, bbox_inches='tight')