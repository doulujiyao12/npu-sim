import json
import matplotlib.pyplot as plt
import numpy as np
import os
from collections import defaultdict
import seaborn as sns

# 设置学术会议风格
plt.rcParams.update({

    'font.size': 12,
    'axes.labelsize': 14,
    'axes.titlesize': 16,
    'xtick.labelsize': 12,
    'ytick.labelsize': 12,
    'legend.fontsize': 11,
    'figure.titlesize': 18,
    'lines.linewidth': 2,
    'axes.linewidth': 1.2,
    'grid.alpha': 0.3,
    'axes.grid': True,
    'grid.linestyle': '--'
})

# 设置颜色调色板
bar_colors = ['#7E8CAD', '#7A9273', '#B37070', '#A28CC2', '#3f8fbf', '#0f5f7f', '#1a8c5c', '#8c564b', '#2f5f8f', '#5f9ea0']
line_colors = ["#5a35ff", '#ffa726', '#FF7F0E', '#ee4266', '#f25c78', '#ff8c42', '#ffb347', '#ff7b7b', '#ff9f40', '#ffa726']

def parse_filename(filepath):
    """从文件路径中提取场景名"""
    filename = os.path.basename(filepath)
    name_without_ext = filename.replace('.json', '')
    parts = name_without_ext.split('_')
    
    # 场景名是最后两个部分 (例如: 1_1, 2_2等)
    if len(parts) >= 2:
        scenario_name = '_'.join(parts[-2:])
    else:
        scenario_name = 'unknown'
    
    return scenario_name

def load_and_process_data(json_files):
    """加载和处理JSON数据"""
    AREA = [749.4, 749.4, 761.7, 648.45]  # 常量
    
    # 按文件和场景组织数据: {file_key: {scenario: data}}
    file_data = {}
    all_scenarios = set()
    cnt = 0
    
    for file_key, data_dict in json_files.items():
        file_data[file_key] = {}
        
        for file_path, metrics in data_dict.items():
            scenario_name = parse_filename(file_path)
            
            # 提取需要的数据
            tbt_mean = metrics['tbt']['min'] / 1e12  # 转换为秒
            throughput = metrics['throughput_tokens_per_second']*1000
            throughput_per_area = throughput / AREA[cnt]
            tbt_area = tbt_mean / AREA[cnt]
            
            file_data[file_key][scenario_name] = {
                'tbt_mean': tbt_mean,
                'throughput': throughput,
                'throughput_per_area': throughput_per_area,
                'tbt_area': tbt_area,
                'file_path': file_path
            }
            
            all_scenarios.add(scenario_name)
        
        cnt += 1
    
    return file_data, sorted(all_scenarios, key=lambda x: (-int(x.split('_')[1]), int(x.split('_')[0])))

