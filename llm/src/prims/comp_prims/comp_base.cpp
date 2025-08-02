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
                                 vector<int> data_size_input) {
#if USE_NB_DRAMSYS == 0
    auto wc = context.wc;
#endif
    auto mau = context.mau;
    auto hmau = context.hmau;
    auto sram_addr = context.sram_addr;
    // dahu ???
    int inp_sram_offset = *sram_addr;

#if DUMMY == 1
    float *dram_start = nullptr;
#else
    float *dram_start = (float *)(dram_array[cid]);
    float *inp = dram_start + inp_offset;
    float *out = dram_start + out_offset;
#endif
    LOG_VERBOSE(1, context.cid,"Prim name:" << name << " check_input_data ");

#if USE_SRAM == 1
    for (int p = 0; p < data_size_input.size(); p++) {
        if (datapass_label.indata[p].find(DRAM_LABEL) == 0) {
            size_t space_pos = datapass_label.indata[p].find(' ');
            if (space_pos != std::string::npos) {
                datapass_label.indata[p] =
                    datapass_label.indata[p].substr(space_pos + 1);
            }
            LOG_VERBOSE(1, context.cid,"Prim name:" << name << " comp_base: read from dram, label: " << datapass_label.indata[p].c_str());
            // printf("[INFO] comp_base: read from dram, label: %s\n",
            //        datapass_label.indata[p].c_str());
#if USE_SRAM_MANAGER == 1
            sram_first_write_generic(context, data_byte * data_size_input[p],
                                     inp_global_addr, dram_time, dram_start,
                                     datapass_label.indata[p], true,
                                     sram_pos_locator);
#else
            sram_first_write_generic(context, data_byte * data_size_input[p],
                                     inp_global_addr, dram_time, dram_start);
            AddrPosKey inp_key =
                AddrPosKey(*sram_addr, data_byte * data_size_input[p]);
            sram_pos_locator->addPair(datapass_label.indata[p], inp_key,
                                      context, dram_time);
#endif
        } else {


            AddrPosKey inp_key;
            LOG_VERBOSE(1, context.cid,"Prim name:" << name << " comp_base: read from sram, label: " << datapass_label.indata[p].c_str());

            // printf("[INFO] comp_base: read from sram, label: %s\n",
            //        datapass_label.indata[p].c_str());
            int flag =
                sram_pos_locator->findPair(datapass_label.indata[p], inp_key);
            if (flag == -1) {
                printf("[ERROR] comp_base: sram_pos_locator cannot find the "
                       "label: %s\n",
                       datapass_label.indata[p].c_str());
                sc_stop();
            } else if (flag > 0) {
                
#if USE_SRAM_MANAGER == 1
                LOG_VERBOSE(1, context.cid,"Prim name:" << name << " comp_base: sram_pos_locator find the label: " << datapass_label.indata[p] << " with flag: " << flag);

                // std::cout << "[INFO] comp_base: sram_pos_locator find the "
                //              "label: "
                //           << datapass_label.indata[p] << " with flag: " << flag
                //           << std::endl;
                sram_first_write_generic(
                    context, flag, inp_global_addr, dram_time, dram_start,
                    datapass_label.indata[p], true, sram_pos_locator);

#else
                LOG_VERBOSE(1, context.cid,"Prim name:" << name << " comp_base: sram has spill" );

                sram_first_write_generic(context, flag, inp_global_addr,
                                         dram_time, dram_start);
                inp_key.size = data_size_input[p];
                inp_key.spill_size = 0;
                sram_pos_locator->addPair(datapass_label.indata[p], inp_key,
                                          context, dram_time);
#endif
            } else {
                // send receive input data
#if USE_SRAM_MANAGER == 1
                LOG_VERBOSE(1, context.cid,"Prim name:" << name << " comp_base: send receive sram: " << datapass_label.indata[p] << " with flag: " << flag);

                AddrPosKey inp_key;
                int flag = sram_pos_locator->findPair(datapass_label.indata[p],
                                                      inp_key);
                if (inp_key.alloc_id == 0) {
                    sram_first_write_generic(
                        context, data_byte * data_size_input[p], inp_global_addr,
                        dram_time, dram_start, datapass_label.indata[p], true,
                        sram_pos_locator, true);
                }
#else
                LOG_VERBOSE(1, context.cid,"Prim name:" << name << " comp_base: send receive sram: " << datapass_label.indata[p] << " with flag: " << flag << " key size " << inp_key.size << " data size " << data_size_input[p]);

                inp_key.size = data_size_input[p];
                inp_key.spill_size = 0;
                sram_pos_locator->addPair(datapass_label.indata[p], inp_key,
                                          context, dram_time);


#endif
            }
#if USE_SRAM_MANAGER == 1

            // mla kvcache 
            AddrPosKey sc_key;
            sram_pos_locator->findPair(datapass_label.indata[p], sc_key);


            int data_bits = data_byte * data_size_input[p] * 8;
            assert((SRAM_BLOCK_SIZE * 8) % SRAM_BITWIDTH == 0 &&
                   "SRAM_BLOCK_SIZE * 8 must be a multiple of SRAM_BITWIDTH");
            int alignment = std::max(SRAM_BITWIDTH, SRAM_BLOCK_SIZE * 8);

            int aligned_data_bits =
                static_cast<int>(
                    std::ceil(static_cast<double>(data_bits) / alignment)) *
                alignment;
            int aligned_data_byte = aligned_data_bits / 8;
            if (sram_pos_locator->data_map[datapass_label.indata[p]].size <
                aligned_data_byte) {
                LOG_VERBOSE(1, context.cid,"Prim name:" << name << "\033[1;33m"<< "warning!! input output not mapping" << "\033[0m");

                // std::cout << "\033[1;33m"
                //           << "warning!! input output not mapping" << "\033[0m"
                //           << std::endl;
                auto sram_manager_ = context.sram_manager_;
#if ASSERT == 1
                assert(sram_pos_locator->validateTotalSize());
#endif

                // cout << "[INFO] comp_base: sram_pos_locator update the size
                // of " << aligned_data_byte -
                // sram_pos_locator->data_map[datapass_label.indata[p]].size <<
                // std::endl;
                int ori_size = sram_pos_locator->data_map[datapass_label.indata[p]].size;
                sc_key.size +=
                    aligned_data_byte -
                    sram_pos_locator->data_map[datapass_label.indata[p]].size;
                sram_pos_locator->addPair(datapass_label.indata[p], sc_key,
                                          context, dram_time, false);
                sram_manager_->allocate_append(
                    aligned_data_byte - ori_size,
                    sc_key.alloc_id);
                // sc_key.size +=
                //     aligned_data_byte -
                //     sram_pos_locator->data_map[datapass_label.indata[p]].size;
                // sram_pos_locator->addPair(datapass_label.indata[p], sc_key,
                //                           false);
#if ASSERT == 1 
                assert(sram_pos_locator->validateTotalSize());
#endif
            }
#else
            if (sram_pos_locator->data_map[datapass_label.indata[p]].size <
                inp_key.size) {
                // std::cout << "address " << (void*)&inp_key << "address " << (void*)&sram_pos_locator->data_map[datapass_label.indata[p]] << std::endl;
                // LOG_VERBOSE(1, context.cid,"Prim name:" << name <<  " key size " << inp_key.size << " data size " << sram_pos_locator->data_map[datapass_label.indata[p]].size);

                assert(false);
                LOG_VERBOSE(1, context.cid,"Prim name:" << name << "\033[1;33m"<< "warning!! input output not mapping" << "\033[0m");
                sram_pos_locator->addPair(datapass_label.indata[p], inp_key,
                                          false);  


                }


#endif
        }

#if USE_SRAM_MANAGER == 1
        AddrPosKey input_key;
        sram_pos_locator->findPair(datapass_label.indata[p], input_key);
        sram_pos_locator->printAllKeysWithAllocId();
        // Print allocation IDs for debugging
        LOG_VERBOSE(1, context.cid,"Prim name:" << name << "Input Key Allocation ID: " << input_key.alloc_id);

        // std::cout << "Input Key Allocation ID: " << input_key.alloc_id
        //           << std::endl;
        sram_read_generic(context, data_byte * data_size_input[p],
                          inp_sram_offset, dram_time, input_key.alloc_id, true,
                          sram_pos_locator);
#else
        // 读出input
        LOG_VERBOSE(1, context.cid,"Prim name:" << name << " read input ");

        sram_pos_locator->findPair(datapass_label.indata[p], inp_sram_offset);
        sram_read_generic(context, data_byte * data_size_input[p],
                          inp_sram_offset, dram_time);
#endif

        inp_global_addr += data_size_input[p] * data_byte;
    }
#endif
}



