#pragma once

#define ASSERT 0
#define ASSERT_MODE 1

#ifndef DCACHE
#define DCACHE                                                                 \
    0 // 1: cache and DRAM in separete die, 2: cache and DRAM in the same HBM
      // die
#endif

// // 自定义宏
// #define DUMMY 1
// #define DUMMY_SRAM 1
// #define DUMMY_SRAMV 1
// #define USE_SRAM 1            // otherwise use Dcache
// // #define MAX_SRAM_SIZE 8388608 //33554432  // byte
// #define SRAM_BLOCK_SIZE 128   // byte
// #define USE_SRAM_MANAGER 0
// #define USE_NB_DRAMSYS 1
// #define USE_L1L2_CACHE 1
// #define USE_BEHA_SRAM 1
// #define FAST_WARMUP 1
// #define SR_PARA 0
// #define GPU_CACHE_DEBUG 0
// #define GPU_TMP_DEBUG 0
// #define NB_CACHE_DEBUG 0
// #define DEBUG_SRAM_MANAGER 0
// #define DRAM_ALIGN 1024
// #define DRAM_BURST_BYTE 0
// #define ENABLE_COLORS 0
// #define USE_GLOBAL_DRAM 0

// 自定义宏
#ifndef DUMMY
#define DUMMY 1
#endif

#ifndef DUMMY_SRAM
#define DUMMY_SRAM 1
#endif

#ifndef DUMMY_SRAMV
#define DUMMY_SRAMV 1
#endif

#ifndef USE_SRAM
#define USE_SRAM 1 // otherwise use Dcache
#endif

// #ifndef MAX_SRAM_SIZE
// #define MAX_SRAM_SIZE 8388608 //33554432  // byte
// #endif

#ifndef SRAM_BLOCK_SIZE
#define SRAM_BLOCK_SIZE 64 // byte
#endif

#ifndef USE_SRAM_MANAGER
#define USE_SRAM_MANAGER 0
#endif

#ifndef USE_NB_DRAMSYS
#define USE_NB_DRAMSYS 1
#endif

#ifndef USE_L1L2_CACHE
#define USE_L1L2_CACHE 1
#endif

#ifndef USE_BEHA_SRAM
#define USE_BEHA_SRAM 1
#endif

#ifndef FAST_WARMUP
#define FAST_WARMUP 1
#endif

#ifndef SR_PARA
#define SR_PARA 0
#endif

#ifndef GPU_CACHE_DEBUG
#define GPU_CACHE_DEBUG 0
#endif

#ifndef GPU_TMP_DEBUG
#define GPU_TMP_DEBUG 0
#endif

#ifndef NB_CACHE_DEBUG
#define NB_CACHE_DEBUG 0
#endif

#ifndef DEBUG_SRAM_MANAGER
#define DEBUG_SRAM_MANAGER 0
#endif

#ifndef DRAM_ALIGN
#define DRAM_ALIGN 1024
#endif

#ifndef DRAM_BURST_BYTE
#define DRAM_BURST_BYTE 2048
#endif
// same as DRAM_BURST_BYTE
#ifndef L1CACHELINESIZE
#define L1CACHELINESIZE 2048
#endif

#ifndef L2CACHELINESIZE
#define L2CACHELINESIZE 2048
#endif

#ifndef L1CACHESIZE
#define L1CACHESIZE 4194304
#endif


#ifndef L2CACHESIZE
#define L2CACHESIZE 15099494
#endif

#ifndef ENABLE_COLORS
#define ENABLE_COLORS 0
#endif

#ifndef USE_GLOBAL_DRAM
#define USE_GLOBAL_DRAM 0
#endif
#ifndef ROUTER_PIPE
#define ROUTER_PIPE 0
#endif
#ifndef ROUTER_LOOP
#define ROUTER_LOOP 0
#endif


#ifndef MSHRHIT
#define MSHRHIT 2
#endif

// 数据包占位
#define M_D_IS_END 1
#define M_D_MSG_TYPE 4
#define M_D_SEQ_ID 32
#define M_D_DES 16
#define M_D_OFFSET 32
#define M_D_TAG_ID 16
#define M_D_SOURCE 16
#define M_D_LENGTH 8
#define M_D_REFILL 1
#define M_D_DATA 128

// 路由消息负载相关
#define MAX_PACKET_ID 5
#define MAX_BUFFER_PACKET_SIZE 3

// 脉动阵列相关
#ifndef PE_GRID_X
#define PE_GRID_X 4
#endif
#define PE_GRID_SIZE (PE_GRID_X * PE_GRID_X)

// 绘图相关
#define VERBOSE_TRACE 0
#define USE_SFML 0
#define USE_CARIO 1

// 默认计算核dram配置文件路径
#define DEFAULT_DRAM_CONFIG_PATH "../DRAMSys/configs/ddr4-example-8bit.json"

// sram配置相关
#define SRAM_BITWIDTH 128
#define SRAM_BANKS 16
#define BANK_DEPTH 256
#define BANK_DEPTH_TMP 256
#define BANK_PORT_NUM 4
#define BANK_HIGH_READ_PORT_NUM 1
#define SIMU_READ_PORT 2
#define SIMU_WRITE_PORT 1
#define ADDRESS_MAX (SRAM_BANKS * BANK_DEPTH)
#define VERBOSE_PRINT 1
#define PERFORMANCE_MODE 0
#define KVCACHE_PRIOR_SPILL 1


// mem_access_unit相关
#define wait_time 10

