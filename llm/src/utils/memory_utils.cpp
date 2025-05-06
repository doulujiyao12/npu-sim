#include "utils/memory_utils.h"
#include "common/system.h"
#include "defs/const.h"
#include "defs/global.h"
#include "macros/macros.h"
#include "memory/gpu/GPU_L1L2_Cache.h"
#include "utils/file_utils.h"
#include "utils/system_utils.h"

#include "systemc.h"
#include <tlm>
#include <tlm_utils/peq_with_cb_and_phase.h>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>

void sram_first_write_generic(TaskCoreContext &context, int data_size_in_byte,
                              int global_addr, u_int64_t &dram_time,
                              float *dram_start) {
    int dma_read_count =
        data_size_in_byte * 8 / (int)(SRAM_BITWIDTH * SRAM_BANKS);
    int byte_residue =
        data_size_in_byte * 8 - dma_read_count * (SRAM_BITWIDTH * SRAM_BANKS);
    int single_read_count = ceiling_division(byte_residue, SRAM_BITWIDTH);
    int inp_global_addr = global_addr;

#if USE_NB_DRAMSYS == 0
    auto wc = context.wc;
#endif
    auto mau = context.mau;
    auto hmau = context.hmau;
    auto &msg_data = context.msg_data;
    auto sram_addr = context.sram_addr;
    auto sram_addr_temp = *sram_addr;
#if USE_NB_DRAMSYS == 1
    auto nb_dcache = context.nb_dcache;
#endif
    auto s_nbdram = context.s_nbdram;
    auto e_nbdram = context.e_nbdram;

    vector<sc_bv<SRAM_BITWIDTH>> data_tmp(SRAM_BANKS);
    for (int i = 0; i < SRAM_BANKS; i++) {
        data_tmp[i] = 0;
    }
    // std::vector<std::pair<u_int64_t, sc_time>> addr_time_pairs;
    // // 获取当前线程的名字
    // std::string thread_name = sc_core::sc_get_current_process_b()->name();
    // std::string filename = "addr_time_data_" + thread_name + ".txt";
    u_int64_t in_dcacheline = 0;
    int cache_lines = 1 << (dcache_words_in_line_log2 + 2 + 3);
    int cache_count = SRAM_BANKS * SRAM_BITWIDTH / cache_lines;
    int sram_time = 0;

#if USE_NB_DRAMSYS == 1
    sc_time start_first_write_time = sc_time_stamp();
#endif

#if USE_NB_DRAMSYS == 1

    nb_dcache->reconfigure(inp_global_addr, dma_read_count, cache_count,
                           cache_lines, 0);
    sc_time start_nbdram = sc_time_stamp();
    cout << "start nbdram: " << sc_time_stamp().to_string() << endl;
    wait(*e_nbdram);
    sc_time end_nbdram = sc_time_stamp();
    cout << "end nbdram: " << sc_time_stamp().to_string() << endl;
    u_int64_t nbdram_time = (end_nbdram - start_nbdram).to_seconds() * 1e9;

    for (int i = 0; i < dma_read_count; i++) {
        sc_time elapsed_time;
        hmau->mem_read_port->multiport_write(sram_addr_temp, data_tmp,
                                             elapsed_time);
        u_int64_t sram_timer = elapsed_time.to_seconds() * 1e9;
        sram_time += sram_timer;
        sram_addr_temp = sram_addr_temp + SRAM_BANKS;
    }
    if (nbdram_time < sram_time) {
        wait(sram_time - nbdram_time, SC_NS);
    }


#else
    for (int i = 0; i < dma_read_count; i++) {
        for (int j = 0; j < cache_count; j++) {
            in_dcacheline = inp_global_addr;
            tlm::tlm_generic_payload trans;
            trans.set_address(in_dcacheline);
            trans.set_data_length(cache_lines / 8);
            trans.set_streaming_width(cache_lines / 8);
            trans.set_command(tlm::TLM_READ_COMMAND);
#if DUMMY == 1
            trans.set_data_ptr(reinterpret_cast<unsigned char *>((void *)0));
#else
            trans.set_data_ptr(reinterpret_cast<unsigned char *>(dram_start));
#endif
            trans.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
            sc_time delay = sc_time(0, SC_NS);
            wc->isocket->b_transport(trans, delay);
            u_int64_t timer = delay.to_seconds() * 1e9;
            // dram_time += timer;
            // wait(delay);
            // addr_time_pairs.emplace_back(inp_global_addr, sc_time_stamp());
            inp_global_addr += cache_lines;
        }
        sc_time elapsed_time;
        hmau->mem_read_port->multiport_write(sram_addr_temp, data_tmp,
                                             elapsed_time);
        u_int64_t sram_timer = elapsed_time.to_seconds() * 1e9;
        dram_time += sram_timer;
        // cout << "sram_timer: " << sram_timer << endl;
        //  hmau->mem_read_port->multiport_read(sram_addr_tmp, data_tmp,
        //  elapsed_time); sram_timer = elapsed_time.to_seconds() * 1e9;
        //  dram_time += sram_timer;
        sram_addr_temp = sram_addr_temp + SRAM_BANKS;
    }
#endif
    // 将记录的数据写入文本文件
    // std::ofstream outfile(filename, std::ios::app); // 使用 std::ios::app
    // 模式 for (const auto& pair : addr_time_pairs) {
    //     outfile << pair.first << " " << pair.second.to_seconds() << "\n";
    // }
    // outfile.close();
    // cout << "dram_timer: " << dram_time << endl;
    if (single_read_count > 0) {

#if USE_NB_DRAMSYS == 1
std::cout << "inp_global_addr: " << inp_global_addr << std::endl;
std::cout << "cache_lines: " << cache_lines << std::endl;
std::cout << "cache_count: " << cache_count << std::endl;
std::cout << "dma_read_count: " << dma_read_count << std::endl;
        nb_dcache->reconfigure(inp_global_addr +
                                   cache_lines * cache_count * dma_read_count,
                               1, cache_count, cache_lines, 0);
        start_nbdram = sc_time_stamp();
        cout << "start write back padding nbdram: " << sc_time_stamp().to_string() << endl;
        wait(*e_nbdram);
        end_nbdram = sc_time_stamp();
        cout << "end padding nbdram: " << sc_time_stamp().to_string() << endl;
        nbdram_time = (end_nbdram - start_nbdram).to_seconds() * 1e9;
        sram_time = 0;
        sc_bv<SRAM_BITWIDTH> data_tmp2;
        data_tmp2 = 0;
        sc_time elapsed_time;
        for (int i = 0; i < single_read_count; i++) {
            mau->mem_write_port->write(sram_addr_temp, data_tmp2, elapsed_time);
            u_int64_t sram_timer = elapsed_time.to_seconds() * 1e9;
            sram_time += sram_timer;
            // mau->mem_read_port->read(sram_addr_tmp, data_tmp2, elapsed_time);
            sram_addr_temp = sram_addr_temp + 1;
        }

        if (nbdram_time < sram_time) {
            wait(sram_time - nbdram_time, SC_NS);
        }
    }


#else
        for (int j = 0; j < cache_count; j++) {
            in_dcacheline = inp_global_addr;
            tlm::tlm_generic_payload trans;
            trans.set_address(in_dcacheline);
            trans.set_data_length(cache_lines / 8);
            trans.set_streaming_width(cache_lines / 8);
            trans.set_command(tlm::TLM_READ_COMMAND);
#if DUMMY == 1
            trans.set_data_ptr(reinterpret_cast<unsigned char *>((void *)0));
#else
            trans.set_data_ptr(reinterpret_cast<unsigned char *>(dram_start));
#endif
            trans.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
            sc_time delay = sc_time(0, SC_NS);
            wc->isocket->b_transport(trans, delay);
            u_int64_t timer = delay.to_seconds() * 1e9;
            // dram_time += timer;
            // wait(delay);
            inp_global_addr += cache_lines;
        }

        sc_bv<SRAM_BITWIDTH> data_tmp2;
        data_tmp2 = 0;
        sc_time elapsed_time;
        for (int i = 0; i < single_read_count; i++) {
            mau->mem_write_port->write(sram_addr_temp, data_tmp2, elapsed_time);
            u_int64_t sram_timer = elapsed_time.to_seconds() * 1e9;
            dram_time += sram_timer;
            sram_addr_temp = sram_addr_temp + 1;
        }
    }
