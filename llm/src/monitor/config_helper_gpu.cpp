#include "monitor/config_helper_gpu.h"
#include "common/config.h"
#include "utils/prim_utils.h"
#include "utils/system_utils.h"

Config_helper_gpu::Config_helper_gpu(string filename, string font_ttf) {
    cout << "Loading config file " << filename << endl;
    json j;
    // plot_dataflow(filename, font_ttf);
    ifstream jfile(filename);
    jfile >> j;

    // 收集相关参数
    auto config_vars = j["vars"];
    for (auto var : config_vars.items()) {
        vtable.push_back(make_pair(var.key(), var.value()));
    }

    auto config_streams = j["streams"];
    if (config_streams.size() != 1) {
        cout << "[ERROR] more than 1 stream is not supported." << endl;
        sc_stop();
    }

    vector<StreamConfig> streams;
    for (int i = 0; i < config_streams.size(); i++) {
        StreamConfig stream = config_streams[i];
        streams.push_back(stream);
    }

    // 将stream的原语放入coreconfigs中
    for (int i = 0; i < GRID_SIZE; i++) {
        CoreConfig core;
        core.id = i;
        core.prim_refill = false;
        core.prim_copy = -1;
        core.repeat = 1;
        core.loop = 1;

        core.worklist.resize(1);
        core.worklist[0].recv_cnt = 1;
        core.worklist[0].recv_tag = i;
        core.worklist[0].loop = 1;

        coreconfigs.push_back(core);
    }

    // 处理stream的原语
    for (int i = 0; i < streams.size(); i++) {
        auto stream = streams[i];
        auto prims = stream.prims;

        for (int j = 0; j < prims.size(); j++) {
            gpu_base *prim = (gpu_base *)prims[j];
            int sms = prim->grid_x * prim->grid_y / (CORE_PER_SM / (prim->block_x * prim->block_y));

            int cycles = ceiling_division(sms, GRID_SIZE);
            cout << "sms size, cycle: " << sms << " " << GRID_SIZE << " " << cycles << endl;
            for (int k = 0; k < GRID_SIZE; k++) {
                auto core = coreconfigs[k];
                auto work = core.worklist[0];

                for (int c = 0; c < cycles; c++) {
                    auto prim_copy = prim->clone();
                    prim_copy->mock = c * GRID_SIZE + k < sms;

                    coreconfigs[k].worklist[0].prims.push_back(prim_copy);
                }
            }
        }
    }

    // 处理收发原语（启动）
    for (int i = 0; i < coreconfigs.size(); i++) {
        generate_prims(i);
    }

    end_cores = GRID_SIZE;
    pipeline = 1;
    sequential = false;

    print_self();
}

void Config_helper_gpu::fill_queue_config(queue<Msg> *q) {
    for (auto config : coreconfigs) {
        int index = config.id / GRID_X;
        int prim_seq = 0;
        vector<Msg> single_rep;

        for (auto work : config.worklist) {
            single_rep.push_back(Msg(false, MSG_TYPE::CONFIG, ++prim_seq, config.id, work.prims_in_loop[0]->serialize()));
            for (auto lcnt = 0; lcnt < work.loop; lcnt++) {
                for (auto prim : work.prims)
                    single_rep.push_back(Msg(false, MSG_TYPE::CONFIG, ++prim_seq, config.id, prim->serialize()));
            }

            for (auto prim : work.prims_last_loop)
                single_rep.push_back(Msg(false, MSG_TYPE::CONFIG, ++prim_seq, config.id, prim->serialize()));
        }

        int single_rep_cnt = prim_seq;
        cout << "single size " << single_rep.size() << endl;

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

void Config_helper_gpu::generate_prims(int i) {
    CoreConfig *c = &coreconfigs[i];

    auto &work = c->worklist[0];
    work.prims_in_loop.push_back(new Recv_prim(RECV_TYPE::RECV_DRAM, work.recv_tag, work.recv_cnt));

    work.prims_last_loop.push_back(new Send_prim(SEND_TYPE::SEND_DONE));
}

void Config_helper_gpu::calculate_address(bool do_loop) {}

void Config_helper_gpu::fill_queue_start(queue<Msg> *q) {
    for (auto config : coreconfigs) {
        int index = config.id / GRID_X;
        int pkg_index = 0;

        sc_bv<128> d(0x1);
        Msg m = Msg(true, MSG_TYPE::S_DATA, pkg_index + 1, config.id, 0, config.id, 0, d);
        m.source = GRID_SIZE;
        q[index].push(m);
    }
}

void Config_helper_gpu::print_self() {
    for (auto core : coreconfigs) {
        cout << "[Core " << core.id << "]\n";

        cout << "\tCore prims: \n";
        for (auto work : core.worklist) {
            for (auto prim : work.prims_in_loop) {
                prim->print_self("\t\t");
            }
            for (auto prim : work.prims) {
                prim->print_self("\t\t");
            }
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
                cout << "\t-> " << cast.dest << ", weight = " << cast.weight << (cast.loopout == FALSE ? (" (loopout: FALSE)") : (cast.loopout == TRUE ? (" (loopout: TRUE)") : (" (loopout: BOTH)")))
                     << endl;
            }
            cout << "Work recv_cnt: " << work.recv_cnt << endl;
        }
    }
}