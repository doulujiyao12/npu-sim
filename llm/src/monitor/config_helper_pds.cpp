#include "monitor/config_helper_pds.h"
#include "prims/norm_prims.h"
#include "prims/pd_prims.h"
#include "utils/prim_utils.h"
#include "utils/system_utils.h"

config_helper_pds::config_helper_pds(string filename, string font_ttf,
                                     sc_event *ev_sig, int config_chip_id) {
    cout << "Loading config file " << filename << endl;
    json j;
    ifstream jfile(filename);
    jfile >> j;

    decode_done = 0;

    // 收集相关参数
    auto config_reqs = j["requests"];
    int req_cnt = config_reqs["count"];
    heads = config_reqs["heads"];
    eof_chance = config_reqs["eof_chance"];
    prefill_stage = config_reqs["prefill_stage"];
    decode_stage = config_reqs["decode_stage"];

    for (int i = 0; i < GRID_SIZE; i++) {
        int cal_stage = i % (prefill_stage + decode_stage) + 1;
        CoreStatus status = CoreStatus(
            i, (cal_stage <= prefill_stage) ? JOB_PREFILL : JOB_DECODE);
        coreStatus.push_back(status);
    }

    if (config_reqs["arrival"].size() != req_cnt) {
        cout << "[ERROR] In config helper pd: arrival time length "
                "incompatible.\n";
        sc_stop();
    }

    for (int i = 0; i < req_cnt; i++) {
        int interv = config_reqs["arrival"][i];
        arrival_time.push_back(interv);
    }

    for (int i = 0; i < GRID_SIZE / (prefill_stage + decode_stage); i++) {
        queue<int> q;
        idle_decode.push_back(q);
    }

    for (int i = 0; i < req_cnt; i++) {
        RequestRecord record =
            RequestRecord(i, config_reqs["seq_len"], heads, arrival_time[i]);
        requestRecords.push_back(record);
    }

    json_template_p = j["chips"][0]["cores"][0];
    json_template_d = j["chips"][0]["cores"][1];
    busy_d = busy_p = false;
    g_recv_ack_cnt_d = g_recv_ack_cnt_p = g_recv_done_cnt_d =
        g_recv_done_cnt_p = 0;
    wait_schedule_p = true;
    wait_schedule_d = false;
    wait_send_start = false;

    ev_sig->notify(0, SC_NS);
}

void config_helper_pds::fill_queue_config(queue<Msg> *q) {
    // 将temp中的所有内容搬运到q中，并清空temp
    for (auto msg : temp_config) {
        auto des = msg.des;
        int index = des / GRID_X;
        q[index].push(msg);
    }

    temp_config.clear();
}

