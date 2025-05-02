#include "systemc.h"

#include "defs/global.h"
#include "memory/dram/Dcachecore.h"
#include "prims/comp_base.h"
#include "prims/comp_prims.h"
#include "utils/memory_utils.h"
#include "utils/system_utils.h"

void Max_pool::print_self(string prefix) {
    cout << prefix << "<Maxpool_forward>\n";
    cout << prefix << "\tB: " << B << ", W: " << W << ", H: " << H
         << ", C:" << C << endl;
    cout << prefix << "\tpX: " << pX << ", pY: " << pY << ", sX: " << sX
         << ", sY:" << sY << endl;
    cout << prefix << "\toW: " << oW << ", oH: " << oH << ", oC: " << oC
         << endl;
    cout << prefix << "\tout_size: " << out_size << " , inp_size: " << inp_size
         << ", previous_inp_size: " << p_inp_size << endl;
    cout << prefix << "\toutput_offset: " << out_offset
         << ", input_offset: " << inp_offset << endl;
}

void Max_pool::initialize() {
    oH = (H + 2 * pY - kY) / sY + 1;
    oW = (W + 2 * pX - kX) / sX + 1;
    oC = C;

    out_size = B * oC * oH * oW;
    p_inp_size = B * C * H * W;
    inp_size = B * C * H * W;

    out_dim.push_back(B);
    out_dim.push_back(oC);
    out_dim.push_back(oH);
    out_dim.push_back(oW);
}

void Max_pool::parse_json(json j) {
    B = find_var(j["B"]);
    W = find_var(j["W"]);
    H = find_var(j["H"]);
    C = find_var(j["C"]);
    pX = find_var(j["pX"]);
    pY = find_var(j["pY"]);
    sX = find_var(j["sX"]);
    sY = find_var(j["sY"]);
    kX = find_var(j["kX"]);
    kY = find_var(j["kY"]);


    initialize();

    if (j.contains("dram_address")) {
        parse_address(j["dram_address"]);
    }

    if (j.contains("sram_address")) {
        parse_sram_label(j["sram_address"]);
    }
}


int Max_pool::sram_utilization(DATATYPE datatype) {

    int total_sram = 0;
    int data_byte = 0;
    if (datatype == INT8) {
        data_byte = 1;
    } else if (datatype == FP16) {
        data_byte = 2;
    }

    int p_inp_sram =
        ceiling_division(B * C * H * W * data_byte * 8, SRAM_BITWIDTH);
    int out_sram = ceiling_division(out_size * data_byte * 8, SRAM_BITWIDTH);


    total_sram = p_inp_sram + out_sram;

    return total_sram;
}

void Max_pool::deserialize(sc_bv<128> buffer) {
    inp_offset = buffer.range(23, 8).to_uint64();
    inp_offset = inp_offset * 1024;
    out_offset = buffer.range(39, 24).to_uint64();
    out_offset = out_offset * 1024;
    W = buffer.range(55, 40).to_uint64();
    H = buffer.range(71, 56).to_uint64();
    C = buffer.range(83, 72).to_uint64();
    pX = buffer.range(87, 84).to_uint64();
    pY = buffer.range(91, 88).to_uint64();
    sX = buffer.range(95, 92).to_uint64();
    sY = buffer.range(99, 96).to_uint64();
    kX = buffer.range(103, 100).to_uint64();
    kY = buffer.range(107, 104).to_uint64();

    B = buffer.range(123, 120).to_uint64();
    datatype = DATATYPE(buffer.range(125, 124).to_uint64());

    initialize();

    k_offset = B * C * H * W + inp_offset;
    b_offset = 1 * C * kX * kY + k_offset;
}

sc_bv<128> Max_pool::serialize() {
    sc_bv<128> d;
    d.range(7, 0) = sc_bv<8>(0x13);
    d.range(23, 8) = sc_bv<16>(inp_offset);
    d.range(39, 24) = sc_bv<16>(out_offset);
    d.range(55, 40) = sc_bv<16>(W);
    d.range(71, 56) = sc_bv<16>(H);
    d.range(83, 72) = sc_bv<12>(C);
    d.range(87, 84) = sc_bv<4>(pX);
    d.range(91, 88) = sc_bv<4>(pY);
    d.range(95, 92) = sc_bv<4>(sX);
    d.range(99, 96) = sc_bv<4>(sY);
    d.range(103, 100) = sc_bv<4>(kX);
    d.range(107, 104) = sc_bv<4>(kY);

    d.range(123, 120) = sc_bv<4>(B);
    d.range(125, 124) = sc_bv<2>(datatype);

    return d;
}

