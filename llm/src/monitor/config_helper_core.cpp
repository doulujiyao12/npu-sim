#include "nlohmann/json.hpp"
#include <SFML/Graphics.hpp>
#include <sstream>

#include "monitor/config_helper_core.h"
#include "prims/moe_base.h"
#include "utils/display_utils.h"
#include "utils/prim_utils.h"
#include "utils/system_utils.h"

using json = nlohmann::json;

CoreConfig *config_helper_core::get_core(int id) {
    for (int i = 0; i < coreconfigs.size(); i++) {
        if (coreconfigs[i].id == id)
            return &(coreconfigs[i]);
    }

    cout << "Error: core id " << id << " not found!" << endl;
    sc_stop();

    return NULL;
}

void config_helper_core::print_self() {
    for (auto core : coreconfigs) {
        cout << "[Core " << core.id << "]\n";

        cout << "\tCore prims: \n";
        for (auto work : core.worklist) {
            cout << "IN LOOP\n";
            for (auto prim : work.prims_in_loop) {
                prim->print_self("\t\t");
            }
            cout << "LAST LOOP\n";
            for (auto prim : work.prims_last_loop) {
                prim->print_self("\t\t");
            }
        }
    }

    cout << "\n\n------------------------------------------------------------"
            "\n\n";

    for (auto core : coreconfigs) {
        cout << "[Core " << core.id << "]\n";

        cout << "\tCore cast: \n";
        for (auto work : core.worklist) {
            for (auto cast : work.cast) {
                cout << "\t-> " << cast.dest << ", weight = " << cast.weight
                     << (cast.loopout == FALSE
                             ? (" (loopout: FALSE)")
                             : (cast.loopout == TRUE ? (" (loopout: TRUE)")
                                                     : (" (loopout: BOTH)")))
                     << endl;
            }
            cout << "Work recv_cnt: " << work.recv_cnt << endl;
        }
    }
}

void config_helper_core::random_core(string font_ttf) {
    int o2r[GRID_SIZE];
    int r2o[GRID_SIZE];
    for (int i = 0; i < GRID_SIZE; i++) {
        o2r[i] = r2o[i] = -1;
    }

    std::srand(std::time(nullptr));
    for (auto config : coreconfigs) {
        int id = config.id;
        int rand = 0;
        do {
            rand = std::rand() % GRID_SIZE;
        } while (r2o[rand] != -1);
        o2r[id] = rand;
        r2o[rand] = id;
        cout << "id " << id << " -> rand " << rand << endl;
    }

    // 改写
    for (auto &config : coreconfigs) {
        int oid = config.id;
        config.id = o2r[oid];
        if (config.prim_copy != -1)
            config.prim_copy = o2r[config.prim_copy];

        if (config.send_global_mem != -1)
            config.send_global_mem = o2r[config.send_global_mem];

        for (auto &work : config.worklist) {
            if (work.recv_tag < GRID_SIZE)
                work.recv_tag = o2r[oid];
            for (auto &cast : work.cast) {
                if (cast.tag < GRID_SIZE && cast.dest >= 0)
                    cast.tag = o2r[cast.tag];
                if (cast.dest < GRID_SIZE && cast.dest >= 0)
                    cast.dest = o2r[cast.dest];
            }
        }

        config.print_self();
    }

    // 改写source
    for (auto &pair : source_info) {
        pair.first = o2r[pair.first];
    }

    // 重新绘图
    set<int> source_ids;
    for (auto &pair : source_info) {
        source_ids.emplace(pair.first);
    }

    unordered_map<int, Display::Core> cores;
    int core_id = 0;
    for (const auto &core_j : coreconfigs) {
        Display::Core core;
        core.id = core_j.id;
        core.x = core.id % GRID_X; // X 坐标
        core.y = core.id / GRID_X; // Y 坐标

        // 提取每个 core 的 dest 信息
        for (const auto &work : core_j.worklist) {
            vector<int> temp_cast;
            for (const auto &cast : work.cast) {
                if (cast.critical) {
                    int d = cast.dest;
                    temp_cast.push_back(1e5 + d);
                } else
                    temp_cast.push_back(cast.dest);
            }

            core.dests.push_back(temp_cast);
        }

        cores[core.id] = core;
        core_id++;
    }

    plot_dataflow(cores, source_ids, font_ttf);
}

