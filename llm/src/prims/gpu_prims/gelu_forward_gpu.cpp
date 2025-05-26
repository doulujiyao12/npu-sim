#include "prims/gpu_prims.h"
#include "utils/memory_utils.h"
#include "utils/system_utils.h"

void Gelu_f_gpu::print_self(string prefix) {
    cout << prefix << "<gelu_forward_gpu>\n";
    cout << prefix << "\tN: " << N << endl;
}

void Gelu_f_gpu::parse_json(json j) {
    N = find_var(j["N"]);

    if (j.contains("compose")) {
        parse_compose(j["compose"]);
    }

    if (j.contains("address")) {
        parse_addr_label(j["address"]);
    }
}

sc_bv<128> Gelu_f_gpu::serialize() {
    sc_bv<128> d;
    d.range(7, 0) = sc_bv<8>(0xe2);
    d.range(71, 40) = sc_bv<32>(N);
    d.range(73, 72) = sc_bv<2>(datatype);

    return d;
}

void Gelu_f_gpu::deserialize(sc_bv<128> buffer) {
    N = buffer.range(71, 40).to_uint64();
    datatype = DATATYPE(buffer.range(73, 72).to_uint64());
}

int Gelu_f_gpu::sram_utilization(DATATYPE datatype) { return 0; }

int Gelu_f_gpu::task_core(TaskCoreContext &context) {
    int data_byte = 0;
    if (datatype == INT8) {
        data_byte = 1;
    } else if (datatype == FP16) {
        data_byte = 2;
    }

    int data_size_input = N;
    int data_size_out = N;

    int mem_time = 0;
    auto input_mem_offset = 0;
    if (!gpu_pos_locator->findPair(datapass_label.indata[0],
                                   input_mem_offset)) {
        printf("[ERROR] Gelu_f_gpu: gpu_pos_locator cannot find the label: "
               "%s\n",
               datapass_label.indata[0].c_str());
        sc_stop();
    }

    int overlap_time = 0;
#if USE_L1L2_CACHE == 1
    if (SYSTEM_MODE == SIM_GPU) {
        gpu_read_generic(context, input_mem_offset, data_byte * data_size_input,
                         mem_time);

        overlap_time = mem_time;
        AddrPosKey out_key = AddrPosKey(0, data_byte * data_size_out);
        gpu_pos_locator->addPair(datapass_label.outdata, out_key);
        gpu_write_generic(context, out_key.pos, data_byte * data_size_out,
                          overlap_time);
    }
#endif

    cout << "[Gelu_f_gpu] after write: " << overlap_time << endl;

    return overlap_time;
}

int Gelu_f_gpu::task() { return 0; }

gpu_base *Gelu_f_gpu::clone() { return new Gelu_f_gpu(*this); }