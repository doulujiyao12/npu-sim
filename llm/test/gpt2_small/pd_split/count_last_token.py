import re
import matplotlib.pyplot as plt
import numpy as np
from collections import defaultdict

def parse_token_data(file_path):
    """
    解析txt文件，提取所有request的最后一个token（Token 100）的生成时间
    """
    last_token_times = []
    
    with open(file_path, 'r', encoding='utf-8') as file:
        content = file.read()
    
    # 使用正则表达式匹配所有的测试块
    test_blocks = re.findall(r'\*([^*]+)\*\n(.*?)(?=\*|$)', content, re.DOTALL)
    
    for test_name, test_content in test_blocks:
        print(f"处理测试: {test_name}")
        
        # 匹配所有request
        request_blocks = re.findall(r'Request (\d+):\s*\n(.*?)(?=Request \d+:|$)', test_content, re.DOTALL)
        
        for request_num, request_content in request_blocks:
            # 查找Token 100的时间
            token_100_match = re.search(r'Token 100:\s*([0-9.]+)', request_content)
            if token_100_match:
                time_value = float(token_100_match.group(1))
                last_token_times.append(time_value)
                
    return last_token_times

def plot_token_distribution(times, output_file='token_distribution.png'):
    """
    绘制最后一个token生成时间的分布图
    """
    # 转换为毫秒或更合适的单位（假设原始数据是纳秒）
    times_ms = [t / 1_000_000 for t in times]  # 转换为毫秒
    
    # 创建图表
    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(12, 10))
    
    # 直方图
    ax1.hist(times_ms, bins=50, alpha=0.7, color='skyblue', edgecolor='black')
    ax1.set_xlabel('最后Token生成时间 (毫秒)')
    ax1.set_ylabel('频次')
    ax1.set_title('所有Request最后Token生成时间分布 - 直方图')
    ax1.grid(True, alpha=0.3)
    
    # 添加统计信息
    mean_time = np.mean(times_ms)
    median_time = np.median(times_ms)
    std_time = np.std(times_ms)
    
    ax1.axvline(mean_time, color='red', linestyle='--', label=f'平均值: {mean_time:.2f}ms')
    ax1.axvline(median_time, color='green', linestyle='--', label=f'中位数: {median_time:.2f}ms')
    ax1.legend()
    
    # 箱线图
    ax2.boxplot(times_ms, vert=False, patch_artist=True, 
               boxprops=dict(facecolor='lightblue', alpha=0.7))
    ax2.set_xlabel('最后Token生成时间 (毫秒)')
    ax2.set_title('所有Request最后Token生成时间分布 - 箱线图')
    ax2.grid(True, alpha=0.3)
    
    plt.tight_layout()
    
    # 显示统计信息
    print(f"\n统计信息:")
    print(f"总Request数量: {len(times)}")
    print(f"平均时间: {mean_time:.2f} 毫秒")
    print(f"中位数时间: {median_time:.2f} 毫秒")
    print(f"标准差: {std_time:.2f} 毫秒")
    print(f"最小值: {min(times_ms):.2f} 毫秒")
    print(f"最大值: {max(times_ms):.2f} 毫秒")
    
    # 保存图片
    plt.savefig(output_file, dpi=300, bbox_inches='tight')
    plt.show()
    
    return times_ms

def main():
    # 指定txt文件路径
    file_path = input("请输入txt文件路径: ").strip()
    
    try:
        # 解析数据
        print("开始解析数据...")
        last_token_times = parse_token_data(file_path)
        
        if not last_token_times:
            print("未找到有效的Token 100数据！")
            return
            
        print(f"成功解析 {len(last_token_times)} 个request的最后token时间")
        
        # 绘制分布图
        print("绘制分布图...")
        times_ms = plot_token_distribution(last_token_times)
        
        # 可选：保存原始数据到CSV
        save_csv = input("是否保存原始数据到CSV文件？(y/n): ").strip().lower()
        if save_csv == 'y':
            import pandas as pd
            df = pd.DataFrame({
                'request_index': range(len(times_ms)),
                'last_token_time_ms': times_ms
            })
            df.to_csv('last_token_times.csv', index=False)
            print("数据已保存到 last_token_times.csv")
            
    except FileNotFoundError:
        print(f"文件未找到: {file_path}")
    except Exception as e:
        print(f"处理过程中出现错误: {e}")

if __name__ == "__main__":
    main()