#endif

#if USE_NB_DRAMSYS
    sc_time end_first_write_time = sc_time_stamp();
    dram_time +=
        (end_first_write_time - start_first_write_time).to_seconds() * 1e9;

#endif

    *sram_addr = sram_addr_temp;
}

void sram_spill_back_generic(TaskCoreContext &context, int data_size_in_byte,
                             int global_addr, u_int64_t &dram_time) {
    int dma_read_count =
        data_size_in_byte * 8 / (int)(SRAM_BITWIDTH * SRAM_BANKS);
    int byte_residue =
        data_size_in_byte * 8 - dma_read_count * (SRAM_BITWIDTH * SRAM_BANKS);
    int single_read_count = ceiling_division(byte_residue, SRAM_BITWIDTH);
    int inp_global_addr = global_addr;

#if USE_NB_DRAMSYS == 0
    auto wc = context.wc;
#endif
    auto mau = context.mau;
    auto hmau = context.hmau;
    auto &msg_data = context.msg_data;
    auto sram_addr = context.sram_addr;
    auto sram_addr_temp = *sram_addr;
#if USE_NB_DRAMSYS == 1
    auto nb_dcache = context.nb_dcache;
#endif
    auto s_nbdram = context.s_nbdram;
    auto e_nbdram = context.e_nbdram;

    vector<sc_bv<SRAM_BITWIDTH>> data_tmp(SRAM_BANKS);
    for (int i = 0; i < SRAM_BANKS; i++) {
        data_tmp[i] = 0;
    }

    u_int64_t in_dcacheline = 0;
    int cache_lines = 1 << (dcache_words_in_line_log2 + 2 + 3);
    int cache_count = SRAM_BANKS * SRAM_BITWIDTH / cache_lines;
    int sram_time = 0;

#if USE_NB_DRAMSYS == 1
    sc_time start_first_write_time = sc_time_stamp();
#endif

#if USE_NB_DRAMSYS == 1

    nb_dcache->reconfigure(inp_global_addr, dma_read_count, cache_count,
                           cache_lines, 1);
    sc_time start_nbdram = sc_time_stamp();
    cout << "start spill back nbdram: " << sc_time_stamp().to_string() << endl;
    wait(*e_nbdram);
    sc_time end_nbdram = sc_time_stamp();
    cout << "end nbdram: " << sc_time_stamp().to_string() << endl;
    u_int64_t nbdram_time = (end_nbdram - start_nbdram).to_seconds() * 1e9;

    for (int i = 0; i < dma_read_count; i++) {
        sc_time elapsed_time;
        hmau->mem_read_port->multiport_read(sram_addr_temp, data_tmp,
                                            elapsed_time);
        u_int64_t sram_timer = elapsed_time.to_seconds() * 1e9;
        sram_time += sram_timer;
        sram_addr_temp = sram_addr_temp + SRAM_BANKS;
    }
    if (nbdram_time < sram_time) {
        wait(sram_time - nbdram_time, SC_NS);
    }
#else
    // do nothing
#endif

    if (single_read_count > 0) {

#if USE_NB_DRAMSYS == 1
        nb_dcache->reconfigure(inp_global_addr +
                                   cache_lines * cache_count * dma_read_count,
                               1, cache_count, cache_lines, 1);
        start_nbdram = sc_time_stamp();
        cout << "start padding nbdram: " << sc_time_stamp().to_string() << endl;
        wait(*e_nbdram);
        end_nbdram = sc_time_stamp();
        cout << "end padding nbdram: " << sc_time_stamp().to_string() << endl;
        nbdram_time = (end_nbdram - start_nbdram).to_seconds() * 1e9;
        sram_time = 0;
        sc_bv<SRAM_BITWIDTH> data_tmp2;
        data_tmp2 = 0;
        sc_time elapsed_time;
        for (int i = 0; i < single_read_count; i++) {
            mau->mem_read_port->read(sram_addr_temp, data_tmp2, elapsed_time);
            u_int64_t sram_timer = elapsed_time.to_seconds() * 1e9;
            sram_time += sram_timer;
            // mau->mem_read_port->read(sram_addr_tmp, data_tmp2, elapsed_time);
            sram_addr_temp = sram_addr_temp + 1;
        }

        if (nbdram_time < sram_time) {
            wait(sram_time - nbdram_time, SC_NS);
        }
#else
        // do nothing
#endif
    }

#if USE_NB_DRAMSYS == 1
    sc_time end_first_write_time = sc_time_stamp();
    dram_time +=
        (end_first_write_time - start_first_write_time).to_seconds() * 1e9;
#endif
}

