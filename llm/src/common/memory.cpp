#include "common/memory.h"
#include "utils/memory_utils.h"
#include "utils/system_utils.h"

#include <iostream>

using namespace std;

int AddrLabelTable::addRecord(const std::string &key) {
    for (int i = 0; i < table.size(); i++) {
        if (table[i] == key) {
            cout << "LabelTable: Find existing label: " << key << " at " << i << endl;
            return i;
        }
    }

    table.push_back(key);
    cout << "LabelTable: Add new label: " << key << " at " << table.size() - 1 << endl;

    return table.size() - 1;
}

string AddrLabelTable::findRecord(int index) const {
    if (index >= 0 && index < table.size()) {
        return table[index];
    }
    return UNSET_LABEL;
}

void AddrLabelTable::clearAll() { table.clear(); }

void SramPosLocator::addPair(const std::string &key, AddrPosKey value, TaskCoreContext &context, u_int64_t &dram_time) {
    // 先放入sram
    visit += 1;
    value.record = visit;
    data_map[key] = value;
    cout << "[SRAM pos locator] id " << cid << " add pair.\n";
    cout << "[Add pair]: label -> " << key << endl;

    // 检查所有的大小是否超过能够容纳的上限
    int used = 0;
    for (auto pair : data_map) {
        // cout << "[Traverse SRAM]: " << pair.first << endl;
        // valid = true 表示还没有被spill过
        if (pair.second.valid)
            used += pair.second.size;
        else
            used += pair.second.size - pair.second.spill_size;
    }

    cout << "[SRAM CHECK] used: " << used << ", max: " << max_sram_size << endl;

    // 放得下
    if (used <= max_sram_size)
        return;

    cout << "[SRAM CHECK] Sram fail to allocate enough space! Need to spill & "
            "rearrange.\n";

    // 放不下，需要spill，查找里面record最小的成员（除了key）
    while (used > max_sram_size) {
        int min_record = 1e9 + 3;
        string min_label = "";
        int min_pos = 0;
        for (auto pair : data_map) {
            if (pair.first == key)
                continue; // 不能spill自己
            if (!pair.second.valid && pair.second.spill_size == pair.second.size)
                continue; // 已经全部spill到dram中去了
            if (pair.first == ETERNAL_PREFIX + string(KVCACHE_PREFIX) + string("k") || pair.first == ETERNAL_PREFIX + string(KVCACHE_PREFIX) + string("v"))
                continue; // 简单策略：不spill kvcache

            if (pair.second.record < min_record) {
                min_record = pair.second.record;
                min_label = pair.first;
                min_pos = pair.second.pos;
            }
        }

        cout << "[SRAM SPILL] Sram chose to spill label " << min_label << ".\n";

        if (min_record == 1e9 + 3) {
            cout << "[ERROR] SRAM have no more data to spill " << max_sram_size << "<" << used << endl;
            sc_stop();
        }

        // 如果已经spill一部分了，则选择剩余能spill的大小
        int upper_spill_limit;
        if (data_map[min_label].valid) {
            upper_spill_limit = data_map[min_label].size;
        } else {
            upper_spill_limit = data_map[min_label].size - data_map[min_label].spill_size;
        }

        data_map[min_label].valid = false;

        int delta_space = used - max_sram_size;
        // 表示已经被放到dram中的数据大小
        int spill_size = min(int(delta_space), upper_spill_limit);
        used -= spill_size;
        data_map[min_label].spill_size += spill_size;

        // spill 耗时
        // spill in nb_dcache utils
        sram_spill_back_generic(context, spill_size, 0, dram_time);

        cout << "[SRAM SPILL] After spill: used: " << used << ", max sram size: " << max_sram_size << endl;
    }

    // 重排 每次addPair后都需要重排sram_addr地址，保证最前面的一块是连续使用的，sram指向最前面空闲的
    *(context.sram_addr) = rearrangeAll(context);
}

int SramPosLocator::findPair(std::string &key, int &result) {
    visit += 1;
    auto it = data_map.find(key);
    if (it != data_map.end()) {
        it->second.record = visit;
        result = it->second.pos;
        return it->second.spill_size;
    }
    return -1;
}

int SramPosLocator::findPair(std::string &key, AddrPosKey &result) {
    visit += 1;
    auto it = data_map.find(key);
    if (it != data_map.end()) {
        it->second.record = visit;
        result = it->second;
        return it->second.spill_size;
    }
    return -1;
}

void SramPosLocator::deletePair(std::string &key) { data_map.erase(key); }

void SramPosLocator::clearAll() { data_map.clear(); }

int SramPosLocator::rearrangeAll(TaskCoreContext &context) {
    vector<pair<string, AddrPosKey>> temp_list;
    for (auto record : data_map)
        temp_list.push_back(record);

    clearAll();
    int pos = 0;
    for (auto record : temp_list) {
        auto size = record.second.size;
        if (!record.second.valid)
            size = 0;

        int dma_read_count = size * 8 / (int)(SRAM_BITWIDTH * SRAM_BANKS);
        int byte_residue = size * 8 - dma_read_count * (SRAM_BITWIDTH * SRAM_BANKS);
        int single_read_count = ceiling_division(byte_residue, SRAM_BITWIDTH);

        AddrPosKey temp_key = AddrPosKey(pos, size);
        int temp_pos = *(context.sram_addr);
        u_int64_t temp_addr = 0;
        addPair(record.first, temp_key, context, temp_addr);

        if (temp_pos != *(context.sram_addr)) {
            cout << "[ERROR] Loop rearrange in spill DRAM." << endl;
            sc_stop();
        }

        cout << "\tAdd label <" << record.first << "> at offset " << pos << endl;

        pos += dma_read_count * SRAM_BANKS + single_read_count;
    }

    return pos;
}

// 以下为GpuPosLocator相关
void GpuPosLocator::addPair(const std::string &key, AddrPosKey &value) {
    value.pos = addr_top;
    data_map[key] = value;
    addr_top += value.size;
}

void GpuPosLocator::fetchPair(std::string &key, AddrPosKey &result) {
    auto it = data_map.find(key);
    if (it != data_map.end()) {
        result = it->second;
        return;
    }

    addPair(key, result);
}

bool GpuPosLocator::findPair(std::string &key, int &result) {
    auto it = data_map.find(key);
    if (it != data_map.end()) {
        result = it->second.pos;
        return true;
    }

    return false;
}

bool GpuPosLocator::findPair(std::string &key, AddrPosKey &result) {
    auto it = data_map.find(key);
    if (it != data_map.end()) {
        result = it->second;
        return true;
    }

    return false;
}

void GpuPosLocator::deletePair(std::string &key) {
    data_map.erase(key);
}

void GpuPosLocator::clearAll() {
    data_map.clear();
}