#pragma once
#include "defs/enums.h"
#include <iostream>

std::string ToHexString(int value);

// 打印表格
void PrintBar(int length);
void PrintRow(const std::string &label, int value);

// 获取枚举类型
std::string GetEnumSendType(SEND_TYPE type);
std::string GetEnumRecvType(RECV_TYPE type);

// 输出工具
#define ARGUS_PRINT(var)                                                       \
    do {                                                                       \
        std::cout << "@" << __PRETTY_FUNCTION__ << " > " << #var << ": "       \
                  << (var) << std::endl;                                       \
    } while (0)

#define ARGUS_EXIT(...)                                                        \
    do {                                                                       \
        std::ostringstream oss;                                                \
        oss << __VA_ARGS__;                                                    \
        std::cout << "[ERROR]: " << oss.str() << std::endl;                    \
        sc_stop();                                                             \
    } while (0)
    