void sram_read_generic(TaskCoreContext &context, int data_size_in_byte,
                       int sram_addr_offset, u_int64_t &dram_time) {
    int dma_read_count =
        data_size_in_byte * 8 / (int)(SRAM_BITWIDTH * SRAM_BANKS);
    int byte_residue =
        data_size_in_byte * 8 - dma_read_count * (SRAM_BITWIDTH * SRAM_BANKS);
    int single_read_count = ceiling_division(byte_residue, SRAM_BITWIDTH);

    // cout << "[INFO] sram_read_generic: dma_read_count: " << dma_read_count <<
    // ", single_read_count: " << single_read_count << endl; cout << "[INFO]
    // sram_read_generic: data_size_in_byte: " << data_size_in_byte << ",
    // sram_addr_offset: " << sram_addr_offset << endl;

#if USE_NB_DRAMSYS == 0
    auto wc = context.wc;
#endif
    auto mau = context.mau;
    auto hmau = context.hmau;

    vector<sc_bv<SRAM_BITWIDTH>> data_tmp(SRAM_BANKS);
    for (int i = 0; i < SRAM_BANKS; i++) {
        data_tmp[i] = 0;
    }

    for (int i = 0; i < dma_read_count; i++) {
        sc_time elapsed_time;
        hmau->mem_read_port->multiport_read(sram_addr_offset, data_tmp,
                                            elapsed_time);
        u_int64_t sram_timer = elapsed_time.to_seconds() * 1e9;
        dram_time += sram_timer;
        sram_addr_offset = sram_addr_offset + SRAM_BANKS;
    }

    sc_bv<SRAM_BITWIDTH> data_tmp2;
    data_tmp2 = 0;

    sc_time elapsed_time;
    for (int i = 0; i < single_read_count; i++) {
        mau->mem_read_port->read(sram_addr_offset, data_tmp2, elapsed_time);
        sram_addr_offset = sram_addr_offset + 1;
        u_int64_t sram_timer = elapsed_time.to_seconds() * 1e9;
        dram_time += sram_timer;
    }
}

