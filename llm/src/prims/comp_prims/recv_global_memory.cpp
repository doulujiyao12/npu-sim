#include "systemc.h"
#include <tlm>

#include "memory/dram/Dcachecore.h"
#include "prims/comp_base.h"
#include "prims/comp_prims.h"
#include "utils/memory_utils.h"
#include "utils/system_utils.h"

sc_bv<128> Recv_global_memory::serialize() {
    sc_bv<128> d;
    d.range(7, 0) = sc_bv<8>(RECV_GLOBAL_MEMORY_TYPE);
    d.range(11, 8) = sc_bv<4>(type);
    d.range(19, 12) = sc_bv<8>(tag_id);
    d.range(27, 20) = sc_bv<8>(recv_cnt);
    d.range(31, 28) = sc_bv<4>(datatype);
    return d;
}

void Recv_global_memory::deserialize(sc_bv<128> buffer) {
    type = GLOBAL_RECV_TYPE(buffer.range(11, 8).to_uint64());
    tag_id = buffer.range(19, 12).to_uint64();
    recv_cnt = buffer.range(27, 20).to_uint64();
    datatype = DATATYPE(buffer.range(31, 28).to_uint64());
}

void Recv_global_memory::parse_json(json j) {
    assert(false && "Recv_global_memory is not implemented");
}

void Recv_global_memory::print_self(string prefix) {
    std::cout << prefix << "<Recv_global_memory>" << std::endl;
    std::cout << prefix << "\ttype: " << type << std::endl;
    std::cout << prefix << "\ttag_id: " << tag_id << std::endl;
    std::cout << prefix << "\trecv_cnt: " << recv_cnt << std::endl;
}

int Recv_global_memory::sram_utilization(DATATYPE datatype) {
    return 0;
}

int Recv_global_memory::task() {
    assert(false && "Recv_global_memory is not implemented");
    return 0;
}

int Recv_global_memory::task_core(TaskCoreContext &context) {
    assert(false && "Recv_global_memory is not implemented");
    return 0;
}