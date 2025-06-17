#include "common/memory.h"
#include "utils/memory_utils.h"
#include "utils/system_utils.h"

#include <iostream>

using namespace std;

int AddrLabelTable::addRecord(const std::string &key) {
    for (int i = 0; i < table.size(); i++) {
        if (table[i] == key) {
            // cout << "LabelTable: Find existing label: " << key << " at " << i
            //      << endl;
            return i;
        }
    }

    table.push_back(key);
    cout << "[CONFIG] LabelTable: Add new label: " << key << " at "
         << table.size() - 1 << endl;

    return table.size() - 1;
}

string AddrLabelTable::findRecord(int index) const {
    if (index >= 0 && index < table.size()) {
        return table[index];
    }
    return UNSET_LABEL;
}

void AddrLabelTable::clearAll() { table.clear(); }

void SramPosLocator::addPair(std::string &key, AddrPosKey value, bool update_key) {
    visit += 1;
    value.record = visit;
    if (update_key) {
        data_map[key] = value;
    } else {
        AddrPosKey old_key;

        findPair(key, old_key);

        value.dram_addr = old_key.dram_addr;

        data_map[key] = value;
    }
    
    // cout << "[SRAM pos locator] id " << cid << " add pair.\n";
    // cout << "[Add pair]: label -> " << key << endl;
}