config_helper_core::config_helper_core(string filename, string font_ttf,
                                       int config_chip_id) {
    cout << "Loading config file " << filename << endl;
    plot_dataflow(filename, font_ttf);
    ifstream jfile(filename);
    if (!jfile.is_open()) {
        cout << "[ERROR] Cannot open config file " << filename << endl;
        sc_stop();
    }

    json j;
    try {
        jfile >> j;
    } catch (const json::parse_error &e) {
        std::cerr << "Parse error: " << e.what() << std::endl;
    }

    // 收集相关参数
    auto config_vars = j["vars"];
    for (auto var : config_vars.items()) {
        vtable.push_back(make_pair(var.key(), var.value()));
    }

    if (config_vars.contains("B"))
        batch_size = config_vars["B"];
    else
        batch_size = 1;

    if (config_vars.contains("T"))
        seq_len = config_vars["T"];
    else
        seq_len = 128;

    auto config_source = j["source"];
    for (auto source : config_source) {
        if (source.contains("loop")) {
            int loop_cnt = find_var(source["loop"]);
            for (int i = 0; i < loop_cnt; i++) {
                source_info.push_back(
                    make_pair(source["dest"], find_var(source["size"])));
            }
        } else {
            source_info.push_back(
                make_pair(source["dest"], find_var(source["size"])));
        }
    }

    auto config_cores = j["chips"][config_chip_id]["cores"];
    for (int i = 0; i < config_cores.size(); i++) {
        // 调用 config_helper_base中的from_json
        CoreConfig core = config_cores[i]; // 这里不直接转化prims
        coreconfigs.push_back(core);
    }

    if (j.contains("random") && j["random"]) {
        random_core(font_ttf);
    }

    if (j.contains("pipeline")) {
        j.at("pipeline").get_to(pipeline);
    } else {
        pipeline = 1;
    }

    // 检查是否需要复制原语的核，config书写要求：需要重新写明所有work的cast、recv_cnt,数量等同于需要复制的那个核的work数量
    for (int i = 0; i < coreconfigs.size(); i++) {
        if (coreconfigs[i].prim_copy != -1) {
            auto worklist_temp = coreconfigs[i].worklist;
            coreconfigs[i].worklist.clear();
            for (int j = 0; j < worklist_temp.size(); j++) {
                auto prev_job = worklist_temp[j];

                auto target_core = get_core(coreconfigs[i].prim_copy);
                auto target_work = target_core->worklist[j];
                for (int c = 0; c < prev_job.cast.size(); c++) {
                    if (c >= target_work.cast.size()) {
                        target_work.cast.push_back(prev_job.cast[c]);
                        continue;
                    }
                    if (target_work.cast[c].tag == target_work.cast[c].dest ||
                        prev_job.cast[c].tag != prev_job.cast[c].dest)
                        target_work.cast[c].tag = prev_job.cast[c].tag;
                    target_work.cast[c].dest = prev_job.cast[c].dest;
                    target_work.cast[c].loopout = prev_job.cast[c].loopout;
                }
                target_work.recv_cnt = prev_job.recv_cnt;
                if (target_work.recv_tag == coreconfigs[i].prim_copy ||
                    prev_job.recv_tag != coreconfigs[i].id)
                    target_work.recv_tag = prev_job.recv_tag;
                coreconfigs[i].worklist.push_back(target_work);
            }
        }
    }

    end_cores = 0;
    g_recv_ack_cnt = 0;
    g_recv_done_cnt = 0;

    for (int i = 0; i < coreconfigs.size(); i++) {
        // //std::cout << "Coreiii " << i << " prims: " <<
        // coreconfigs[i].prims.size() << std::endl;
        generate_prims(i);

        // for (auto work : coreconfigs[i].worklist) {
        //     cout << "work\n";
        //     for (auto prim : work.prims_last_loop) {
        //         prim->print_self("\t\t");
        //     }
        // }
        // cout << "core\n";
    }

    // 再去重新填写send的收发地址
    calculate_address(true);
    calculate_address(false);

    // print_self();
}

