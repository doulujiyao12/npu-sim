#include "prims/base.h"
#include "utils/config_utils.h"
#include "utils/memory_utils.h"
#include "utils/prim_utils.h"
#include "utils/print_utils.h"
#include "utils/system_utils.h"

void NpuBase::parseAddress(json j) {
    SetParamFromJson(j, "input", &inp_offset);
    SetParamFromJson(j, "data", &data_offset,
                     inp_offset + input_size * data_byte);

    int total_data_size = 0;
    for (auto &pair : data_chunk)
        total_data_size += pair.second;

    SetParamFromJson(j, "output", &out_offset,
                     data_offset + total_data_size * data_byte);
}

void NpuBase::parseSramLabel(json j) {
    string in_label = j["indata"];
    prim_context->datapass_label_->outdata = j["outdata"];

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
        prim_context->datapass_label_->indata[i] = in_labels[i];
    }
}

sc_bv<128> NpuBase::serialize() {
    sc_bv<128> d;
    d.range(7, 0) = sc_bv<8>(PrimFactory::getInstance().getPrimId(name));
    d.range(8, 8) = sc_bv<1>(datatype);
    d.range(24, 9) = sc_bv<16>(inp_offset);
    d.range(40, 25) = sc_bv<16>(data_offset);
    d.range(56, 41) = sc_bv<16>(out_offset);

    // 所有参数平均分配
    if (param_name.size() > 10) {
        ARGUS_EXIT("Primitive with # params = ", param_name.size(),
                   " is not supported.\n");
        return d;
    }

    int pos = 57;
    if (param_name.size() <= 2) {
        for (auto &pair : param_value) {
            d.range(pos + 31, pos) = sc_bv<32>(pair.second);
            pos += 32;
        }
    } else if (param_name.size() <= 4) {
        for (auto &pair : param_value) {
            d.range(pos + 15, pos) = sc_bv<16>(pair.second);
            pos += 16;
        }
    } else {
        for (auto &pair : param_value) {
            d.range(pos + 7, pos) = sc_bv<8>(pair.second);
            pos += 8;
        }
    }
}

void NpuBase::deserialize(sc_bv<128> buffer) {
    datatype = DATATYPE(buffer.range(8, 8).to_uint64());
    inp_offset = buffer.range(24, 9).to_uint64();
    data_offset = buffer.range(40, 25).to_uint64();
    out_offset = buffer.range(56, 41).to_uint64();

    int pos = 57;
    if (param_name.size() <= 2) {
        for (auto &param : param_name) {
            param_value[param] = buffer.range(pos + 31, pos).to_uint64();
            pos += 32;
        }
    } else if (param_name.size() <= 4) {
        for (auto &param : param_name) {
            param_value[param] = buffer.range(pos + 15, pos).to_uint64();
            pos += 16;
        }
    } else {
        for (auto &param : param_name) {
            param_value[param] = buffer.range(pos + 7, pos).to_uint64();
            pos += 8;
        }
    }

    initialize();
    initializeDefault();
}

void NpuBase::parseJson(json j) {
    for (auto &param : param_name) {
        SetParamFromJson(j, param, &param_value[param]);
    }

    initialize();
    initializeDefault();

    if (j.contains("dram_address"))
        parseAddress(j["dram_address"]);

    if (j.contains("sram_address"))
        parseSramLabel(j["sram_address"]);

    cout << "\033[1;33m" << name << "\033[0m" << endl;
    cout << "inp_offset: " << inp_offset << endl;
    cout << "out_offset: " << out_offset << endl;
}

int NpuBase::sramUtilization(DATATYPE datatype, int cid) {
    int total_sram = 0;

    total_sram += CeilingDivision(input_size * data_byte * 8,
                                  GetCoreHWConfig(cid)->sram_bitwidth);

    for (auto &pair : data_chunk) {
        total_sram += CeilingDivision(pair.second * data_byte * 8,
                                      GetCoreHWConfig(cid)->sram_bitwidth);
    }

    total_sram *= GetCoreHWConfig(cid)->sram_bitwidth / 8;
    return total_sram;
}

void NpuBase::initializeDefault() {
    if (datatype == INT8)
        data_byte = 1;
    else if (datatype == FP16)
        data_byte = 2;

    input_size = 0;
    for (auto &input : data_size_input)
        input_size += input;

    out_size = -1;
    for (const auto &chunk : data_chunk) {
        if (chunk.first == "output") {
            out_size = chunk.second;
            break;
        }
    }
    if (out_size < 0) {
        ARGUS_EXIT("No output chunk found.\n");
        return;
    }

    int pos = data_offset;
    for (auto &chunk : data_chunk) {
        data_chunk_addr[chunk.first] = pos;
        pos += chunk.second * data_byte;
    }
}

