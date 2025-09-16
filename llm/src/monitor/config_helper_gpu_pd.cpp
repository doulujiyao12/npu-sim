#include "monitor/config_helper_gpu_pd.h"
#include "prims/gpu_prims.h"
#include "prims/norm_prims.h"
#include "utils/prim_utils.h"
#include "utils/system_utils.h"

config_helper_gpu_pd::config_helper_gpu_pd(string filename, string font_ttf,
                                           sc_event *ev_sig,
                                           int config_chip_id) {
    cout << "Loading config file: " << filename << endl;
    json j;
    ifstream jfile(filename);
    jfile >> j;

    decode_done = 0;
    prim_index = 0;

    auto config_reqs = j["requests"];
    int req_cnt = config_reqs["count"];

    heads = config_reqs["heads"];
    head_size = config_reqs["head_size"];
    kv_heads = config_reqs["kv_heads"];
    eof_chance = config_reqs["eof_chance"];
    batch_size = config_reqs["batch_size"];

    auto config_source = j["source"];
    for (int i = 0; i < config_source.size(); i++) {
        source_info.push_back(
            make_pair(config_source[i]["label"], config_source[i]["size"]));
    }

    iter_status = CoreStatus(0, JOB_BOTH);

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

    for (int i = 0; i < req_cnt; i++) {
        RequestRecord record =
            RequestRecord(i, config_reqs["seq_len"], heads, arrival_time[i]);
        requestRecords.push_back(record);
    }

    // 建立原语模板
    json_template = j["chips"][0]["streams"][0];
    busy = false;
    g_recv_ack_cnt = 0;
    g_recv_done_cnt = 0;

    ev_sig->notify(0, SC_NS);
}

void config_helper_gpu_pd::fill_queue_config(queue<Msg> *q) {
    // 将temp中的所有内容搬运到q中，并清空temp
    for (auto msg : temp_config) {
        auto des = msg.des;
        int index = des / GRID_X;
        q[index].push(msg);
    }

    temp_config.clear();
}

void config_helper_gpu_pd::fill_queue_start(queue<Msg> *q) {
    cout << "GPU fill start queue, phase " << prim_index << "\n";

    // 如果是第一个原语且有prefill任务，则需要预先发送数据
    bool has_prefill = false;
    for (auto stage : iter_status.batchInfo) {
        if (stage.type == PREFILL)
            has_prefill = true;
    }

    // 发送数据的大小等于通过source查找
    if (prim_index == 0 && has_prefill) {
        for (auto source : source_info) {
            AddrPosKey source_key = AddrPosKey(0, GetDefinedParam(source.second));
            gpu_pos_locator->addPair(source.first, source_key);
        }
    }

    // 直接获取这一个prim有几个核参加
    int sms = ((gpu_base *)prim_list[prim_index])->req_sm;
    for (int i = 0; i < min(sms, GRID_SIZE); i++) {
        int index = i / GRID_X;
        int pkg_index = 0;

        // 这里相当于quick start，实际上也只有第一个原语需要初始数据
        sc_bv<128> d(0x1);
        Msg m = Msg(true, MSG_TYPE::S_DATA, pkg_index + 1, i, 0, i, 0, d);
        m.source = GRID_SIZE;
        q[index].push(m);
    }
}

void config_helper_gpu_pd::iter_done(vector<Msg> done_msg) {
    // 更新prim_index，如果prim_index等于prim_list的长度，则说明所有原语已经完成
    // 则根据iter_status更新requestRecords
    prim_index++;

    if (prim_index == prim_list.size()) {
        for (auto &stage : iter_status.batchInfo) {
            auto &record = requestRecords[stage.req_id];
            switch (record.phase) {
            case PREFILL:
                if (++record.prefill_counter == record.prefill_iters) {
                    stage.type = record.phase = DECODE;
                    stage.token_num = 1;
                }
                break;
            case DECODE:
                record.decode_counter++;
                if (record.decode_counter >= (1.5) / (eof_chance)) {
                    stage.type = record.phase = PD_DONE;

                    if (++decode_done == requestRecords.size()) {
                        cout << "All reqs done.\n";
                        cout << "[CATCH TEST] " << sc_time_stamp() << endl;
                        ofstream outfile("simulation_result.txt", ios::app);
                        if (outfile.is_open()) {
                            outfile << "[CATCH TEST] " << sc_time_stamp() << "L1CACHESIZE " << L1CACHESIZE << " L2CACHESIZE "
                            << L2CACHESIZE << " BANDWIDTH " << gpu_bw
                            << endl;
                            outfile.close();
                        } else {
                            cout << "Error: Unable to open file for writing timestamp." << endl;
                        }
                        sc_stop();
                    }
                }
                break;
            }
        }

        prim_index = 0;
    }

    busy = false;
}

