#include "prims/gpu_prims.h"
#include "utils/memory_utils.h"
#include "utils/system_utils.h"

void Gelu_f_gpu::print_self(string prefix) {
    cout << prefix << "<gelu_forward_gpu>\n";
    cout << prefix << "\tN: " << N << endl;
    cout << prefix << "slice_x: " << slice_x << ", slice_y: " << slice_y
         << endl;
}

void Gelu_f_gpu::parseJson(json j) {
    N = GetDefinedParam(j["N"]);
    slice_x = j["slice_x"];
    slice_y = j["slice_y"];

    if (j.contains("compose")) {
        parse_compose(j["compose"]);
    }

    if (j.contains("address")) {
        parse_addr_label(j["address"]);
    }
}

sc_bv<128> Gelu_f_gpu::serialize() {
    sc_bv<128> d;
    d.range(7, 0) = sc_bv<8>(GELU_F_GPU_TYPE);
    d.range(15, 8) = sc_bv<8>(slice_x);
    d.range(23, 16) = sc_bv<8>(slice_y);
    d.range(71, 40) = sc_bv<32>(N);
    d.range(73, 72) = sc_bv<2>(datatype);
    d.range(89, 74) = sc_bv<16>(fetch_index);

    return d;
}

void Gelu_f_gpu::deserialize(sc_bv<128> buffer) {
    slice_x = buffer.range(15, 8).to_uint();
    slice_y = buffer.range(23, 16).to_uint();
    N = buffer.range(71, 40).to_uint64();
    datatype = DATATYPE(buffer.range(73, 72).to_uint64());
    fetch_index = buffer.range(89, 74).to_uint64();
}

int Gelu_f_gpu::sram_utilization(DATATYPE datatype, int cid) { return 0; }

int Gelu_f_gpu::taskCoreDefault(TaskCoreContext &context) {
    int data_byte = 0;
    if (datatype == INT8) {
        data_byte = 1;
    } else if (datatype == FP16) {
        data_byte = 2;
    }
    N = GetDefinedParam("T") * GetDefinedParam("C");
    N = N * gpu_B;

    int data_size_input = N * data_byte;
    int data_size_out = data_byte * N / (slice_x * slice_y);

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
    gpu_read_generic(context,
                     input_mem_offset +
                         data_size_input / (slice_x * slice_y) * fetch_index,
                     data_size_input / (slice_x * slice_y), mem_time);

    // overlap_time = mem_time;
    AddrPosKey out_key;
    gpu_pos_locator->updatePair(datapass_label.outdata, data_size_out);
    gpu_pos_locator->findPair(datapass_label.outdata, out_key);

    gpu_write_generic(context, out_key.pos + data_size_out * fetch_index,
                      data_size_out, mem_time);
    int cycle = 0;
    int cid = context.cid;

    CoreHWConfig core_config = GetCoreHWConfig(cid);
    ExuConfig *exu = core_config.exu;
    SfuConfig *sfu = core_config.sfu;

    if (exu->type == MAC_Array)
        cycle += 0 / (slice_x * slice_y) /
                 (exu->x_dims * exu->y_dims * 2 * comp_util) * CYCLE;
    else
        assert(false && "Unsupported tile type");

    if (sfu->type == Linear)
        cycle += N / (slice_x * slice_y) / sfu->x_dims * CYCLE;
    else
        assert(false && "Unsupported tile type");


    if (mem_time > cycle) {
        // 因为dram 已经wait 过了，所以额外的 overlap_time = 0
        overlap_time = 0;
        LOG_VERBOSE(1, context.cid,
                    "Prim name:" << name << RED << " cycle: " << cycle
                                 << ", dram_time: " << mem_time << RESET);

        // std::cout << RED << "cycle: " << cycle << ", dram_time: " <<
        // dram_time
        //           << RESET << std::endl;

    } else {
        overlap_time = cycle - mem_time;
        LOG_VERBOSE(1, context.cid,
                    "Prim name:" << name << GREEN << " cycle: " << cycle
                                 << ", dram_time: " << mem_time << RESET);
    }
#endif

    cout << "[Gelu_f_gpu] after write: " << overlap_time << endl;
    N = N / gpu_B;
    assert(N > 0);
    return overlap_time;
}

int Gelu_f_gpu::task() { return 0; }

GpuBase *Gelu_f_gpu::clone() { return new Gelu_f_gpu(*this); }