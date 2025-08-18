#include "prims/gpu_prims.h"
#include "utils/memory_utils.h"
#include "utils/system_utils.h"

int matmul_forward_gpu_pd::task_core(TaskCoreContext &context) {
    int data_byte = 0;
    if (datatype == INT8)
        data_byte = 1;
    else if (datatype == FP16)
        data_byte = 2;
    B = B * gpu_B;
    T = find_var("T");

    // 这里记录的是总大小，实际取用的时候需要除以slice大小
    int data_size_input = B * T * C * data_byte;
    int data_size_weight = OC * C * data_byte;
    int data_size_bias = OC * data_byte;
    int data_size_out = B * T * OC * data_byte / (slice_x * slice_y) / 3;

    cout << "[GPU MATMUL PDS]: data size: T: " << T << endl;
    int mem_time = 0;
    auto input_mem_offset = 0;
    if (!gpu_pos_locator->findPair(datapass_label.indata[0],
                                   input_mem_offset)) {
        printf("[ERROR] matmul_forward_gpu_pd: gpu_pos_locator cannot find the "
               "label: "
               "%s\n",
               datapass_label.indata[0].c_str());
        sc_stop();
    }

    // 获取前缀label
    cout << "[GPU MATMUL PDS]: output label: " << datapass_label.outdata << endl;
    std::size_t pos = datapass_label.outdata.find_last_of('_');
    std::string prefix;
    if (pos != std::string::npos) {
        prefix = datapass_label.outdata.substr(0, pos);
    } else {
        prefix = datapass_label.outdata;
    }
    cout << "[GPU MATMUL PDS]: prefix: " << prefix << endl;

    auto label_weight = prefix + "_w";
    AddrPosKey w_key = AddrPosKey(0, data_size_weight);
    gpu_pos_locator->fetchPair(label_weight, w_key);

    auto label_bias = prefix + "_b";
    AddrPosKey b_key = AddrPosKey(0, data_size_bias);
    gpu_pos_locator->fetchPair(label_bias, b_key);

    cout << cid << " [matmul_forward_gpu_pd] before read1: " << mem_time << " at addr "
         << input_mem_offset << endl;

    int overlap_time = 0;
    AddrPosKey out_key;

#if USE_L1L2_CACHE == 1
if (gpu_inner == true){
    // 通过fetch_index计算位置
    int row_index = fetch_index / slice_x;
    int col_index = fetch_index % slice_x;

    // input 读入
    gpu_read_generic(context,
                     input_mem_offset + data_size_input / slice_y * row_index,
                     data_size_input / slice_y, mem_time);
#if GPU_CACHE_DEBUG == 1

    cout << " data_size_weight / slice_x " << data_size_weight / slice_x << endl;

#endif
    // weight 读入

    gpu_read_generic(context, w_key.pos + w_key.size / slice_x * col_index,
                     data_size_weight / slice_x, mem_time);
    // bias 读入
    gpu_read_generic(context, b_key.pos + b_key.size / slice_x * col_index,
                     data_size_bias / slice_x, mem_time);

    for (auto stage : batchInfo) {
        int size = 0;
        switch (job_type) {
        case JOB_PREFILL:
        case JOB_BOTH:
            size = data_byte * B * OC * stage.token_num / (slice_y * slice_x) / 3;
            break;
        case JOB_DECODE:
            size = data_byte * B * OC * 1 / (slice_y * slice_x) / 3;
            break;
        default:
            assert(false && "Unsupported job type");
        }

        cout << "[GPU MATMUL PD]: size: " << size << endl;

        char format_label_k[100];
        sprintf(format_label_k, "%s%s%sk#%d", prefix, ETERNAL_PREFIX, KVCACHE_PREFIX,
                stage.req_id);
        string label_k = format_label_k;

        char format_label_v[100];
        sprintf(format_label_v, "%s%s%sv#%d", prefix, ETERNAL_PREFIX, KVCACHE_PREFIX,
                stage.req_id);
        string label_v = format_label_v;

        gpu_pos_locator->updatePair(label_k, size);
        gpu_pos_locator->updatePair(label_v, size);

        AddrPosKey key_k, key_v;
        gpu_pos_locator->findPair(label_k, key_k);
        gpu_pos_locator->findPair(label_v, key_v);

        gpu_write_generic(context, key_k.pos + (key_k.size - size), size,
                          mem_time, false);
        gpu_write_generic(context, key_v.pos + (key_v.size - size), size,
                          mem_time, false);
    }


    
    gpu_pos_locator->updatePair(datapass_label.outdata, data_size_out);
    gpu_pos_locator->findPair(datapass_label.outdata, out_key);

    cout << cid << " [matmul_forward_gpu_pd] before write: " << mem_time
         << " at addr " << out_key.pos << endl;
    gpu_write_generic(context, out_key.pos + data_size_out * fetch_index,
                      data_size_out, mem_time);
    int cycle = 0;
    int cid = context.cid;
    ExuConfig *exu = get_exu_config(cid);
    SfuConfig *sfu = get_sfu_config(cid);
                    
    if (exu->type == MAC_Array)
        cycle += (B * T * C * OC * 2 / (slice_x * slice_y)) / (exu->x_dims * exu->y_dims * 2 * comp_util) * CYCLE;
    else
        assert(false && "Unsupported tile type");

    if (sfu->type == Linear)
        cycle += 0 / sfu->x_dims * CYCLE;
    else
        assert(false && "Unsupported tile type");
                    

    if (mem_time > cycle) {
        // 因为dram 已经wait 过了，所以额外的 overlap_time = 0
        overlap_time = 0;
        LOG_VERBOSE(1, context.cid, "Prim name:" << name << RED << " cycle: " << cycle << ", dram_time: " << mem_time << RESET);

        // std::cout << RED << "cycle: " << cycle << ", dram_time: " << dram_time
        //           << RESET << std::endl;

    } else {
        overlap_time = cycle - mem_time;
        LOG_VERBOSE(1, context.cid, "Prim name:" << name << GREEN << " cycle: " << cycle << ", dram_time: " << mem_time << RESET);

    }
    }else{
    int slice_total = slice_x * slice_y;
    // input 读入
    gpu_read_generic(context,
        input_mem_offset + data_size_input / slice_total * fetch_index,
        data_size_input / slice_total, mem_time);
#if GPU_CACHE_DEBUG == 1

    LOG_VERBOSE(1, context.cid," data_size_weight / slice_x " << data_size_weight / slice_x);


#endif
    // weight 读入
    // LOG_VERBOSE(1, context.cid," data_size_weight / slice_x " << data_size_weight / slice_x);

    gpu_read_generic(context, w_key.pos + w_key.size / slice_total * fetch_index,
            data_size_weight / slice_total, mem_time);
    // assert(false && "Unsupported job type");
    // bias 读入
    gpu_read_generic(context, b_key.pos + b_key.size / slice_total * fetch_index,
            data_size_bias / slice_total, mem_time);

    for (auto stage : batchInfo) {
        int size = 0;
        switch (job_type) {
        case JOB_PREFILL:
        case JOB_BOTH:
            size = data_byte * B * OC * stage.token_num / (slice_y * slice_x) / 3;
            break;
        case JOB_DECODE:
            size = data_byte * B * OC * 1 / (slice_y * slice_x) / 3;
            break;
        default:
            assert(false && "Unsupported job type");
        }

        char format_label_k[1000];
        sprintf(format_label_k, "%s%s%sk#%d", prefix.c_str(), ETERNAL_PREFIX, KVCACHE_PREFIX,
                stage.req_id);
        string label_k = format_label_k;

        char format_label_v[1000];
        sprintf(format_label_v, "%s%s%sv#%d", prefix.c_str(), ETERNAL_PREFIX, KVCACHE_PREFIX,
                stage.req_id);
        string label_v = format_label_v;

        gpu_pos_locator->updatePair(label_k, size);
        gpu_pos_locator->updatePair(label_v, size);

        AddrPosKey key_k, key_v;
        gpu_pos_locator->findPair(label_k, key_k);
        gpu_pos_locator->findPair(label_v, key_v);

        // LOG_VERBOSE(1, context.cid," matmul kv " << " prefix "<< prefix << " " << size << " key size " << key_k.size << " " << label_k);

        gpu_write_generic(context, key_k.pos + (key_k.size - size), size,
                          mem_time, false);
        gpu_write_generic(context, key_v.pos + (key_v.size - size), size,
                          mem_time, false);
    }


    gpu_pos_locator->updatePair(datapass_label.outdata, data_size_out);
    gpu_pos_locator->findPair(datapass_label.outdata, out_key);

    cout << cid << " [matmul_forward_gpu_pd] before write: " << mem_time
         << " at addr " << out_key.pos << endl;
    gpu_write_generic(context, out_key.pos + data_size_out * fetch_index,
                      data_size_out, mem_time);
    int cycle = 0;
    int cid = context.cid;
    ExuConfig *exu = get_exu_config(cid);
    SfuConfig *sfu = get_sfu_config(cid);
                    
    if (exu->type == MAC_Array)
        cycle += (B * T * C * OC * 2 / (slice_x * slice_y)) / (exu->x_dims * exu->y_dims * 2 * comp_util) * CYCLE;
    else
        assert(false && "Unsupported tile type");

    if (sfu->type == Linear)
        cycle += 0 / sfu->x_dims * CYCLE;
    else
        assert(false && "Unsupported tile type");
                    

    if (mem_time > cycle) {
        // 因为dram 已经wait 过了，所以额外的 overlap_time = 0
        overlap_time = 0;
        LOG_VERBOSE(1, context.cid, "Prim name:" << name << RED << " cycle: " << cycle << ", dram_time: " << mem_time << RESET);

        // std::cout << RED << "cycle: " << cycle << ", dram_time: " << dram_time
        //           << RESET << std::endl;

    } else {
        overlap_time = cycle - mem_time;
        LOG_VERBOSE(1, context.cid, "Prim name:" << name << GREEN << " cycle: " << cycle << ", dram_time: " << mem_time << RESET);

    }
    // cout << "B: " << B << ", T: " << T << ", C: " << C << ", OC: " << OC << endl;
    // assert(false);

}
#endif

    cout << cid << " [matmul_forward_gpu_pd] after write: " << mem_time
         << " at addr " << out_key.pos << endl;
    B = B / gpu_B;
    assert(B > 0);
    return overlap_time;
}

