#include "monitor/config_helper_pd.h"
#include "prims/norm_prims.h"
#include "prims/pd_base.h"
#include "utils/prim_utils.h"
#include "utils/system_utils.h"

config_helper_pd::config_helper_pd(string filename, string font_ttf,
                                   sc_event *ev_sig, int config_chip_id) {
    cout << "Loading config file " << filename << endl;

    json j;
    ifstream jfile(filename);
    jfile >> j;

    decode_done = 0;
    for (int i = 0; i < GRID_SIZE; i++) {
        CoreStatus status = CoreStatus(i, JOB_BOTH);
        coreStatus.push_back(status);
    }

    // 收集相关参数
    auto config_reqs = j["requests"];
    int req_cnt = config_reqs["count"];
    heads = config_reqs["heads"];
    eof_chance = config_reqs["eof_chance"];
    model_stage = config_reqs["stage"];
    batch_size = config_reqs["batch"];

    // 收集req的arrive时间
    if (config_reqs["arrival"].size() != req_cnt) {
        cout << "[ERROR] In config helper pd: arrival time length "
                "incompatible.\n";
        sc_stop();
    }

    for (int i = 0; i < req_cnt; i++)
        arrival_time.push_back(config_reqs["arrival"][i]);

    for (int i = 0; i < req_cnt; i++) {
        RequestRecord record =
            RequestRecord(i, config_reqs["seq_len"], heads, arrival_time[i]);
        requestRecords.push_back(record);
    }

    for (int i = 0; i < GRID_SIZE / model_stage; i++) {
        queue<int> p, q;
        idle_decode.push_back(p);
        unfinished_prefill.push_back(q);
    }

    // 建立原语模板
    json_template = j["chips"][0]["cores"][0];
    busy = false;
    g_recv_ack_cnt = 0;
    g_recv_done_cnt = 0;

    ev_sig->notify(0, SC_NS);
}

void config_helper_pd::fill_queue_config(queue<Msg> *q) {
    // 将temp中的所有内容搬运到q中，并清空temp
    for (auto msg : temp_config) {
        auto des = msg.des;
        int index = des / GRID_X;
        q[index].push(msg);
    }

    temp_config.clear();
}

void config_helper_pd::fill_queue_start(queue<Msg> *q) {
    // 只有在stage 1的core进行prefill的时候，才需要发送start data
    // 在调用这个函数的时候，已经完成对core的config发放

    for (auto status : coreStatus) {
        if ((status.id + 1) % model_stage != 1)
            continue;

        int index = status.id / GRID_X;
        int total_pkg = 0;
        bool has_prefill = false;

        for (int i = 0; i < status.batchInfo.size(); i++) {
            auto stage = status.batchInfo[i];
            if (stage.type == PREFILL) {
                has_prefill = true;
                auto record = requestRecords[stage.req_id];
                int size = record.seq_len / record.prefill_iters * heads * 64;
                int send_size_in_bit = size * sizeof(float) * 8;
                int pkg_num = (send_size_in_bit % M_D_DATA)
                                  ? (send_size_in_bit / M_D_DATA + 1)
                                  : (send_size_in_bit / M_D_DATA);

                for (int j = 1; j <= pkg_num; j++) {
                    sc_bv<M_D_DATA> d(0x1);

                    Msg m =
                        Msg(false, MSG_TYPE::S_DATA, j + total_pkg, status.id,
                            M_D_DATA * (j - 1), status.id, M_D_DATA, d);
                    m.source = GRID_SIZE;
                    q[index].push(m);
                }

                total_pkg += pkg_num;
            }
        }

        if (has_prefill) {
            sc_bv<M_D_DATA> d(0x1);
            q[index].push(Msg(true, MSG_TYPE::S_DATA, total_pkg + 1, status.id,
                              0, status.id, 1, d));
        }
    }
}

