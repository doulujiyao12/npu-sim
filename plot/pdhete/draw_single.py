import json
import matplotlib.pyplot as plt
import numpy as np
import seaborn as sns

plt.style.use('seaborn-v0_8-whitegrid')
sns.set_palette("husl")

# CHIP_AREA = [749.4, 761.7, 798.5, 685.29, 758.97, 861.45, 648.45, 636.17, 623.89, 726.37, 738.65, 714.09]
# CHIP_AREA = [726.37, 749.4, 714.09, 761.7, 738.65, 798.5, 861.45, 623.89, 636.17, 648.45, 685.29, 758.97]
CHIP_AREA = [726.37, 749.4, 761.7, 798.5, 861.45, 623.89, 636.17, 648.45, 685.29, 758.97]
x_label = [ 'base_C64D60', '1_C64D30', '2_C64D120', '3_C64D240', '4_C64D480', '5_C32D30', '6_C32D60', '7_C32D120', '8_C32D240', '9_C32D480']

def load_and_process_data_multiple(json_file_paths):
    """Load multiple JSON files, only first two configs per file"""
    all_data = []
    cnt = 0

    for file_path in json_file_paths:
        with open(file_path, 'r') as f:
            data = json.load(f)

        configs = []
        tbt_mins = []
        throughput_vals = []
        throughput_per_area = []
        tbt_per_area = []

        # Only take first two items
        for config_name, metrics in list(data.items())[:2]:
            configs.append(config_name)
            tbt_mins.append(metrics['tbt']['min'] / 1e12)    # sec
            throughput_vals.append(metrics['throughput_tokens_per_second']*1000)
            throughput_per_area.append(
                1000*metrics['throughput_tokens_per_second'] / CHIP_AREA[cnt]
            )
            tbt_per_area.append(
                (metrics['tbt']['mean'] / 1e12) / CHIP_AREA[cnt]
            )

        all_data.append({
            'file': file_path,
            'configs': configs,
            'tbt': tbt_mins,
            'throughput': throughput_vals,
            'throughput_area': throughput_per_area,
            'tbt_area': tbt_per_area
        })

        cnt += 1

    return all_data


