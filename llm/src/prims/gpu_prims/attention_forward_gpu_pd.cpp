#include "defs/global.h"
#include "defs/enums.h"
#include "prims/gpu_prims.h"
#include "utils/memory_utils.h"
#include "utils/system_utils.h"

int attention_forward_gpu_pd::task_core(TaskCoreContext &context) {
    int data_byte = 0;
    if (datatype == INT8) {
        data_byte = 1;
    } else if (datatype == FP16) {
        data_byte = 2;
    }
    B = B * gpu_B;

    int data_size_input = data_byte * B * T * C;       // QKV input
    int data_size_preatt = data_byte * B * NH * T * T; // preatt
    int data_size_att = data_byte * B * NH * T * T;    // att
    int data_size_out = data_byte * B * T * C / (slice_x * slice_y); // output

    int mem_time = 0;
    auto input_mem_offset = 0;
    if (!gpu_pos_locator->findPair(datapass_label.indata[0],
                                   input_mem_offset)) {
        printf("[ERROR] attention_forward_gpu_pd: gpu_pos_locator cannot find "
               "the label: "
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

    cout << cid << " [attention_forward_gpu_pd] before read1: " << mem_time
         << " at addr " << input_mem_offset << endl;

    int overlap_time = 0;
#if USE_L1L2_CACHE == 1
    for (auto stage : batchInfo) {
        char format_label_k[100];
        sprintf(format_label_k, "%s%sk#%d", ETERNAL_PREFIX, KVCACHE_PREFIX,
                stage.req_id);
        string label_k = format_label_k;

        char format_label_v[100];
        sprintf(format_label_v, "%s%sv#%d", ETERNAL_PREFIX, KVCACHE_PREFIX,
                stage.req_id);
        string label_v = format_label_v;

        AddrPosKey k_key, v_key;
        gpu_pos_locator->findPair(label_k, k_key);
        gpu_pos_locator->findPair(label_v, v_key);

        gpu_read_generic(
            context, k_key.pos + k_key.size / (slice_x * slice_y) * fetch_index,
            k_key.size / (slice_x * slice_y), mem_time);
        gpu_read_generic(
            context, v_key.pos + v_key.size / (slice_x * slice_y) * fetch_index,
            v_key.size / (slice_x * slice_y), mem_time);
    }

    gpu_write_generic(context,
                      p_key.pos +
                          data_size_preatt / (slice_x * slice_y) * fetch_index,
                      data_size_preatt / (slice_x * slice_y), mem_time);
    gpu_read_generic(context,
                     p_key.pos +
                         data_size_preatt / (slice_x * slice_y) * fetch_index,
                     data_size_preatt / (slice_x * slice_y), mem_time);

    gpu_write_generic(
        context, a_key.pos + data_size_att / (slice_x * slice_y) * fetch_index,
        data_size_att / (slice_x * slice_y), mem_time);
    gpu_read_generic(
        context, a_key.pos + data_size_att / (slice_x * slice_y) * fetch_index,
        data_size_att / (slice_x * slice_y), mem_time);

    // Q
    gpu_read_generic(context,
                     input_mem_offset + data_size_input /
                                            (3 * slice_x * slice_y) *
                                            fetch_index,
                     data_size_input / (3 * slice_x * slice_y), mem_time);

    // overlap_time = 0;
    AddrPosKey out_key;
    gpu_pos_locator->updatePair(datapass_label.outdata, data_size_out);
    gpu_pos_locator->findPair(datapass_label.outdata, out_key);

    gpu_write_generic(context, out_key.pos, data_size_out, mem_time);
    int cycle = 0;
    int cid = context.cid;
    ExuConfig *exu = get_exu_config(cid);
    SfuConfig *sfu = get_sfu_config(cid);
                  
    if (exu->type == MAC_Array)
        cycle += B * NH * T * (T - 1) / 2 * (4 * C / NH + 5) / (slice_x * slice_y) / (exu->x_dims * exu->y_dims * 2 * comp_util) * CYCLE;
    else
        assert(false && "Unsupported tile type");

    if (sfu->type == Linear)
        cycle += 0 / (slice_x * slice_y) / sfu->x_dims * CYCLE;
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
#endif

    cout << cid << " [attention_forward_gpu_pd] after write: " << overlap_time
         << endl;
    B = B / gpu_B;
    assert(B > 0);
    return overlap_time;
}

int attention_forward_gpu_pd::task() { return 0; }

int attention_forward_gpu_pd::sram_utilization(DATATYPE datatype, int cid) {
    return 0;
}

void attention_forward_gpu_pd::deserialize(sc_bv<128> buffer) {
    slice_x = buffer.range(15, 8).to_uint();
    slice_y = buffer.range(23, 16).to_uint();
    B = buffer.range(55, 40).to_uint64();
    T = buffer.range(71, 56).to_uint64();
    C = buffer.range(87, 72).to_uint64();
    NH = buffer.range(103, 88).to_uint64();
    datatype = DATATYPE(buffer.range(105, 104).to_uint64());
    fetch_index = buffer.range(121, 106).to_uint64();
}

sc_bv<128> attention_forward_gpu_pd::serialize() {
    sc_bv<128> d;
    d.range(7, 0) = sc_bv<8>(ATTENTION_FORWARD_GPU_PD_TYPE);
    d.range(15, 8) = sc_bv<8>(slice_x);
    d.range(23, 16) = sc_bv<8>(slice_y);
    d.range(55, 40) = sc_bv<16>(B);
    d.range(71, 56) = sc_bv<16>(T);
    d.range(87, 72) = sc_bv<16>(C);
    d.range(103, 88) = sc_bv<16>(NH);
    d.range(105, 104) = sc_bv<2>(datatype);
    d.range(121, 106) = sc_bv<16>(fetch_index);

    return d;
}

void attention_forward_gpu_pd::print_self(string prefix) {
    cout << prefix << "<attention_forward_gpu>\n";
    cout << prefix << "\tB: " << B << ", T: " << T << ", C: " << C << endl;
    cout << prefix << "slice_x: " << slice_x << ", slice_y: " << slice_y
         << endl;
}

gpu_base *attention_forward_gpu_pd::clone() {
    return new attention_forward_gpu_pd(*this);
}

void attention_forward_gpu_pd::parse_json(json j) {
    B = find_var(j["B"]);
    T = find_var(j["T"]);
    C = find_var(j["C"]);
    NH = find_var(j["NH"]);
    slice_x = j["slice_x"];
    slice_y = j["slice_y"];

    if (j.contains("compose")) {
        parse_compose(j["compose"]);
    }

    if (j.contains("address")) {
        parse_addr_label(j["address"]);
    }
}