#include "monitor/config_helper_pd.h"
#include "prims/norm_prims.h"
#include "utils/prim_utils.h"
#include "utils/system_utils.h"

config_helper_pd::config_helper_pd(string filename, string font_ttf,
                                   int config_chip_id = 0) {
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

    for (int i = 1; i <= req_cnt; i++) {
        RequestRecord record =
            RequestRecord(i, config_reqs["seq_len"], config_reqs["heads"]);
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

bool config_helper_pd::judge_next_dis_config() {
    return temp_config.size() > 0;
}

bool config_helper_pd::judge_next_dis_start() {
    for (auto status : coreStatus) {
        if (!status.available && !status.data_sent)
            return true;
    }

    return false;
}

void config_helper_pd::fill_queue_start(queue<Msg> *q) {
    for (auto &status : coreStatus) {
        if (status.available || status.data_sent)
            continue;

        int index = status.id / GRID_X;
        int size = 0;
        for (auto request : status.reqs) {
            auto record = requestRecords[request];

            switch (record.phase) {
            case PREFILL:
                size += record.seq_len / record.prefill_iters * heads * 64;
                break;
            case DECODE:
                size += 1 * heads * 64;
                break;
            }
        }

        int send_size_in_bit = size * sizeof(float) * 8;
        int pkg_num = (send_size_in_bit % M_D_DATA)
                          ? (send_size_in_bit / M_D_DATA + 1)
                          : (send_size_in_bit / M_D_DATA);

        for (int j = 1; j <= pkg_num; j++) {
            sc_bv<M_D_DATA> d(0x1);
            int length = M_D_DATA;
            bool is_end_packet = j == pkg_num;
            if (is_end_packet) {
                length = size * sizeof(float) - M_D_DATA * (pkg_num - 1);
            }

            Msg m = Msg(j == pkg_num, MSG_TYPE::S_DATA, j, status.id,
                        M_D_DATA * (j - 1), status.id, length, d);
            m.source = GRID_SIZE;
            q[index].push(m);
        }

        status.data_sent = true;
    }
}

void config_helper_pd::process_core_done(int cid, Msg m) {
    coreStatus[cid].available = true;

    for (int i = 0; i < coreStatus[cid].reqs.size(); i++) {
        auto req = coreStatus[cid].reqs[i];
        auto &record = requestRecords[req];
        record.lock = false;

        switch (record.phase) {
        case PREFILL:
            record.prefill_counter++;
            if (record.prefill_counter == record.prefill_iters) {
                record.phase = DECODE;
            }
            break;
        case DECODE:
            record.decode_counter++;
            if (m.data.range(i, i).to_uint64()) {
                record.phase = PD_DONE;
                decode_done++;
                cout << "[PD] Decode done " << decode_done << "/"
                     << requestRecords.size() << endl;

                if (decode_done == requestRecords.size()) {
                    cout << "[PD] All request finished.\n";
                    sc_stop();
                }
            }
        }
    }
}

void config_helper_pd::schedule() {
    // 检查是否有available的核
    for (auto &status : coreStatus) {
        if (status.available) {
            // 分配新的request batch
            vector<int> new_reqs;
            for (auto req : status.reqs) {
                auto &record = requestRecords[req];
                if (record.phase != PD_DONE) {
                    record.lock = true;
                    new_reqs.push_back(record.id);
                }
            }

            while (new_reqs.size() < CORE_CREDIT) {
                // 添加一个新的request，从prefill开始
                bool has_new = false;
                for (auto &record : requestRecords) {
                    if (record.phase == UNTOUCHED) {
                        record.phase = PREFILL;
                        record.lock = true;
                        new_reqs.push_back(record.id);
                        has_new = true;
                        break;
                    }
                }

                if (!has_new)
                    break;
            }

            // 重写原语，填入temp_config队列中
            status.reqs = new_reqs;
            generate_prims(status.id);
        }
    }
}

void config_helper_pd::generate_prims(int i) {
    // 根据template
    // json组织新的config，在每一个计算原语之间插入set_pd_work原语，用于告知混合batch内容
    auto status = coreStatus[i];
    vector<RequestRecord> records;
    for (auto req : status.reqs) {
        records.push_back(requestRecords[req]);
    }

    BatchInfo batch(records);
    int B = 1, NH = heads, T = 0, C = heads * 64;
    for (auto record : records) {
        switch (record.phase) {
        case PREFILL:
            T += record.seq_len / record.prefill_iters;
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

    int index = i / GRID_X;
    int prim_seq = 0;

    prim_base *recv_data =
        new Recv_prim(RECV_TYPE::RECV_DATA, work.recv_tag, work.recv_cnt);
    temp_config.push_back(
        Msg(false, MSG_TYPE::CONFIG, ++prim_seq, i, recv_data->serialize()));

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

    prim_base *send_done = new Send_prim(SEND_TYPE::SEND_DONE);
    temp_config.push_back(
        Msg(true, MSG_TYPE::CONFIG, ++prim_seq, i, send_done->serialize()));
}