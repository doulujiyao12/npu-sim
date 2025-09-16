#include "utils/print_utils.h"
#include "defs/enums.h"

#include <iomanip>
#include <iostream>
#include <sstream>
#include <unordered_map>

std::string ToHexString(int value) {
    std::ostringstream stream;
    stream << std::setw(3) << std::setfill('0') << value;
    return stream.str();
}

void PrintBar(int length) {
    for (int i = 0; i < length; ++i)
        std::cout << "=";

    std::cout << "\n";
}

void PrintRow(const std::string &label, int value) {
    std::cout << "| " << std::left << std::setw(20) << label << "| "
              << std::right << std::setw(15) << value << " |\n";
}

std::string GetEnumSendType(SEND_TYPE type) {
    const std::unordered_map<SEND_TYPE, std::string> SEND_TYPE_NAMES = {
        {SEND_ACK, "SEND_ACK"},   {SEND_REQ, "SEND_REQ"},
        {SEND_DATA, "SEND_DATA"}, {SEND_SRAM, "SEND_SRAM"},
        {SEND_DONE, "SEND_DONE"},
    };

    auto it = SEND_TYPE_NAMES.find(type);
    if (it != SEND_TYPE_NAMES.end()) {
        return it->second;
    }

    return "Unknown SEND_TYPE";
}

std::string GetEnumRecvType(RECV_TYPE type) {
    const std::unordered_map<RECV_TYPE, std::string> RECV_TYPE_NAMES = {
        {RECV_TYPE::RECV_CONF, "RECV_CONF"},
        {RECV_TYPE::RECV_ACK, "RECV_ACK"},
        {RECV_TYPE::RECV_FLAG, "RECV_FLAG"},
        {RECV_TYPE::RECV_DATA, "RECV_DATA"},
        {RECV_TYPE::RECV_SRAM, "RECV_SRAM"},
        {RECV_TYPE::RECV_WEIGHT, "RECV_WEIGHT"},
        {RECV_TYPE::RECV_START, "RECV_START"}};

    auto it = RECV_TYPE_NAMES.find(type);
    if (it != RECV_TYPE_NAMES.end()) {
        return it->second;
    }

    return "Unknown RECV_TYPE";
}