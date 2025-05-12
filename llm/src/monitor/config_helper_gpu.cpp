#include "monitor/config_helper_gpu.h"
#include "common/config.h"
#include "utils/prim_utils.h"
#include "utils/system_utils.h"

config_helper_gpu::config_helper_gpu(string filename, string font_ttf,
                                     int config_chip_id) {
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

    auto config_streams = j["chips"][0]["streams"];
    if (config_streams.size() != 1) {
        cout << "[ERROR] more than 1 stream is not supported." << endl;
        sc_stop();
    }

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
        core.send_global_mem = -1;
        core.repeat = 1;
        core.loop = 1;

        coreconfigs.push_back(core);
    }

    // 处理stream的原语
    for (int i = 0; i < streams.size(); i++) {
        auto stream = streams[i];
        auto prims = stream.prims;

        for (int j = 0; j < prims.size(); j++) {
            gpu_base *prim = (gpu_base *)prims[j];
            int sms = prim->req_sm;

            int cycles = sms / GRID_SIZE;
            int rest = sms - cycles * GRID_SIZE;
            for (int c = 0; c < GRID_SIZE; c++) {
                auto &core = coreconfigs[c];

                CoreJob new_job(1, c, cycles + (c < rest));
                auto prim_copy = prim->clone();
                new_job.prims.push_back(prim_copy);
                core.worklist.push_back(new_job);
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

void config_helper_gpu::fill_queue_config(queue<Msg> *q) {
    for (auto config : coreconfigs) {
        int index = config.id / GRID_X;
        int prim_seq = 0;
        vector<Msg> single_rep;

        for (auto work : config.worklist) {
            // single_rep.push_back(Msg(false, MSG_TYPE::CONFIG, ++prim_seq,
            // config.id, work.prims_in_loop[0]->serialize())); for (auto lcnt =
            // 0; lcnt < work.loop; lcnt++) {
            //     for (auto prim : work.prim_last_loop)
            //         single_rep.push_back(Msg(false, MSG_TYPE::CONFIG,
            //         ++prim_seq, config.id, prim->serialize()));
            // }

            for (auto prim : work.prims_last_loop)
                single_rep.push_back(Msg(false, MSG_TYPE::CONFIG, ++prim_seq,
                                         config.id, prim->serialize()));
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

void config_helper_gpu::generate_prims(int i) {
    CoreConfig *c = &coreconfigs[i];
    cout << i << endl;

    c->worklist[0].prims_last_loop.push_back(
        new Recv_prim(RECV_TYPE::RECV_WEIGHT, c->worklist[0].recv_tag, 0));

    for (auto &work : c->worklist) {
        // 不向in_loop推入任何原语，只操作last_loop
        work.prims_last_loop.push_back(
            new Recv_prim(RECV_TYPE::RECV_DATA, work.recv_tag, work.recv_cnt));

        for (auto prim : work.prims) {
            prim_base *p = new_prim("Set_addr");
            auto label = ((Set_addr *)p)->datapass_label;

            // Set_addr 的label 指向其后面的那条原语
            for (int i = 0; i < MAX_SPLIT_NUM; i++) {
                label->indata[i] = ((gpu_base *)prim)->datapass_label.indata[i];
            }
            label->outdata = ((gpu_base *)prim)->datapass_label.outdata;

            // 这里直接推入字符串形式的label，之后会在序列化的时候转化为整形label
            work.prims_last_loop.push_back(p);
            work.prims_last_loop.push_back(prim);
        }

        work.prims_last_loop.push_back(new Send_prim(SEND_TYPE::SEND_DONE));
    }
}

void config_helper_gpu::calculate_address(bool do_loop) {}

void config_helper_gpu::fill_queue_start(queue<Msg> *q) {
    cout << "GPU fill start queue, phase " << gpu_index << "\n";
    int sms = ((gpu_base *)(streams[0].prims[gpu_index]))->req_sm;
    cout << "her1e\n";

    for (auto stream : streams) {
        for (auto source : stream.sources) {
            AddrPosKey source_key = AddrPosKey(0, source.second);
            gpu_pos_locator->addPair(source.first + "#1", source_key);
        }
    }
    cout << "here\n";

    for (int i = 0; i < min(sms, GRID_SIZE); i++) {
        auto config = coreconfigs[i];
        int index = config.id / GRID_X;     
        int pkg_index = 0;

        sc_bv<128> d(0x1);
        Msg m = Msg(true, MSG_TYPE::S_DATA, pkg_index + 1, config.id, 0,
                    config.id, 0, d);
        m.source = GRID_SIZE;
        q[index].push(m);
    }

    gpu_index++;
}

void config_helper_gpu::print_self() {
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