int NpuBase::taskCoreDefault(TaskCoreContext &context) {
    // 所用时间
    u_int64_t dram_time = 0;
    u_int64_t overlap_time = 0;

    // 检查数据重利用
    bool input_reuse[data_size_input.size()];
    for (int i = 0; i < data_size_input.size(); i++) {
        input_reuse[i] = false;
        if (prim_context->datapass_label_->indata[i][0] == '_') {
            input_reuse[i] = true;
            prim_context->datapass_label_->indata[i] =
                prim_context->datapass_label_->indata[i].substr(1);
        }
    }

    // 获取前缀label
    std::size_t pos = prim_context->datapass_label_->outdata.find_last_of('_');
    std::string prefix;
    if (pos != std::string::npos)
        prefix = prim_context->datapass_label_->outdata.substr(0, pos);
    else
        prefix = prim_context->datapass_label_->outdata;

    // 读入input数据
    checkInputData(context, dram_time, inp_offset, data_size_input);

    u_int64_t exu_flops = 0;
    u_int64_t sfu_flops = 0;
#if USE_SRAM == 1
    {
        // 自定义task
        taskCore(context, prefix, dram_time, exu_flops, sfu_flops);

        // 删除标签
        for (int i = 0; i < data_size_input.size(); i++) {
            if (!input_reuse[i] &&
                prim_context->datapass_label_->indata[i] != UNSET_LABEL)
                prim_context->sram_pos_locator_->deletePair(
                    prim_context->datapass_label_->indata[i]);
        }
    }
#endif

    // 计算overlap并写回output数据
    writeOutputData(context, exu_flops, sfu_flops, dram_time, overlap_time,
                    out_size, data_chunk_addr["output"]);

    return overlap_time;
}

