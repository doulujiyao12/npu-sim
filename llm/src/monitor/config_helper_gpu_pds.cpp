#include "monitor/config_helper_gpu_pds.h"
#include "prims/gpu_prims.h"
#include "prims/norm_prims.h"
#include "utils/prim_utils.h"

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
                    req_decode.push(stage.req_id);
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
    vector<pair<int, vector<Stage>>> temp_stage;

    if (type == JOB_PREFILL) {
        for (auto status : coreStatus) {
            int id = status.id;
            if (id >= prefill_core)
                continue;

            int done = 0;
            vector<Stage> new_stage;

            // 拿出上一个iter的所有任务
            auto batchInfo = coreStatus[id].batchInfo;
            for (auto stage : batchInfo) {
                auto &record = requestRecords[stage.req_id];
                if (record.prefill_distribute < record.prefill_iters) {
                    record.prefill_distribute++;
                    new_stage.push_back(stage);
                    done++;
                }
            }

            // 如果此时还没有被分配任务，则需要分配一个prefill。优先寻找已经在做prefill但是没有完全分发完毕的请求。
            if (done < batch_size) {
                for (auto &req : requestRecords) {
                    sc_core::sc_time arv_time(req.arrival_time, sc_core::SC_NS);
                    if (req.phase == PREFILL &&
                        req.prefill_distribute < req.prefill_iters) {
                        req.prefill_distribute++;
                        new_stage.push_back(Stage(
                            req.id, PREFILL, req.seq_len / req.prefill_iters));

                        cout << "[PD SCHEDULE] Core " << id
                             << " push in old request PREFILL " << req.id
                             << endl;

                        if (++done == batch_size)
                            break;
                    } else if (req.phase == UNTOUCHED &&
                               arv_time <= sc_time_stamp()) {
                        new_stage.push_back(Stage(
                            req.id, PREFILL, req.seq_len / req.prefill_iters));
                        req.phase = PREFILL;
                        req.prefill_distribute++;
                        cout << "[PD SCHEDULE] Core " << id
                             << " push in new request PREFILL " << req.id
                             << endl;

                        if (++done == batch_size)
                            break;
                    }
                }
            }

            temp_stage.push_back(make_pair(id, new_stage));
        }

        wait_schedule_p = false;
    }

    else if (type == JOB_DECODE) {
        for (auto status : coreStatus) {
            int id = status.id;
            if (id < prefill_core)
                continue;

            // 拿出上一个iter的所有任务
            int credit = 0;
            vector<Stage> new_stage;

            for (auto stage : coreStatus[id].batchInfo) {
                if (stage.type == PD_DONE)
                    continue;

                if (credit < CORE_CREDIT) {
                    credit += 1;
                    new_stage.push_back(stage);
                } else {
                    idle_decode[id].push(stage.req_id);
                }
            }

            // 如果此时还有空余，则查看是否有等待队列中的decode
            auto &waiting_list = idle_decode[id];
            while (waiting_list.size() && credit < CORE_CREDIT) {
                int req_id = waiting_list.front();
                waiting_list.pop();
                credit += 1;
                new_stage.push_back(Stage(req_id, DECODE, 1));
                cout << "[PD SCHEDULE] Core " << id
                     << " push in new request DECODE " << req_id << endl;
            }

            // 最后检查是否有新转为decode的请求
            while (req_decode.size() && credit < CORE_CREDIT) {
                int req_id = req_decode.front();
                req_decode.pop();
                credit += 1;
                new_stage.push_back(Stage(req_id, DECODE, 1));
                cout << "[PD SCHEDULE] Core " << id
                     << " push in new request DECODE " << req_id << endl;
            }

            temp_stage.push_back(make_pair(id, new_stage));
        }

        wait_schedule_d = false;
    }

    // 统一更新所有的batchInfo，生成原语
    bool complete_idle = true;
    vector<Msg> temp_buffer;

    cout << "<<<<<<SCHEDULE ITER>>>>>>\n";
    cout << sc_time_stamp() << endl;
    for (auto pair : temp_stage) {
        auto &status = coreStatus[pair.first];
        status.batchInfo = pair.second;

        cout << "[SCHEDULE] Core " << status.id << endl;
        for (auto stage : status.batchInfo) {
            complete_idle = false;

            cout << "REQ: " << stage.req_id << ", TYPE: " << stage.type
                 << ", finished iter: "
                 << ((requestRecords[stage.req_id].phase == PREFILL)
                         ? requestRecords[stage.req_id].prefill_counter
                         : requestRecords[stage.req_id].decode_counter)
                 << ", iter count "
                 << requestRecords[stage.req_id].prefill_iters << endl;
        }

        generate_prims(status.id, temp_buffer);
    }

    if (complete_idle) {
        // 如果当前iter没有任何core有工作，则不发放config
        if (type == JOB_PREFILL) {
            busy_p = false;
            wait_schedule_p = true;
        } else if (type == JOB_DECODE) {
            busy_d = false;
            wait_schedule_d = true;
        }

        cout << "[SCHEDULE] Complete idle.\n";
    } else {
        for (auto msg : temp_buffer)
            temp_config.push_back(msg);

        if (type == JOB_PREFILL)
            busy_p = true;
        else if (type == JOB_DECODE)
            busy_d = true;
    }
}

