#include "prims/gpu_prims.h"
#include "utils/memory_utils.h"
#include "utils/system_utils.h"

int Attention_f_gpu::task_core(TaskCoreContext &context) {
    int data_byte = 0;
    if (datatype == INT8) {
        data_byte = 1;
    } else if (datatype == FP16) {
        data_byte = 2;
    }

    int data_size_input = B * T * 3 * C;   // QKV input
    int data_size_preatt = B * NH * T * T; // preatt
    int data_size_att = B * NH * T * T;    // att
    int data_size_out = B * T * C;         // output

    int mem_time = 0;
    auto input_mem_offset = 0;
    if (!gpu_pos_locator->findPair(datapass_label.indata[0], input_mem_offset)) {
        printf("[ERROR] Attention_f_gpu: gpu_pos_locator cannot find the label: "
            "%s\n",
            datapass_label.indata[0].c_str());
        sc_stop();
    }

    // 获取前缀label
    std::size_t pos = datapass_label.outdata.find_last_of('_');
    std::string prefix;
    if (pos != std::string::npos) {
        prefix = datapass_label.outdata.substr(0, pos);
    } else {
        prefix = datapass_label.outdata;
    }

    auto label_preatt = prefix + "_preatt";
    AddrPosKey p_key = AddrPosKey(0, data_size_preatt);
    gpu_pos_locator->fetchPair(label_preatt, p_key);

    auto label_att = prefix + "_att";
    AddrPosKey a_key = AddrPosKey(0, data_size_att);
    gpu_pos_locator->fetchPair(label_att, a_key);

    cout << cid << " [Attention_f_gpu] before read1: " << mem_time << " at addr " << input_mem_offset << endl;
    gpu_read_generic(context, input_mem_offset, data_byte*data_size_input/3*2, mem_time);
    cout << cid << " [Attention_f_gpu] after read1: " << mem_time << endl;
    cout << cid << " [Attention_f_gpu] before write1: " << mem_time << " at addr " << p_key.pos << endl;
    gpu_write_generic(context, p_key.pos, data_byte*data_size_preatt, mem_time);
    cout << cid << " [Attention_f_gpu] after write1: " << mem_time << endl;
    gpu_read_generic(context, p_key.pos, data_byte*data_size_preatt, mem_time);
    gpu_write_generic(context, a_key.pos, data_byte*data_size_att, mem_time);
    gpu_read_generic(context, a_key.pos, data_byte*data_size_att, mem_time);
    gpu_read_generic(context, input_mem_offset, data_byte*data_size_input/3, mem_time);

    cout << cid << " [Attention_f_gpu] after this: " << mem_time << endl;

    int overlap_time = 0;
    AddrPosKey out_key = AddrPosKey(0, data_byte * data_size_out);
    gpu_pos_locator->addPair(datapass_label.outdata, out_key);
    // cout << "dddddddddd" << endl;
    gpu_write_generic(context, out_key.pos, data_byte*data_size_out, overlap_time);

    cout << cid << " [Attention_f_gpu] after write: " << overlap_time << endl;

    return overlap_time;
}

int Attention_f_gpu::task() { return 0; }

int Attention_f_gpu::sram_utilization(DATATYPE datatype) { return 0; }

void Attention_f_gpu::deserialize(sc_bv<128> buffer) {
    B = buffer.range(55, 40).to_uint64();
    T = buffer.range(71, 56).to_uint64();
    C = buffer.range(87, 72).to_uint64();
    NH = buffer.range(103, 88).to_uint64();
    datatype = DATATYPE(buffer.range(105, 104).to_uint64());
}

sc_bv<128> Attention_f_gpu::serialize() {
    sc_bv<128> d;
    d.range(7, 0) = sc_bv<8>(0xe1);
    d.range(55, 40) = sc_bv<16>(B);
    d.range(71, 56) = sc_bv<16>(T);
    d.range(87, 72) = sc_bv<16>(C);
    d.range(103, 88) = sc_bv<16>(NH);
    d.range(105, 104) = sc_bv<2>(datatype);

    return d;
}

void Attention_f_gpu::print_self(string prefix) {
    cout << prefix << "<attention_forward_gpu>\n";
    cout << prefix << "\tB: " << B << ", T: " << T << ", C: " << C << endl;
}

gpu_base *Attention_f_gpu::clone() { return new Attention_f_gpu(*this); }

void Attention_f_gpu::parse_json(json j) {
    B = find_var(j["B"]);
    T = find_var(j["T"]);
    C = find_var(j["C"]);
    NH = find_var(j["NH"]);

    if (j.contains("compose")) {
        parse_compose(j["compose"]);
    }

    if (j.contains("address")) {
        parse_addr_label(j["address"]);
    }
}