void NpuBase::checkInputData(TaskCoreContext &context, uint64_t &dram_time,
                             uint64_t inp_global_addr,
                             vector<int> data_size_input) {
#if USE_NB_DRAMSYS == 0
    auto wc = context.wc;
#endif
    auto mau = context.mau;
    auto hmau = context.hmau;
    auto sram_addr = context.sram_addr;
    int inp_sram_offset = *sram_addr;

#if DUMMY == 1
    float *dram_start = nullptr;
#else
    float *dram_start = (float *)(dram_array[cid]);
    float *inp = dram_start + inp_offset;
    float *out = dram_start + out_offset;
#endif
    LOG_VERBOSE(1, context.cid, "Prim name:" << name << " checkInputData ");

#if USE_SRAM == 1
    for (int p = 0; p < data_size_input.size(); p++) {
        if (prim_context->datapass_label_->indata[p].find(DRAM_LABEL) == 0) {
            size_t space_pos =
                prim_context->datapass_label_->indata[p].find(' ');
            if (space_pos != std::string::npos) {
                prim_context->datapass_label_->indata[p] =
                    prim_context->datapass_label_->indata[p].substr(space_pos +
                                                                    1);
            }
            LOG_VERBOSE(
                1, context.cid,
                "Prim name:"
                    << name << " NpuBase: read from dram, label: "
                    << prim_context->datapass_label_->indata[p].c_str());
            // printf("[INFO] NpuBase: read from dram, label: %s\n",
            //        prim_context->datapass_label_->indata[p].c_str());
#if USE_SRAM_MANAGER == 1
            sram_first_write_generic(context, data_byte * data_size_input[p],
                                     inp_global_addr, dram_time, dram_start,
                                     prim_context->datapass_label_->indata[p],
                                     true, prim_context->sram_pos_locator_);
#else
            sram_first_write_generic(context, data_byte * data_size_input[p],
                                     inp_global_addr, dram_time, dram_start);
            AddrPosKey inp_key =
                AddrPosKey(*sram_addr, data_byte * data_size_input[p]);
            prim_context->sram_pos_locator_->addPair(
                prim_context->datapass_label_->indata[p], inp_key, context,
                dram_time);
#endif
        } else {


            AddrPosKey inp_key;
            LOG_VERBOSE(
                1, context.cid,
                "Prim name:"
                    << name << " NpuBase: read from sram, label: "
                    << prim_context->datapass_label_->indata[p].c_str());

            // printf("[INFO] NpuBase: read from sram, label: %s\n",
            //        prim_context->datapass_label_->indata[p].c_str());
            int flag = prim_context->sram_pos_locator_->findPair(
                prim_context->datapass_label_->indata[p], inp_key);
            if (flag == -1) {
                printf("[ERROR] NpuBase: sram_pos_locator_ cannot find the "
                       "label: %s\n",
                       prim_context->datapass_label_->indata[p].c_str());
                sc_stop();
            } else if (flag > 0) {

#if USE_SRAM_MANAGER == 1
                LOG_VERBOSE(
                    1, context.cid,
                    "Prim name:"
                        << name
                        << " NpuBase: sram_pos_locator_ find the label: "
                        << prim_context->datapass_label_->indata[p]
                        << " with flag: " << flag);

                // std::cout << "[INFO] NpuBase: sram_pos_locator_ find the "
                //              "label: "
                //           << prim_context->datapass_label_->indata[p] << "
                //           with flag: " << flag
                //           << std::endl;
                sram_first_write_generic(
                    context, flag, inp_global_addr, dram_time, dram_start,
                    prim_context->datapass_label_->indata[p], true,
                    prim_context->sram_pos_locator_);

#else
                LOG_VERBOSE(1, context.cid,
                            "Prim name:" << name << " NpuBase: sram has spill");

                sram_first_write_generic(context, flag, inp_global_addr,
                                         dram_time, dram_start);
                inp_key.size = data_size_input[p];
                inp_key.spill_size = 0;
                prim_context->sram_pos_locator_->addPair(
                    prim_context->datapass_label_->indata[p], inp_key, context,
                    dram_time);
#endif
            } else {
                // send receive input data
#if USE_SRAM_MANAGER == 1
                LOG_VERBOSE(1, context.cid,
                            "Prim name:"
                                << name << " NpuBase: send receive sram: "
                                << prim_context->datapass_label_->indata[p]
                                << " with flag: " << flag);

                AddrPosKey inp_key;
                int flag = sram_pos_locator_->findPair(
                    prim_context->datapass_label_->indata[p], inp_key);
                if (inp_key.alloc_id == 0) {
                    sram_first_write_generic(
                        context, data_byte * data_size_input[p],
                        inp_global_addr, dram_time, dram_start,
                        prim_context->datapass_label_->indata[p], true,
                        prim_context->sram_pos_locator_, true);
                }
#else
                LOG_VERBOSE(1, context.cid,
                            "Prim name:"
                                << name << " NpuBase: send receive sram: "
                                << prim_context->datapass_label_->indata[p]
                                << " with flag: " << flag << " key size "
                                << inp_key.size << " data size "
                                << data_size_input[p]);

                inp_key.size = data_size_input[p];
                inp_key.spill_size = 0;
                prim_context->sram_pos_locator_->addPair(
                    prim_context->datapass_label_->indata[p], inp_key, context,
                    dram_time);


#endif
            }
#if USE_SRAM_MANAGER == 1

            // mla kvcache
            AddrPosKey sc_key;
            prim_context->sram_pos_locator_->findPair(
                prim_context->datapass_label_->indata[p], sc_key);


            int data_bits = data_byte * data_size_input[p] * 8;
            assert((SRAM_BLOCK_SIZE * 8) % SRAM_BITWIDTH == 0 &&
                   "SRAM_BLOCK_SIZE * 8 must be a multiple of SRAM_BITWIDTH");
            int alignment = std::max(SRAM_BITWIDTH, SRAM_BLOCK_SIZE * 8);

            int aligned_data_bits =
                static_cast<int>(
                    std::ceil(static_cast<double>(data_bits) / alignment)) *
                alignment;
            int aligned_data_byte = aligned_data_bits / 8;
            if (prim_context->sram_pos_locator_
                    ->data_map[prim_context->datapass_label_->indata[p]]
                    .size < aligned_data_byte) {
                LOG_VERBOSE(1, context.cid,
                            "Prim name:" << name << "\033[1;33m"
                                         << "warning!! input output not mapping"
                                         << "\033[0m");

                // std::cout << "\033[1;33m"
                //           << "warning!! input output not mapping" <<
                //           "\033[0m"
                //           << std::endl;
                auto sram_manager_ = context.sram_manager_;
#if ASSERT == 1
                assert(prim_context->sram_pos_locator_->validateTotalSize());
#endif

                // cout << "[INFO] NpuBase: sram_pos_locator_ update the size
                // of " << aligned_data_byte -
                // sram_pos_locator_->data_map[prim_context->datapass_label_->indata[p]].size
                // << std::endl;
                int ori_size =
                    prim_context->sram_pos_locator_
                        ->data_map[prim_context->datapass_label_->indata[p]]
                        .size;
                sc_key.size +=
                    aligned_data_byte -
                    prim_context->sram_pos_locator_
                        ->data_map[prim_context->datapass_label_->indata[p]]
                        .size;
                prim_context->sram_pos_locator_->addPair(
                    prim_context->datapass_label_->indata[p], sc_key, context,
                    dram_time, false);
                prim_context->sram_manager_->allocate_append(
                    aligned_data_byte - ori_size, sc_key.alloc_id);
                // sc_key.size +=
                //     aligned_data_byte -
                //     sram_pos_locator_->data_map[prim_context->datapass_label_->indata[p]].size;
                // sram_pos_locator_->addPair(prim_context->datapass_label_->indata[p],
                // sc_key,
                //                           false);
#if ASSERT == 1
                assert(prim_context->sram_pos_locator_->validateTotalSize());
#endif
            }
#else
            if (prim_context->sram_pos_locator_
                    ->data_map[prim_context->datapass_label_->indata[p]]
                    .size < inp_key.size) {
                // std::cout << "address " << (void*)&inp_key << "address " <<
                // (void*)&sram_pos_locator_->data_map[prim_context->datapass_label_->indata[p]]
                // << std::endl; LOG_VERBOSE(1, context.cid,"Prim name:" << name
                // <<  " key size " << inp_key.size << " data size " <<
                // sram_pos_locator_->data_map[prim_context->datapass_label_->indata[p]].size);

                assert(false);
                LOG_VERBOSE(1, context.cid,
                            "Prim name:" << name << "\033[1;33m"
                                         << "warning!! input output not mapping"
                                         << "\033[0m");
                prim_context->sram_pos_locator_->addPair(
                    prim_context->datapass_label_->indata[p], inp_key, false);
            }


#endif
        }

#if USE_SRAM_MANAGER == 1
        AddrPosKey input_key;
        prim_context->sram_pos_locator_->findPair(
            prim_context->datapass_label_->indata[p], input_key);
        prim_context->sram_pos_locator_->printAllKeysWithAllocId();
        // Print allocation IDs for debugging
        LOG_VERBOSE(1, context.cid,
                    "Prim name:" << name << "Input Key Allocation ID: "
                                 << input_key.alloc_id);

        // std::cout << "Input Key Allocation ID: " << input_key.alloc_id
        //           << std::endl;
        sram_read_generic(context, data_byte * data_size_input[p],
                          inp_sram_offset, dram_time, input_key.alloc_id, true,
                          prim_context->sram_pos_locator_);
#else
        // 读出input
        LOG_VERBOSE(1, context.cid, "Prim name:" << name << " read input ");

        prim_context->sram_pos_locator_->findPair(
            prim_context->datapass_label_->indata[p], inp_sram_offset);
        sram_read_generic(context, data_byte * data_size_input[p],
                          inp_sram_offset, dram_time);
#endif

        inp_global_addr += data_size_input[p] * data_byte;
    }
#endif
}