def create_subplot(ax, ax2, file_data, scenarios, plot_type, file_keys, file_color_map, line_color_map):
    """创建单个子图"""
    # 计算布局参数
    n_files = len(file_data)
    n_scenarios = len(scenarios)
    
    # 每个场景组的宽度和柱子宽度
    group_width = 0.8
    bar_width = group_width / n_files
    group_spacing = 0.3  # 场景组之间的间隔
    
    # 计算每个场景组的中心位置
    group_positions = np.arange(n_scenarios) * (1 + group_spacing)
    
    if plot_type == 'throughput':
        # 绘制柱状图 (显示 throughput)
        for file_idx, file_key in enumerate(file_keys):
            throughput_values = []
            bar_positions = []
            
            for scenario_idx, scenario in enumerate(scenarios):
                # 计算该文件在该场景组中的位置
                group_center = group_positions[scenario_idx]
                bar_pos = group_center + (file_idx - (n_files-1)/2) * bar_width
                
                if scenario in file_data[file_key]:
                    throughput_value = file_data[file_key][scenario]['throughput']
                    throughput_values.append(throughput_value)
                    bar_positions.append(bar_pos)
            
            # 绘制该文件的所有柱子
            if throughput_values:  # 只有当有数据时才绘制
                bars = ax.bar(bar_positions, throughput_values, bar_width, 
                             label=f'File {file_key} (Throughput)', 
                             color=file_color_map[file_key], alpha=0.8, 
                             edgecolor='black', linewidth=0.8, hatch="//")
                
                # # 添加数值标签
                # for bar, value in zip(bars, throughput_values):
                #     ax.text(bar.get_x() + bar.get_width()/2, 
                #            bar.get_height() + max([max(file_data[fk].get(s, {'throughput': 0})['throughput'] for s in scenarios) for fk in file_keys])*0.01,
                #            f'{value:.2f}', ha='center', va='bottom', fontsize=8, rotation=0)
        
        # 绘制折线图 (throughput/area)
        for file_idx, file_key in enumerate(file_keys):
            line_x = []
            line_y = []
            
            for scenario_idx, scenario in enumerate(scenarios):
                if scenario in file_data[file_key]:
                    line_x.append(group_positions[scenario_idx])
                    line_y.append(file_data[file_key][scenario]['throughput_per_area'])
            
            if len(line_x) > 0:
                ax2.plot(line_x, line_y, marker='o', linewidth=3, markersize=8,
                        label=f'File {file_key} (T/A)', 
                        color=line_color_map[file_key],
                        markerfacecolor='white', markeredgecolor=line_color_map[file_key],
                        markeredgewidth=2, zorder=10)
        
        # 设置标签
        ax.set_ylabel('Throughput (tokens/s)', fontweight='bold', fontsize=35)
        ax2.set_ylabel('Throughput/Area (tokens/s/mm²)', fontweight='bold', fontsize=30)
        ax.set_title('Throughput', fontweight='bold', pad=15, fontsize=35)
        ax2.ticklabel_format(style='scientific', axis='y', scilimits=(0,0))
        ax2.yaxis.get_offset_text().set_fontsize(30)

        # ax.ticklabel_format(style='scientific', axis='y', scilimits=(0,0))
        # ax.yaxis.get_offset_text().set_fontsize(15)
        
    elif plot_type == 'tbt':
        # 绘制柱状图 (显示 TBT)
        for file_idx, file_key in enumerate(file_keys):
            tbt_values = []
            bar_positions = []
            
            for scenario_idx, scenario in enumerate(scenarios):
                group_center = group_positions[scenario_idx]
                bar_pos = group_center + (file_idx - (n_files-1)/2) * bar_width
                
                if scenario in file_data[file_key]:
                    tbt_value = file_data[file_key][scenario]['tbt_mean']
                    tbt_values.append(tbt_value)
                    bar_positions.append(bar_pos)
            
            if tbt_values:
                bars = ax.bar(bar_positions, tbt_values, bar_width, 
                             label=f'File {file_key} (TBT)', 
                             color=file_color_map[file_key], alpha=0.8, 
                             edgecolor='black', linewidth=0.8,hatch="--")
                
                # # 添加数值标签
                # for bar, value in zip(bars, tbt_values):
                #     ax.text(bar.get_x() + bar.get_width()/2, 
                #            bar.get_height() + max([max(file_data[fk].get(s, {'tbt_mean': 0})['tbt_mean'] for s in scenarios) for fk in file_keys])*0.01,
                #            f'{value:.1f}', ha='center', va='bottom', fontsize=8, rotation=0)
        
        # 绘制折线图 (tbt/area)
        for file_idx, file_key in enumerate(file_keys):
            line_x = []
            line_y = []
            
            for scenario_idx, scenario in enumerate(scenarios):
                if scenario in file_data[file_key]:
                    line_x.append(group_positions[scenario_idx])
                    line_y.append(file_data[file_key][scenario]['tbt_area'])
            
            if len(line_x) > 0:
                ax2.plot(line_x, line_y, marker='o', linewidth=3, markersize=8,
                        label=f'File {file_key} (T/A)', 
                        color=line_color_map[file_key],
                        markerfacecolor='white', markeredgecolor=line_color_map[file_key],
                        markeredgewidth=2, zorder=10)
        
        # 设置标签
        ax.set_ylabel('Time Between Tokens (s)', fontweight='bold', fontsize=35)
        ax2.set_ylabel('TBT/Area (s/mm²)', fontweight='bold', fontsize=35)
        ax.set_title('TBT', fontweight='bold', pad=15, fontsize=35)
        ax2.ticklabel_format(style='scientific', axis='y', scilimits=(0,0))
        ax2.yaxis.get_offset_text().set_fontsize(30)
    
    # 通用设置
    ax.set_xticks(group_positions)
    ax.set_xticklabels([f'P/D {s}' for s in scenarios], fontsize=10)
    
    # 在场景组之间添加分隔线
    for i in range(len(group_positions) - 1):
        separator_x = (group_positions[i] + group_positions[i+1]) / 2
        ax.axvline(x=separator_x, color='gray', linestyle=':', alpha=0.5, linewidth=1)
    
    # ax.set_xlabel('Scenarios', fontweight='bold', fontsize=12)
    ax.tick_params(axis='y', labelsize=30)
    ax2.tick_params(axis='y', labelsize=30)
    ax.tick_params(axis='x', labelsize=30)
    ax.set_ylim(bottom=0)
    ax2.set_ylim(bottom=0)
    ax.grid(True, alpha=0.3, linestyle='--')
    ax.set_xlim(-0.5, group_positions[-1] + 0.5)