void config_helper_pds::fill_queue_start(queue<Msg> *q) {
    // 只有在stage 1的core进行prefill的时候，才需要发送start data
    // 在调用这个函数的时候，已经完成对core的config发放
    cout << "Prepare to send start data!\n";
    if (!wait_send_start)
        return;

    for (auto status : coreStatus) {
        cout << "status " << status.id << endl;
        if (status.id % (prefill_stage + decode_stage) + 1 != 1)
            continue;

        int index = status.id / GRID_X;
        int total_pkg = 0;

        for (int i = 0; i < status.batchInfo.size(); i++) {
            auto stage = status.batchInfo[i];
            auto record = requestRecords[stage.req_id];
            int size = record.seq_len / record.prefill_iters * heads * 64;
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

void config_helper_pds::iter_done(PD_JOB type) {
    // 按照coreStatus更新requestRecords，理论来说只要获取所有core的batch的req_id即可
    // 如果其中有DECODE done的话就额外更新一次
    // 只有最后一个stage的core才能够更新
    vector<Msg> done_msg;
    if (type == JOB_PREFILL)
        done_msg = g_done_msg_p;
    else if (type == JOB_DECODE)
        done_msg = g_done_msg_d;

    for (auto msg : done_msg) {
        int id = msg.source;
        int cal_stage = id % (prefill_stage + decode_stage) + 1;
        if (cal_stage != prefill_stage &&
            cal_stage != prefill_stage + decode_stage)
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

void config_helper_pds::iter_start(PD_JOB type) {
    if (type == JOB_PREFILL && busy_p || type == JOB_DECODE && busy_d)
        return;

    cout << "Iter Start, type " << type << endl;

    // 为每一个核进行schedule，如果这个核不是第一个stage，则复制前一个stage上一个iter的任务
    vector<pair<int, vector<Stage>>> temp_stage;

    if (type == JOB_PREFILL) {
        for (auto status : coreStatus) {
            int id = status.id;
            int cal_stage = id % (prefill_stage + decode_stage) + 1;
            if (cal_stage > prefill_stage)
                continue;

            if (cal_stage != 1)
                temp_stage.push_back(
                    make_pair(id, coreStatus[id - 1].batchInfo));
            else {
                // 为stage1核分配任务，如果是prefill核，则只能做prefill任务
                bool done = false;
                vector<Stage> new_stage_1;

                // 优先做没有做完的prefill任务
                for (auto stage : coreStatus[id].batchInfo) {
                    auto &record = requestRecords[stage.req_id];
                    if (record.prefill_distribute + 1 <= record.prefill_iters) {
                        record.prefill_distribute++;
                        new_stage_1.push_back(stage);
                        done = true;
                    }
                }

                // 最后一个阶段的prefill是否完成，于第一阶段的prefill核没有关系，直接跳过

                // 如果此时还没有被分配任务，则需要分配一个prefill。寻找UNTOUCHED
                if (!done) {
                    for (auto &req : requestRecords) {
                        sc_core::sc_time arv_time(req.arrival_time,
                                                  sc_core::SC_NS);
                        if (req.phase == UNTOUCHED &&
                            arv_time <= sc_time_stamp()) {
                            done = true;
                            new_stage_1.push_back(
                                Stage(req.id, PREFILL,
                                      req.seq_len / req.prefill_iters));
                            req.phase = PREFILL;
                            req.prefill_distribute++;
                            cout << "[PD SCHEDULE] Core " << id
                                 << " push in new request PREFILL " << req.id
                                 << endl;
                            break;
                        }
                    }
                }

                temp_stage.push_back(make_pair(id, new_stage_1));
            }
        }

        wait_schedule_p = false;
    }

    else if (type == JOB_DECODE) {
        for (auto status : coreStatus) {
            int id = status.id;
            int cal_stage = id % (prefill_stage + decode_stage) + 1;
            if (cal_stage <= prefill_stage)
                continue;

            if (cal_stage != prefill_stage + 1)
                temp_stage.push_back(
                    make_pair(id, coreStatus[id - 1].batchInfo));
            else {
                // 为stage1核分配任务，如果是decode核，则只能做decode任务
                int credit = 0;
                vector<Stage> new_stage_1;

                // 优先看从最后一个阶段下来的任务
                for (auto stage : coreStatus[id + decode_stage - 1].batchInfo) {
                    if (stage.type == PD_DONE)
                        continue;

                    if (credit < CORE_CREDIT) {
                        credit += 1;
                        new_stage_1.push_back(stage);
                    } else {
                        idle_decode[id / (prefill_stage + decode_stage)].push(
                            stage.req_id);
                    }
                }

                // 如果此时还有空余，则查看是否有等待队列中的decode
                auto &waiting_list =
                    idle_decode[id / (prefill_stage + decode_stage)];
                while (waiting_list.size() && credit < CORE_CREDIT) {
                    int req_id = waiting_list.front();
                    waiting_list.pop();
                    credit += 1;
                    new_stage_1.push_back(Stage(req_id, DECODE, 1));
                    cout << "[PD SCHEDULE] Core " << id
                         << " push in new request DECODE " << req_id << endl;
                }

                // 最后检查是否有新转为decode的请求
                while (req_decode.size() && credit < CORE_CREDIT) {
                    int req_id = req_decode.front();
                    req_decode.pop();
                    credit += 1;
                    new_stage_1.push_back(Stage(req_id, DECODE, 1));
                    cout << "[PD SCHEDULE] Core " << id
                         << " push in new request DECODE " << req_id << endl;
                }

                temp_stage.push_back(make_pair(id, new_stage_1));
            }
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

void config_helper_pds::print_self() {}

void config_helper_pds::generate_prims(int i, vector<Msg> &temp_buffer) {
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
    CoreConfig core =
        status.job_type == JOB_PREFILL ? json_template_p : json_template_d;
    auto &work = core.worklist[0];

    // 手动填写recv_cnt
    work.recv_tag = i;
    if (i % (prefill_stage + decode_stage) + 1 != 1)
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
    temp_buffer.push_back(
        Msg(false, MSG_TYPE::CONFIG, ++prim_seq, i, recv_data_1->serialize()));
    prim_base *set_batch = new Set_batch(status.batchInfo);
    temp_buffer.push_back(
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

            temp_buffer.push_back(Msg(false, MSG_TYPE::CONFIG, ++prim_seq, i,
                                      set_addr->serialize()));
            temp_buffer.push_back(
                Msg(false, MSG_TYPE::CONFIG, ++prim_seq, i, prim->serialize()));
        }
    }

    // 处理数据流向下一个core
    int send_dest = i + 1;
    if (send_dest % (prefill_stage + decode_stage) == 0)
        // decode最后一个阶段，发送地址为decode的第一个阶段
        send_dest -= decode_stage;
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

    if (i % (prefill_stage + decode_stage) + 1 == 1) {
        // 如果是第一个核，则只发不收
        temp_buffer.push_back(
            Msg(false, MSG_TYPE::CONFIG, ++prim_seq, i, send_req->serialize()));
        temp_buffer.push_back(
            Msg(false, MSG_TYPE::CONFIG, ++prim_seq, i, recv_ack->serialize()));
        temp_buffer.push_back(Msg(false, MSG_TYPE::CONFIG, ++prim_seq, i,
                                  send_data->serialize()));
    } else if (i % (prefill_stage + decode_stage) + 1 == prefill_stage) {
        // 如果是prefill最后一个核，则只收不发
        temp_buffer.push_back(Msg(false, MSG_TYPE::CONFIG, ++prim_seq, i,
                                  recv_data_2->serialize()));
    } else if (i % (prefill_stage + decode_stage) + 1 == (prefill_stage + 1)) {
        // 如果是decode的第一个核，则先发后收
        temp_buffer.push_back(
            Msg(false, MSG_TYPE::CONFIG, ++prim_seq, i, send_req->serialize()));
        temp_buffer.push_back(
            Msg(false, MSG_TYPE::CONFIG, ++prim_seq, i, recv_ack->serialize()));
        temp_buffer.push_back(Msg(false, MSG_TYPE::CONFIG, ++prim_seq, i,
                                  send_data->serialize()));
        temp_buffer.push_back(Msg(false, MSG_TYPE::CONFIG, ++prim_seq, i,
                                  recv_data_2->serialize()));
    } else {
        // 其余的核，统一先收后发
        temp_buffer.push_back(Msg(false, MSG_TYPE::CONFIG, ++prim_seq, i,
                                  recv_data_2->serialize()));
        temp_buffer.push_back(
            Msg(false, MSG_TYPE::CONFIG, ++prim_seq, i, send_req->serialize()));
        temp_buffer.push_back(
            Msg(false, MSG_TYPE::CONFIG, ++prim_seq, i, recv_ack->serialize()));
        temp_buffer.push_back(Msg(false, MSG_TYPE::CONFIG, ++prim_seq, i,
                                  send_data->serialize()));
    }

    // 每一个核都需要向memInterface发送DONE信号
    prim_base *send_done = new Send_prim(SEND_TYPE::SEND_DONE);
    Msg m = Msg(true, MSG_TYPE::CONFIG, ++prim_seq, i, send_done->serialize());
    m.refill = false;
    temp_buffer.push_back(m);
}

void config_helper_pds::parse_ack_msg(Event_engine *event_engine, int flow_id,
                                      sc_event *notify_event) {
    event_engine->add_event(this->name(), "Waiting Recv Ack", "B",
                            Trace_event_util());

    for (auto m : g_temp_ack_msg) {
        int cid = m.source;
        cout << sc_time_stamp()
             << ": Config helper PD: received ack packet from " << cid
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

    if (g_recv_ack_cnt_p >=
        coreStatus.size() / (prefill_stage + decode_stage) * prefill_stage) {
        g_recv_ack_cnt_p = 0;
        wait_send_start = true;
        notify_event->notify(CYCLE, SC_NS);
    }

    if (g_recv_ack_cnt_d >=
        coreStatus.size() / (prefill_stage + decode_stage) * decode_stage) {
        g_recv_ack_cnt_d = 0;
        // notify_event->notify(CYCLE, SC_NS);
    }
}

void config_helper_pds::parse_done_msg(Event_engine *event_engine,
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

    if (g_recv_done_cnt_p >=
        coreStatus.size() / (prefill_stage + decode_stage) * prefill_stage) {
        iter_done(JOB_PREFILL);

        g_done_msg_p.clear();
        g_recv_done_cnt_p = 0;
        wait_schedule_p = true;
        notify_event->notify(CYCLE, SC_NS);
    } else if (g_recv_done_cnt_d >= coreStatus.size() /
                                        (prefill_stage + decode_stage) *
                                        decode_stage) {
        iter_done(JOB_DECODE);

        g_done_msg_d.clear();
        g_recv_done_cnt_d = 0;
        wait_schedule_d = true;
        notify_event->notify(CYCLE, SC_NS);
    }
}