int Max_pool::task_core(TaskCoreContext &context) {
#if USE_NB_DRAMSYS == 0
    auto wc = context.wc;
#endif
    auto mau = context.mau;
    auto hmau = context.hmau;
    auto &msg_data = context.msg_data;
    auto sram_addr = context.sram_addr;

    int data_byte = 0;
    if (datatype == INT8) {
        data_byte = 1;
    } else if (datatype == FP16) {
        data_byte = 2;
    }
    u_int64_t dram_addr_tile = cid * dataset_words_per_tile * 4;
    u_int64_t out_global_addr = dram_addr_tile + out_offset * data_byte;
    u_int64_t inp_global_addr = dram_addr_tile + inp_offset * data_byte;


#if DUMMY == 1
    float *dram_start = nullptr;
#else
    float *dram_start = (float *)(dram_array[cid]);
    float *input = dram_start + inp_offset;
    float *output = dram_start + out_offset;
    float *kernel = dram_start + k_offset;
    float *bias = dram_start + b_offset;
#endif
    u_int64_t dram_time = 0;


    int data_size_input = B * C * H * W;

    int data_size_out = B * oC * oH * oW;

#if USE_SRAM == 1
    // 检查是否可以在此原语结束之后立刻释放中间结果
    bool input_reuse = false;
    if (datapass_label.indata[0][0] == '_') {
        input_reuse = true;
        datapass_label.indata[0] = datapass_label.indata[0].substr(1);
    }

    auto inp_sram_offset = 0;
    if (datapass_label.indata[0].find(DRAM_LABEL) == 0) {
        sram_first_write_generic(context, data_byte * data_size_input,
                                 inp_global_addr, dram_time, dram_start);

        size_t space_pos = datapass_label.indata[0].find(' ');
        if (space_pos != std::string::npos) {
            datapass_label.indata[0] =
                datapass_label.indata[0].substr(space_pos + 1);
        }

        printf("[INFO] Max_pool: read from dram, label: %s\n",
               datapass_label.indata[0].c_str());

        AddrPosKey inp_key =
            AddrPosKey(*sram_addr, data_byte * data_size_input);
        sram_pos_locator->addPair(datapass_label.indata[0], inp_key, context,
                                  dram_time);
    } else {
        AddrPosKey inp_key;
        int flag = sram_pos_locator->findPair(datapass_label.indata[0],
                                               inp_sram_offset);
        printf("[INFO] Max_pool: read from sram, label: %s, value: %d\n",
               datapass_label.indata[0].c_str(), inp_sram_offset);
        if (flag == -1) {
            printf("[ERROR] Max_pool: sram_pos_locator cannot find the label: "
                   "%s\n",
                   datapass_label.indata[0].c_str());
            sc_stop();
        } else if (flag > 0) {
            sram_first_write_generic(context, flag, inp_global_addr, dram_time,
                                     dram_start);
            inp_key.size = data_byte * data_size_input;
            inp_key.spill_size = 0;
            sram_pos_locator->addPair(datapass_label.indata[0], inp_key,
                                      context, dram_time);
        }
    }

    printf("Max_pool: dram time 1: %ld\n", dram_time);

    // 简化读出所有数据即可
    sram_pos_locator->findPair(datapass_label.indata[0], inp_sram_offset);
    sram_read_generic(context, data_byte * data_size_input, inp_sram_offset,
                      dram_time);

    // 删除标签
    if (!input_reuse) {
        sram_pos_locator->deletePair(datapass_label.indata[0]);
    }

    printf("Max_pool: dram time 2: %ld\n", dram_time);
#else
    assert(false && "Unsupported USE_SRAM == 0");
#endif

    u_int64_t cycle = 0;
    if (tile_sfu.type == Linear) {
        cycle = B * oC * oH * oW * kX * kY / (tile_sfu.x_dims) * CYCLE;
    } else {
        assert(false && "Unsupported tile type");
    }
    u_int64_t overlap_time = 0;

#if USE_SRAM == 1
    if (dram_time > cycle) {
        // 因为dram 已经wait 过了，所以额外的 overlap_time = 0
        overlap_time = 0;
        std::cout << RED << "cycle: " << cycle << ", dram_time: " << dram_time
                  << RESET << std::endl;

    } else {
        overlap_time = cycle - dram_time;
        std::cout << GREEN << "cycle: " << cycle << ", dram_time: " << dram_time
                  << RESET << std::endl;
    }
#else
    if (dram_time > cycle) {
        overlap_time = dram_time;
    } else {
        overlap_time = cycle;
    }
#endif

#if USE_SRAM == 1
    // 写入out
    // label kv in sram locator
    AddrPosKey out_key = AddrPosKey(*sram_addr, data_byte * data_size_out);
    sram_pos_locator->addPair(datapass_label.outdata, out_key, context,
                              dram_time);
    sram_write_append_generic(context, data_byte * data_size_out, overlap_time);
#else
    assert(false && "Unsupported USE_SRAM == 0");
#endif
    printf("output conv_f_cycle %ld \n", overlap_time);
    return overlap_time;
}


int Max_pool::task() {
    // CTODO: 计算cycle数
    // TODO DAHU TIME ??


#if DUMMY == 1


#else
    assert(false && "Unsupported DUMMY == 0");

#endif
    return 0;
}