void config_helper_gpu_pd::iter_start() {
    if (busy)
        return;

    // 如果prim_index是0，则首先清空prim_list，然后生成新的
    // 随后，根据现在的prim_index生成原语（只生成一个原语，如果该原语涉及多轮SM计算，也需要一并生成）
    if (prim_index == 0) {
        prim_list.clear();

        // 根据上一轮的状态生成新的原语
        vector<Stage> new_stage;
        int credit = 0;

        // 优先将上一个iter中的decode加入这个iter。如果塞不下，则放入idle_decode中
        for (auto stage : iter_status.batchInfo) {
            switch (stage.type) {
            case PREFILL:
                break;
            case DECODE:
                if (credit < CORE_CREDIT) {
                    credit += 1;
                    new_stage.push_back(stage);
                } else {
                    idle_decode.push(stage.req_id);
                }
                break;
            case PD_DONE:
                break;
            }
        }

        // 如果此时还放得下，则优先从idle_decode中取
        bool new_reqs = true;
        cout << "[GPU PD SCHEDULE] Now credit: " << credit << endl;

        while (credit < CORE_CREDIT) {
            if (idle_decode.size()) {
                // 这里从idle_decode中取
                int req_id = idle_decode.front();
                idle_decode.pop();
                credit += 1;
                new_stage.push_back(Stage(req_id, DECODE, 1));
                cout << "[GPU PD SCHEDULE] Push in new request DECODE "
                     << req_id << endl;
            }

            else if (CORE_CREDIT - credit >= PD_RATIO &&
                     unfinished_prefill.size()) {
                // 这里选取还没有做完的prefill任务
                int req_id = unfinished_prefill.front();
                unfinished_prefill.pop();
                credit += PD_RATIO;

                auto &record = requestRecords[req_id];
                new_stage.push_back(Stage(
                    record.id, PREFILL, record.seq_len / record.prefill_iters));

                if (++record.prefill_distribute < record.prefill_iters)
                    unfinished_prefill.push(req_id);
            }

            else if (CORE_CREDIT - credit >= PD_RATIO && new_reqs) {
                // 统计现在可以被指派的请求个数
                new_reqs = false;

                for (auto &req : requestRecords) {
                    sc_core::sc_time arv_time(req.arrival_time, sc_core::SC_NS);
                    if (req.phase == UNTOUCHED && arv_time <= sc_time_stamp()) {
                        credit += PD_RATIO;
                        new_stage.push_back(Stage(
                            req.id, PREFILL, req.seq_len / req.prefill_iters));
                        req.phase = PREFILL;

                        if (++req.prefill_distribute < req.prefill_iters)
                            unfinished_prefill.push(req.id);
                        cout << "[GPU PD SCHEDULE] Push in new request PREFILL "
                             << req.id << endl;
                        new_reqs = true;
                        break;
                    }
                }
            } else
                break;
        }

        // 开始生成原语，填入prim_list中
        cout << "<<<<<<SCHEDULE ITER>>>>>>\n";
        iter_status.batchInfo = new_stage;
        generate_prims();

        for (auto stage : iter_status.batchInfo) {
            cout << "REQ: " << stage.req_id << ", TYPE: " << stage.type
                 << ", finished iter: "
                 << ((requestRecords[stage.req_id].phase == PREFILL)
                         ? requestRecords[stage.req_id].prefill_counter
                         : requestRecords[stage.req_id].decode_counter)
                 << ", iter count "
                 << requestRecords[stage.req_id].prefill_iters << endl;
        }
    }

    // 随后按照prim_index为每一个核分配原语
    generate_prims(prim_index);

    if (iter_status.batchInfo.size() == 0) {
        // 如果当前iter没有任何core有工作，则不发放config
        temp_config.clear();
        busy = false;
        cout << "[SCHEDULE] Complete idle.\n";
    } else
        busy = true;
}

