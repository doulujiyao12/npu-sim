#include "monitor/config_helper_pd.h"
#include "prims/norm_prims.h"
#include "utils/prim_utils.h"
#include "utils/system_utils.h"

config_helper_pd::config_helper_pd(string filename, string font_ttf,
                                   int config_chip_id) {
    cout << "Loading config file " << filename << endl;

    json j;
    ifstream jfile(filename);
    jfile >> j;

    decode_done = 0;
    for (int i = 0; i < GRID_SIZE; i++) {
        CoreStatus status = CoreStatus(i);
        coreStatus.push_back(status);
    }

    // 收集相关参数
    auto config_reqs = j["requests"];
    int req_cnt = config_reqs["count"];
    heads = config_reqs["heads"];
    eof_chance = config_reqs["eof_chance"];
    model_stage = config_reqs["stage"];

    for (int i = 1; i <= req_cnt; i++) {
        RequestRecord record = RequestRecord(i, config_reqs["seq_len"], heads);
    }

    // 建立原语模板
    json_template = j["chips"][0]["cores"][0];
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

        for (auto stage : status.batchInfo) {
            if (stage.type == PREFILL) {
                auto record = requestRecords[stage.req_id];
                int size = record.seq_len / record.prefill_iters * heads * 64;
                int send_size_in_bit = size * sizeof(float) * 8;
                int pkg_num = (send_size_in_bit % M_D_DATA)
                                  ? (send_size_in_bit / M_D_DATA + 1)
                                  : (send_size_in_bit / M_D_DATA);

                for (int j = 1; j <= pkg_num; j++) {
                    sc_bv<M_D_DATA> d(0x1);
                    int length = M_D_DATA;
                    bool is_end_packet = j == pkg_num;
                    if (is_end_packet) {
                        length =
                            size * sizeof(float) - M_D_DATA * (pkg_num - 1);
                    }

                    Msg m = Msg(j == pkg_num, MSG_TYPE::S_DATA, j, status.id,
                                M_D_DATA * (j - 1), status.id, length, d);
                    m.source = GRID_SIZE;
                    q[index].push(m);
                }
            }
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

        auto status = coreStatus[id];
        int stage_count = 0;
        for (auto &stage : status.batchInfo) {
            auto &record = requestRecords[stage.req_id];
            switch (record.phase) {
            case UNTOUCHED:
                record.phase = PREFILL; // 这里不需要break
            case PREFILL:
                if (++record.prefill_counter == record.prefill_iters)
                    stage.type = record.phase = DECODE;
                break;
            case DECODE:
                record.decode_counter++;
                if (msg.data.range(stage_count, stage_count).to_uint64()) {
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
}

void config_helper_pd::iter_start() {
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
                    credit += PD_RATIO;
                    new_stage_1.push_back(stage);
                    break;
                case DECODE:
                    credit += 1;
                    new_stage_1.push_back(stage);
                    break;
                case PD_DONE:
                    break;
                }
            }

            bool new_reqs = true;
            while (credit < CORE_CREDIT && new_reqs) {
                // 分配新的任务，TOUCHED PREFILL > TOUCHED DECODE > UNTOUCHED
                // 这里只会分配UNTOUCHED任务，也就是CREDIT为PREFILL
                if (CORE_CREDIT - credit >= PD_RATIO) {
                    new_reqs = false;
                    for (auto &req : requestRecords) {
                        if (req.phase == UNTOUCHED) {
                            new_reqs = true;
                            credit += PD_RATIO;
                            new_stage_1.push_back(Stage(req.id, PREFILL));
                            break;
                        }
                    }
                }

                // 这里不会分配任何DECODE任务
            }

            temp_stage.push_back(new_stage_1);
        }
    }

    // 统一更新所有的batchInfo，生成原语
    for (auto &status : coreStatus) {
        status.batchInfo = temp_stage[status.id];
        generate_prims(status.id);
    }
}

