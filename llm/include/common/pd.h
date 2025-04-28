#pragma once
#include "defs/enums.h"
#include "macros/macros.h"

class RequestRecord {
public:
    // 不作修改
    int id;
    int seq_len;
    int heads;
    int prefill_iters;

    // 需要修改
    bool lock;
    PD_PHASE phase;
    int prefill_counter; // prefill已经执行几次iter
    int decode_counter; // decode已经执行几次iter

    RequestRecord(int id, int seq_len, int heads)
        : id(id), seq_len(seq_len), heads(heads) {
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
    vector<int> reqs;
    bool available;
    bool data_sent;

    CoreStatus(int id) : id(id) {
        available = true;
        data_sent = false;
    }
};