void NpuBase::prefReadData(TaskCoreContext &context, uint64_t &dram_time,
                           int data_size_label, string label_name) {
#if USE_NB_DRAMSYS == 0
    auto wc = context.wc;
#endif
    auto sram_addr = context.sram_addr;


    AddrPosKey sc_key;
    int flag = prim_context->sram_pos_locator_->findPair(label_name, sc_key);
    if (flag == -1) {
        assert(false && "weight data not found");
    } else if (flag > 0) {
        assert(false && "weight data can not be spilled");
    }
    // dahu ??
    int sram_offset = 0;
    prim_context->sram_pos_locator_->findPair(label_name, sc_key);
#if USE_SRAM_MANAGER == 1
    sram_pos_locator_->printAllKeysWithAllocId();
    // Print allocation IDs for debugging
    std::cout << label_name << " Key Allocation ID: " << sc_key.alloc_id
              << std::endl;

    sram_read_generic(context, data_byte * data_size_label, sram_offset,
                      dram_time, sc_key.alloc_id, true,
                      prim_context->sram_pos_locator_);
#else

    sram_read_generic(context, data_byte * data_size_label, sram_offset,
                      dram_time);

#endif
}
void NpuBase::checkStaticData(TaskCoreContext &context, uint64_t &dram_time,
                              uint64_t label_global_addr, int data_size_label,
                              string label_name, bool use_pf) {
#if USE_NB_DRAMSYS == 0
    auto wc = context.wc;
#endif
    auto sram_addr = context.sram_addr;
    int sram_offset = *sram_addr;

#if DUMMY == 1
    float *dram_start = nullptr;
#else
    float *dram_start = (float *)(dram_array[cid]);
    float *inp = dram_start + inp_offset;
    float *out = dram_start + out_offset;
#endif

    AddrPosKey sc_key;
    int flag = prim_context->sram_pos_locator_->findPair(label_name, sc_key);
    if (flag == -1) {
        LOG_VERBOSE(1, context.cid,
                    "Prim name:" << name << " weight data not found");

#if USE_SRAM_MANAGER == 1
        sram_first_write_generic(
            context, data_byte * data_size_label, label_global_addr, dram_time,
            dram_start, label_name, true, prim_context->sram_pos_locator_);
#else
        sram_first_write_generic(context, data_byte * data_size_label,
                                 label_global_addr, dram_time, dram_start);

        sc_key = AddrPosKey(*sram_addr, data_byte * data_size_label);
        prim_context->sram_pos_locator_->addPair(label_name, sc_key, context,
                                                 dram_time);
#endif
    } else if (flag > 0) {
        LOG_VERBOSE(1, context.cid,
                    "Prim name:" << name << " weight data has spill");
#if USE_SRAM_MANAGER == 1
        sram_first_write_generic(context, flag, label_global_addr, dram_time,
                                 dram_start, label_name, true,
                                 prim_context->sram_pos_locator_);

#else
        sram_first_write_generic(context, flag, label_global_addr, dram_time,
                                 dram_start);
        sc_key.size = data_byte * data_size_label;
        sc_key.spill_size = 0;
        prim_context->sram_pos_locator_->addPair(label_name, sc_key, context,
                                                 dram_time);
#endif
    }


    prim_context->sram_pos_locator_->findPair(label_name, sc_key);
    LOG_VERBOSE(1, context.cid,
                "Prim name:" << name << " read weight data from sram");
#if USE_SRAM_MANAGER == 1
    sram_pos_locator_->printAllKeysWithAllocId();
    // Print allocation IDs for debugging
    std::cout << label_name << " Key Allocation ID: " << sc_key.alloc_id
              << std::endl;
    if (use_pf == false) {
        sram_read_generic(context, data_byte * data_size_label, sram_offset,
                          dram_time, sc_key.alloc_id, true,
                          prim_context->sram_pos_locator_);
    }
#else
    if (use_pf == false) {
        sram_read_generic(context, data_byte * data_size_label, sram_offset,
                          dram_time);
    }
#endif
}

