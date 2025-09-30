#pragma once
#include "defs/enums.h"
#include <iostream>
#include <sstream>

std::string ToHexString(int value);

// 打印表格
void PrintBar(int length);
void PrintRow(const std::string &label, int value);

// 获取枚举类型
std::string GetEnumSendType(SEND_TYPE type);
std::string GetEnumRecvType(RECV_TYPE type);

template<typename... Args>
std::string make_string(Args&&... args) {
    std::ostringstream oss;
    // 使用 fold expression 来展开参数
    (oss << ... << args);
    return oss.str();
}

// 输出工具
#define ARGUS_PRINT(var)                                                       \
    do {                                                                       \
        std::cout << "@" << __PRETTY_FUNCTION__ << " > " << #var << ": "       \
                  << (var) << std::endl;                                       \
    } while (0)

#define ARGUS_EXIT(...)                                                        \
    do {                                                                       \
        std::cout << "[ERROR]: " << make_string(__VA_ARGS__) << std::endl;     \
        sc_stop();                                                             \
    } while (0)
