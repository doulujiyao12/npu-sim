#pragma once

// 作用与SEND,RECV TYPE相同，仅仅起到区分作用
enum GLOBAL_SEND_TYPE {
    GLOBAL_SEND_ACK = 0,
    GLOBAL_SEND_REQ,
    GLOBAL_SEND_DATA,
    GLOBAL_SEND_DONE,
};

enum GLOBAL_RECV_TYPE {
    GLOBAL_RECV_ACK = 0,
    GLOBAL_RECV_REQ,
    GLOBAL_RECV_DATA,
    GLOBAL_RECV_DONE,
};

// send原语的类型
enum SEND_TYPE {
    // 向RECV核发送握手信号
    SEND_ACK = 0,
    // 向RECV核发送数据请求信号
    SEND_REQ,
    // 向RECV核发送数据
    SEND_DATA,
    // DEPRECATED
    SEND_SRAM,
    // 最后一个核执行完所有指令后 向HOST发送DONE信号
    SEND_DONE,
};

// recv原语的类型
enum RECV_TYPE {
    // 接受HOST下发的原语
    RECV_CONF = 0,
    // SEND CORE 接受 RECV CORE 的握手信号
    RECV_ACK,
    // DEPRECATED
    RECV_FLAG,
    // 接受SEND核发送过来的路由数据
    RECV_DATA,
    // DEPRECATED
    RECV_SRAM,
    // 接收HOST下发的WEIGHT数据
    RECV_WEIGHT,
    // 接收初始数据
    RECV_START,
};

// 路由方向
enum Directions {
    WEST = 0,
    EAST,
    NORTH,
    SOUTH,
    CENTER,
    DIRECTIONS,
    HOST,
};

// 消息（数据包）类型
enum MSG_TYPE {
    CONFIG = 0,
    DATA,
    REQUEST,
    ACK,
    DONE,
    S_DATA, // start data
    P_DATA  // prepare data
};

// 数据类型
enum DATATYPE {
    INT8 = 0,
    FP16,
};

// 通过硬件组件模拟原语
enum HardwareConduct { SYSTOLIC_MATMUL, UNDEFINED };

// 原语config中执行完毕之后是否继续循环
enum LOOP_TYPE { FALSE, TRUE, BOTH };

// config中split原语类型
enum SPLIT_TYPE { SPLIT_TP, SPLIT_DP, SPLIT_HYBRID, NO_SPLIT };

// workercore正在运行哪一种原语类型
enum CORE_PRIM {
    PRIM_RECV = 0,
    PRIM_COMP,
    PRIM_SEND,
};

// 模拟模式
enum SIM_MODE {
    SIM_DATAFLOW = 0,
    SIM_GPU,
    SIM_PD,
    SIM_PDS,
};

// PD阶段
enum PD_PHASE {
    PREFILL = 0,
    DECODE,
    UNTOUCHED,
    PD_DONE,
};

// 在PD模式下每一个核需要进行的任务类型
enum PD_JOB {
    JOB_PREFILL = 0,
    JOB_DECODE,
    JOB_BOTH,
    JOB_NONE,
};

// 原语类型
enum PRIM_TYPE {
    NORM_PRIM = 0,
    COMP_PRIM,
    GPU_PRIM,
    PD_PRIM,
    MOE_PRIM,
};

// 原语序列化后的类型编号
enum PRIM_TYPE_CODE {
    LAYERNORM_F_TYPE = 0x1,
    MATMUL_F_TYPE = 0x2,
    ATTENTION_F_TYPE = 0x3,
    GELU_F_TYPE = 0x4,
    RESIDUAL_F_TYPE = 0x5,
    SEND_PRIM_TYPE = 0x6,
    RECV_PRIM_TYPE = 0x7,
    LOAD_PRIM_TYPE = 0x8,
    STORE_PRIM_TYPE = 0x9,
    CONV_F_TYPE = 0xa,
    RELU_F_TYPE = 0xb,
    SPLIT_MATMUL_TYPE = 0xc,
    MERGE_MATMUL_TYPE = 0xd,
    SPLIT_CONV_TYPE = 0xe,
    MERGE_CONV_TYPE = 0xf,
    BATCHNORM_F_TYPE = 0x10,
    // MATMUL_F_DECODE_TYPE = 0x11,
    // ATTENTION_F_DECODE_TYPE = 0x12,
    MAX_POOL_TYPE = 0x13,
    // MATMUL_F_PREFILL_TYPE = 0x14,
    // ATTENTION_F_PREFILL_TYPE = 0x15,
    ROPE_F_TYPE = 0x16,
    SILU_F_TYPE = 0x17,
    RMSNORM_F_TYPE = 0x18,
    SWIGLU_F_TYPE = 0x19,
    GATE_FORWARD_TYPE = 0x1a,
    MATMUL_FORWARD_MOE_TYPE = 0x1b,

    SEND_GLOBAL_MEMORY_TYPE = 0x40,
    RECV_GLOBAL_MEMORY_TYPE = 0x41,

    MATMUL_FORWARD_PD_TYPE = 0xc0,
    ATTENTION_FORWARD_PD_TYPE = 0xc1,
    ROPE_FORWARD_PD_TYPE = 0xc2,

    DUMMY_P_TYPE = 0xd0,
    SET_ADDR_TYPE = 0xd1,
    CLEAR_SRAM_TYPE = 0xd2,
    SET_BATCH_TYPE = 0xd3,
    SWITCH_DATA_TYPE = 0xd4,

    MATMUL_F_GPU_TYPE = 0xe0,
    ATTENTION_F_GPU_TYPE = 0xe1,
    GELU_F_GPU_TYPE = 0xe2,
    RESIDUAL_F_GPU_TYPE = 0xe3,
    LAYERNORM_F_GPU_TYPE = 0xe4,
};

// 用于计算核硬件配置
enum Etype { MAC_Array };
enum Sftype { Linear };