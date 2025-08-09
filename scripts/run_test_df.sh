#!/bin/bash

# 脚本说明：批量运行 npusim 实验，遍历顺序：sram_max → df_dram_bw → config_file

# 定义变量
NPUSIM="./npusim"
CORE_CONFIG="../llm/test/core_configs/core_6x6_bw.json"
CONFIGS=(
    "../llm/test/gpt2_small/pd_fuse_12*1_gpu.json"
    "../llm/test/gpt2_small/pd_fuse_12*1_gpu2.json"
)
DF_DRAM_BW_VALUES=(4 8 16)
SRAM_MAX_VALUES=(8388608 16777216 33554432)

# 输出目录
LOG_DIR="./logs"
mkdir -p "$LOG_DIR"

echo "开始运行实验..."
echo "遍历顺序：sram-max → df_dram_bw → config_file"

# 外层：sram-max
for sram_max in "${SRAM_MAX_VALUES[@]}"; do
    echo "=== 开始测试 sram-max: $sram_max ==="
    for config_file in "${CONFIGS[@]}"; do
        for df_bw in "${DF_DRAM_BW_VALUES[@]}"; do
            echo "  --- df_dram_bw: $df_bw ---"
        
            # 构造安全的日志文件名（替换 / 和 *）
            safe_config=$(echo "$config_file" | sed 's|[*/]|_|g')
            log_file="${LOG_DIR}/sram${sram_max}_df${df_bw}_${safe_config}.log"

            echo "    运行配置: $config_file"
            echo "    日志: $log_file"

            ./npusim \
                --config-file "$config_file" \
                --core-config-file "$CORE_CONFIG" \
                --df_dram_bw "$df_bw" \
                --beha_dram false \
                --sram-max "$sram_max" 

            sleep 1
            # 构造要执行的命令（用于显示）
            
        done
    done
done

echo "所有实验完成！"