void config_helper_core::fill_queue_config(queue<Msg> *q) {
    for (auto config : coreconfigs) {
        int index = config.id / GRID_X;
        vector<Msg> single_rep_in_loop;
        vector<Msg> single_rep_next_loop; // 存放第二个loop开始的原语
        vector<Msg> single_rep_last_loop;

        for (auto work : config.worklist) {
            for (auto prim : work.prims_in_loop) {
                single_rep_in_loop.push_back(Msg(false, MSG_TYPE::CONFIG,
                                                 single_rep_in_loop.size() + 1,
                                                 config.id, prim->serialize()));
            }

            for (auto prim : work.prims_in_loop) {
                if (typeid(*prim) == typeid(Recv_prim)) {
                    Recv_prim *recv_prim = (Recv_prim *)prim;
                    if (recv_prim->type == RECV_TYPE::RECV_START)
                        recv_prim->type = RECV_DATA;
                }

                single_rep_next_loop.push_back(Msg(
                    false, MSG_TYPE::CONFIG, single_rep_next_loop.size() + 1,
                    config.id, prim->serialize()));
            }

            for (auto prim : work.prims_last_loop)
                single_rep_last_loop.push_back(Msg(
                    false, MSG_TYPE::CONFIG, single_rep_last_loop.size() + 1,
                    config.id, prim->serialize()));
        }

        prim_base *recv_weight = new Recv_prim(RECV_TYPE::RECV_WEIGHT,
                                               config.worklist[0].recv_tag, 0);
        q[index].push(Msg(false, MSG_TYPE::CONFIG, 1, config.id,
                          recv_weight->serialize()));

        // 组装要处理的req信息，在此根据B简单判断即可
        vector<Stage> batchInfo;
        for (int i = 0; i < batch_size; i++)
            batchInfo.push_back(Stage(i + 1, PREFILL, seq_len));

        prim_base *set_batch = new Set_batch(batchInfo, true);

        cout << "core " << config.id << ", loop: " << config.loop << endl;
        int seq_cnt = 2;

        for (int i = 0; i < config.loop - 1; i++) {
            q[index].push(Msg(false, MSG_TYPE::CONFIG, seq_cnt++, config.id,
                              set_batch->serialize()));
            if (i == 0) {
                for (int j = 1; j <= single_rep_in_loop.size(); j++) {
                    Msg m = single_rep_in_loop[j - 1];
                    m.seq_id = seq_cnt++;
                    q[index].push(m);
                }
            } else {
                for (int j = 1; j <= single_rep_next_loop.size(); j++) {
                    Msg m = single_rep_next_loop[j - 1];
                    m.seq_id = seq_cnt++;
                    q[index].push(m);
                }
            }
        }

        for (int j = 1; j <= single_rep_last_loop.size(); j++) {
            q[index].push(Msg(false, MSG_TYPE::CONFIG, seq_cnt++, config.id,
                              set_batch->serialize()));

            Msg m = single_rep_last_loop[j - 1];
            m.seq_id = seq_cnt++;
            m.refill = m.is_end = j == single_rep_last_loop.size();

            q[index].push(m);
        }
    }
}

