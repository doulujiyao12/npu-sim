#include "prims/comp_base.h"
#include "utils/memory_utils.h"
#include "utils/system_utils.h"

#include "common/memory.h"
#include "memory/dram/Dcachecore.h"

void comp_base::parse_address(json j) {
    if (j.contains("input")) {
        const auto &inputVal = j["input"];
        if (inputVal.is_number_integer())
            inp_offset = inputVal;
        else
            inp_offset = find_var(j["input"]);
    } else
        inp_offset = -1;

    if (j.contains("data")) {
        const auto &dataVal = j["data"];
        if (dataVal.is_number_integer())
            data_offset = dataVal;
        else
            data_offset = find_var(j["data"]);
    } else
        data_offset = -1;

    if (j.contains("out")) {
        const auto &outputVal = j["out"];
        if (outputVal.is_number_integer())
            out_offset = outputVal;
        else
            out_offset = find_var(j["out"]);
    } else
        out_offset = -1;
}

void comp_base::parse_sram_label(json j) {
    string in_label = j["indata"];
    datapass_label.outdata = j["outdata"];

    std::vector<std::string> in_labels;

    std::istringstream iss(in_label);
    std::string word;
    std::string temp;

    // 保证DRAM_LABEL后面跟着另一个label
    while (iss >> word) {
        if (word == DRAM_LABEL || word == "_" + string(DRAM_LABEL)) {
            temp = word;
            if (iss >> word) {
                temp += " " + word;
            }
            in_labels.push_back(temp);
        } else {
            in_labels.push_back(word);
        }
    }

    for (int i = 0; i < in_labels.size(); i++) {
        datapass_label.indata[i] = in_labels[i];
    }
}

void comp_base::check_input_data(TaskCoreContext &context, uint64_t &dram_time,
                                 uint64_t inp_global_addr,
                                 int data_size_input) {
#if USE_NB_DRAMSYS == 0
    auto wc = context.wc;
#endif
    auto mau = context.mau;
    auto hmau = context.hmau;
    auto &msg_data = context.msg_data;
    auto sram_addr = context.sram_addr;

    int inp_sram_offset = 0;

#if DUMMY == 1
    float *dram_start = nullptr;
#else
    float *dram_start = (float *)(dram_array[cid]);
    float *inp = dram_start + inp_offset;
    float *out = dram_start + out_offset;
#endif

#if USE_SRAM == 1
    if (datapass_label.indata[0].find(DRAM_LABEL) == 0) {
        sram_first_write_generic(context, data_byte * data_size_input,
                                 inp_global_addr, dram_time, dram_start);

        size_t space_pos = datapass_label.indata[0].find(' ');
        if (space_pos != std::string::npos) {
            datapass_label.indata[0] =
                datapass_label.indata[0].substr(space_pos + 1);
        }

        printf("[INFO] rope_f: read from dram, label: %s\n",
               datapass_label.indata[0].c_str());

        AddrPosKey inp_key =
            AddrPosKey(*sram_addr, data_byte * data_size_input);
        sram_pos_locator->addPair(datapass_label.indata[0], inp_key, context,
                                  dram_time);
    } else {
        AddrPosKey inp_key;
        int flag = sram_pos_locator->findPair(datapass_label.indata[0],
                                              inp_sram_offset);
        if (flag == -1) {
            printf(
                "[ERROR] rope_f: sram_pos_locator cannot find the label: %s\n",
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

    // 读出inp
    sram_pos_locator->findPair(datapass_label.indata[0], inp_sram_offset);
    sram_read_generic(context, data_byte * data_size_input, inp_sram_offset,
                      dram_time);
#endif
}

void comp_base::check_static_data(TaskCoreContext &context, uint64_t &dram_time,
                                  uint64_t label_global_addr,
                                  int data_size_label, string label_name) {
#if USE_NB_DRAMSYS == 0
    auto wc = context.wc;
#endif
    auto sram_addr = context.sram_addr;

#if DUMMY == 1
    float *dram_start = nullptr;
#else
    float *dram_start = (float *)(dram_array[cid]);
    float *inp = dram_start + inp_offset;
    float *out = dram_start + out_offset;
#endif

    AddrPosKey sc_key;
    int flag = sram_pos_locator->findPair(label_name, sc_key);
    if (flag == -1) {
#if USE_SRAM_MANAGER == 1
        sram_first_write_generic(context, data_byte * data_size_weight,
                                 weight_global_addr, dram_time, dram_start,
                                 label_weight, true, sram_pos_locator);
#else
        sram_first_write_generic(context, data_byte * data_size_label,
                                 label_global_addr, dram_time, dram_start);

        sc_key = AddrPosKey(*sram_addr, data_byte * data_size_label);
        sram_pos_locator->addPair(label_name, sc_key, context, dram_time);
#endif
    } else if (flag > 0) {
#if USE_SRAM_MANAGER == 1
        sram_first_write_generic(context, flag, weight_global_addr, dram_time,
                                 dram_start, label_weight, true,
                                 sram_pos_locator);

#else
        sram_first_write_generic(context, flag, label_global_addr, dram_time,
                                 dram_start);
        sc_key.size = data_byte * data_size_label;
        sc_key.spill_size = 0;
        sram_pos_locator->addPair(label_name, sc_key, context, dram_time);
#endif
    }
}

void comp_base::write_output_data(TaskCoreContext &context, int flops,
                                  uint64_t dram_time, uint64_t &overlap_time,
                                  int data_size_out) {
    int cycle = 0;
    if (tile_exu.type == MAC_Array)
        cycle = flops / (tile_exu.x_dims * tile_exu.y_dims * comp_util) * CYCLE;
    else
        assert(false && "Unsupported tile type");

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

    // 写入out
    AddrPosKey out_key =
        AddrPosKey(*(context.sram_addr), data_byte * data_size_out);
    sram_pos_locator->addPair(datapass_label.outdata, out_key, context,
                              dram_time);
    sram_write_append_generic(context, data_byte * data_size_out, overlap_time);
#endif
}