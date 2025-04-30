#pragma once
#include "defs/enums.h"
#include "macros/macros.h"

class RequestRecord {
public:
    // 不作修改
    int id;
    int seq_len;
    int prefill_iters;

    // 需要修改
    bool lock;
    PD_PHASE phase;
    int prefill_counter; // prefill已经执行几次iter
    int decode_counter; // decode已经执行几次iter

    RequestRecord(int id, int seq_len, int heads)
        : id(id), seq_len(seq_len) {
        lock = false;
        phase = UNTOUCHED;
        prefill_counter = 0;
        decode_counter = 0;
        prefill_iters = seq_len * heads / MAX_PREFILL_WORKLOAD;
    }
};

class CoreStatus {
public:
    // 不作修改
    int id;

    // 正在执行的任务
    std::vector<int> reqs;
    bool available;
    bool data_sent;

    CoreStatus(int id) : id(id) {
        available = true;
        data_sent = false;
    }
};

class BatchInfo {
public:
    int batch_size;
    int req_ids[CORE_CREDIT];
    PD_PHASE types[CORE_CREDIT];
    int iter_count[CORE_CREDIT];

    BatchInfo() {}

    BatchInfo(std::vector<RequestRecord> records) {
        batch_size = records.size();
        for (int i = 0; i < batch_size; i++) {
            req_ids[i] = records[i].id;
            types[i] = records[i].phase;
            iter_count[i] = types[i] ? records[i].decode_counter : records[i].prefill_counter;
        }
    }
};