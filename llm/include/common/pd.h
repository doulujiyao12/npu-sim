#pragma once
#include "defs/enums.h"
#include "macros/macros.h"

#include <vector>
using namespace std;

class RequestRecord {
public:
    // 不作修改
    int id;
    int seq_len;
    int prefill_iters;

    // 需要修改
    PD_PHASE phase;
    int prefill_counter; // prefill已经执行几次iter
    int decode_counter;  // decode已经执行几次iter

    RequestRecord(int id, int seq_len, int heads) : id(id), seq_len(seq_len) {
        phase = UNTOUCHED;
        decode_counter = 0;
        prefill_iters = seq_len * heads / MAX_PREFILL_WORKLOAD;
        prefill_counter = 0;
    }
};

class Stage {
public:
    int req_id;
    PD_PHASE type;

    Stage() {}
    Stage(int id, PD_PHASE type) : req_id(id), type(type) {}
};

class CoreStatus {
public:
    // 不作修改
    int id;

    // 正在执行的任务
    vector<Stage> batchInfo;
    bool available;
    bool data_sent;

    CoreStatus(int id) : id(id) {
        available = true;
        data_sent = false;
    }
};