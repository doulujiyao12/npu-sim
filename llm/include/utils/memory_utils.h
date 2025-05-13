#pragma once
#include "common/system.h"

void sram_first_write_generic(TaskCoreContext &context, int data_size_in_byte,
                              int global_addr, u_int64_t &dram_time,
                              float *dram_start);
void sram_spill_back_generic(TaskCoreContext &context, int data_size_in_byte,
                             int global_addr, u_int64_t &dram_time);
void sram_read_generic_temp(TaskCoreContext &context, int data_size_in_byte,
                       int sram_addr_offset, u_int64_t &dram_time);
void sram_read_generic(TaskCoreContext &context, int data_size_in_byte,
                    int sram_addr_offset, u_int64_t &dram_time);
void sram_write_append_generic(TaskCoreContext &context, int data_size_in_byte,
                               u_int64_t &dram_time);
void sram_write_back_temp(TaskCoreContext &context, int data_size_in_byte,
                             int &temp_sram_addr, u_int64_t &dram_time);

void check_freq(std::unordered_map<u_int64_t, u_int16_t> &freq, u_int64_t *tags,
                u_int32_t set, u_int64_t elem_tag);
#if DCACHE
int dcache_replacement_policy(u_int32_t tileid, u_int64_t *tags,
                              u_int64_t new_tag, u_int16_t &set_empty_lines);
#endif

u_int64_t cache_tag(u_int64_t addr);
bool is_dirty(u_int64_t tag);
int check_dcache(int tX, int tY, u_int64_t array, u_int64_t timer,
                 u_int64_t &time_fetched, u_int64_t &time_prefetched,
                 u_int64_t &prefetch_tag, bool prefetch);

#if USE_L1L2_CACHE == 1
void gpu_read_generic(TaskCoreContext &context, uint64_t addr, int size,
                      int &mem_time);
void gpu_write_generic(TaskCoreContext &context, uint64_t addr, int size,
                       int &mem_time);
#endif