def create_combined_plot(file_data, scenarios):
    """创建合并的双子图"""
    # 创建主图形和子图
    fig, (ax1, ax3) = plt.subplots(1, 2, figsize=(30, 10))
    
    # 创建双y轴
    ax2 = ax1.twinx()
    ax4 = ax3.twinx()
    
    # 为每个文件分配颜色
    file_keys = list(file_data.keys())
    file_color_map = {file_key: bar_colors[i % len(bar_colors)] 
                      for i, file_key in enumerate(file_keys)}
    line_color_map = {file_key: line_colors[i % len(line_colors)] 
                      for i, file_key in enumerate(file_keys)}
    
    # 创建左侧子图 (Throughput)
    create_subplot(ax1, ax2, file_data, scenarios, 'throughput', file_keys, file_color_map, line_color_map)
    
    # 创建右侧子图 (TBT)
    create_subplot(ax3, ax4, file_data, scenarios, 'tbt', file_keys, file_color_map, line_color_map)
    
    # # 设置总标题
    # fig.suptitle('Performance Analysis: Throughput vs TBT by Scenario', 
    #             fontweight='bold', fontsize=20, y=0.98)
    
    # 创建统一的图例
    bar_legend_elements = []
    line_legend_elements = []
    legend_handles = []
    legend_labels = []
    
    for file_key in file_keys:
        # bar_legend_elements.append(plt.Rectangle((0,0),1,1, 
        #                                         fc=file_color_map[file_key], 
        #                                         alpha=0.8, 
        #                                         label=file_key.removesuffix('.json')))
        # line_legend_elements.append(plt.Line2D([0], [0], 
        #                                      color=line_color_map[file_key], 
        #                                      linewidth=3, marker='o', markersize=6,
        #                                      markerfacecolor='white',
        #                                      markeredgecolor=line_color_map[file_key],
        #                                      label=file_key.removesuffix('.json')))
        rect = plt.Rectangle((0,0),1,1, 
                                                fc=file_color_map[file_key], 
                                                alpha=0.8, 
                                                )
        line = plt.Line2D([0], [0], 
                                             color=line_color_map[file_key], 
                                             linewidth=3, marker='o', markersize=6,
                                             markerfacecolor='white',
                                             markeredgecolor=line_color_map[file_key],
                                             )
        legend_handles.append((rect, line))
        legend_labels.append(file_key.removesuffix('.json'))

    # 在图的底部中央添加图例
    # legend1 = fig.legend(handles=bar_legend_elements, loc='lower left', 
    #                     bbox_to_anchor=(0.15, 0.01), ncol=2,
    #                     frameon=True, fancybox=True, shadow=True,
    #                     title='Bar Charts (Left Axis)', title_fontsize=25, fontsize=25)
    # legend2 = fig.legend(handles=line_legend_elements, loc='lower right', 
    #                     bbox_to_anchor=(0.85, 0.01), ncol=2,
    #                     frameon=True, fancybox=True, shadow=True,
    #                     title='Line Charts (Right Axis)', title_fontsize=25, fontsize=25)
    fig.legend(handles=legend_handles,
           labels=legend_labels,
           loc='lower left', 
            bbox_to_anchor=(0.3, 0.01), ncol=2,
            frameon=True, fancybox=True, shadow=True,
             title_fontsize=30, fontsize=30)
    
    plt.tight_layout()
    plt.subplots_adjust(bottom=0.15, top=0.93)  # 为图例和标题留出空间
    return fig

def load_json_files(file_paths):
    """加载JSON文件"""
    json_data = {}
    for file_path in file_paths:
        with open(file_path, 'r', encoding='utf-8') as f:
            json_data[os.path.basename(file_path)] = json.load(f)
    return json_data

# 使用示例数据
if __name__ == "__main__":
    # 使用方法：
    file_paths = ['./P-D fusion.json', './P-D disaggregation(homo).json', './P-D disaggregation(heter_case2).json', './P-D disaggregation(heter_case7).json']  # 您的JSON文件路径列表
    json_files = load_json_files(file_paths)
    file_data, scenarios = load_and_process_data(json_files)
    fig = create_combined_plot(file_data, scenarios)
    plt.subplots_adjust(bottom=0.25)
    plt.show()
    fig.savefig('split_vs_fuse.pdf', format='pdf')
    
    # 保存图表
    # plt.savefig('combined_scenario_analysis.pdf', dpi=300, bbox_inches='tight')
    # plt.savefig('combined_scenario_analysis.png', dpi=300, bbox_inches='tight')