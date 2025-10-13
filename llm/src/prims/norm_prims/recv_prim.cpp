#include "systemc.h"

#include "defs/enums.h"
#include "prims/base.h"
#include "prims/comp_prims.h"
#include "prims/norm_prims.h"
#include "utils/memory_utils.h"
#include "utils/prim_utils.h"

REGISTER_PRIM(Recv_prim);

void Recv_prim::printSelf() {
    cout << "<recv_prim>\n";
    cout << "type: " << type << " tag_id: " << tag_id
         << " recv_cnt: " << recv_cnt << endl;
}

void Recv_prim::deserialize(vector<sc_bv<128>> segments) {
        cout << "Start deserialize " << name << endl;
    auto buffer = segments[0];
    
    type = RECV_TYPE(buffer.range(11, 8).to_uint64());
    tag_id = buffer.range(19, 12).to_uint64();
    recv_cnt = buffer.range(27, 20).to_uint64();
    datatype = DATATYPE(buffer.range(29, 28).to_uint64());
}

vector<sc_bv<128>> Recv_prim::serialize() {
    vector<sc_bv<128>> segments;

    sc_bv<128> d;
    d.range(7, 0) = sc_bv<8>(PrimFactory::getInstance().getPrimId(name));
    d.range(11, 8) = sc_bv<4>(type);
    d.range(19, 12) = sc_bv<8>(tag_id);
    d.range(27, 20) = sc_bv<8>(recv_cnt);
    d.range(29, 28) = sc_bv<2>(datatype);
    segments.push_back(d);

    return segments;
}
int Recv_prim::taskCoreDefault(TaskCoreContext &context) {
    u_int64_t elapsed_time;
    // sram_write_append_generic(context, M_D_DATA, elapsed_time);

    return 0;
}