void config_helper_pd::iter_done(vector<Msg> done_msg) {
    // 按照coreStatus更新requestRecords，理论来说只要获取所有core的batch的req_id即可
    // 如果其中有DECODE done的话就额外更新一次
    // 只有最后一个stage的core才能够更新
    for (auto msg : done_msg) {
        int id = msg.source;
        if ((id + 1) % model_stage)
            continue;

        auto &status = coreStatus[id];
        int stage_count = 0;
        for (auto &stage : status.batchInfo) {
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
                if (msg.data.range(stage_count, stage_count).to_uint64() ||
                    record.decode_counter >= (1.5) / (eof_chance)) {
                    stage.type = record.phase = PD_DONE;

                    if (++decode_done == requestRecords.size()) {
                        cout << "All reqs done.\n";
                        sc_stop();
                    }
                }
                break;
            }

            stage_count++;
        }
    }

    busy = false;
}

void config_helper_pd::iter_start() {
    if (busy)
        return;

    // 为每一个核进行schedule，如果这个核不是第一个stage，则复制前一个stage上一个iter的任务
    vector<vector<Stage>> temp_stage;
    for (auto status : coreStatus) {
        int id = status.id;
        if ((id + 1) % model_stage != 1) {
            temp_stage.push_back(coreStatus[id - 1].batchInfo);
        } else {
            // 为stage1核分配任务，取决于前一个iter的最后一个stage核的执行情况。如果任务打不满，主动寻找新的req任务
            int credit = 0;
            vector<Stage> new_stage_1;

            for (auto stage : coreStatus[id + model_stage - 1].batchInfo) {
                switch (stage.type) {
                case PREFILL:
                    break;
                case DECODE:
                    if (credit < CORE_CREDIT) {
                        credit += 1;
                        new_stage_1.push_back(stage);
                    } else {
                        idle_decode[id / model_stage].push(stage.req_id);
                    }
                    break;
                case PD_DONE:
                    break;
                }
            }

            bool new_reqs = true;
            queue<int> &decode_waiting_list = idle_decode[id / model_stage];
            queue<int> &prefill_waiting_list =
                unfinished_prefill[id / model_stage];
            cout << "[PD SCHEDULE] Core " << id << " credit: " << credit
                 << endl;

            while (credit < CORE_CREDIT) {
                // PREFILL new iter > UNTOUCHED

                if (decode_waiting_list.size()) {
                    // 这里从idle_decode中取
                    int req_id = decode_waiting_list.front();
                    decode_waiting_list.pop();
                    credit += 1;
                    new_stage_1.push_back(Stage(req_id, DECODE, 1));
                    cout << "[PD SCHEDULE] Core " << id
                         << " push in new request DECODE " << req_id << endl;
                }

                else if (CORE_CREDIT - credit >= PD_RATIO &&
                         prefill_waiting_list.size()) {
                    // 这里选取还没有做完的prefill任务
                    int req_id = prefill_waiting_list.front();
                    prefill_waiting_list.pop();
                    credit += PD_RATIO;

                    auto &record = requestRecords[req_id];
                    new_stage_1.push_back(
                        Stage(record.id, PREFILL,
                              record.seq_len / record.prefill_iters));

                    if (++record.prefill_distribute < record.prefill_iters)
                        prefill_waiting_list.push(req_id);
                }

                else if (CORE_CREDIT - credit >= PD_RATIO && new_reqs) {
                    // 统计现在可以被指派的请求个数
                    for (auto &req : requestRecords) {
                        sc_core::sc_time arv_time(req.arrival_time,
                                                  sc_core::SC_NS);
                        if (req.phase == UNTOUCHED &&
                            arv_time <= sc_time_stamp()) {
                            credit += PD_RATIO;
                            new_stage_1.push_back(
                                Stage(req.id, PREFILL,
                                      req.seq_len / req.prefill_iters));
                            req.phase = PREFILL;
                            req.prefill_distribute++;
                            prefill_waiting_list.push(req.id);
                            cout << "[PD SCHEDULE] Core " << id
                                 << " push in new request PREFILL " << req.id
                                 << endl;
                            break;
                        }
                    }

                    new_reqs = false;
                } else
                    break;
            }

            temp_stage.push_back(new_stage_1);
        }
    }

    // 统一更新所有的batchInfo，生成原语
    bool complete_idle = true;

    cout << "<<<<<<SCHEDULE ITER>>>>>>\n";
    cout << sc_time_stamp() << endl;
    for (auto &status : coreStatus) {
        status.batchInfo = temp_stage[status.id];

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

        generate_prims(status.id);
    }

    if (complete_idle) {
        // 如果当前iter没有任何core有工作，则不发放config
        temp_config.clear();
        busy = false;
        cout << "[SCHEDULE] Complete idle.\n";
    } else
        busy = true;
}