void config_helper_core::generate_prims(int i) {
    // 处理单个核的原语，将其放入Coreconfig.prims中
    CoreConfig *c = &coreconfigs[i];

    bool is_source = false;
    for (auto source : source_info) {
        if (source.first == c->id) {
            is_source = true;
            break;
        }
    }

    for (int w = 0; w < c->worklist.size(); w++) {
        auto &work = c->worklist[w];
        bool is_end = judge_is_end_work(work); // 是不是计算图中的汇节点
        if (is_end)
            end_cores++;

        // 先生成loop中的原语
        // 首先是recv，对应 RECV_DATA
        if (is_source && w == 0)
            work.prims_in_loop.push_back(new Recv_prim(
                RECV_TYPE::RECV_START, work.recv_tag, work.recv_cnt));
        else
            work.prims_in_loop.push_back(new Recv_prim(
                RECV_TYPE::RECV_DATA, work.recv_tag, work.recv_cnt));

        // 然后是comp，直接推c中的对应队列即可
        for (auto prim : work.prims) {
            prim_base *p = new_prim("Set_addr");
            auto label = ((Set_addr *)p)->datapass_label;
            // Set_addr 的label 指向其后面的那条原语
            if (prim->prim_type == COMP_PRIM) {
                for (int i = 0; i < MAX_SPLIT_NUM; i++) {
                    label->indata[i] =
                        ((comp_base *)prim)->datapass_label.indata[i];
                }
                label->outdata = ((comp_base *)prim)->datapass_label.outdata;
            } else if (prim->prim_type == MOE_PRIM) {
                for (int i = 0; i < MAX_SPLIT_NUM; i++) {
                    label->indata[i] =
                        ((moe_base *)prim)->datapass_label.indata[i];
                    cout << "Core " << c->id << " moe " << i << " "
                         << label->indata[i] << endl;
                }
                label->outdata = ((moe_base *)prim)->datapass_label.outdata;
            } else if (prim->prim_type == PD_PRIM) {
                for (int i = 0; i < MAX_SPLIT_NUM; i++) {
                    label->indata[i] =
                        ((pd_base *)prim)->datapass_label.indata[i];
                }
                label->outdata = ((pd_base *)prim)->datapass_label.outdata;
            }

            // 这里直接推入字符串形式的label，之后会在序列化的时候转化为整形label
            work.prims_in_loop.push_back(p);
            work.prims_in_loop.push_back(prim);
        }

        // // [传输global memory的原语]
        // if (c->send_global_mem != -1){
        //     work.prims_in_loop.push_back(new Send_global_memory());
        // }

        // 最后是send，如果是多播的话需要加入多个send原语
        // 这里的发送地址和接收地址先不填，等到后续统一填
        // 按照cast 广播的方式添加对应数量的 send 原语数量
        for (int j = 0; j < work.cast.size(); j++) {
            auto ca = work.cast[j];

            // 只需要在末尾循环发送，默认BOTH
            if (ca.loopout == TRUE)
                continue;

            int dest = ca.dest;
            int tag = ca.tag;

            work.prims_in_loop.push_back(
                new Send_prim(SEND_TYPE::SEND_REQ, dest, tag));
            work.prims_in_loop.push_back(new Recv_prim(RECV_TYPE::RECV_ACK));
            work.prims_in_loop.push_back(
                new Send_prim(SEND_TYPE::SEND_DATA, dest, tag));
        }


        // 再生成最后一个loop的原语
        if (is_source && w == 0 && c->loop == 1)
            work.prims_last_loop.push_back(new Recv_prim(
                RECV_TYPE::RECV_START, work.recv_tag, work.recv_cnt));
        else
            work.prims_last_loop.push_back(new Recv_prim(
                RECV_TYPE::RECV_DATA, work.recv_tag, work.recv_cnt));

        for (auto prim : work.prims) {
            // 在Set_addr里面复制一份计算原语的datapass_label
            prim_base *p = new_prim("Set_addr");
            auto label = ((Set_addr *)p)->datapass_label;
            for (int i = 0; i < MAX_SPLIT_NUM; i++) {
                label->indata[i] =
                    ((comp_base *)prim)->datapass_label.indata[i];
            }
            label->outdata = ((comp_base *)prim)->datapass_label.outdata;

            // 这里直接推入字符串形式的label，之后会在序列化的时候转化为整形label
            work.prims_last_loop.push_back(p);
            work.prims_last_loop.push_back(prim);
        }

        // if (c->send_global_mem != -1){
        //     work.prims_last_loop.push_back(new Send_global_memory());
        // }

        if (is_end) {
            work.prims_last_loop.push_back(new Send_prim(SEND_TYPE::SEND_DONE));
            work.prims_last_loop.push_back(new_prim("Clear_sram"));
            continue;
        }


        // if (is_end) {
        //     if (c->send_global_mem != -1) {
        //         work.prims_last_loop.push_back(new Send_global_memory());
        //     } else {
        //         work.prims_last_loop.push_back(new
        //         Send_prim(SEND_TYPE::SEND_DONE));
        //     }
        //     work.prims_last_loop.push_back(new_prim("Clear_sram"));
        //     continue;
        // }

        for (int j = 0; j < work.cast.size(); j++) {
            auto ca = work.cast[j];
            // 只在末尾循环不需要发送，默认BOTH
            if (ca.loopout == FALSE)
                continue;

            int dest = ca.dest;
            int tag = ca.tag;

            work.prims_last_loop.push_back(
                new Send_prim(SEND_TYPE::SEND_REQ, dest, tag));
            work.prims_last_loop.push_back(new Recv_prim(RECV_TYPE::RECV_ACK));
            work.prims_last_loop.push_back(
                new Send_prim(SEND_TYPE::SEND_DATA, dest, tag));
        }

        // 清理sram
        if (w == c->worklist.size() - 1) {
            work.prims_last_loop.push_back(new_prim("Clear_sram"));
        }
    }
}