void config_helper_pd::print_self() {
    cout << "[PD Config]" << endl;
    cout << "Heads: " << heads << endl;
    cout << "EOF Chance: " << eof_chance << endl;
    cout << "Request Records: " << requestRecords.size() << endl;

    // for (int i = 0; i < coreStatus.size(); i++) {
    //     cout << "Core " << i << " Status:" << endl;
    //     cout << "  Available: " << (coreStatus[i].available ? "Yes" : "No")
    //          << endl;
    //     cout << "  Data Sent: " << (coreStatus[i].data_sent ? "Yes" : "No")
    //          << endl;
    //     cout << "  Requests: ";
    //     for (auto req : coreStatus[i].reqs) {
    //         cout << req << " ";
    //     }
    //     cout << endl;
    // }

    // cout << "Decode Done: " << decode_done << "/" << requestRecords.size()
    //      << endl;
}

void config_helper_pd::generate_prims(int i) {
    // 一个iter中有stage个core参与执行，id 1要流向id end，id end要传回id 1
    // core中原语为单个corejob，需要配置收发规则
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
    if (i % model_stage != 1)
        work.recv_cnt = 1;
    else if (exist_prefill)
        work.recv_cnt = 2;
    else
        work.recv_cnt = 1;

    int index = i / GRID_X;
    int prim_seq = 0;

    prim_base *recv_data =
        new Recv_prim(RECV_TYPE::RECV_DATA, work.recv_tag, work.recv_cnt);
    temp_config.push_back(
        Msg(false, MSG_TYPE::CONFIG, ++prim_seq, i, recv_data->serialize()));
    prim_base *set_batch = new Set_batch(status.batchInfo);
    temp_config.push_back(
        Msg(false, MSG_TYPE::CONFIG, ++prim_seq, i, set_batch->serialize()));

    for (auto prim : work.prims) {
        prim_base *set_addr = new_prim("Set_addr");
        auto label = ((Set_addr *)set_addr)->datapass_label;
        for (int i = 0; i < MAX_SPLIT_NUM; i++) {
            label->indata[i] = ((comp_base *)prim)->datapass_label.indata[i];
        }
        label->outdata = ((comp_base *)prim)->datapass_label.outdata;

        temp_config.push_back(
            Msg(false, MSG_TYPE::CONFIG, ++prim_seq, i, set_addr->serialize()));
        temp_config.push_back(
            Msg(false, MSG_TYPE::CONFIG, ++prim_seq, i, prim->serialize()));
    }

    // 每一个核都需要向memInterface发送DONE信号
    prim_base *send_done = new Send_prim(SEND_TYPE::SEND_DONE);
    temp_config.push_back(
        Msg(false, MSG_TYPE::CONFIG, ++prim_seq, i, send_done->serialize()));

    // 处理数据流向下一个core
    int send_dest = i + 1;
    if (send_dest % model_stage == 0)
        send_dest -= model_stage;
    int send_tag = send_dest;

    prim_base *send_req =
        new Send_prim(SEND_TYPE::SEND_REQ, send_dest, send_tag);
    prim_base *recv_ack = new Recv_prim(RECV_TYPE::RECV_ACK);
    Send_prim *send_data =
        new Send_prim(SEND_TYPE::SEND_DATA, send_dest, send_tag);

    int output_size = C * T * B * sizeof(float);
    int pkg_nums = (output_size % M_D_DATA) ? (output_size / M_D_DATA + 1)
                                            : (output_size / M_D_DATA);
    int end_length = output_size - (pkg_nums - 1) * M_D_DATA;

    send_data->max_packet = pkg_nums;
    send_data->end_length = end_length;

    temp_config.push_back(
        Msg(false, MSG_TYPE::CONFIG, ++prim_seq, i, send_req->serialize()));
    temp_config.push_back(
        Msg(false, MSG_TYPE::CONFIG, ++prim_seq, i, recv_ack->serialize()));
    temp_config.push_back(
        Msg(false, MSG_TYPE::CONFIG, ++prim_seq, i, send_data->serialize()));
}