void config_helper_gpu_pds::print_self() {}

void config_helper_gpu_pds::generate_prims(int i, vector<Msg> &temp_buffer) {
    // 每一个core，计算完之后，直接返回HOST即可
    cout << "Generate prims: Core " << i << endl;
    auto status = coreStatus[i];

    int B = 1, NH = heads, T = 0, C = heads * head_size;
    // 计算T
    for (auto stage : status.batchInfo) {
        auto record = requestRecords[stage.req_id];
        switch (stage.type) {
        case PREFILL:
            T += record.seq_len / record.prefill_iters;
            break;
        case DECODE:
            T += 1;
            break;
        }
    }

    set_global_vars(T);
    CoreConfig core =
        status.job_type == JOB_PREFILL ? json_template_p : json_template_d;
    auto &work = core.worklist[0];

    // prefill core recv_cnt统一为1, decode统一为0
    work.recv_tag = i;
    if (i < prefill_core)
        work.recv_cnt = 1;
    else
        work.recv_cnt = 0;

    int index = i / GRID_X;
    int prim_seq = 0;
    prim_base *recv_data_1 = new Recv_prim(work.recv_cnt ? RECV_TYPE::RECV_START
                                                         : RECV_TYPE::RECV_DATA,
                                           work.recv_tag, work.recv_cnt);
    temp_buffer.push_back(
        Msg(false, MSG_TYPE::CONFIG, ++prim_seq, i, recv_data_1->serialize()));
    prim_base *set_batch = new Set_batch(status.batchInfo);
    temp_buffer.push_back(
        Msg(false, MSG_TYPE::CONFIG, ++prim_seq, i, set_batch->serialize()));

    if (status.batchInfo.size()) {
        for (int p = 0; p < work.prims.size(); p++) {
            auto prim = work.prims[p];
            prim_base *set_addr = new_prim("Set_addr");
            auto label = ((Set_addr *)set_addr)->datapass_label;

            // Set_addr 的label 指向其后面的那条原语
            for (int i = 0; i < MAX_SPLIT_NUM; i++) {
                label->indata[i] = ((gpu_base *)prim)->datapass_label.indata[i];
            }
            label->outdata = ((gpu_base *)prim)->datapass_label.outdata;

            temp_buffer.push_back(Msg(false, MSG_TYPE::CONFIG, ++prim_seq, i,
                                      set_addr->serialize()));
            temp_buffer.push_back(
                Msg(false, MSG_TYPE::CONFIG, ++prim_seq, i, prim->serialize()));
        }
    }

    // 直接发送done消息
    prim_base *send_done = new Send_prim(SEND_TYPE::SEND_DONE);
    Msg m = Msg(true, MSG_TYPE::CONFIG, ++prim_seq, i, send_done->serialize());
    m.refill = false;
    temp_buffer.push_back(m);
}

