#pragma once

// send原语的类型
enum SEND_TYPE {
    SEND_ACK = 0,
    SEND_REQ,
    SEND_DRAM,
    SEND_SRAM,
    SEND_DONE,
};

// recv原语的类型
enum RECV_TYPE {
    RECV_CONF = 0,
    RECV_ACK,
    RECV_REQ,
    RECV_DRAM,
    RECV_SRAM,
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
};