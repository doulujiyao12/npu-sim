#include "systemc.h"

#include "defs/enums.h"
#include "prims/comp_prims.h"
#include "prims/norm_prims.h"
#include "prims/prim_base.h"
#include "utils/memory_utils.h"

void Recv_prim::print_self(string prefix) {
    cout << prefix << "<recv_prim>\n";

    string type_s;
    if (type == RECV_CONF)
        type_s = "RECV_CONF";
    if (type == RECV_ACK)
        type_s = "RECV_ACK";
    if (type == RECV_FLAG)
        type_s = "RECV_FLAG";
    if (type == RECV_DATA)
        type_s = "RECV_DATA";
    if (type == RECV_SRAM)
        type_s = "RECV_SRAM";
    if (type == RECV_WEIGHT)
        type_s = "RECV_WEIGHT";

    cout << prefix << "\t[" << type_s << "] > tag_id: " << tag_id
         << ", recv_cnt: " << recv_cnt << endl;
}
int Recv_prim::sram_utilization(DATATYPE datatype, int cid) {
    int total_sram = 0;

    return total_sram;
}

void Recv_prim::parseJson(json j, vector<pair<string, int>> vtable) {}

Recv_prim::Recv_prim(RECV_TYPE type) {
    this->type = type;
    name = "Recv_prim";
}

void Recv_prim::deserialize(sc_bv<128> buffer) {
    type = RECV_TYPE(buffer.range(11, 8).to_uint64());
    tag_id = buffer.range(19, 12).to_uint64();
    recv_cnt = buffer.range(27, 20).to_uint64();
    datatype = DATATYPE(buffer.range(29, 28).to_uint64());
}

sc_bv<128> Recv_prim::serialize() {
    sc_bv<128> d;
    d.range(7, 0) = sc_bv<8>(RECV_PRIM_TYPE);
    d.range(11, 8) = sc_bv<4>(type);
    d.range(19, 12) = sc_bv<8>(tag_id);
    d.range(27, 20) = sc_bv<8>(recv_cnt);
    d.range(29, 28) = sc_bv<2>(datatype);

    return d;
}
int Recv_prim::taskCoreDefault(TaskCoreContext &context) {
    u_int64_t elapsed_time;
    // sram_write_append_generic(context, M_D_DATA, elapsed_time);

    return 0;
}

int Recv_prim::task() { return 0; }