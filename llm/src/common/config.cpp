#include "common/config.h"
#include "common/msg.h"
#include "utils/prim_utils.h"
#include "utils/system_utils.h"

void CoreJob::print_self() {
    for (auto cast : cast) {
        cout << "{\n";
        cout << "\tdest: " << cast.dest << endl;
        cout << "\ttag: " << cast.tag << endl;
        cout << "\tweight: " << cast.weight << endl;
        cout << "\taddr: " << cast.addr << endl;
        cout << "}\n";
    }

    cout << "recv_cnt: " << recv_cnt << endl;
    cout << "recv_tag: " << recv_tag << endl;
}

void CoreConfig::print_self() {
    cout << "Core id: " << id << endl;
    cout << "\tprim_copy: " << prim_copy << endl;
    cout << "\tsend_global_mem: " << send_global_mem << endl;
    cout << "\tloop: " <<  " " << loop << endl;
    for (auto work : worklist) {
        work.print_self();
    }
}

void from_json(const json &j, Cast &c) {
    j.at("dest").get_to(c.dest);

    if (!j.contains("tag"))
        c.tag = c.dest;
    else
        j.at("tag").get_to(c.tag);

    if (!j.contains("weight"))
        c.weight = 1;
    else
        j.at("weight").get_to(c.weight);

    if (!j.contains("addr"))
        c.addr = -1;
    else
        j.at("addr").get_to(c.addr);

    if (!j.contains("loopout"))
        c.loopout = BOTH;
    else {
        if (j.at("loopout") == "false")
            c.loopout = FALSE;
        else if (j.at("loopout") == "true")
            c.loopout = TRUE;
        else
            c.loopout = BOTH;
    }

    if (!j.contains("critical"))
        c.critical = false;
    else
        j.at("critical").get_to(c.critical);
}

void from_json(const json &j, CoreJob &c) {
    if (j.contains("cast")) {
        for (int i = 0; i < j["cast"].size(); i++) {
            Cast temp = j["cast"][i];
            c.cast.push_back(temp);
        }
    } else {
        // cout << "[WARN] You need to designate CAST field unless you are running SIM_PD.\n";
    } // NOTE: 如果配置文件中没有cast，需要手动指派。

    j.at("recv_cnt").get_to(c.recv_cnt);

    if (!j.contains("recv_tag"))
        c.recv_tag = 0;
    else
        j.at("recv_tag").get_to(c.recv_tag);

    if (j.contains("prims")) {
        auto prims = j["prims"];
        for (auto prim : prims) {
            comp_base *p = nullptr;
            string type = prim.at("type");

            p = (comp_base *)new_prim(type);
            p->parse_json(prim);

            // 是否使用硬件
            if (prim.contains("use_hw")) {
                prim.at("use_hw").get_to(p->use_hw);
            } else {
                p->use_hw = false;
            }

            c.prims.push_back((prim_base *)p);
        }
    }
}

void from_json(const json &j, CoreConfig &c) {
    j.at("id").get_to(c.id);

    if (j.contains("prim_copy")) {
        j.at("prim_copy").get_to(c.prim_copy);
    } else {
        c.prim_copy = -1;
    }

    if (j.contains("send_global_mem")) {
        j.at("send_global_mem").get_to(c.send_global_mem);
    } else {
        c.send_global_mem = -1;
    }

    if (j.contains("loop")) {
        c.loop = find_var(j["loop"]);
    } else {
        c.loop = 1;
    }

    if (j.contains("worklist")) {
        for (int i = 0; i < j["worklist"].size(); i++) {
            CoreJob cjob = j["worklist"][i];

            if (!j["worklist"][i].contains("recv_tag")) {
                cjob.recv_tag = c.id;
            }

            c.worklist.push_back(cjob);
        }
    } else {
        // 如果是旧版config，没有worklist条目，则将所有内容作为一个单独的job
        CoreJob cjob = j;

        if (!j.contains("recv_tag")) {
            cjob.recv_tag = c.id;
        }

        c.worklist.push_back(cjob);
    }
}

void from_json(const json &j, LayerConfig &c) {
    // 将json转化为LayerConfig
    j.at("id").get_to(c.id);

    for (int i = 0; i < j["cast"].size(); i++) {
        Cast temp = j["cast"][i];
        c.cast.push_back(temp);
    }

    // loop统一在外部填写

    if (j.contains("split")) {
        if (j["split"]["type"] == "TP")
            c.split = SPLIT_TP;
        else
            c.split = SPLIT_DP;

        c.split_dim = j["split"]["dim"];
        c.split_slice = j["split"]["slice"];
    } else {
        c.split = NO_SPLIT;
    }
}

void from_json(const json &j, StreamConfig &c) {
    j.at("id").get_to(c.id);

    if (j.contains("prims")) {
        auto prims = j["prims"];
        for (auto prim : prims) {
            gpu_base *p = nullptr;
            string type = prim.at("type");
            cout << type << endl;

            p = (gpu_base *)new_prim(type);
            p->parse_json(prim);

            c.prims.push_back((prim_base *)p);
        }
    }

    if (j.contains("source")) {
        auto sources = j["source"];
        for (auto source : sources) {
            c.sources.push_back(
                make_pair(source["label"], find_var(source["size"])));
        }
    }

    if (j.contains("loop")) {
        c.loop = find_var(j["loop"]);
    } else {
        c.loop = 1;
    }
}