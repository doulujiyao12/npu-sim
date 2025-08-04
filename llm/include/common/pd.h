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
    int arrival_time;

    // 需要修改
    PD_PHASE phase;
    int prefill_distribute; // prefill已经派发几次iter
    int prefill_counter; // prefill已经执行几次iter
    int decode_counter;  // decode已经执行几次iter

    RequestRecord(int id, int seq_len, int heads, int arrival_time) : id(id), seq_len(seq_len), arrival_time(arrival_time) {
        phase = UNTOUCHED;
        decode_counter = 0;
        prefill_iters = seq_len * heads / MAX_PREFILL_WORKLOAD;
        prefill_counter = 0;
        prefill_distribute = 0;
    }
};

class Stage {
public:
    int req_id;
    PD_PHASE type;
    int token_num;
    int total_iter; // 用于prefill

    Stage() {}
    Stage(int id, PD_PHASE type, int token) : req_id(id), type(type), token_num(token) {}
};

class CoreStatus {
public:
    // 不作修改
    int id;
    PD_JOB job_type;

    // 正在执行的任务
    vector<Stage> batchInfo;
    bool available;
    bool data_sent;

    CoreStatus() {}
    CoreStatus(int id, PD_JOB type) : id(id), job_type(type) {
        available = true;
        data_sent = false;
    }
};