void comp_base::perf_read_data(TaskCoreContext &context, uint64_t &dram_time,
                                  int data_size_label, string label_name) {
#if USE_NB_DRAMSYS == 0
    auto wc = context.wc;
#endif
    auto sram_addr = context.sram_addr;


    AddrPosKey sc_key;
    int flag = sram_pos_locator->findPair(label_name, sc_key);
    if (flag == -1) {
        assert(false && "weight data not found");
    } else if (flag > 0) {
        assert(false && "weight data can not be spilled");
    }
    // dahu ??
    int sram_offset = 0;
    sram_pos_locator->findPair(label_name, sc_key);
#if USE_SRAM_MANAGER == 1
    sram_pos_locator->printAllKeysWithAllocId();
    // Print allocation IDs for debugging
    std::cout << label_name << " Key Allocation ID: " << sc_key.alloc_id
              << std::endl;

    sram_read_generic(context, data_byte * data_size_label, sram_offset,
                      dram_time, sc_key.alloc_id, true, sram_pos_locator);
#else

    sram_read_generic(context, data_byte * data_size_label, sram_offset,
                      dram_time);

#endif
}
void comp_base::check_static_data(TaskCoreContext &context, uint64_t &dram_time,
                                  uint64_t label_global_addr,
                                  int data_size_label, string label_name, bool use_pf) {
#if USE_NB_DRAMSYS == 0
    auto wc = context.wc;
#endif
    auto sram_addr = context.sram_addr;
    // dahu ??
    int sram_offset = *sram_addr;

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
        LOG_VERBOSE(1, context.cid,"Prim name:" << name << " weight data not found" );

#if USE_SRAM_MANAGER == 1
        sram_first_write_generic(context, data_byte * data_size_label,
                                 label_global_addr, dram_time, dram_start,
                                 label_name, true, sram_pos_locator);
#else
        sram_first_write_generic(context, data_byte * data_size_label,
                                 label_global_addr, dram_time, dram_start);

        sc_key = AddrPosKey(*sram_addr, data_byte * data_size_label);
        sram_pos_locator->addPair(label_name, sc_key, context, dram_time);
#endif
    } else if (flag > 0) {
        LOG_VERBOSE(1, context.cid,"Prim name:" << name << " weight data has spill" );
#if USE_SRAM_MANAGER == 1
        sram_first_write_generic(context, flag, label_global_addr, dram_time,
                                 dram_start, label_name, true,
                                 sram_pos_locator);

#else
        sram_first_write_generic(context, flag, label_global_addr, dram_time,
                                 dram_start);
        sc_key.size = data_byte * data_size_label;
        sc_key.spill_size = 0;
        sram_pos_locator->addPair(label_name, sc_key, context, dram_time);
#endif
    }
    
    
    sram_pos_locator->findPair(label_name, sc_key);
    LOG_VERBOSE(1, context.cid,"Prim name:" << name << " read weight data from sram" );