void config_helper_gpu_pds::parse_ack_msg(Event_engine *event_engine,
                                          int flow_id, sc_event *notify_event) {
    event_engine->add_event(this->name(), "Waiting Recv Ack", "B",
                            Trace_event_util());

    for (auto m : g_temp_ack_msg) {
        int cid = m.source;
        cout << sc_time_stamp()
             << ": Config helper GPU PDS: received ack packet from " << cid
             << ", type: " << coreStatus[cid].job_type;

        if (coreStatus[cid].job_type == JOB_PREFILL) {
            g_recv_ack_cnt_p++;
            cout << ", total " << g_recv_ack_cnt_p << endl;
        } else if (coreStatus[cid].job_type == JOB_DECODE) {
            g_recv_ack_cnt_d++;
            cout << ", total " << g_recv_ack_cnt_d << endl;
        }
    }

    g_temp_ack_msg.clear();
    event_engine->add_event(this->name(), "Waiting Recv Ack", "E",
                            Trace_event_util());

    if (g_recv_ack_cnt_p >= prefill_core) {
        g_recv_ack_cnt_p = 0;
        wait_send_start = true;
        notify_event->notify(CYCLE, SC_NS);
    }

    if (g_recv_ack_cnt_d >= decode_core) {
        g_recv_ack_cnt_d = 0;
        // notify_event->notify(CYCLE, SC_NS);
    }
}

void config_helper_gpu_pds::parse_done_msg(Event_engine *event_engine,
                                           sc_event *notify_event) {
    event_engine->add_event(this->name(), "Waiting Core busy", "B",
                            Trace_event_util());

    for (auto m : g_temp_done_msg) {
        int cid = m.source;
        cout << sc_time_stamp()
             << ": Config helper PD: received done packet from " << cid
             << ", type: " << coreStatus[cid].job_type;

        if (coreStatus[cid].job_type == JOB_PREFILL) {
            g_recv_done_cnt_p++;
            cout << ", total " << g_recv_done_cnt_p << endl;
            g_done_msg_p.push_back(m);
        } else if (coreStatus[cid].job_type == JOB_DECODE) {
            g_recv_done_cnt_d++;
            cout << ", total " << g_recv_done_cnt_d << endl;
            g_done_msg_d.push_back(m);
        }
    }
    g_temp_done_msg.clear();
    event_engine->add_event(this->name(), "Waiting Core busy", "E",
                            Trace_event_util());

    if (g_recv_done_cnt_p >= prefill_core) {
        iter_done(JOB_PREFILL);

        g_done_msg_p.clear();
        g_recv_done_cnt_p = 0;
        wait_schedule_p = true;
        notify_event->notify(CYCLE, SC_NS);
    }

    if (g_recv_done_cnt_d >= decode_core) {
        iter_done(JOB_DECODE);

        g_done_msg_d.clear();
        g_recv_done_cnt_d = 0;
        wait_schedule_d = true;
        notify_event->notify(CYCLE, SC_NS);
    }
}

void config_helper_gpu_pds::set_global_vars(int T) {
    vtable.clear();
    vtable.push_back(make_pair("B", 1));
    vtable.push_back(make_pair("T", T));
    vtable.push_back(make_pair("C", heads * head_size));
    vtable.push_back(make_pair("NH", heads));
    vtable.push_back(make_pair("DH", head_size));
    vtable.push_back(make_pair("R", heads / kv_heads));
    vtable.push_back(make_pair("3C", 3 * heads * head_size));
    vtable.push_back(make_pair("4C", 4 * heads * head_size));
    vtable.push_back(make_pair("BTC", T * heads * head_size));
    vtable.push_back(make_pair("2BTC", 2 * T * heads * head_size));
    vtable.push_back(make_pair("3BTC", 3 * T * heads * head_size));
    vtable.push_back(make_pair("4BTC", 4 * T * heads * head_size));
    vtable.push_back(make_pair("CR", head_size * kv_heads));
    vtable.push_back(make_pair("3CR", 3 * kv_heads * head_size));
}