#include "prims/gpu_prims.h"
#include "utils/memory_utils.h"
#include "utils/system_utils.h"

int Residual_f_gpu::task_core(TaskCoreContext &context) {
    int data_byte = 0;
    if (datatype == INT8) {
        data_byte = 1;
    } else if (datatype == FP16) {
        data_byte = 2;
    }

    int data_size_input = 2 * N;
    int data_size_single_input = N;
    int data_size_out = N;

    int in_label_cnt = 0;
    for (int i = 0; i < MAX_SPLIT_NUM; i++) {
        if (datapass_label.indata[i] == UNSET_LABEL)
            continue;
        in_label_cnt++;
    }

    int mem_time = 0;
    int input_mem_offset[MAX_SPLIT_NUM];
    for (int i = 0; i < MAX_SPLIT_NUM; i++) {
        input_mem_offset[i] = 0;
    }

    for (int i = 0; i < in_label_cnt;) {
        if (datapass_label.indata[i] == UNSET_LABEL)
            continue;

        if (!gpu_pos_locator->findPair(datapass_label.indata[i],
                                       input_mem_offset[i])) {
            printf("[ERROR] Residual_f_gpu: gpu_pos_locator cannot find the "
                   "label: "
                   "%s\n",
                   datapass_label.indata[i].c_str());
            sc_stop();
        }
    }

    int overlap_time = 0;
#if USE_L1L2_CACHE == 1
    for (int i = 0; i < in_label_cnt; i++) {
        gpu_read_generic(context, input_mem_offset[i],
                         data_byte * data_size_input, mem_time);
    }

    overlap_time = mem_time;
    AddrPosKey out_key = AddrPosKey(0, data_byte * data_size_out);
    gpu_pos_locator->addPair(datapass_label.outdata, out_key);
    gpu_write_generic(context, out_key.pos, data_byte * data_size_out,
                      overlap_time);
#endif

    cout << "[Residual_f_gpu] after write: " << overlap_time << endl;

    return overlap_time;
}

int Residual_f_gpu::task() { return 0; }

int Residual_f_gpu::sram_utilization(DATATYPE datatype, int cid) { return 0; }

void Residual_f_gpu::deserialize(sc_bv<128> buffer) {
    N = buffer.range(71, 40).to_uint64();
    datatype = DATATYPE(buffer.range(73, 72).to_uint64());
}

sc_bv<128> Residual_f_gpu::serialize() {
    sc_bv<128> d;
    d.range(7, 0) = sc_bv<8>(RESIDUAL_F_GPU_TYPE);
    d.range(71, 40) = sc_bv<32>(N);
    d.range(73, 72) = sc_bv<2>(datatype);

    return d;
}

void Residual_f_gpu::print_self(string prefix) {
    cout << prefix << "<residual_forward_gpu>\n";
    cout << prefix << "\tN: " << N << endl;
}

gpu_base *Residual_f_gpu::clone() { return new Residual_f_gpu(*this); }

void Residual_f_gpu::parse_json(json j) {
    N = find_var(j["N"]);

    if (j.contains("compose")) {
        parse_compose(j["compose"]);
    }

    if (j.contains("address")) {
        parse_addr_label(j["address"]);
    }
}