import re
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd
from matplotlib.colors import LinearSegmentedColormap

def parse_pipeline_data(file_path):
    """
    解析txt文件，提取所有request的所有token生成时间
    返回两个配置的数据
    """
    configs_data = {}
    
    with open(file_path, 'r', encoding='utf-8') as file:
        content = file.read()
    
    # 使用正则表达式匹配所有的测试块
    test_blocks = re.findall(r'\*([^*]+)\*\n(.*?)(?=\*|$)', content, re.DOTALL)
    
    for test_name, test_content in test_blocks:
        print(f"Processing configuration: {test_name}")
        
        # Store all data for this configuration
        config_data = []
        
        # Match all requests
        request_blocks = re.findall(r'Request (\d+):\s*\n(.*?)(?=Request \d+:|$)', test_content, re.DOTALL)
        
        for request_num, request_content in request_blocks:
            request_tokens = {}
            
            # Match all token times
            token_matches = re.findall(r'Token (\d+):\s*([0-9.]+)', request_content)
            
            for token_id, time_value in token_matches:
                request_tokens[int(token_id)] = float(time_value)
            
            if request_tokens:
                config_data.append({
                    'request_id': int(request_num),
                    'tokens': request_tokens
                })
        
        configs_data[test_name] = config_data
        print(f"  - Parsed {len(config_data)} requests")
    
    return configs_data

def analyze_pipeline_patterns(configs_data):
    """
    Analyze pipeline patterns
    """
    fig = plt.figure(figsize=(20, 12))
    
    config_names = list(configs_data.keys())
    
    for config_idx, (config_name, config_data) in enumerate(configs_data.items()):
        
        # Create subplots for each configuration
        ax1 = plt.subplot(2, 3, config_idx * 3 + 1)  # Timeline plot
        ax2 = plt.subplot(2, 3, config_idx * 3 + 2)  # Heatmap
        ax3 = plt.subplot(2, 3, config_idx * 3 + 3)  # Interval analysis
        
        # Prepare data
        all_times = []
        request_ids = []
        token_ids = []
        
        # Convert to milliseconds and collect data
        for req_data in config_data[:20]:  # Only show first 20 requests to avoid overcrowding
            req_id = req_data['request_id']
            for token_id, time_ns in req_data['tokens'].items():
                time_ms = time_ns / 1_000_000  # Convert to milliseconds
                all_times.append(time_ms)
                request_ids.append(req_id)
                token_ids.append(token_id)
        
        # 1. Timeline scatter plot - show pipeline overlap
        colors = plt.cm.tab20(np.linspace(0, 1, 20))
        for i, req_data in enumerate(config_data[:20]):
            req_id = req_data['request_id']
            times = [t / 1_000_000 for t in req_data['tokens'].values()]
            tokens = list(req_data['tokens'].keys())
            ax1.plot(tokens, times, 'o-', color=colors[i], alpha=0.7, markersize=2, linewidth=1)
        
        ax1.set_xlabel('Token ID')
        ax1.set_ylabel('Generation Time (ms)')
        ax1.set_title(f'{config_name}\nTimeline Plot (First 20 requests)')
        ax1.grid(True, alpha=0.3)
        
        # 2. Heatmap - show concurrent patterns
        # Create matrix: request x token
        matrix_data = np.full((20, 101), np.nan)  # 20 requests, 101 tokens (0-100)
        
        for i, req_data in enumerate(config_data[:20]):
            for token_id, time_ns in req_data['tokens'].items():
                if token_id <= 100:
                    matrix_data[i, token_id] = time_ns / 1_000_000
        
        im = ax2.imshow(matrix_data, aspect='auto', cmap='viridis', interpolation='nearest')
        ax2.set_xlabel('Token ID')
        ax2.set_ylabel('Request ID')
        ax2.set_title(f'{config_name}\nGeneration Time Heatmap')
        
        # 3. Token interval analysis
        intervals_by_token = {}
        for req_data in config_data:
            tokens = req_data['tokens']
            sorted_tokens = sorted(tokens.items())
            
            for i in range(1, len(sorted_tokens)):
                token_id = sorted_tokens[i][0]
                prev_time = sorted_tokens[i-1][1] / 1_000_000
                curr_time = sorted_tokens[i][1] / 1_000_000
                interval = curr_time - prev_time
                
                if token_id not in intervals_by_token:
                    intervals_by_token[token_id] = []
                intervals_by_token[token_id].append(interval)
        
        # Calculate average interval for each token position
        avg_intervals = []
        token_positions = []
        for token_id in sorted(intervals_by_token.keys()):
            if len(intervals_by_token[token_id]) > 0:
                avg_intervals.append(np.mean(intervals_by_token[token_id]))
                token_positions.append(token_id)
        
        ax3.plot(token_positions, avg_intervals, 'b-o', markersize=3)
        ax3.set_xlabel('Token Position')
        ax3.set_ylabel('Average Generation Interval (ms)')
        ax3.set_title(f'{config_name}\nToken Generation Intervals')
        ax3.grid(True, alpha=0.3)
    
    plt.tight_layout()
    plt.savefig('pipeline_analysis.png', dpi=300, bbox_inches='tight')
    plt.show()
    
    return True