void config_helper_gpu_pd::generate_prims() {
    // 根据iter_status填满prim_list，这里不包含任何收发原语，只有计算原语
    cout << "[GPU PDS SCHEDULE] Generate iteration pass prims.\n";

    int B = 1, NH = heads, T = 0, C = heads * head_size;
    for (auto stage : iter_status.batchInfo) {
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
    StreamConfig stream = json_template;
    prim_list = stream.prims;
}

void config_helper_gpu_pd::generate_prims(int i) {
    cout << "[GPU PD SCHEDULE] Generate prims for index " << i << ".\n";

    gpu_base *prim = (gpu_base *)prim_list[i];
    int sms = prim->req_sm;

    for (int c = 0; c < GRID_SIZE; c++) {
        int prim_seq = 0;

        // 若c大于sms，则跳过
        if (c >= sms)
            continue;

        PrimBase *recv_data_1 = new Recv_prim(RECV_TYPE::RECV_START, c, 1);
        temp_config.push_back(Msg(false, MSG_TYPE::CONFIG, ++prim_seq, c,
                                  recv_data_1->serialize()));

        PrimBase *set_batch = new Set_batch(iter_status.batchInfo, false);
        temp_config.push_back(Msg(false, MSG_TYPE::CONFIG, ++prim_seq, c,
                                  set_batch->serialize()));

        // 只需要看单个原语重复次数
        int repeat = sms / GRID_SIZE + (sms % GRID_SIZE > c);
        PrimBase *set_addr = new_prim("Set_addr");
        auto label = ((Set_addr *)set_addr)->datapass_label;

        for (int r = 0; r < repeat; r++) {
            for (int i = 0; i < MAX_SPLIT_NUM; i++) {
                label->indata[i] = ((gpu_base *)prim)->datapass_label.indata[i];
            }
            label->outdata = ((gpu_base *)prim)->datapass_label.outdata;

            temp_config.push_back(Msg(false, MSG_TYPE::CONFIG, ++prim_seq, c,
                                      set_addr->serialize()));

            prim->fetch_index = c + r * GRID_SIZE;
            temp_config.push_back(
                Msg(false, MSG_TYPE::CONFIG, ++prim_seq, c, prim->serialize()));
            prim->fetch_index = 0;
        }

        // 发送DONE信号
        PrimBase *send_done = new Send_prim(SEND_TYPE::SEND_DONE);
        Msg m =
            Msg(true, MSG_TYPE::CONFIG, ++prim_seq, c, send_done->serialize());
        m.refill = false;
        temp_config.push_back(m);
    }
}

void config_helper_gpu_pd::set_global_vars(int T) {
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

void config_helper_gpu_pd::parse_ack_msg(Event_engine *event_engine,
                                         int flow_id, sc_event *notify_event) {
    event_engine->add_event(this->name(), "Waiting Recv Ack", "B",
                            Trace_event_util());

    for (auto m : g_temp_ack_msg) {
        int cid = m.source;
        cout << sc_time_stamp()
             << ": Config helper PD: received ack packet from " << cid
             << ". total " << g_recv_ack_cnt + 1 << "/" << coreconfigs.size()
             << ".\n";

        g_recv_ack_cnt++;
    }

    g_temp_ack_msg.clear();
    event_engine->add_event(this->name(), "Waiting Recv Ack", "E",
                            Trace_event_util());

    // 计算本iter参与计算的core数量
    int sms = ((gpu_base *)prim_list[prim_index])->req_sm;
    int attend_cores = sms >= GRID_SIZE ? GRID_SIZE : sms;
    if (g_recv_ack_cnt >= attend_cores) {
        g_recv_ack_cnt = 0;
        notify_event->notify(CYCLE, SC_NS);
    }
}

void config_helper_gpu_pd::parse_done_msg(Event_engine *event_engine,
                                          sc_event *notify_event) {
    event_engine->add_event(this->name(), "Waiting Core busy", "B",
                            Trace_event_util());

    for (auto m : g_temp_done_msg) {
        int cid = m.source;
        cout << sc_time_stamp()
             << ": Config helper GPU PDS: received done packet from " << cid
             << endl;

        g_recv_done_cnt++;
        g_done_msg.push_back(m);
    }
    g_temp_done_msg.clear();
    event_engine->add_event(this->name(), "Waiting Core busy", "E",
                            Trace_event_util());

    // 计算本iter参与计算的core数量
    int sms = ((gpu_base *)prim_list[prim_index])->req_sm;
    int attend_cores = sms >= GRID_SIZE ? GRID_SIZE : sms;
    if (g_recv_done_cnt >= attend_cores) {
        iter_done(g_done_msg);

        g_done_msg.clear();
        g_recv_done_cnt = 0;
        notify_event->notify(CYCLE, SC_NS);
    }
}

void config_helper_gpu_pd::print_self() {}