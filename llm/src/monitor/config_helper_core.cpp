#include "nlohmann/json.hpp"
#include <SFML/Graphics.hpp>

#include "monitor/config_helper_core.h"
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
    json j;
    // cout << "Loading config file3 " << filename << endl;
    // plot_dataflow(filename, font_ttf);
    // cout << "Loading config file2 " << filename << endl;
    ifstream jfile(filename);
    jfile >> j;

    // 收集相关参数
    auto config_vars = j["vars"];
    for (auto var : config_vars.items()) {
        vtable.push_back(make_pair(var.key(), var.value()));
    }

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

    // 初步处理核信息
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

    if (j.contains("sequential")) {
        j.at("sequential").get_to(sequential);
    } else {
        sequential = false;
    }
    seq_index = 0;

    // 检查是否需要复制原语的核，config书写要求：需要重新写明所有work的cast、recv_cnt,数量等同于需要复制的那个核的work数量
    for (int i = 0; i < coreconfigs.size(); i++) {
        if (coreconfigs[i].prim_copy != -1) {
            auto worklist_temp = coreconfigs[i].worklist;
            coreconfigs[i].worklist.clear();
            for (int j = 0; j < worklist_temp.size(); j++) {
                auto prev_job = worklist_temp[j];

                auto target_core = get_core(coreconfigs[i].prim_copy);
                auto target_work = target_core->worklist[j];
                target_work.cast = prev_job.cast;
                target_work.recv_cnt = prev_job.recv_cnt;
                target_work.recv_tag = prev_job.recv_tag;
                coreconfigs[i].worklist.push_back(target_work);
            }
        }
    }

    for (int i = 0; i < coreconfigs.size(); i++) {
        // //std::cout << "Coreiii " << i << " prims: " <<
        // coreconfigs[i].prims.size() << std::endl;
        generate_prims(i);

        for (auto work : coreconfigs[i].worklist) {
            cout << "work\n";
            for (auto prim : work.prims_last_loop) {
                prim->print_self("\t\t");
            }
        }
        cout << "core\n";
    }

    if (sequential)
        end_cores = 1;

    // 再去重新填写send的收发地址
    calculate_address(true);
    calculate_address(false);

    print_self();
}

void config_helper_core::fill_queue_config(queue<Msg> *q) {
    for (auto config : coreconfigs) {
        int index = config.id / GRID_X;
        int prim_seq = 0;
        vector<Msg> single_rep;

        for (auto work : config.worklist) {
            cout << "work:" << endl;
            for (auto lcnt = 0; lcnt < work.loop - 1; lcnt++) {
                cout << "lcnt:" << lcnt << endl;
                for (auto prim : work.prims_in_loop) {
                    cout << "1\n";
                    single_rep.push_back(Msg(false, MSG_TYPE::CONFIG,
                                             ++prim_seq, config.id,
                                             prim->serialize()));
                }
            }

            for (auto prim : work.prims_last_loop)
                single_rep.push_back(Msg(false, MSG_TYPE::CONFIG, ++prim_seq,
                                         config.id, prim->serialize()));
        }

        int single_rep_cnt = prim_seq;

        for (int i = 0; i < config.repeat; i++) {
            for (int j = 1; j <= single_rep.size(); j++) {
                Msg m = single_rep[j - 1];
                m.seq_id = j + prim_seq * i;

                if (i == config.repeat - 1 && j == single_rep.size())
                    m.is_end = true;
                if (m.is_end)
                    m.refill = config.prim_refill;

                q[index].push(m);
            }
        }
    }
}