#if USE_SRAM_MANAGER == 1
    sram_pos_locator->printAllKeysWithAllocId();
    // Print allocation IDs for debugging
    std::cout << label_name << " Key Allocation ID: " << sc_key.alloc_id
              << std::endl;
    if (use_pf == false){
    sram_read_generic(context, data_byte * data_size_label, sram_offset,
                      dram_time, sc_key.alloc_id, true, sram_pos_locator);
    }
#else
    if (use_pf == false){
    sram_read_generic(context, data_byte * data_size_label, sram_offset,
                      dram_time);
    }
#endif
}

void comp_base::write_output_data(TaskCoreContext &context, int exu_flops,
                                  int sfu_flops, uint64_t dram_time,
                                  uint64_t &overlap_time, int data_size_out,
                                  uint64_t out_global_addr) {
    int cycle = 0;
    int cid = context.cid;
    ExuConfig *exu = get_exu_config(cid);
    SfuConfig *sfu = get_sfu_config(cid);

    if (exu->type == MAC_Array)
        cycle += exu_flops / (exu->x_dims * exu->y_dims * 2 * comp_util) * CYCLE;
    else
        assert(false && "Unsupported tile type");

    if (sfu->type == Linear)
        cycle += sfu_flops / sfu->x_dims * CYCLE;
    else
        assert(false && "Unsupported tile type");

#if USE_SRAM == 1
    if (dram_time > cycle) {
        // 因为dram 已经wait 过了，所以额外的 overlap_time = 0
        overlap_time = 0;
        LOG_VERBOSE(1, context.cid, "Prim name:" << name << RED << " cycle: " << cycle << ", dram_time: " << dram_time << RESET);

        // std::cout << RED << "cycle: " << cycle << ", dram_time: " << dram_time
        //           << RESET << std::endl;

    } else {
        overlap_time = cycle - dram_time;
        LOG_VERBOSE(1, context.cid, "Prim name:" << name << GREEN << " cycle: " << cycle << ", dram_time: " << dram_time << RESET);

    }

    // 写入out
    std::vector<std::string> out_labels;
    std::istringstream iss(datapass_label.outdata);
    std::string label;
    while (iss >> label)
        out_labels.push_back(label);

    int temp_out_sram_offset = *(context.sram_addr);


#if USE_SRAM_MANAGER == 1
    for (int i = 0; i < out_labels.size(); i++) {
        sram_write_append_generic(
            context, data_byte * data_size_out / out_labels.size(),
            overlap_time, out_labels[i], true, sram_pos_locator,
            out_global_addr +
                i * data_byte * data_size_out / out_labels.size());
    }
#else
    sram_write_append_generic(context, data_byte * data_size_out, overlap_time);
    auto interval =
        (*(context.sram_addr) - temp_out_sram_offset) / out_labels.size();

    for (int i = 0; i < out_labels.size(); i++) {
        AddrPosKey out_key =
            AddrPosKey(static_cast<int>(temp_out_sram_offset + i * interval),
                       data_byte * data_size_out / out_labels.size());
        // already wait in addPair do not add overlap_time
        sram_pos_locator->addPair(out_labels[i], out_key, context, dram_time);
    }
#endif
#endif
}