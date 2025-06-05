#include "systemc.h"
#include <tlm>

#include "memory/dram/Dcachecore.h"
#include "prims/comp_base.h"
#include "prims/comp_prims.h"
#include "utils/memory_utils.h"
#include "utils/system_utils.h"
#include "link/instr/recv_global_mem.h"

sc_bv<128> Send_global_memory::serialize() {
    sc_bv<128> d;
    d.range(7, 0) = sc_bv<8>(SEND_GLOBAL_MEMORY_TYPE);
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

void Send_global_memory::deserialize(sc_bv<128> buffer) {
    des_id = buffer.range(23, 8).to_uint64();
    des_offset = buffer.range(39, 24).to_uint64();
    local_offset = buffer.range(55, 40).to_uint64();
    type = GLOBAL_SEND_TYPE(buffer.range(59, 56).to_uint64());
    max_packet = buffer.range(75, 60).to_uint64();
    tag_id = buffer.range(83, 76).to_uint64();
    end_length = buffer.range(91, 84).to_uint64();
    datatype = DATATYPE(buffer.range(93, 92).to_uint64());
}

int Send_global_memory::task() {
    assert(false && "Send_global_memory is not implemented");
    return 0;
}

int Send_global_memory::task_core(TaskCoreContext &context) {
    std::cout << "[Global Mem]: Send_global_memory::task_core" << std::endl;
    std::cout << "[Global Mem]: Send_global_memory::tag_id: " << tag_id << std::endl;
    // Determine element size (INT8=1 byte, FP16=2 bytes)
    int elem_bytes = (datatype == DATATYPE::FP16 ? 2 : 1);
    // Total bytes to send (end_length elements)
    int byte_count = end_length * elem_bytes;
    // Remote DRAM byte address = des_offset * element size
    uint64_t address = static_cast<uint64_t>(des_offset) * elem_bytes;

    // Create a write transaction (simulate latency only, no data pointer)
    tlm::tlm_generic_payload trans;
    trans.set_command(tlm::TLM_WRITE_COMMAND);
    trans.set_address(address);
    trans.set_data_ptr(nullptr);
    trans.set_data_length(byte_count);
    trans.set_streaming_width(byte_count);
    trans.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
        
    // Dispatch the transaction via the DRAM interface
    sc_core::sc_time delay = sc_core::SC_ZERO_TIME;
    context.nb_global_memif->socket->b_transport(trans, delay);


    std::cout << "[Global Mem]: Send_global_memory::end " << std::endl;
    // Return the simulated write latency in nanoseconds
    return static_cast<int>(delay.to_seconds() * 1e9);
}

void Send_global_memory::parse_json(json j) {
    // assert(0 && "Not Implemented yet");

    enable = 0;
    if (j.contains("enable")) {
        enable = j["enable"].is_number() ? j["enable"].get<int>()
                                         : (j["enable"].get<bool>() ? 1 : 0);
    }

    if(j.contains("addr")) {
        auto &addr_field = j["addr"];
        if (addr_field.is_string()) {
            des_offset = find_var(addr_field.get<string>());
        } else {
            des_offset = addr_field.get<int>();
        }
    }

}

void Send_global_memory::print_self(string prefix) {
    cout << prefix << "<Send_global_memory>\n";
    cout << prefix << "\tdatatype: " << datatype << endl;
    cout << prefix << "\tdes_id: " << des_id << endl;
    cout << prefix << "\tdes_offset: " << des_offset << endl;
    cout << prefix << "\tlocal_offset: " << local_offset << endl;
    cout << prefix << "\tmax_packet: " << max_packet << endl;
    cout << prefix << "\ttag_id: " << tag_id << endl;
    cout << prefix << "\tend_length: " << end_length << endl;
}

int Send_global_memory::sram_utilization(DATATYPE datatype) {
    return 0;
}

// int Send_global_memory::task_core(TaskCoreContext &context) {

//     // // 计算总字节数 (INT8=1B, FP16=2B)
//     // int byte_count = out_size * elem_bytes;
//     // // DRAM 字节地址 = out_offset * element_size
//     // uint64_t address = uint64_t(out_offset) * elem_bytes;

//     // // 创建写事务（只模拟时延，不带实际数据指针）
//     // tlm::tlm_generic_payload trans;
//     // trans.set_command(tlm::TLM_WRITE_COMMAND);
//     // trans.set_address(address);
//     // trans.set_data_ptr(nullptr);
//     // trans.set_data_length(byte_count);
//     // trans.set_streaming_width(byte_count);
//     // trans.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);

//     // // 通过 on-chip DCache 接口写入全局 DRAM
//     // sc_time delay = SC_ZERO_TIME;
//     // context.wc->isocket->b_transport(trans, delay);

//     // // 返回模拟的写延迟（纳秒）
//     // return static_cast<uint64_t>(delay.to_seconds() * 1e9);
// }
// void Send_global_memory::parse_json(json j) {
//     assert(0 && "Not Implemented yet");
// }