void config_helper_core::generate_prims(int i) {
    // 处理单个核的原语，将其放入Coreconfig.prims中
    CoreConfig *c = &coreconfigs[i];

    c->worklist[0].prims_in_loop.push_back(
        new Recv_prim(RECV_TYPE::RECV_WEIGHT, c->worklist[0].recv_tag, 0));
    c->worklist[0].prims_last_loop.push_back(
        new Recv_prim(RECV_TYPE::RECV_WEIGHT, c->worklist[0].recv_tag, 0));


    for (int w = 0; w < c->worklist.size(); w++) {
        auto &work = c->worklist[w];
        bool is_end = judge_is_end_work(work); // 是不是计算图中的汇节点
        if (is_end)
            end_cores++;

        // 先生成loop中的原语
        // 首先是recv，对应 RECV_DATA
        work.prims_in_loop.push_back(
            new Recv_prim(RECV_TYPE::RECV_DATA, work.recv_tag, work.recv_cnt));

        // 然后是comp，直接推c中的对应队列即可
        for (auto prim : work.prims) {
            prim_base *p = new_prim("Set_addr");
            auto label = ((Set_addr *)p)->datapass_label;
            // Set_addr 的label 指向其后面的那条原语
            for (int i = 0; i < MAX_SPLIT_NUM; i++) {
                label->indata[i] =
                    ((comp_base *)prim)->datapass_label.indata[i];
            }
            label->outdata = ((comp_base *)prim)->datapass_label.outdata;

            // 这里直接推入字符串形式的label，之后会在序列化的时候转化为整形label
            work.prims_in_loop.push_back(p);
            work.prims_in_loop.push_back(prim);
        }

        // 最后是send，如果是多播的话需要加入多个send原
        // 这里的发送地址和接收地址先不填，等到后续统一填
        // 按照cast 广播的方式添加对应数量的 send 原语数量
        for (int j = 0; j < work.cast.size(); j++) {
            auto ca = work.cast[j];

            // 在sequential情况下，如果dest是-1,同样发送done信号
            if (ca.dest == -1) {
                work.prims_in_loop.push_back(
                    new Send_prim(SEND_TYPE::SEND_DONE));
                work.prims_in_loop.push_back(new_prim("Clear_sram"));
                continue;
            }

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
        work.prims_last_loop.push_back(
            new Recv_prim(RECV_TYPE::RECV_DATA, work.recv_tag, work.recv_cnt));

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

        if (is_end) {
            work.prims_last_loop.push_back(new Send_prim(SEND_TYPE::SEND_DONE));
            work.prims_last_loop.push_back(new_prim("Clear_sram"));
            continue;
        }

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

            // CTODO: 假设平均分配输出
            int output_size = 0;
            int output_offset = 0;
            int index = 0;

            if (!do_loop && judge_is_end_work(work))
                continue; // 汇节点

            // 拿到这个核的output size
            for (int j = v->size() - 1; j >= 0; j--) {
                auto p = (*v)[j];
                if (is_comp_prim(p)) {
                    comp_base *cp = (comp_base *)p;
                    output_size = cp->out_size;
                    output_offset = cp->out_offset;
                    break;
                }
            }

            // cast send 原语每一个有不同的 des_offset
            for (auto &prim : (*v)) {
                if (typeid(*prim) == typeid(Send_prim)) {
                    Send_prim *temp = (Send_prim *)prim;
                    if (temp->type != SEND_DATA)
                        continue;

                    int weight = work.cast[index].weight;
                    int slice_size = (output_size % weight)
                                         ? (output_size / weight + 1)
                                         : (output_size / weight);
                    int slice_size_in_bit = slice_size * sizeof(float);
                    int pkg_nums = (slice_size_in_bit % M_D_DATA)
                                       ? (slice_size_in_bit / M_D_DATA + 1)
                                       : (slice_size_in_bit / M_D_DATA);
                    int end_length =
                        slice_size_in_bit - (pkg_nums - 1) * M_D_DATA;

                    // local offset
                    temp->local_offset = output_offset;
                    if (weight > 1)
                        output_offset += slice_size; // CTODO: fix this

                    // max pkg nums
                    temp->max_packet = pkg_nums;

                    // des offset
                    if (work.cast[index].addr != -1) {
                        temp->des_offset = work.cast[index].addr;
                        cout << "Core " << i << ": work gets actual dram des: "
                             << temp->des_offset << endl;
                    } else {
                        temp->des_offset = delta_offset[temp->des_id];
                        delta_offset[temp->des_id] += slice_size;
                    }

                    // end length
                    temp->end_length = end_length;

                    index++;
                }
            }
        }
    }
}

void config_helper_core::fill_queue_start(queue<Msg> *q) {
    for (int pipe = 0; pipe < pipeline; pipe++) {
        if (sequential == false) {
            for (auto source : source_info) {
                int i = source.first;
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
                        length =
                            size * sizeof(float) - M_D_DATA * (pkg_num - 1);
                    }

                    // CTODO: recv_tag default to i
                    Msg m =
                        Msg(j == pkg_num, MSG_TYPE::S_DATA, j + pkg_index, i,
                            send_offset + M_D_DATA * (j - 1), i, length, d);
                    m.source = GRID_SIZE;
                    q[index].push(m);
                }
            }
        } else { // sequential == true
            auto source = source_info[seq_index];
            int i = source.first;
            int size = source.second;

            int index = i / GRID_X;
            int pkg_index = 0;
            int send_offset = delta_offset[i];
            int send_size_in_bit = size * sizeof(float);
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

            seq_index++;
        }
    }
}