def create_performance_visualization_multiple(json_file_paths):
    """Plot dual subplots: Left (Throughput + Throughput/Area), Right (TBT + TBT/Area)"""
    all_data = load_and_process_data_multiple(json_file_paths)

    fig, (ax1, ax3) = plt.subplots(1, 2, figsize=(20, 6))

    num_files = len(json_file_paths)
    x_pos = np.arange(num_files)
    bar_width = 0.35

    colors = sns.color_palette("husl", 2)  # cfg1, cfg2 用不同颜色

    # ======================
    # LEFT SUBPLOT: Throughput
    # ======================
    throughput_cfg1 = [d['throughput'][0] for d in all_data]
    throughput_cfg2 = [d['throughput'][1] for d in all_data]
    
    print(throughput_cfg1)

    ax1.bar(x_pos - bar_width/2, throughput_cfg1, bar_width, 
            label="Throughput - Output 100", alpha=0.8, hatch = "//", color='#B37070')
    ax1.bar(x_pos + bar_width/2, throughput_cfg2, bar_width, 
            label="Throughput - Output 200", alpha=0.8,hatch = "--", color='#7A9273')

    ax1.set_ylabel('Throughput (tokens/s)', fontsize=20, fontweight='bold')
    ax1.set_title('Throughput Performance', fontsize=20, fontweight='bold')

    # 横坐标标签
    ax1.set_xticks(x_pos)
    ax1.set_xticklabels(x_label, fontsize=15, rotation=45, ha='right')

    # Secondary axis for Throughput/Area
    ax2 = ax1.twinx()
    ax2.set_ylabel('Throughput/Area (tokens/s/mm²)', fontsize=20, fontweight='bold')
    ax1.tick_params(axis='y', labelsize=15)
    ax2.tick_params(axis='y', labelsize=15)
    ax2.ticklabel_format(style='scientific', axis='y', scilimits=(0,0))
    ax2.yaxis.get_offset_text().set_fontsize(15)
    ax1.set_ylim(0, max(throughput_cfg1) * 1.3)
    # 两根折线（Throughput/Area）
    tpa_cfg1 = [d['throughput_area'][0] for d in all_data]
    tpa_cfg2 = [d['throughput_area'][1] for d in all_data]
    print("ea")
    print(tpa_cfg1)

    ax2.plot(x_pos, tpa_cfg1, 'o-', linewidth=3, markersize=8, 
             label="Throughput/Area - Output 100", color='#7E8CAD')
    ax2.plot(x_pos, tpa_cfg2, 's-', linewidth=3, markersize=8, 
             label="Throughput/Area - Output 200", color='#A28CC2')
    ax2.set_ylim(0, max(tpa_cfg1) * 1.3)

    # Legends for left subplot
    ax1.legend(loc='upper left', fontsize=15, frameon=True, fancybox=True, shadow=True)
    ax2.legend(loc='upper right', fontsize=15, frameon=True, fancybox=True, shadow=True)

    # ======================
    # RIGHT SUBPLOT: TBT
    # ======================
    tbt_cfg1 = [d['tbt'][0] for d in all_data]
    tbt_cfg2 = [d['tbt'][1] for d in all_data]

    ax3.bar(x_pos - bar_width/2, tbt_cfg1, bar_width, 
            label="Min TBT - Output 100", alpha=0.8,hatch = "//", color='#B37070')
    ax3.bar(x_pos + bar_width/2, tbt_cfg2, bar_width, 
            label="Min TBT - Output 200", alpha=0.8,hatch = "--", color='#7A9273')

    ax3.set_ylabel('Min TBT (s)', fontsize=20, fontweight='bold')
    ax3.set_title('Minimum Time Between Tokens Performance', fontsize=20, fontweight='bold')

    # 横坐标标签
    ax3.set_xticks(x_pos)
    ax3.set_xticklabels(x_label, fontsize=15, rotation=45, ha='right')

    # Secondary axis for TBT/Area
    ax4 = ax3.twinx()
    ax4.set_ylabel('Min TBT/Area (s/mm²)', fontsize=20, fontweight='bold')
    ax3.tick_params(axis='y', labelsize=15)
    ax4.tick_params(axis='y', labelsize=15)
    ax3.set_ylim(0, max(tbt_cfg2) * 1.3)


    # 两根折线（TBT/Area）
    tbt_area_cfg1 = [d['tbt_area'][0] for d in all_data]
    tbt_area_cfg2 = [d['tbt_area'][1] for d in all_data]
    print("aaa")
    print(tbt_area_cfg1)

    ax4.plot(x_pos, tbt_area_cfg1, 'o-', linewidth=3, markersize=8, 
             label="Min TBT/Area - Output 100", color='#7E8CAD')
    ax4.plot(x_pos, tbt_area_cfg2, 's-', linewidth=3, markersize=8, 
             label="Min TBT/Area - Output 200", color='#A28CC2')
    ax4.ticklabel_format(style='scientific', axis='y', scilimits=(0,0))
    ax4.yaxis.get_offset_text().set_fontsize(15)

    # Legends for right subplot
    ax3.legend(loc='upper left', fontsize=15, frameon=True, fancybox=True, shadow=True)
    ax4.legend(loc='upper right', fontsize=15, frameon=True, fancybox=True, shadow=True)
    ax4.set_ylim(0, max(tbt_area_cfg2) * 1.3)

    plt.tight_layout()
    plt.savefig('llm_performance_analysis_dual.pdf', dpi=300, bbox_inches='tight')
    plt.savefig('llm_performance_analysis_dual.png', dpi=300, bbox_inches='tight')
    plt.show()


if __name__ == "__main__":
    files = [
        "base_analysis_results.json",
        "9_analysis_results.json",
        # "pdhete/11_analysis_results.json",
        "1_analysis_results.json",
        # "pdhete/10_analysis_results.json",
        "2_analysis_results.json",
        "5_analysis_results.json",
        "8_analysis_results.json",
        "7_analysis_results.json",
        "6_analysis_results.json",
        "3_analysis_results.json",
        "4_analysis_results.json",
    ]
    create_performance_visualization_multiple(files)