void NpuBase::writeOutputData(TaskCoreContext &context, uint64_t exu_flops,
                              uint64_t sfu_flops, uint64_t dram_time,
                              uint64_t &overlap_time, int data_size_out,
                              uint64_t out_global_addr) {
    int cycle = 0;
    int cid = context.cid;
    CoreHWConfig *core_config = GetCoreHWConfig(cid);
    ExuConfig *exu = core_config->exu;
    SfuConfig *sfu = core_config->sfu;

    cout << "exu_flops: " << exu_flops << " sfu_flops: " << sfu_flops << endl;

    if (exu->type == MAC_Array)
        cycle +=
            exu_flops / (exu->x_dims * exu->y_dims * 2 * comp_util) * CYCLE;
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
        LOG_VERBOSE(1, context.cid,
                    "Prim name:" << name << RED << " cycle: " << cycle
                                 << ", dram_time: " << dram_time << RESET);

        // std::cout << RED << "cycle: " << cycle << ", dram_time: " <<
        // dram_time
        //           << RESET << std::endl;

    } else {
        overlap_time = cycle - dram_time;
        LOG_VERBOSE(1, context.cid,
                    "Prim name:" << name << GREEN << " cycle: " << cycle
                                 << ", dram_time: " << dram_time << RESET);
    }

    // 写入out
    std::vector<std::string> out_labels;
    std::istringstream iss(prim_context->datapass_label_->outdata);
    std::string label;
    while (iss >> label)
        out_labels.push_back(label);

    int temp_out_sram_offset = *(context.sram_addr);


#if USE_SRAM_MANAGER == 1
    for (int i = 0; i < out_labels.size(); i++) {
        sram_write_append_generic(
            context, data_byte * data_size_out / out_labels.size(),
            overlap_time, out_labels[i], true, prim_context->sram_pos_locator_,
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
        prim_context->sram_pos_locator_->addPair(out_labels[i], out_key,
                                                 context, dram_time);
    }
#endif
#endif
}

void NpuBase::printSelf() {
    cout << "<" + name + ">\n";

    for (auto &pair : param_value)
        cout << "\t" << pair.first << ": " << pair.second << endl;

    for (auto &pair : data_chunk)
        cout << "\t" << pair.first << ": " << pair.second << endl;

    for (auto &pair : data_chunk_addr)
        cout << "\t" << pair.first << ": " << pair.second << endl;
}