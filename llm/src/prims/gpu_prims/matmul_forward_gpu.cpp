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
    gpu_read_generic(context, 12298, data_size_input, mem_time);
    cout << cid << " [Matmul_f_gpu] after read1: " << mem_time << endl;
    gpu_read_generic(context, 12298, data_size_weight, mem_time);
    cout << cid << " [Matmul_f_gpu] after read2: " << mem_time << endl;
    gpu_read_generic(context, 12298, data_size_bias, mem_time);
    cout << cid << " [Matmul_f_gpu] after read3: " << mem_time << endl;

    // TODO: 模拟计算cycle数
    int overlap_time = mem_time;
    gpu_write_generic(context, 12321, data_size_bias, overlap_time);

    cout << "[Matmul_f_gpu] after write: " << overlap_time << endl;

    return overlap_time;
}

int Matmul_f_gpu::task() { return 0; }

int Matmul_f_gpu::sram_utilization(DATATYPE datatype) { return 0; }

sc_bv<128> Matmul_f_gpu::serialize() {
    sc_bv<128> d;
    d.range(7, 0) = sc_bv<8>(0xe0);
    // d.range(23, 8) = sc_bv<16>(inp_offset);
    // d.range(39, 24) = sc_bv<16>(out_offset);
    d.range(55, 40) = sc_bv<16>(B);
    d.range(71, 56) = sc_bv<16>(T);
    d.range(87, 72) = sc_bv<16>(C);
    d.range(103, 88) = sc_bv<16>(OC);
    // d.range(105, 104) = sc_bv<2>(datatype);
    // d.range(107, 106) = sc_bv<2>(use_hw);

    return d;
}

void Matmul_f_gpu::deserialize(sc_bv<128> buffer) {
    B = buffer.range(55, 40).to_uint64();
    T = buffer.range(71, 56).to_uint64();
    C = buffer.range(87, 72).to_uint64();
    OC = buffer.range(103, 88).to_uint64();

    initialize();
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

    initialize();
}

void Matmul_f_gpu::initialize() {
    inp_size = B * T * C;
    out_size = B * T * OC;
}