void sram_write_append_generic(TaskCoreContext &context, int data_size_in_byte,
                               u_int64_t &dram_time) {
    int dma_read_count =
        data_size_in_byte * 8 / (int)(SRAM_BITWIDTH * SRAM_BANKS);
    int byte_residue =
        data_size_in_byte * 8 - dma_read_count * (SRAM_BITWIDTH * SRAM_BANKS);
    int single_read_count = ceiling_division(byte_residue, SRAM_BITWIDTH);

#if USE_NB_DRAMSYS == 0
    auto wc = context.wc;
#endif
    auto mau = context.mau;
    auto hmau = context.hmau;
    auto sram_addr = context.sram_addr;
    auto sram_addr_temp = *sram_addr;

    vector<sc_bv<SRAM_BITWIDTH>> data_tmp(SRAM_BANKS);
    for (int i = 0; i < SRAM_BANKS; i++) {
        data_tmp[i] = 0;
    }

    for (int i = 0; i < dma_read_count; i++) {
        sc_time elapsed_time;
        hmau->mem_read_port->multiport_write(sram_addr_temp, data_tmp,
                                             elapsed_time);
        u_int64_t sram_timer = elapsed_time.to_seconds() * 1e9;
        // dram_time += sram_timer;
        sram_addr_temp = sram_addr_temp + SRAM_BANKS;
    }

    sc_bv<SRAM_BITWIDTH> data_tmp2;
    data_tmp2 = 0;

    sc_time elapsed_time;
    for (int i = 0; i < single_read_count; i++) {
        mau->mem_write_port->write(sram_addr_temp, data_tmp2, elapsed_time);
        sram_addr_temp = sram_addr_temp + 1;
        u_int64_t sram_timer = elapsed_time.to_seconds() * 1e9;
        // dram_time += sram_timer;
    }

    *sram_addr = sram_addr_temp;
}