void config_helper_pd::print_self() {
    cout << "[PD Config]" << endl;
    cout << "Heads: " << heads << endl;
    cout << "EOF Chance: " << eof_chance << endl;
    cout << "Request Records: " << requestRecords.size() << endl;

    // for (int i = 0; i < coreStatus.size(); i++) {
    //     cout << "Core " << i << " Status:" << endl;
    //     cout << "  Available: " << (coreStatus[i].available ? "Yes" :
    //     "No")
    //          << endl;
    //     cout << "  Data Sent: " << (coreStatus[i].data_sent ? "Yes" :
    //     "No")
    //          << endl;
    //     cout << "  Requests: ";
    //     for (auto req : coreStatus[i].reqs) {
    //         cout << req << " ";
    //     }
    //     cout << endl;
    // }

    // cout << "Decode Done: " << decode_done << "/" <<
    // requestRecords.size()
    //      << endl;
}

void config_helper_pd::generate_prims(int i) {
    // 一个iter中有stage个core参与执行，id 1要流向id end，id end要传回id 1
    // core中原语为单个corejob，需要配置收发规则
    cout << "Generate prims: Core " << i << endl;
    auto status = coreStatus[i];

    int B = 1, NH = heads, T = 0, C = heads * 64;
    bool exist_prefill = false;
    for (auto stage : status.batchInfo) {
        auto record = requestRecords[stage.req_id];
        switch (stage.type) {
        case PREFILL:
            T += record.seq_len / record.prefill_iters;
            exist_prefill = true;
            break;
        case DECODE:
            T += 1;
            break;
        }
    }

    // TODO: 其他decoder模型适配？
    set_var_gpt2(B, T, C, NH);
    CoreConfig core = json_template;
    auto &work = core.worklist[0];

    // 手动填写recv_cnt
    work.recv_tag = i;
    if ((i + 1) % model_stage != 1)
        work.recv_cnt = 0;
    else if (exist_prefill)
        work.recv_cnt = 1;
    else
        work.recv_cnt = 0;

    int index = i / GRID_X;
    int prim_seq = 0;

    prim_base *recv_data_1 = new Recv_prim(work.recv_cnt ? RECV_TYPE::RECV_START
                                                         : RECV_TYPE::RECV_DATA,
                                           work.recv_tag, work.recv_cnt);
    temp_config.push_back(
        Msg(false, MSG_TYPE::CONFIG, ++prim_seq, i, recv_data_1->serialize()));
    prim_base *set_batch = new Set_batch(status.batchInfo);
    temp_config.push_back(
        Msg(false, MSG_TYPE::CONFIG, ++prim_seq, i, set_batch->serialize()));

    if (status.batchInfo.size()) {
        for (auto prim : work.prims) {
            prim_base *set_addr = new_prim("Set_addr");
            auto label = ((Set_addr *)set_addr)->datapass_label;
            for (int i = 0; i < MAX_SPLIT_NUM; i++) {
                if (prim->prim_type == PD_PRIM) {
                    label->indata[i] =
                        ((pd_base *)prim)->datapass_label.indata[i];
                } else if (prim->prim_type == COMP_PRIM) {
                    label->indata[i] =
                        ((comp_base *)prim)->datapass_label.indata[i];
                }
            }
            if (prim->prim_type == PD_PRIM) {
                label->outdata = ((pd_base *)prim)->datapass_label.outdata;
            } else if (prim->prim_type == COMP_PRIM) {
                label->outdata = ((comp_base *)prim)->datapass_label.outdata;
            }

            temp_config.push_back(Msg(false, MSG_TYPE::CONFIG, ++prim_seq, i,
                                      set_addr->serialize()));
            temp_config.push_back(
                Msg(false, MSG_TYPE::CONFIG, ++prim_seq, i, prim->serialize()));
        }
    }

    // 处理数据流向下一个core
    int send_dest = i + 1;
    if (send_dest % model_stage == 0)
        send_dest -= model_stage;
    int send_tag = send_dest;

    prim_base *recv_data_2 =
        new Recv_prim(RECV_TYPE::RECV_DATA, work.recv_tag, 1);
    prim_base *send_req =
        new Send_prim(SEND_TYPE::SEND_REQ, send_dest, send_tag);
    prim_base *recv_ack = new Recv_prim(RECV_TYPE::RECV_ACK);
    Send_prim *send_data =
        new Send_prim(SEND_TYPE::SEND_DATA, send_dest, send_tag);

    int output_size = max(int(C * T * B * sizeof(float)), 1);
    int pkg_nums = (output_size % M_D_DATA) ? (output_size / M_D_DATA + 1)
                                            : (output_size / M_D_DATA);
    int end_length = output_size - (pkg_nums - 1) * M_D_DATA;

    send_data->max_packet = pkg_nums;
    send_data->end_length = end_length;

    if ((i + 1) % model_stage != 1) {
        temp_config.push_back(Msg(false, MSG_TYPE::CONFIG, ++prim_seq, i,
                                  recv_data_2->serialize()));
        temp_config.push_back(
            Msg(false, MSG_TYPE::CONFIG, ++prim_seq, i, send_req->serialize()));
        temp_config.push_back(
            Msg(false, MSG_TYPE::CONFIG, ++prim_seq, i, recv_ack->serialize()));
        temp_config.push_back(Msg(false, MSG_TYPE::CONFIG, ++prim_seq, i,
                                  send_data->serialize()));
    } else {
        temp_config.push_back(
            Msg(false, MSG_TYPE::CONFIG, ++prim_seq, i, send_req->serialize()));
        temp_config.push_back(
            Msg(false, MSG_TYPE::CONFIG, ++prim_seq, i, recv_ack->serialize()));
        temp_config.push_back(Msg(false, MSG_TYPE::CONFIG, ++prim_seq, i,
                                  send_data->serialize()));
        temp_config.push_back(Msg(false, MSG_TYPE::CONFIG, ++prim_seq, i,
                                  recv_data_2->serialize()));
    }

    // 每一个核都需要向memInterface发送DONE信号
    prim_base *send_done = new Send_prim(SEND_TYPE::SEND_DONE);
    Msg m = Msg(true, MSG_TYPE::CONFIG, ++prim_seq, i, send_done->serialize());
    m.refill = false;
    temp_config.push_back(m);
}

void config_helper_pd::parse_ack_msg(Event_engine *event_engine, int flow_id,
                                     sc_event *notify_event) {
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

    if (g_recv_ack_cnt >= coreStatus.size()) {
        g_recv_ack_cnt = 0;
        notify_event->notify(CYCLE, SC_NS);
    }
}

void config_helper_pd::parse_done_msg(Event_engine *event_engine,
                                      sc_event *notify_event) {
    event_engine->add_event(this->name(), "Waiting Core busy", "B",
                            Trace_event_util());

    for (auto m : g_temp_done_msg) {
        int cid = m.source;
        cout << sc_time_stamp()
             << ": Config helper PD: received done packet from " << cid
             << ", working type " << coreStatus[cid].job_type << ".\n";

        g_recv_done_cnt++;
        g_done_msg.push_back(m);
    }
    g_temp_done_msg.clear();
    event_engine->add_event(this->name(), "Waiting Core busy", "E",
                            Trace_event_util());

    if (g_recv_done_cnt >= coreStatus.size()) {
        iter_done(g_done_msg);

        g_done_msg.clear();
        g_recv_done_cnt = 0;
        notify_event->notify(CYCLE, SC_NS);
    }
}
