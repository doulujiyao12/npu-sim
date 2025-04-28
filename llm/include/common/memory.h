#pragma once
#include <string>
#include <unordered_map>
#include <vector>

#include "common/system.h"
#include "macros/macros.h"

using namespace std;

class KVCache {
public:
    // 可能已经obsolete
    int Kstart;
    int Vstart;

    float kvcache[KVCACHE_MAX_SIZE];

    KVCache() {
        Kstart = 0;
        Vstart = KVCACHE_MAX_SIZE / 2;
    }
};

// 以下为sram_pos_locator相关
class AddrDatapassLabel {
public:
    string indata[MAX_SPLIT_NUM];
    string outdata;
    AddrDatapassLabel() {
        for (int i = 0; i < MAX_SPLIT_NUM; i++) {
            indata[i] =
                UNSET_LABEL; // 读入sram的输入数据标签，绝大多数情况下只使用第一个元素
        }

        outdata = UNSET_LABEL; // 写回sram的输出数据标签
    }

    AddrDatapassLabel(string indata_v, string outdata_v) : outdata(outdata_v) {
        indata[0] = indata_v;
    }
};


class AddrLabelTable {
public:
    vector<string> table;

    int addRecord(const std::string &key);
    string findRecord(int index) const;

    void clearAll();
};

class AddrPosKey {
public:
    int pos;
    int size;
    bool valid;     // 是否已经被evict到DRAM上了
    int spill_size; // 已经spill到DRAM上的大小
    int record;     // 根据LRU策略记录的访问时间，越大表示访问时间越靠近现在
    AddrPosKey() {
        valid = true;
        record = 0;
        spill_size = 0;
        pos = 0;
        size = 0;
    }

    AddrPosKey(int pos, int size) : pos(pos), size(size) {
        // valid = true; 表示没有被spill的过
        valid = true;
        // 表示被重复使用的次数
        record = 0;
        // 表示被spill到dram中的数据的大小
        spill_size = 0;
    }
};

class SramPosLocator { // one per core
public:
    std::unordered_map<std::string, AddrPosKey> data_map;
    int max_sram_size;
    int visit;
    int cid; // 属于哪一个核

    SramPosLocator(int id) {
        cid = id;
        visit = 1;
        max_sram_size = MAX_SRAM_SIZE;
    }

    void addPair(const std::string &key, AddrPosKey value,
                 TaskCoreContext &context, u_int64_t &dram_time);
    // void addPair(const std::string &key, AddrPosKey value);

    int findPair(std::string &key, int &result);
    int findPair(std::string &key, AddrPosKey &result);

    void deletePair(std::string &key);
    void clearAll();

    int rearrangeAll(TaskCoreContext &context);
};

// 以下为gpu_pos_locator相关
class GpuPosLocator { // 一个系统维护一个
public:
    std::unordered_map<std::string, AddrPosKey> data_map;
    int addr_top;

    GpuPosLocator() { addr_top = 1024; }

    void addPair(const std::string &key, AddrPosKey &value);

    void fetchPair(std::string &key,
                   AddrPosKey &result); // 如果没找到，直接添加一个
    bool findPair(std::string &key, int &result);
    bool findPair(std::string &key, AddrPosKey &result);

    void deletePair(std::string &key);
    void clearAll();
};