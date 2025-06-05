#include "prims/gpu_prims.h"
#include "utils/memory_utils.h"
#include "utils/system_utils.h"

int Matmul_f_gpu::task_core(TaskCoreContext &context) {
    int data_byte = 0;
    if (datatype == INT8) {
        data_byte = 1;
    } else if (datatype == FP16) {
        data_byte = 2;
    }

    int data_size_input = B * T * C * data_byte;
    int data_size_weight = OC * C * data_byte;
    int data_size_bias = OC * data_byte;
    int data_size_out = B * T * OC * data_byte;

    int mem_time = 0;
    auto input_mem_offset = 0;
    if (!gpu_pos_locator->findPair(datapass_label.indata[0],
                                   input_mem_offset)) {
        printf("[ERROR] Matmul_f_gpu: gpu_pos_locator cannot find the label: "
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

    auto label_weight = prefix + "_w";
    AddrPosKey w_key = AddrPosKey(0, data_size_weight);
    gpu_pos_locator->fetchPair(label_weight, w_key);

    auto label_bias = prefix + "_b";
    AddrPosKey b_key = AddrPosKey(0, data_size_bias);
    gpu_pos_locator->fetchPair(label_bias, b_key);

    cout << cid << " [Matmul_f_gpu] before read1: " << mem_time << " at addr "
         << input_mem_offset << endl;

    int overlap_time = 0;
#if USE_L1L2_CACHE == 1
    gpu_read_generic(context, input_mem_offset, data_byte * data_size_input,
                     mem_time);
    cout << cid << " [Matmul_f_gpu] after read1: " << mem_time << endl;
    cout << cid << " [Matmul_f_gpu] before read2: " << mem_time << " at addr "
         << w_key.pos << endl;
    gpu_read_generic(context, w_key.pos, data_byte * data_size_weight,
                     mem_time);
    cout << cid << " [Matmul_f_gpu] after read2: " << mem_time << endl;
    cout << cid << " [Matmul_f_gpu] before read3: " << mem_time << " at addr "
         << b_key.pos << endl;
    gpu_read_generic(context, b_key.pos, data_byte * data_size_bias, mem_time);
    cout << cid << " [Matmul_f_gpu] after read3: " << mem_time << endl;

    // TODO: 模拟计算cycle数
    overlap_time = mem_time;
    AddrPosKey out_key = AddrPosKey(0, data_byte * data_size_out);
    gpu_pos_locator->addPair(datapass_label.outdata, out_key);
    cout << cid << " [Matmul_f_gpu] before write: " << mem_time << " at addr "
         << out_key.pos << endl;
    gpu_write_generic(context, out_key.pos, data_byte * data_size_out,
                      overlap_time);
#endif

    cout << cid << " [Matmul_f_gpu] after write: " << overlap_time << endl;

    return overlap_time;
}

int Matmul_f_gpu::task() { return 0; }

int Matmul_f_gpu::sram_utilization(DATATYPE datatype) { return 0; }

sc_bv<128> Matmul_f_gpu::serialize() {
    sc_bv<128> d;
    d.range(7, 0) = sc_bv<8>(MATMUL_F_GPU_TYPE);
    d.range(55, 40) = sc_bv<16>(B);
    d.range(71, 56) = sc_bv<16>(T);
    d.range(87, 72) = sc_bv<16>(C);
    d.range(103, 88) = sc_bv<16>(OC);
    d.range(105, 104) = sc_bv<2>(datatype);

    return d;
}

void Matmul_f_gpu::deserialize(sc_bv<128> buffer) {
    B = buffer.range(55, 40).to_uint64();
    T = buffer.range(71, 56).to_uint64();
    C = buffer.range(87, 72).to_uint64();
    OC = buffer.range(103, 88).to_uint64();
    datatype = DATATYPE(buffer.range(105, 104).to_uint64());
}

void Matmul_f_gpu::print_self(string prefix) {
    cout << prefix << "<Matmul_f_gpu:>" << endl;
    cout << prefix << "\tB: " << B << endl;
    cout << prefix << "\tT: " << T << endl;
    cout << prefix << "\tC: " << C << endl;
    cout << prefix << "\tOC: " << OC << endl;
}

gpu_base *Matmul_f_gpu::clone() { return new Matmul_f_gpu(*this); }

void Matmul_f_gpu::parse_json(json j) {
    B = find_var(j["B"]);
    T = find_var(j["T"]);
    C = find_var(j["C"]);
    OC = find_var(j["OC"]);

    if (j.contains("compose")) {
        parse_compose(j["compose"]);
    }

    if (j.contains("address")) {
        parse_addr_label(j["address"]);
    }
}