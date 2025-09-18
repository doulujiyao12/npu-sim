#pragma once
#include "systemc.h"

#include "common/memory.h"
#include "common/pd.h"
#include "prims/base.h"

class Clear_sram : public PrimBase {
public:
    int taskCoreDefault(TaskCoreContext &context);

    sc_bv<128> serialize();
    void deserialize(sc_bv<128> buffer);
    void printSelf();

    Clear_sram() { name = "Clear_sram"; }
};


class Load_prim : public PrimBase {
public:
    int taskCoreDefault(TaskCoreContext &context);

    sc_bv<128> serialize();
    void deserialize(sc_bv<128> buffer);
    void printSelf();

    Load_prim() { name = "Load_prim"; }
};


class Recv_prim : public PrimBase {
public:
    RECV_TYPE type;
    int tag_id;   // 和send原语对应的tag
    int recv_cnt; // 需要接收到的end包数量（用于多发一）

    int taskCoreDefault(TaskCoreContext &context);

    sc_bv<128> serialize();
    void deserialize(sc_bv<128> buffer);
    void printSelf();

    Recv_prim() { name = "Recv_prim"; }
    Recv_prim(RECV_TYPE type);
    Recv_prim(RECV_TYPE type, int tag, int cnt)
        : type(type), tag_id(tag), recv_cnt(cnt) {
        name = "Recv_prim";
    }
};


class Send_prim : public PrimBase {
public:
    SEND_TYPE type;
    int des_id; // 目标id
    // int des_offset;   // 目标的地址偏移
    // int local_offset; // 本地的地址偏移
    string output_label = UNSET_LABEL; // 需要从哪一个数据块标签获取结果，并发送
    int max_packet;                    // 需要发送的包裹数量
    int tag_id;                        // send_tag，用于与recv原语对应
    int end_length;                    // 尾包长度，避免覆盖

    int data_packet_id; // 已经发送的包裹数量

    SramPosLocator *sram_pos_locator;

    int task();
    int taskCoreDefault(TaskCoreContext &context);

    sc_bv<128> serialize();
    void deserialize(sc_bv<128> buffer);

    void parseJson(json j, vector<pair<string, int>> vtable);
    void print_self(string prefix);
    int sram_utilization(DATATYPE datatype, int cid = 0);
    void initialize() {};

    Send_prim() { name = "Send_prim"; }
    Send_prim(SramPosLocator *sram_pos_locator)
        : sram_pos_locator(sram_pos_locator) {
        name = "Send_prim";
    }
    Send_prim(SEND_TYPE type) : type(type) { name = "Send_prim"; }
    Send_prim(SEND_TYPE type, int des, int tag)
        : type(type), des_id(des), tag_id(tag) {
        name = "Send_prim";
    } // 用于SEND_ACK
    Send_prim(SEND_TYPE type, int des, int max_packet, int tag)
        : des_id(des), type(type), max_packet(max_packet), tag_id(tag) {
        name = "Send_prim";
    }
};


class Set_addr : public PrimBase {
public:
    AddrDatapassLabel *target;
    AddrDatapassLabel *datapass_label;

    int task();
    int taskCoreDefault(TaskCoreContext &context);

    sc_bv<128> serialize();
    void deserialize(sc_bv<128> buffer);

    void parseJson(json j);
    void print_self(string prefix);
    int sram_utilization(DATATYPE datatype, int cid = 0);
    void initialize() {};

    Set_addr() {
        datapass_label = new AddrDatapassLabel();
        name = "Set_addr";
    }

    Set_addr(AddrDatapassLabel *target) {
        datapass_label = new AddrDatapassLabel();
        this->target = target;
        name = "Set_addr";
    }
};

class Set_batch : public PrimBase {
public:
    vector<Stage> *target;
    vector<Stage> batchInfo;
    bool auto_pd;
    int *stage_cnt;

    int task();
    int taskCoreDefault(TaskCoreContext &context);

    sc_bv<128> serialize();
    void deserialize(sc_bv<128> buffer);

    void parseJson(json j);
    void print_self(string prefix);
    int sram_utilization(DATATYPE datatype, int cid = 0);
    void initialize() {};

    Set_batch() { auto_pd = false; }

    Set_batch(vector<Stage> batchInfo) { this->batchInfo = batchInfo; }
    Set_batch(vector<Stage> batchInfo, bool auto_pd) {
        this->batchInfo = batchInfo;
        this->auto_pd = auto_pd;
    }

    Set_batch(vector<Stage> *target) { this->target = target; }

    Set_batch(vector<Stage> *target, int *stage_cnt) {
        this->target = target;
        this->stage_cnt = stage_cnt;
    }
};

class Store_prim : public PrimBase {
public:
    int dram_addr;
    int sram_addr;
    int size;

    int task();
    int taskCoreDefault(TaskCoreContext &context);

    void deserialize(sc_bv<128> buffer);
    sc_bv<128> serialize();

    void parseJson(json j, vector<pair<string, int>> vtable);
    void print_self(string prefix);
    int sram_utilization(DATATYPE datatype, int cid = 0);
    void initialize() {};
    Store_prim() { name = "Store_prim"; }
    Store_prim(int da, int sa, int s) : dram_addr(da), sram_addr(sa), size(s) {
        name = "Store_prim";
    }
};