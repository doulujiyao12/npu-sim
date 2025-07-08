#include "monitor/config_helper_gpu_pds.h"
#include "prims/gpu_prims.h"
#include "common/pd.h"

config_helper_gpu_pds::config_helper_gpu_pds(string filename, string font_ttf,
                                             sc_event *ev_sig,
                                             int config_chip_id) {
    cout << "Loading config file: " << filename << endl;
    json j;
    ifstream jfile(filename);
    jfile >> j;

    decode_done = 0;

    auto config_reqs = j["requests"];
    int req_cnt = config_reqs["count"];

    heads = config_reqs["heads"];
    head_size = config_reqs["head_size"];
    kv_heads = config_reqs["kv_heads"];
    eof_chance = config_reqs["eof_chance"];
    prefill_core = config_reqs["prefill_cores"];
    decode_core = config_reqs["decode_cores"];
    batch_size = config_reqs["batch_size"];

    // 决定哪些workercore是prefill，哪些是decode
    for (int i = 0; i < prefill_core + decode_core; i++) {
        if (i < prefill_core) 
            coreStatus.push_back(CoreStatus(i, JOB_PREFILL));
        else 
            coreStatus.push_back(CoreStatus(i, JOB_DECODE));
    }

    int arr_size = config_reqs["arrival"].size();
    if (arr_size < req_cnt) {
        for (int i = 0; i < arr_size; i++)
            arrival_time.push_back(config_reqs["arrival"][i]);

        for (int i = arr_size; i < req_cnt; i++)
            arrival_time.push_back(config_reqs["arrival"][arr_size - 1]);
    } else {
        for (int i = 0; i < req_cnt; i++)
            arrival_time.push_back(config_reqs["arrival"][i]);
    }

    // 检查batch_size参数的合理性，同时依此修改arrive时间
    if (batch_size * PD_RATIO > CORE_CREDIT) {
        cout << "[ERROR] In config helper pd: batch size too large.\n";
        sc_stop();
    } else {
        for (int i = 0; i < req_cnt; i++) {
            int target = min((i / batch_size + 1) * batch_size - 1, req_cnt);
            arrival_time[i] = arrival_time[target];
        }
    }

    for (int i = 0; i < req_cnt; i++) {
        RequestRecord record =
            RequestRecord(i, config_reqs["seq_len"], heads, arrival_time[i]);
        requestRecords.push_back(record);
    }
}