bool SramPosLocator::validateTotalSize() const {
    int dataSizeSum = 0;
    for (const auto& pair : data_map) {
        dataSizeSum += pair.second.size - pair.second.spill_size;
    }

    int allocationSizeSum = 0;
    for (const auto& alloc : sram_manager_->allocations_) {
        AllocationID id = alloc.first;
        allocationSizeSum += sram_manager_->get_allocation_byte_capacity(id);
    }

    if (dataSizeSum != allocationSizeSum) {
        std::cerr << "[ERROR] Data map size total (" << dataSizeSum
                  << ") does not match SRAM manager allocation total ("
                  << allocationSizeSum << ")." << std::endl;
        return false;
    }

    std::cout << "[INFO] Total size validation passed: " << dataSizeSum << " bytes." << std::endl;
    return true;
}
void SramPosLocator::addPair(std::string &key, AddrPosKey value,
                             TaskCoreContext &context, u_int64_t &dram_time, bool update_key) {
    // 先放入sram
    visit += 1;
    value.record = visit;
    if (update_key) {
        data_map[key] = value;
    } else {
        AddrPosKey old_key;

        findPair(key, old_key);

        value.dram_addr = old_key.dram_addr;

        data_map[key] = value;
    }


    // cout << "[SRAM pos locator] id " << cid << " add pair.\n";
    // cout << "[Add pair]: label -> " << key << ", size: " << value.size << endl;

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

    // cout << "[SRAM CHECK] used: " << used << ", max: " << max_sram_size << endl;

    // 放得下
    if (used <= max_sram_size) {
        return;
    }

    cout << "[SRAM CHECK] Sram fail to allocate enough space! Need to spill & "
            "rearrange.\n";

    // 放不下，需要spill，查找里面record最小的成员（除了key）
    while (used > max_sram_size) {
        std::cout << "\033[1;31m" << ": Sram check: used: " << used
                  << ", max sram size: " << max_sram_size << "\033[0m" << endl;
        int min_record = 1e9 + 3;
        string min_label = "";
        int min_pos = 0;
        AllocationID sram_id  = 0;
        for (auto pair : data_map) {
            if (pair.first == key)
                continue; // 不能spill自己
            if (!pair.second.valid &&
                pair.second.spill_size == pair.second.size)
                continue; // 已经全部spill到dram中去了
            // if (pair.first ==
            //         ETERNAL_PREFIX + string(KVCACHE_PREFIX) + string("k") ||
            //     pair.first ==
            //         ETERNAL_PREFIX + string(KVCACHE_PREFIX) + string("v"))
            //     continue; // 简单策略：不spill kvcache

            if (pair.second.record < min_record) {
                min_record = pair.second.record;
                min_label = pair.first;
                min_pos = pair.second.pos;
                sram_id = pair.second.alloc_id;
            }
        }

        // cout << "[SRAM SPILL] Core " << cid << ": Sram chose to spill label "
        //      << min_label << ", size " << data_map[min_label].size
        //      << ", spill_size: " << data_map[min_label].spill_size << endl;

        if (min_record == 1e9 + 3) {
            cout << "[ERROR] SRAM have no more data to spill " << max_sram_size
                 << "<" << used << endl;
            sc_stop();
        }

        // 如果已经spill一部分了，则选择剩余能spill的大小
        int upper_spill_limit;
        if (data_map[min_label].valid) {
            upper_spill_limit = data_map[min_label].size;
        } else {
            upper_spill_limit =
                data_map[min_label].size - data_map[min_label].spill_size;
        }

        data_map[min_label].valid = false;

        int delta_space = used - max_sram_size;
        // 表示已经被放到dram中的数据大小
        // int spill_size =
        //     min(double(delta_space) * 1, (double)upper_spill_limit);
        int spill_size = upper_spill_limit;
        used -= spill_size;
        data_map[min_label].spill_size += spill_size;
#if USE_SRAM_MANAGER
        cout << "add pair " << key << endl;
        sram_manager_->deallocate(sram_id);
        cout <<  " Deallocate " << sram_id << " from sram manager." << key << endl;
#endif      
        // spill 耗时
        // spill in nb_dcache utils
        sram_spill_back_generic(context, spill_size, data_map[min_label].dram_addr, dram_time);

        // cout << "[SRAM SPILL] Core " << cid << ": After spill: used: " << used
        //      << ", max sram size: " << max_sram_size << endl;
        // cout << "[SRAM SPILL] Core " << cid
        //      << ": label size: " << data_map[min_label].size
        //      << ", spill_size: " << data_map[min_label].spill_size << endl;
    }

    // 重排
    // 每次addPair后都需要重排sram_addr地址，保证最前面的一块是连续使用的，sram指向最前面空闲的
#if USE_SRAM_MANAGER
#else
    *(context.sram_addr) = rearrangeAll(context);
#endif
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

void SramPosLocator::printAllKeys() {
    for (const auto& pair : data_map) {
        std::cout << "Key: " << pair.first << std::endl;
    }
}
void SramPosLocator::printAllKeysWithAllocId() {
    std::cout << "[SRAM Pos Locator] All keys and their Allocation IDs:\n";
    for (const auto& pair : data_map) {
        std::cout << "Key: " << pair.first 
                  << ", Alloc ID: " << pair.second.alloc_id << std::endl;
    }
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


void SramPosLocator::updateKVPair(TaskCoreContext &context, std::string &key, uint64_t kv_daddr, int data_size_in_byte) {
#if USE_SRAM_MANAGER == 1
    visit += 1;

    AddrPosKey result;
    int spill_size = findPair(key, result);
    u_int64_t dram_time_tmp;

    if (spill_size == -1) {
        // 还未建立 KV sram block
        sram_write_append_generic(context, data_size_in_byte, dram_time_tmp,
        key, true, this, kv_daddr);

        return;

        
    } else if (spill_size > 0) {
        sram_first_write_generic(context, spill_size, kv_daddr, dram_time_tmp,
            nullptr, key, true, this);
        // KV sram block 之前被建立，但是被放回dram


    } else {

    }

    spill_size = findPair(key, result);

    assert(spill_size >= 0);

    if (result.left_byte > data_size_in_byte){
        result.spill_size = 0;
        result.left_byte -= data_size_in_byte;
        return;
    }else{
        int alignment = std::max(get_sram_bitwidth(cid), SRAM_BLOCK_SIZE * 8);
        int alignment_byte = alignment / 8;

        result.size += alignment_byte;
        result.left_byte = result.left_byte - data_size_in_byte + alignment_byte;
         
        addPair(key, result, context, dram_time_tmp, false);

        auto sram_manager_ = context.sram_manager_;
        sram_manager_->allocate_append(alignment_byte, result.alloc_id);
        return;
    }




#else
    assert(0);
#endif


}
// 为sram中标签为key的数据块增加size的大小。如果该数据块还不存在，则创建一个。
void SramPosLocator::updatePair(std::string &key, int size,
                                TaskCoreContext &context,
                                u_int64_t &dram_time) {
    visit += 1;

    AddrPosKey result;
    int spill_size = findPair(key, result);

    if (spill_size == -1) {
        result.pos = *context.sram_addr;
        result.size = size;
    } else if (spill_size > 0) {
        // 需要先把所有内容取回
        sram_first_write_generic(context, spill_size, result.pos, dram_time,
                                 nullptr);
        result.spill_size = 0;
        result.size += size;

    } else {
        result.size += size;
    }

    addPair(key, result, context, dram_time);
    // cout << "Core " << cid << " update label " << key
    //      << ", new size: " << data_map[key].size << endl;
}

void SramPosLocator::deletePair(std::string &key) { 
    // data_map.erase(key); 
    cout << "delete label " << key << endl;
    auto it = data_map.find(key);
    if (it != data_map.end()) {
#if USE_SRAM_MANAGER
        sram_manager_->deallocate(it->second.alloc_id); // 释放 SRAM
#endif
        data_map.erase(it);
    }
}

void SramPosLocator::clearAll() { data_map.clear(); }

int SramPosLocator::rearrangeAll(TaskCoreContext &context) {
    vector<pair<string, AddrPosKey>> temp_list;
    for (auto record : data_map)
        temp_list.push_back(record);

    clearAll();
    int pos = 0;
    for (auto record : temp_list) {
        auto size = record.second.size;
        auto spill_size = record.second.spill_size;

        int dma_read_count = spill_size * 8 / (int)(get_sram_bitwidth(cid) * SRAM_BANKS);
        int byte_residue =
        spill_size * 8 - dma_read_count * (get_sram_bitwidth(cid) * SRAM_BANKS);
        int single_read_count = ceiling_division(byte_residue, get_sram_bitwidth(cid));

        int temp_pos = *(context.sram_addr);
        u_int64_t temp_addr = 0;
        addPair(record.first, record.second, context, temp_addr);

        if (temp_pos != *(context.sram_addr)) {
            cout << "[ERROR] Loop rearrange in spill DRAM." << endl;
            sc_stop();
        }

        // cout << "\tAdd label <" << record.first << "> at offset " << pos
        //      << endl;

        pos += dma_read_count * SRAM_BANKS + single_read_count;
    }

    return pos;
}

// 以下为GpuPosLocator相关
void GpuPosLocator::addPair(const std::string &key, AddrPosKey &value) {
    value.pos = addr_top;
    data_map[key] = value;
    addr_top += value.size;

    cout << key << " " << value.size << endl;

    // 对齐
    addr_top = ceiling_division(addr_top, 64) * 64;

    cout << "[GPU]: add pair: " << key << endl;
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
    cout << "[GpuPosLocator] try to find key: " << key << endl;
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

void GpuPosLocator::deletePair(std::string &key) { data_map.erase(key); }

void GpuPosLocator::clearAll() { data_map.clear(); }