#include "prims/gpu_prims.h"
#include "utils/memory_utils.h"
#include "utils/system_utils.h"

int Matmul_f_gpu::taskCoreDefault(TaskCoreContext &context) {
    int data_byte = 0;
    if (datatype == INT8) {
        data_byte = 1;
    } else if (datatype == FP16) {
        data_byte = 2;
    }
    B = B * gpu_B;
    T = GetDefinedParam("T");

    int data_size_input = B * T * C * data_byte;
    int data_size_weight = OC * C * data_byte;
    int data_size_bias = OC * data_byte;
    int data_size_out = B * T * OC * data_byte / (slice_x * slice_y);

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
    if (gpu_inner == true) {
        // 通过fetch_index计算位置
        int row_index = fetch_index / slice_x;
        int col_index = fetch_index % slice_x;

        // input 读入
        gpu_read_generic(
            context, input_mem_offset + data_size_input / slice_y * row_index,
            data_size_input / slice_y, mem_time);

        // weight 读入
        gpu_read_generic(context, w_key.pos + w_key.size / slice_x * col_index,
                         data_size_weight / slice_x, mem_time);

        // bias 读入
        gpu_read_generic(context, b_key.pos + b_key.size / slice_x * col_index,
                         data_size_bias / slice_x, mem_time);

        // TODO: 模拟计算cycle数
        // overlap_time = mem_time;
        AddrPosKey out_key;
        gpu_pos_locator->updatePair(datapass_label.outdata,
                                    data_size_out * (slice_x * slice_y));
        gpu_pos_locator->findPair(datapass_label.outdata, out_key);
        cout << cid << " [Matmul_f_gpu] before write: " << mem_time
             << " at addr " << out_key.pos << endl;
        gpu_write_generic(context, out_key.pos + data_size_out * fetch_index,
                          data_size_out, mem_time);

        int cycle = 0;
        int cid = context.cid;

        CoreHWConfig core_config = GetCoreHWConfig(cid);
        ExuConfig *exu = core_config.exu;
        SfuConfig *sfu = core_config.sfu;

        if (exu->type == MAC_Array)
            cycle += (B * T * C * OC * 2 / (slice_x * slice_y)) /
                     (exu->x_dims * exu->y_dims * 2 * comp_util) * CYCLE;
        else
            assert(false && "Unsupported tile type");

        if (sfu->type == Linear)
            cycle += 0 / sfu->x_dims * CYCLE;
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
    } else {

        int slice_total = slice_x * slice_y;
        // input 读入
        gpu_read_generic(context,
                         input_mem_offset +
                             data_size_input / slice_total * fetch_index,
                         data_size_input / slice_total, mem_time);

        // weight 读入
        gpu_read_generic(context,
                         w_key.pos + w_key.size / slice_total * fetch_index,
                         data_size_weight / slice_total, mem_time);

        // bias 读入
        gpu_read_generic(context,
                         b_key.pos + b_key.size / slice_total * fetch_index,
                         data_size_bias / slice_total, mem_time);

        AddrPosKey out_key;
        gpu_pos_locator->updatePair(datapass_label.outdata,
                                    data_size_out * slice_total);
        gpu_pos_locator->findPair(datapass_label.outdata, out_key);
        cout << cid << " [Matmul_f_gpu] before write: " << mem_time
             << " at addr " << out_key.pos << endl;
        gpu_write_generic(context, out_key.pos, data_size_out * slice_total,
                          mem_time);

        int cycle = 0;
        int cid = context.cid;

        CoreHWConfig core_config = GetCoreHWConfig(cid);
        ExuConfig *exu = core_config.exu;
        SfuConfig *sfu = core_config.sfu;

        if (exu->type == MAC_Array)
            cycle += (B * T * C * OC * 2 / (slice_x * slice_y)) /
                     (exu->x_dims * exu->y_dims * 2 * comp_util) * CYCLE;
        else
            assert(false && "Unsupported tile type");

        if (sfu->type == Linear)
            cycle += 0 / sfu->x_dims * CYCLE;
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
    }
#endif

    cout << cid << " [Matmul_f_gpu] after write: " << overlap_time << endl;
    B = B / gpu_B;
    assert(B > 0);
    return overlap_time;
}

int Matmul_f_gpu::task() { return 0; }

int Matmul_f_gpu::sram_utilization(DATATYPE datatype, int cid) { return 0; }

sc_bv<128> Matmul_f_gpu::serialize() {
    sc_bv<128> d;
    d.range(7, 0) = sc_bv<8>(MATMUL_F_GPU_TYPE);
    d.range(15, 8) = sc_bv<8>(slice_x);
    d.range(23, 16) = sc_bv<8>(slice_y);
    d.range(55, 40) = sc_bv<16>(B);
    d.range(71, 56) = sc_bv<16>(T);
    d.range(87, 72) = sc_bv<16>(C);
    d.range(103, 88) = sc_bv<16>(OC);
    d.range(105, 104) = sc_bv<2>(datatype);
    d.range(121, 106) = sc_bv<16>(fetch_index);

    return d;
}

void Matmul_f_gpu::deserialize(sc_bv<128> buffer) {
    slice_x = buffer.range(15, 8).to_uint();
    slice_y = buffer.range(23, 16).to_uint();
    B = buffer.range(55, 40).to_uint64();
    T = buffer.range(71, 56).to_uint64();
    C = buffer.range(87, 72).to_uint64();
    OC = buffer.range(103, 88).to_uint64();
    datatype = DATATYPE(buffer.range(105, 104).to_uint64());
    fetch_index = buffer.range(121, 106).to_uint64();
}

void Matmul_f_gpu::print_self(string prefix) {
    cout << prefix << "<Matmul_f_gpu:>" << endl;
    cout << prefix << "\tB: " << B << endl;
    cout << prefix << "\tT: " << T << endl;
    cout << prefix << "\tC: " << C << endl;
    cout << prefix << "\tOC: " << OC << endl;
    cout << prefix << "slice_x: " << slice_x << ", slice_y: " << slice_y
         << endl;
}

gpu_base *Matmul_f_gpu::clone() { return new Matmul_f_gpu(*this); }

void Matmul_f_gpu::parseJson(json j) {
    B = GetDefinedParam(j["B"]);
    T = GetDefinedParam(j["T"]);
    C = GetDefinedParam(j["C"]);
    OC = GetDefinedParam(j["OC"]);
    slice_x = j["slice_x"];
    slice_y = j["slice_y"];

    if (j.contains("compose")) {
        parse_compose(j["compose"]);
    }

    if (j.contains("address")) {
        parse_addr_label(j["address"]);
    }
}