int matmul_forward_gpu_pd::task() { return 0; }

int matmul_forward_gpu_pd::sram_utilization(DATATYPE datatype, int cid) {
    return 0;
}

sc_bv<128> matmul_forward_gpu_pd::serialize() {
    sc_bv<128> d;
    d.range(7, 0) = sc_bv<8>(MATMUL_FORWARD_GPU_PD_TYPE);
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

void matmul_forward_gpu_pd::deserialize(sc_bv<128> buffer) {
    slice_x = buffer.range(15, 8).to_uint();
    slice_y = buffer.range(23, 16).to_uint();
    B = buffer.range(55, 40).to_uint();
    T = buffer.range(71, 56).to_uint();
    C = buffer.range(87, 72).to_uint();
    OC = buffer.range(103, 88).to_uint();
    datatype = (DATATYPE)buffer.range(105, 104).to_uint();
    fetch_index = buffer.range(121, 106).to_uint();
}

void matmul_forward_gpu_pd::print_self(string prefix) {
    cout << prefix << "matmul_forward_gpu_pd" << endl;
    cout << prefix << "\tB: " << B << endl;
    cout << prefix << "\tT: " << T << endl;
    cout << prefix << "\tC: " << C << endl;
    cout << prefix << "\tOC: " << OC << endl;
    cout << prefix << "slice_x: " << slice_x << ", slice_y: " << slice_y
         << endl;
}

void matmul_forward_gpu_pd::parse_json(json j) {
    B = find_var(j["B"]);
    T = find_var(j["T"]);
    C = find_var(j["C"]);
    OC = find_var(j["OC"]);
    slice_x = j["slice_x"];
    slice_y = j["slice_y"];

    if (j.contains("compose")) {
        parse_compose(j["compose"]);
    }

    if (j.contains("address")) {
        parse_addr_label(j["address"]);
    }
}