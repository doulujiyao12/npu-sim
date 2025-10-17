#include "prims/norm_prims.h"
#include "utils/prim_utils.h"

REGISTER_PRIM(Set_batch);

int Set_batch::taskCoreDefault(TaskCoreContext &context) {
    prim_context->loop_cnt++;
    prim_context->auto_pd_ = auto_pd;

    prim_context->batch_info_.clear();
    for (auto stage : batch_info) {
        cout << prim_context->cid << ": ###auto_pd: " << auto_pd
             << ", loop_cnt: " << prim_context->loop_cnt << endl;
        if (auto_pd && prim_context->loop_cnt > auto_pd) {
            LOG_VERBOSE(1, prim_context->cid,
                        "Auto PD: " << prim_context->loop_cnt);
            prim_context->batch_info_.push_back(
                Stage(prim_context->loop_cnt % auto_pd, PD_PHASE(DECODE), 1));
        } else if (auto_pd)
            prim_context->batch_info_.push_back(Stage(
                prim_context->loop_cnt % auto_pd, stage.type, stage.token_num));
        else
            prim_context->batch_info_.push_back(stage);
    }

    for (auto stage : prim_context->batch_info_) {
        cout << prim_context->cid << ": " << stage.req_id << ": type "
             << stage.type << ": token " << stage.token_num << endl;
    }

    return 0;
}

void Set_batch::printSelf() { cout << "<Set_batch>\n"; }

void Set_batch::deserialize(vector<sc_bv<128>> segments) {
    cout << "Start deserialize " << name << endl;
    auto buffer = segments[0];

    int batch_size = buffer.range(11, 8).to_uint64();
    auto_pd = buffer.range(19, 12).to_uint64();
    int pos = 20;
    for (int i = 0; i < batch_size; i++) {
        Stage s = Stage(buffer.range(pos + 7, pos).to_uint64(),
                        PD_PHASE(buffer.range(pos + 9, pos + 8).to_uint64()),
                        buffer.range(pos + 21, pos + 10).to_uint64());
        batch_info.push_back(s);
        pos += 22;
    }
}

vector<sc_bv<128>> Set_batch::serialize() {
    vector<sc_bv<128>> segments;

    sc_bv<128> d;
    d.range(7, 0) = sc_bv<8>(PrimFactory::getInstance().getPrimId(name));
    d.range(11, 8) = sc_bv<4>(batch_info.size());
    d.range(19, 12) = sc_bv<8>(auto_pd);

    int pos = 20;
    for (int i = 0; i < batch_info.size(); i++) {
        d.range(pos + 7, pos) = sc_bv<8>(batch_info[i].req_id);
        d.range(pos + 9, pos + 8) = sc_bv<2>(batch_info[i].type);
        d.range(pos + 21, pos + 10) = sc_bv<12>(batch_info[i].token_num);
        pos += 22;
    }
    segments.push_back(d);

    return segments;
}