void config_helper_core::calculate_address(bool do_loop) {
    // 遍历每一个核的prim，填写其中原语的收发地址
    for (int i = 0; i < coreconfigs.size(); i++) {
        // 初始化
        // id 表示实际的core id
        delta_offset[coreconfigs[i].id] =
            ((comp_base *)coreconfigs[i].worklist[0].prims[0])->inp_offset;
    }

    // 自动设置 send 和 receive 的地址
    for (int i = 0; i < coreconfigs.size(); i++) {
        for (auto &work : coreconfigs[i].worklist) {
            // 遍历每一个核中的send原语
            vector<prim_base *> *v = nullptr;
            if (do_loop)
                v = &(work.prims_in_loop);
            else
                v = &(work.prims_last_loop);

            int output_size = 0;
            int output_offset = 0;
            int index = 0;
            string output_label = "";

            if (!do_loop && judge_is_end_work(work))
                continue; // 汇节点

            // 拿到这个核的output size
            for (int j = v->size() - 1; j >= 0; j--) {
                auto p = (*v)[j];
                if (p->prim_type == COMP_PRIM) {
                    comp_base *cp = (comp_base *)p;
                    output_size = cp->out_size;
                    // output_offset = cp->out_offset;
                    output_label = cp->datapass_label.outdata;
                    break;
                }
            }

            vector<string> output_label_split;
            stringstream ss(output_label);
            string word;

            while (ss >> word)
                output_label_split.push_back(word);

            for (auto &prim : (*v)) {
                if (typeid(*prim) == typeid(Send_prim)) {
                    Send_prim *temp = (Send_prim *)prim;
                    if (temp->type != SEND_DATA)
                        continue;

                    int weight = work.cast[index].weight;
                    int slice_size = (output_size % weight)
                                         ? (output_size / weight + 1)
                                         : (output_size / weight);
                    cout << "[SIZE CHECK] name: " << prim->name << endl;
                    cout << "output_size: " << output_size
                         << ", slice_size: " << slice_size << endl;
                    int slice_size_in_bit = slice_size * prim->datatype * 8;
                    int pkg_nums = (slice_size_in_bit % M_D_DATA)
                                       ? (slice_size_in_bit / M_D_DATA + 1)
                                       : (slice_size_in_bit / M_D_DATA);
                    int end_length =
                        slice_size_in_bit - (pkg_nums - 1) * M_D_DATA;

                    // max pkg nums
                    temp->max_packet = pkg_nums % CORE_COMM_PAYLOAD
                                           ? pkg_nums / CORE_COMM_PAYLOAD + 1
                                           : pkg_nums / CORE_COMM_PAYLOAD;
                    cout << "max_packet: " << temp->max_packet
                         << ", CORECOMM: " << CORE_COMM_PAYLOAD
                         << ", pkg_nums: " << pkg_nums << endl;
                    if (pkg_nums == 0) {
                        cout << "weight " << weight << " slice size "
                             << slice_size << " slice size in bit "
                             << slice_size_in_bit << " pkg nums " << pkg_nums
                             << " end length " << end_length << endl;
                    }
                    temp->output_label = output_label_split.size() == 1
                                             ? output_label_split[0]
                                             : output_label_split[index];
                    temp->end_length = end_length;

                    // des offset
                    // if (work.cast[index].addr != -1) {
                    //     temp->des_offset = work.cast[index].addr;
                    //     cout << "Core " << i << ": work gets actual dram des:
                    //     "
                    //          << temp->des_offset << endl;
                    // } else {
                    //     temp->des_offset = delta_offset[temp->des_id];
                    //     delta_offset[temp->des_id] += slice_size;
                    // }

                    index++;
                } else if (typeid(*prim) == typeid(Send_global_memory)) {
                    int weight = 1; // 先假设是-1
                    Send_global_memory *g = (Send_global_memory *)prim;
                    g->type = GLOBAL_SEND_DATA;
                    g->des_id = coreconfigs[i].send_global_mem;
                    int slice_size = (output_size % weight)
                                         ? (output_size / weight + 1)
                                         : (output_size / weight);
                    int slice_size_in_bit = slice_size * 8;
                    int pkg_nums = (slice_size_in_bit % M_D_DATA)
                                       ? (slice_size_in_bit / M_D_DATA + 1)
                                       : (slice_size_in_bit / M_D_DATA);
                    int end_length =
                        slice_size_in_bit - (pkg_nums - 1) * M_D_DATA;
                    g->local_offset = output_offset;
                    g->max_packet = pkg_nums;
                    g->end_length = end_length;
                    g->des_offset = delta_offset[g->des_id];
                    delta_offset[g->des_id] += slice_size;
                    g->tag_id = 15;
                }
            }
        }
    }
}

