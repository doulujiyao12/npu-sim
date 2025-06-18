#include "systemc.h"

#include "defs/enums.h"
#include "prims/norm_prims.h"
#include "prims/prim_base.h"

void Send_prim::print_self(string prefix) {
    cout << prefix << "<send_prim>\n";

    string type_s;
    if (type == SEND_ACK)
        type_s = "SEND_ACK";
    if (type == SEND_REQ)
        type_s = "SEND_REQ";
    if (type == SEND_DATA)
        type_s = "SEND_DATA";
    if (type == SEND_SRAM)
        type_s = "SEND_SRAM";
    if (type == SEND_DONE)
        type_s = "SEND_DONE";

    cout << prefix << "\t[" << type_s << "] > send to " << des_id
         << ", des_offset: " << des_offset << ", local_offset: " << local_offset
         << endl;
    cout << prefix << "\tmax_packet: " << max_packet << ", tag_id: " << tag_id
         << ", end_length: " << end_length << endl;
}

void Send_prim::parse_json(json j, vector<pair<string, int>> vtable) {}

int Send_prim::sram_utilization(DATATYPE datatype) {
    int total_sram = 0;

    return total_sram;
}

void Send_prim::deserialize(sc_bv<128> buffer) {
    des_id = buffer.range(23, 8).to_uint64();
    des_offset = buffer.range(39, 24).to_uint64();
    local_offset = buffer.range(55, 40).to_uint64();
    type = SEND_TYPE(buffer.range(59, 56).to_uint64());
    max_packet = buffer.range(75, 60).to_uint64();
    tag_id = buffer.range(83, 76).to_uint64();
    end_length = buffer.range(91, 84).to_uint64();
    datatype = DATATYPE(buffer.range(93, 92).to_uint64());
}

sc_bv<128> Send_prim::serialize() {
    sc_bv<128> d;
    d.range(7, 0) = sc_bv<8>(SEND_PRIM_TYPE);
    d.range(23, 8) = sc_bv<16>(des_id);
    d.range(39, 24) = sc_bv<16>(des_offset);
    d.range(55, 40) = sc_bv<16>(local_offset);
    d.range(59, 56) = sc_bv<4>(type);
    d.range(75, 60) = sc_bv<16>(max_packet);
    d.range(83, 76) = sc_bv<8>(tag_id);
    d.range(91, 84) = sc_bv<8>(end_length);
    d.range(93, 92) = sc_bv<2>(datatype);

    return d;
}
int Send_prim::task_core(TaskCoreContext &context) {
#if USE_NB_DRAMSYS == 0
    auto wc = context.wc;
#endif
    auto mau = context.mau;
    auto hmau = context.hmau;
    sc_bv<128> msg_data;
    sc_time elapsed_time;
    mau->mem_read_port->read(local_offset + M_D_DATA * (data_packet_id - 1),
                             msg_data, elapsed_time);
    msg_data = 0b1;
    // std::cout << "msg_data (hex) after send" << msg_data.to_string(SC_HEX) <<
    // std::endl;
    return 0;
}
int Send_prim::task() { return 0; }