// sram pos相关
#define UNSET_LABEL "unset_label"
#define DRAM_LABEL "dram_label"
#define ETERNAL_PREFIX "eternal_"
#define KVCACHE_PREFIX "kvcache_"
#define INPUT_LABEL "input_label"
// 最大输入input数量
#define MAX_SPLIT_NUM 6

// PD中的分块策略
#define MAX_PREFILL_WORKLOAD 384
#define PD_RATIO 4
#define CORE_CREDIT 4

// 是否开启数据流模式中的流水线并行（此时循环PD将不能使用）
#define PIPELINE_MODE 0

// 函数宏
#define ceil_macro(x) ((x) - (int)(x) > 0.1 ? (int)(x) + 1 : (int)(x))

// GPT2模型大小
#define NUM_PARAMETER_TENSORS 16

// obsolete
#define KVCACHE_MAX_SIZE 10240

// // BOARD PARAMETERS
// BOARD_W <= GRID_X

// Grid >= Board >= PACK_H > Die
// Grid 表示在x维度上所有的tile数量，最大
// PROXY_W 不会大于 GRID_X 如果等于表示 没有tascade模式， 没有proxy 域
// PACK_H 表示几个die封装成一个package
#ifndef BOARD_W // default max package size is 64, unless board is specificied,
                // then it's the same as board
#if PROXY_W < GRID_X // Proxy active, Tascade, package as die
#define DEFAULT_PACK_SIZE (DIE_W)
#else
#define DEFAULT_PACK_SIZE (DIE_W * 4) // Package of 16 dies
#endif
#define PACK_W (4 > DEFAULT_PACK_SIZE ? DEFAULT_PACK_SIZE : GRID_X)
#else
#define PACK_W (BOARD_W)
#endif

#ifndef BOARD_W
#define BOARD_W (4)
#endif
#define BOARD_H (BOARD_W)
#define BOARD_W_HALF (BOARD_W / 2)
#define BOARD_H_HALF (BOARD_H / 2)
#define BOARD_FACTOR (4 / BOARD_W)
#define BOARDS (BOARD_FACTOR * BOARD_FACTOR)
#if BOARD_W < GRID_X
#define MUX_BUS                                                                \
    2 // Multiplex output links to reduce the number of buses going out of the
      // board
#else
#define MUX_BUS 1 // Fixed to 1
#endif
#define BOARD_BUSES (BOARD_W / MUX_BUS)


#define PACK_H (PACK_W)
#define PACK_W_HALF (PACK_W / 2)
#define PACK_H_HALF (PACK_H / 2)
#define PACK_FACTOR (4 / PACK_W)
#define PACKAGES (PACK_FACTOR * PACK_FACTOR)
#define PACKAGES_BOARD_FACTOR (BOARD_W / PACK_W)
#define PACKAGES_PER_BOARD (PACKAGES_BOARD_FACTOR * PACKAGES_BOARD_FACTOR)

// PROXY_W < BOARD_W
// A PROXY MUST BE SMALLER THAN A BOARD
// PROXY PARAMETERS
#ifndef PROXY_W
#define PROXY_W (BOARD_W > 32 ? 32 : BOARD_W)
#endif
#define PROXY_H (PROXY_W)
#define PROXY_FACTOR (4 / PROXY_W)
#define PROXYS (PROXY_FACTOR * PROXY_FACTOR)

// DIE_W >= 4
// CHIPLET DIE PARAMETERS
#ifndef DIE_W
#define DIE_W (BOARD_W)
#endif
#ifndef DIE_H
#define DIE_H (DIE_W)
#endif
#define DIE_Wm1 (DIE_W - 1)
#define DIE_Hm1 (DIE_H - 1)

#define DIE_W_HALF (DIE_W / 2)
#define DIE_W_HALFm1 (DIE_W_HALF - 1)
#define DIE_H_HALF (DIE_H / 2)
#define DIE_H_HALFm1 (DIE_H_HALF - 1)
#define TILES_PER_DIE (DIE_W * DIE_H)
#define BORDER_TILES_PER_DIE (DIE_W * 2 + DIE_H * 2)
#define INNER_TILES_PER_DIE (TILES_PER_DIE - BORDER_TILES_PER_DIE)

#define DIE_FACTOR (4 / DIE_W)
#define DIES (DIE_FACTOR * DIE_FACTOR)

#define DIES_PACK_FACTOR (PACK_W / DIE_W)
#define DIES_PER_PACK (DIES_PACK_FACTOR * DIES_PACK_FACTOR)

#define DIES_BOARD_FACTOR (BOARD_W / DIE_W)


// NETWORK PARAMETERS
#ifndef TORUS
#define TORUS 0
#endif

// die的编号是先y方向编号的
#define die_id(tX, tY) ((tX / DIE_W) * DIE_FACTOR + (tY / DIE_H))

#define set_id_dcache(tag) ((tag % dcache_sets))
#define hops_to_mc(die_w) ((((die_w) / 2) + 0.5) * 2)

#define DIRECT_MAPPED 0
#if DIRECT_MAPPED
#define CACHE_WAY_BITS 0
#define CACHE_FREQ_BITS 0
#else
#define CACHE_WAY_BITS 3 // 8-way cache
#define CACHE_FREQ_BITS 3
#endif

#define CACHE_WAYS (1 << CACHE_WAY_BITS)
#define CACHE_MAX_FREQ (1 << CACHE_FREQ_BITS)
#define GLOBAL_COUNTERS 24
#define CYCLE 2