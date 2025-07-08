#include "monitor/config_helper_gpu_pds.h"
#include "prims/gpu_prims.h"

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

    // 建立原语模板
    json_template_p = j["chips"][0]["prefill_template"];
    json_template_d = j["chips"][0]["decode_template"];
    busy_d = busy_p = false;
    g_recv_ack_cnt_d = g_recv_ack_cnt_p = g_recv_done_cnt_d =
        g_recv_done_cnt_p = 0;
    wait_schedule_p = true;
    wait_schedule_d = false;
    wait_send_start = false;

    ev_sig->notify(0, SC_NS);
}

void config_helper_gpu_pds::fill_queue_config(queue<Msg> *q) {
    // 将temp中的所有内容搬运到q中，并清空temp
    for (auto msg : temp_config) {
        auto des = msg.des;
        int index = des / GRID_X;
        q[index].push(msg);
    }

    temp_config.clear();
}

void config_helper_gpu_pds::fill_queue_start(queue<Msg> *q) {
    // 所有核都需要发送start data
    // 在调用这个函数的时候，已经完成了对core的config的发放
    cout << "Prepare to send start data!\n";
    if (!wait_send_start)
        return;

    for (auto status : coreStatus) {
        cout << "status " << status.id << endl;

        int index = status.id / GRID_X;
        int total_pkg = 0;

        for (int i = 0; i < status.batchInfo.size(); i++) {
            auto stage = status.batchInfo[i];
            auto record = requestRecords[stage.req_id];

            if (stage.type == JOB_DECODE)
                continue;

            int size =
                record.seq_len / record.prefill_iters * heads * head_size;
            int send_size_in_bit = size * sizeof(float) * 8;
            int pkg_num = (send_size_in_bit % M_D_DATA)
                              ? (send_size_in_bit / M_D_DATA + 1)
                              : (send_size_in_bit / M_D_DATA);

            for (int j = 1; j <= pkg_num; j++) {
                sc_bv<M_D_DATA> d(0x1);

                Msg m = Msg(false, MSG_TYPE::S_DATA, j + total_pkg, status.id,
                            M_D_DATA * (j - 1), status.id, M_D_DATA, d);
                m.source = GRID_SIZE;
                q[index].push(m);
            }

            total_pkg += pkg_num;
        }

        cout << "total_pkg " << total_pkg << endl;
        sc_bv<M_D_DATA> d(0x1);
        q[index].push(Msg(true, MSG_TYPE::S_DATA, total_pkg + 1, status.id, 0,
                          status.id, 1, d));
    }

    wait_send_start = false;
}

void config_helper_gpu_pds::iter_done(PD_JOB type) {
    // 按照coreStatus更新requestRecords
    // 所有core都需要更新
    vector<Msg> done_msg;
    if (type == JOB_PREFILL)
        done_msg = g_done_msg_p;
    else if (type == JOB_DECODE)
        done_msg = g_done_msg_d;

    for (auto msg : done_msg) {
        int id = msg.source;
        auto &status = coreStatus[id];
        int stage_count = 0;
        for (auto &stage : status.batchInfo) {
            auto &record = requestRecords[stage.req_id];
            switch (record.phase) {
            case PREFILL:
                if (++record.prefill_counter == record.prefill_iters) {
                    stage.type = record.phase = DECODE;
                    stage.token_num = 1;
                    // req_decode.push(stage.req_id);
                    if (!busy_d)
                        wait_schedule_d = true;
                }
                break;
            case DECODE:
                record.decode_counter++;
                if (msg.data.range(stage_count, stage_count).to_uint64() ||
                    record.decode_counter >= (1.5) / (eof_chance)) {
                    stage.type = record.phase = PD_DONE;

                    if (++decode_done == requestRecords.size()) {
                        cout << "All reqs done.\n";
                        cout << "[CATCH TEST] " << sc_time_stamp() << endl;
                        sc_stop();
                    }
                }
                break;
            }

            stage_count++;
        }
    }

    if (type == JOB_PREFILL)
        busy_p = false;
    else if (type == JOB_DECODE)
        busy_d = false;
}

void config_helper_gpu_pds::iter_start(PD_JOB type) {
    if (type == JOB_PREFILL && busy_p || type == JOB_DECODE && busy_d)
        return;

    cout << "Iter Start, type " << type << endl;

    // 为每一个核进行schedule
    // 每一个核优先做上一个iteration的工作，如果已经完成，则寻找新的任务

}

void config_helper_gpu_pds::print_self() {}

void config_helper_gpu_pds::generate_prims(int i, vector<Msg> &temp_buffer) {

}

void config_helper_gpu_pds::parse_ack_msg(Event_engine *event_engine, int flow_id, sc_event *notify_event) {

} 

void config_helper_gpu_pds::parse_done_msg(Event_engine *event_engine, sc_event *notify_event) {

}

void config_helper_gpu_pds::set_global_vars(int T) {
     
}