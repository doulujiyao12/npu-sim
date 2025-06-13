#include "nlohmann/json.hpp"
#include <iostream>
#include <map>
#include <queue>
#include <vector>

#include "common/msg.h"
#include "monitor/config_helper_base.h"
#include "defs/global.h"

using json = nlohmann::json;

bool config_helper_base::judge_is_end_core(int i) {
    // 判断一个核是否是汇节点
    CoreConfig c = coreconfigs[i];
    for (auto work : c.worklist) {
        for (auto cast : work.cast) {
            if (cast.dest == -1)
                return true;
        }
    }

    return false;
}

bool config_helper_base::judge_is_end_work(CoreJob work) {
    for (auto cast : work.cast) {
        if (cast.dest == -1)
            return true;
    }

    return false;
}

// 下发权重数据
void config_helper_base::fill_queue_data(queue<Msg> *q) {
    // 根据上述的地址生成策略，核之间的中间结果在input区域较前的位置，而初始生成的data会放在input区域较后的位置
    // dram内容安排如下：（左侧offset更小）
    // | output area | input (last core's output) | input (get from host) |
    // 可以复用delta offset这个map
    for (auto config : coreconfigs) {
        int index = config.id / GRID_X;
        int pkg_index = 0;
        int core_prim_cnt = 0;

#if FAST_WARMUP == 0
        for (auto work : config.worklist) {
            core_prim_cnt += work.prims.size();

            for (auto prim : work.prims) {
                comp_base *cp = (comp_base *)prim;

                int send_offset = cp->data_offset;
                if (send_offset == -1)
                    continue;

                // p_inp_size 是 输入 input的大小
                int send_size = cp->inp_size - cp->p_inp_size;
                int send_size_in_bit = send_size * 8;
                int pkg_num = (send_size_in_bit % M_D_DATA)
                                  ? (send_size_in_bit / M_D_DATA + 1)
                                  : (send_size_in_bit / M_D_DATA);

                for (int j = 1; j <= pkg_num; j++) {
                    // CTODO: 拿到真正的数据
                    sc_bv<M_D_DATA> d(0x1);
                    int length = M_D_DATA;
                    bool is_end_packet = j == pkg_num;
                    if (is_end_packet) {
                        length = send_size * 8 - M_D_DATA * (pkg_num - 1);
                    }

                    Msg m = Msg(false, MSG_TYPE::P_DATA, j + pkg_index,
                                config.id, send_offset + M_D_DATA * (j - 1),
                                (1 << M_D_TAG_ID) - 1, length, d);
                    m.source = GRID_SIZE;
                    q[index].push(m);
                }

                pkg_index += pkg_num;
                // cout << "prim " << prim->name << " send " << pkg_num << "
                // packages, now total " << pkg_index << " packages.\n";
            }
        }
        cout << "core " << config.id << " send " << core_prim_cnt
             << " prims.\n";
#else
        // do nothing
#endif

        // HOST DATA END 包
        sc_bv<128> d(0x1);
        // Msg(bool e, MSG_TYPE m, int seq, int des, int offset, int tag, int
        // length, sc_bv<128> d) : is_end(e), msg_type(m), seq_id(seq),
        // des(des), offset(offset), tag_id(tag), length(length), data(d) {} (1
        // << M_D_TAG_ID) - 1 已被弃用 P_DATA 包的 tag
        // 不会被用于router中的lock，默认最大tag_id (1 << 16) - 1 end 包的
        // offset 弃用
        Msg m = Msg(true, MSG_TYPE::P_DATA, pkg_index + 1, config.id,
                    (1 << 16) - 1, (1 << M_D_TAG_ID) - 1, 0, d);
        m.source = GRID_SIZE;
        q[index].push(m);

        cout << "core " << config.id << " send " << pkg_index + 1
             << " data packages.\n";
    }
}

void config_helper_base::set_hw_config(string filename) {
    json j;

    ifstream jfile(filename);
    if (!jfile.is_open()) {
        cout << "[ERROR] Failed to open file " << filename << ".\n";
        sc_stop();
    }

    jfile >> j;

    auto config_cores = j["cores"];
    if (config_cores.size() != GRID_SIZE) {
        cout << "[ERROR] Core number mismatch in core hw config and "
                "GRID_SIZE.\n";
        sc_stop();
    }

    for (auto core : config_cores) {
        CoreHWConfig c = core;
        tile_exu.push_back(
            make_pair(c.id, new ExuConfig(MAC_Array, c.exu_x, c.exu_y)));
        tile_sfu.push_back(make_pair(c.id, new SfuConfig(Linear, c.sfu_x)));
        mem_sram_bw.push_back(make_pair(c.id, c.sram_bitwidth));
    }
}