void config_helper_core::fill_queue_start(queue<Msg> *q) {
    for (int pipe = 0; pipe < pipeline; pipe++) {
        for (auto source : source_info) {
            int i = source.first;
            cout << "Sending source to " << i << endl;
            int size = source.second;

            int index = i / GRID_X;
            int pkg_index = 0;
            int send_offset = delta_offset[i];
            int send_size_in_bit = size * sizeof(float) * 8;
            int pkg_num = (send_size_in_bit % M_D_DATA)
                              ? (send_size_in_bit / M_D_DATA + 1)
                              : (send_size_in_bit / M_D_DATA);

            for (int j = 1; j <= pkg_num; j++) {
                // CTODO: 拿到真正的数据
                sc_bv<M_D_DATA> d(0x1);
                int length = M_D_DATA;
                bool is_end_packet = j == pkg_num;
                if (is_end_packet) {
                    length = size * sizeof(float) - M_D_DATA * (pkg_num - 1);
                }

                // CTODO: recv_tag default to i
                Msg m = Msg(j == pkg_num, MSG_TYPE::S_DATA, j + pkg_index, i,
                            send_offset + M_D_DATA * (j - 1), i, length, d);
                m.source = GRID_SIZE;
                q[index].push(m);
            }
        }
    }
}

void config_helper_core::parse_ack_msg(Event_engine *event_engine, int flow_id,
                                       sc_event *notify_event) {
    event_engine->add_event(this->name(), "Waiting Recv Ack", "B",
                            Trace_event_util());

    for (auto m : g_temp_ack_msg) {
        int cid = m.source;
        cout << sc_time_stamp()
             << ": Config helper DATAFLOW: received ack packet from " << cid
             << ". total " << g_recv_ack_cnt + 1 << "/" << coreconfigs.size()
             << ".\n";

        g_recv_ack_cnt++;
    }

    g_temp_ack_msg.clear();
    event_engine->add_event(this->name(), "Waiting Recv Ack", "E",
                            Trace_event_util());


    if (g_recv_ack_cnt >= coreconfigs.size()) {
        notify_event->notify(CYCLE, SC_NS);
        g_recv_ack_cnt = 0;

        // 使用唯一的flow ID替换名称
        std::string flow_name = "flow_" + std::to_string(flow_id);
        event_engine->add_event(this->name(), "Waiting Recv Ack", "f",
                                Trace_event_util(flow_name), sc_time(0, SC_NS),
                                100, "e");
        cout << "Config helper DATAFLOW: received all ack packets.\n";
    }
}

void config_helper_core::parse_done_msg(Event_engine *event_engine,
                                        sc_event *notify_event) {
    notify_event = nullptr; // 无需触发任何信号
    event_engine->add_event(this->name(), "Waiting Core busy", "B",
                            Trace_event_util());

    for (auto m : g_temp_done_msg) {
        int cid = m.source;
        cout << sc_time_stamp()
             << ": Config helper DATAFLOW: received done packet from " << cid
             << ", total " << g_recv_done_cnt + 1 << ".\n";

        g_recv_done_cnt++;
        // g_done_msg.push_back(m);
    }
    g_temp_done_msg.clear();
    event_engine->add_event(this->name(), "Waiting Core busy", "E",
                            Trace_event_util());

    cout << "g_recv_done_cnt " << g_recv_done_cnt << " end " << end_cores
         << " total pipe " << pipeline << endl;

    if (g_recv_done_cnt >= end_cores * pipeline) {
        cout << "Config helper DATAFLOW: all work done, end_core: " << end_cores
             << ", recv_cnt: " << g_recv_done_cnt << endl;

        g_recv_done_cnt = 0;
        cout << "[CATCH TEST] " << sc_time_stamp() << endl;
        sc_stop();
    }
}