void sram_write_back_generic(TaskCoreContext &context, int data_size_in_byte,
                             int sram_addr, u_int64_t &dram_time) {
    int dma_read_count =
        data_size_in_byte * 8 / (int)(SRAM_BITWIDTH * SRAM_BANKS);
    int byte_residue =
        data_size_in_byte * 8 - dma_read_count * (SRAM_BITWIDTH * SRAM_BANKS);
    int single_read_count = ceiling_division(byte_residue, SRAM_BITWIDTH);

#if USE_NB_DRAMSYS == 0
    auto wc = context.wc;
#endif
    auto mau = context.mau;
    auto hmau = context.hmau;

    vector<sc_bv<SRAM_BITWIDTH>> data_tmp(SRAM_BANKS);
    for (int i = 0; i < SRAM_BANKS; i++) {
        data_tmp[i] = 0;
    }

    for (int i = 0; i < dma_read_count; i++) {
        sc_time elapsed_time;
        hmau->mem_read_port->multiport_write(sram_addr, data_tmp, elapsed_time);
        u_int64_t sram_timer = elapsed_time.to_seconds() * 1e9;
        // dram_time += sram_timer;
        sram_addr = sram_addr + SRAM_BANKS;
    }

    sc_bv<SRAM_BITWIDTH> data_tmp2;
    data_tmp2 = 0;

    sc_time elapsed_time;
    for (int i = 0; i < single_read_count; i++) {
        mau->mem_write_port->write(sram_addr, data_tmp2, elapsed_time);
        sram_addr = sram_addr + 1;
        u_int64_t sram_timer = elapsed_time.to_seconds() * 1e9;
        // dram_time += sram_timer;
    }
}

void check_freq(std::unordered_map<u_int64_t, u_int16_t> &freq, u_int64_t *tags,
                u_int32_t set, u_int64_t elem_tag) {
    if (freq[elem_tag] == CACHE_MAX_FREQ) {
        // Halve the frequency of every line in the cache set
        u_int64_t set_base = set << CACHE_WAY_BITS;
        for (u_int32_t i = 0; i < CACHE_WAYS; i++) {
            u_int64_t tag = tags[set_base + i];
            if (tag < UINT64_MAX && freq[tag] > 1)
                freq[tag] >>= 1;
        }
    }
}

#if DCACHE
int dcache_replacement_policy(u_int32_t tileid, u_int64_t *tags,
                              u_int64_t new_tag, u_int16_t &set_empty_lines) {
    // Search the element of tags whose index into freq has the lowest value
    int evict_dcache_idx, dcache_idx;
    u_int16_t line_freq, min_freq = UINT16_MAX;

    // Set base is the index of the first cache line in a set
    // Set_id hashes the cache line tag to a set
    // 这里感觉set在way之前，也不是不可以，低地址位先是set，然后才是对应的way
    // bit
    u_int32_t set = set_id_dcache(new_tag);
    u_int64_t set_base = set << CACHE_WAY_BITS;

    // REPLACEMENT POLICY IN HW
    // 但是对tag进行索引的时候，还是先way 然后set
    for (u_int32_t i = 0; i < CACHE_WAYS; i++) {
        dcache_idx = i + set_base;
        u_int64_t tag = tags[dcache_idx];
        // if (tag<UINT64_MAX) line_freq = dcache_freq[tag]; //
        // 这里表示这个dcache被是用的频率多不多
        if (tag < UINT64_MAX)
            line_freq = dcache_freq_v2[tag];
        else {
            line_freq = 0;
            set_empty_lines++;
        } // 原本的set中有一个way是空的，所以就用它，并且告诉外面原本set有空的，不用dirty

        if (line_freq < min_freq) {
            // Best candidate for eviction
            evict_dcache_idx = dcache_idx;
            min_freq = line_freq; // 找最小被是用的dcache去替换
        }
    }
    // // DAHU 这个==好像有问题？？
    // assert(evict_dcache_idx==set);
    // cout << "\nDCACHE_DAHU " <<  evict_dcache_idx << "\n";
    // cout << "\nDCACHE_DAHU " <<  set << "\n";
    return evict_dcache_idx;
}
#endif