def detailed_pipeline_analysis(configs_data):
    """
    Detailed pipeline analysis - focus on overlap and concurrency
    """
    fig, axes = plt.subplots(2, 2, figsize=(16, 10))
    
    config_names = list(configs_data.keys())
    
    for config_idx, (config_name, config_data) in enumerate(configs_data.items()):
        row = config_idx
        
        # Left plot: detailed timeline for first 10 requests
        ax_left = axes[row, 0]
        
        colors = plt.cm.Set3(np.linspace(0, 1, 10))
        for i, req_data in enumerate(config_data[:10]):
            req_id = req_data['request_id']
            times = [t / 1_000_000 for t in req_data['tokens'].values()]
            tokens = list(req_data['tokens'].keys())
            
            # Draw token generation timeline for each request
            ax_left.plot(times, tokens, 'o-', color=colors[i], 
                        label=f'Req {req_id}', alpha=0.8, markersize=3, linewidth=1.5)
        
        ax_left.set_xlabel('Generation Time (ms)')
        ax_left.set_ylabel('Token ID')
        ax_left.set_title(f'{config_name} - Detailed Timeline\n(First 10 requests)')
        ax_left.legend(bbox_to_anchor=(1.05, 1), loc='upper left', fontsize=8)
        ax_left.grid(True, alpha=0.3)
        
        # Right plot: concurrency analysis
        ax_right = axes[row, 1]
        
        # Calculate how many requests are processing in parallel at any given time
        time_points = []
        concurrent_requests = []
        
        # Collect all time points
        all_times = []
        for req_data in config_data:
            for time_ns in req_data['tokens'].values():
                all_times.append(time_ns / 1_000_000)
        
        all_times = sorted(set(all_times))
        sample_times = all_times[::len(all_times)//1000] if len(all_times) > 1000 else all_times
        
        for sample_time in sample_times:
            concurrent_count = 0
            for req_data in config_data:
                req_times = [t / 1_000_000 for t in req_data['tokens'].values()]
                if req_times and min(req_times) <= sample_time <= max(req_times):
                    concurrent_count += 1
            
            time_points.append(sample_time)
            concurrent_requests.append(concurrent_count)
        
        ax_right.plot(time_points, concurrent_requests, 'g-', linewidth=2)
        ax_right.fill_between(time_points, concurrent_requests, alpha=0.3, color='green')
        ax_right.set_xlabel('Time (ms)')
        ax_right.set_ylabel('Concurrent Requests Count')
        ax_right.set_title(f'{config_name} - Concurrency Analysis')
        ax_right.grid(True, alpha=0.3)
        
        # Add statistics
        max_concurrent = max(concurrent_requests) if concurrent_requests else 0
        avg_concurrent = np.mean(concurrent_requests) if concurrent_requests else 0
        ax_right.text(0.02, 0.98, f'Max Concurrent: {max_concurrent}\nAvg Concurrent: {avg_concurrent:.1f}', 
                     transform=ax_right.transAxes, verticalalignment='top',
                     bbox=dict(boxstyle='round', facecolor='wheat', alpha=0.8))
    
    plt.tight_layout()
    plt.savefig('detailed_pipeline_analysis.png', dpi=300, bbox_inches='tight')
    plt.show()
    
    # Print pipeline analysis results
    print("\n=== Pipeline Analysis Results ===")
    for config_name, config_data in configs_data.items():
        print(f"\nConfiguration: {config_name}")
        
        # Calculate time span between first and last tokens
        first_token_times = []
        last_token_times = []
        
        for req_data in config_data:
            tokens = req_data['tokens']
            if tokens:
                first_token_times.append(min(tokens.values()) / 1_000_000)
                last_token_times.append(max(tokens.values()) / 1_000_000)
        
        if first_token_times and last_token_times:
            total_span = max(last_token_times) - min(first_token_times)
            avg_request_time = np.mean([max_t - min_t for max_t, min_t in zip(last_token_times, first_token_times)])
            
            print(f"  Total processing time span: {total_span:.2f} ms")
            print(f"  Average single request time: {avg_request_time:.2f} ms")
            print(f"  Theoretical serial time: {avg_request_time * len(config_data):.2f} ms")
            print(f"  Actual parallel efficiency: {(avg_request_time * len(config_data) / total_span):.2f}x")

def main():
    file_path = input("Please enter txt file path: ").strip()
    
    try:
        print("Starting data parsing...")
        configs_data = parse_pipeline_data(file_path)
        
        if len(configs_data) != 2:
            print(f"Warning: Expected 2 configurations, but found {len(configs_data)} configurations")
        
        print("Drawing pipeline analysis plots...")
        analyze_pipeline_patterns(configs_data)
        
        print("Drawing detailed analysis plots...")
        detailed_pipeline_analysis(configs_data)
        
        print("Analysis completed! Images saved as 'pipeline_analysis.png' and 'detailed_pipeline_analysis.png'")
        
    except FileNotFoundError:
        print(f"File not found: {file_path}")
    except Exception as e:
        print(f"Error occurred during processing: {e}")

if __name__ == "__main__":
    main()