u_int64_t cache_tag(u_int64_t addr) {
    u_int64_t word_index = (u_int64_t)addr >> 2; // 4bytes in a word
    // 全局的darray加起来，所有的tile
    // dataset_words_per_tile dram 大小在一个tile中
    data_footprint_in_words =
        GRID_SIZE * dataset_words_per_tile; // global variable
    word_index = word_index % data_footprint_in_words;
    // 在全局darray中的索引
    return word_index >> dcache_words_in_line_log2;
}

bool is_dirty(u_int64_t tag) { return dcache_dirty[tag]; }

// LEGACY VERSION WHERE THE FUNCTION THAT ONLY RETURN THE PENALTY
// Next-line prefetching only
// array是全局地址，所有的tile索引的地址
int check_dcache(int tX, int tY, u_int64_t array, u_int64_t timer,
                 u_int64_t &time_fetched, u_int64_t &time_prefetched,
                 u_int64_t &prefetch_tag, bool prefetch) {
    int pu_penalty = sram_read_latency;
#if DCACHE >= 1
#if DCACHE == 1
    if (dataset_cached) {
#elif DCACHE >= 2
    {
#endif
        // freq is the number of hits of a per-neighbour element
        // // globalid 表示的是全局中tile的编号

        // 先宽的方向后高度方向，编号
        u_int32_t tileid = global(tX, tY);
        // Tags that are currently in the per-tile dcache
        u_int64_t *tags = dcache_tags[tileid];

        // the tag is a global tag (not within the dcache)
        // array其实按照byte的地址
        // elem_tag 就是 全体dram地址除以dcache中一行的word数
        u_int64_t elem_tag = cache_tag(array);

#if DCACHE <= 4
        // If the data I'm fetching was prefetched, then update the
        // time_fetched, and we issue next prefetch now
        // 如果提前一轮去拿数据，这一轮的数据上一轮拿
        // 所以prefetch的时间是上一轮time的时间
        // 当前的fetch时间是prefetch的时间就是上一轮的timer的时间
        if (elem_tag == prefetch_tag && prefetch == true) {
            time_fetched = time_prefetched;
            time_prefetched = timer;
            prefetch_tag++;
        }
#else
        // 如果当前轮拿的话，就是当前的时间
        time_fetched = timer;
#endif

#if DCACHE <= 5 // If we allow some type of data hits
        // dcache_freq is a global array (not per tile)
        // if (dcache_freq[elem_tag] > 0){
        //   // ==== CACHE HIT ====
        //   dcache_hits++;
        //   //printf("dcache hits ++ \n");
        // } else{
        if (dcache_freq_v2.find(elem_tag) != dcache_freq_v2.end() &&
            dcache_freq_v2[elem_tag] > 0) {
            dcache_hits++;
        } else {
#else
        {
#endif
            // ==== CACHE MISS ====
            // printf("dcache misses ++ \n");
            dcache_misses++;
            // memory channel
            u_int16_t mc_queue_id = die_id(tX, tY) * hbm_channels +
                                    (tY * DIE_W + tX) % hbm_channels;
            // Number of transactions that have been fetched from the HBM
            // channel since the beggining of the program Assume that the Mem.
            // controller channel can take one request per HBM cycle
            // 该通道历史上所有的交易总和
            int64_t trans_count = (int64_t)mc_transactions[mc_queue_id];
            mc_transactions[mc_queue_id] = trans_count + 1;
            // 历史上所有的交易的总和，所以后面time_fetch的时间不能超过他
            int64_t pu_cy_last_mc_trans =
                ceil_macro(trans_count * pu_mem_ratio);

            // Fetched when the last request has satisfieda
            // time fetched 是拿到数据的时间
            if (pu_cy_last_mc_trans > time_fetched)
                time_fetched = pu_cy_last_mc_trans;

            // May be a negative number!
            // time 是当前运行的时间
            int fetch_cycles_ahead = (int64_t)timer - (int64_t)time_fetched;

            // If cycles ahead is smaller than HBM latency, then we calculate
            // the real latency
            int read_latency = 0;
            int hbm_lat = (int)hbm_read_latency;
            // fetch后还会有hbm的延迟
            if (fetch_cycles_ahead < hbm_lat)
                read_latency = (hbm_lat - fetch_cycles_ahead);
            mc_latency[mc_queue_id] += read_latency;
            pu_penalty += read_latency;

            u_int16_t set_empty_lines = 0;
            int evict_dcache_idx = dcache_replacement_policy(
                tileid, tags, elem_tag, set_empty_lines);
#if ASSERT_MODE && DCACHE <= 5
            // assert(dcache_freq[elem_tag]==0);
            assert(evict_dcache_idx < dcache_size);
#endif
            u_int64_t evict_tag = tags[evict_dcache_idx];

            // An eviction occurs when dcache_freq (valid bit) is 0, but a tag
            // is already in the dcache
            // FIXME: A local DCache can have evictions by collision of tags
            // since the address space is not aligned locally (e.g. a dcache
            // with 100 lines may get evictions even if its footprint is only 10
            // vertices and 40 edges, since we don't consider the local address
            // space)

            if (set_empty_lines == 0) { // IF CACHE SET FULL
                // Evict Line
                dcache_evictions++;
#if ASSERT_MODE
                assert(evict_tag < UINT64_MAX);
#endif
                // Mark the previous element as evicted (dcache_freq = 0)
                // dcache_freq[evict_tag] = 0;
                if (dcache_freq_v2.find(evict_tag) != dcache_freq_v2.end()) {
                    dcache_freq_v2.erase(evict_tag);
                } else {
                    assert(0);
                }
                // Writeback only if dirty!
                if (is_dirty(evict_tag)) {
                    // cout << "Writeback: " << evict_tag << endl;
                    mc_writebacks[mc_queue_id]++;
                    mc_transactions[mc_queue_id]++;
                }
            } else { // IF CACHE NOT FULL
                     // Increase the occupancy count
#if ASSERT_MODE
                assert(evict_tag == UINT64_MAX);
#endif
                dcache_occupancy[tileid]++;
            }

            // Update the dcache tag with the new elem
            tags[evict_dcache_idx] = elem_tag;
            // dcache_freq[elem_tag]++;
            if (dcache_freq_v2.find(elem_tag) == dcache_freq_v2.end()) {
                dcache_freq_v2[elem_tag] = 1; // 如果不存在，则初始化为 1
            } else {
                dcache_freq_v2[elem_tag]++; // 如果存在，则自增
            }
        }

#if DIRECT_MAPPED == 0
        // If the elem was not in the dcache, it's 1 now. If it was in the
        // dcache, freq is incremented by 1. dcache_freq[elem_tag]++;
        if (dcache_freq_v2.find(elem_tag) == dcache_freq_v2.end()) {
            dcache_freq_v2[elem_tag] = 1; // 如果不存在，则初始化为 1
        } else {
            dcache_freq_v2[elem_tag]++; // 如果存在，则自增
        }
        u_int32_t set = set_id_dcache(elem_tag);
        check_freq(dcache_freq_v2, tags, set, elem_tag);
#endif
    }
#endif
#if ASSERT_MODE
    assert(pu_penalty > 0);
#endif
    return pu_penalty;
}

#if USE_L1L2_CACHE == 1
void gpu_read_generic(TaskCoreContext &context, uint64_t global_addr,
                      int data_size_in_byte, int &mem_time) {

    int inp_global_addr =
        (global_addr / 64) * 64; // 向下取整到dram 取址的整数倍，这里是32
    int end_addr = global_addr + data_size_in_byte;
    int end_global_addr = ((end_addr + 63) / 64) * 64; // 尾地址向上取整

    int aligned_data_size_in_byte = end_global_addr - inp_global_addr;


    auto gpunb_dcache_if = context.gpunb_dcache_if;

    auto s_nbdram = context.start_nb_gpu_dram_event;
    auto e_nbdram = context.end_nb_gpu_dram_event;

    u_int64_t in_dcacheline = 0;
    int cache_lines = 1 << (dcache_words_in_line_log2 + 2 + 3);
    int cache_count =
        ceiling_division(aligned_data_size_in_byte * 8, cache_lines);


    sc_time start_first_write_time = sc_time_stamp();
#if GPU_CACHE_DEBUG == 1

    cout << "read cache_count: " << cache_count << "cache_lines " << cache_lines
         << endl;
    cout << "start gpu_nbdram: " << sc_time_stamp().to_string() << " id "
         << gpunb_dcache_if->id << endl;

#endif
    gpunb_dcache_if->reconfigure(inp_global_addr, cache_count, cache_lines, 0);

    context.event_engine->add_event("Core " + toHexString(*context.cid),
                                    "read_gpu", "B",
                                    Trace_event_util("read_gpu"));

    wait(*e_nbdram);
    context.event_engine->add_event("Core " + toHexString(*context.cid),
                                    "read_gpu", "E",
                                    Trace_event_util("read_gpu"));
#if GPU_CACHE_DEBUG == 1

    cout << "end gpu_nbdram: " << sc_time_stamp().to_string() << " id "
         << gpunb_dcache_if->id << endl;

#endif

#if USE_NB_DRAMSYS
    sc_time end_first_write_time = sc_time_stamp();
    mem_time +=
        (end_first_write_time - start_first_write_time).to_seconds() * 1e9;

#endif
}

void gpu_write_generic(TaskCoreContext &context, uint64_t global_addr,
                       int data_size_in_byte, int &mem_time) {

    int inp_global_addr =
        (global_addr / 64) * 64; // 向下取整到dram 取址的整数倍，这里是32
    int end_addr = global_addr + data_size_in_byte;
    int end_global_addr = ((end_addr + 63) / 64) * 64; // 尾地址向上取整

    int aligned_data_size_in_byte = end_global_addr - inp_global_addr;


    auto gpunb_dcache_if = context.gpunb_dcache_if;

    auto s_nbdram = context.start_nb_gpu_dram_event;
    auto e_nbdram = context.end_nb_gpu_dram_event;

    u_int64_t in_dcacheline = 0;
    int cache_lines = 1 << (dcache_words_in_line_log2 + 2 + 3);
    int cache_count =
        ceiling_division(aligned_data_size_in_byte * 8, cache_lines);

    sc_time start_first_write_time = sc_time_stamp();
#if GPU_CACHE_DEBUG == 1

    cout << "write gpu cache_count: " << cache_count << "cache_lines "
         << cache_lines << endl;
    cout << "start gpu_nbdram: " << sc_time_stamp().to_string() << " id "
         << gpunb_dcache_if->id << endl;
#endif
    // cout << "dahudahu" <<endl;
    gpunb_dcache_if->reconfigure(inp_global_addr, cache_count, cache_lines, 1);

    wait(*e_nbdram);
#if GPU_CACHE_DEBUG == 1

    cout << "end gpu_nbdram: " << sc_time_stamp().to_string() << " id "
         << gpunb_dcache_if->id << endl;
#endif


#if USE_NB_DRAMSYS
    sc_time end_first_write_time = sc_time_stamp();
    mem_time +=
        (end_first_write_time - start_first_write_time).to_seconds() * 1e9;

#endif
}
#endif