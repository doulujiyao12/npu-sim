#pragma once
#include "systemc.h"

#include "common/memory.h"
#include "common/msg.h"
#include "common/system.h"
#include "common/pd.h"
#include "defs/const.h"
#include "hardware/systolic/systolic.h"
#include "macros/macros.h"
#include "memory/dram/Dcache.h"
#include "memory/dram/DummyDcache.h"
#include "memory/gpu/GPU_L1L2_Cache.h"
#include "memory/sram/dynamic_bandwidth_ram_row.h"
#include "trace/Event_engine.h"
#include "link/nb_global_memif.h"

class WorkerCoreExecutor;

class WorkerCore : public sc_module {
public:
    int cid;
    Event_engine *event_engine;

    WorkerCoreExecutor *executor;
    DCache *dcache;
    SystolicArray *systolic;
    // DynamicBandwidthRamRow<sc_bv<256>, column_num> ram_array("ram_array", 0,
    // bank_depth, 2, 1, port_num + column_num * high_bw_port_num, port_num,
    // high_bw_port_num, event_engine_test); DynamicBandwidthRamRow<sc_bv<128>,
    // 4> ram_array(sc_gen_unique_name("ram_array"), 0, BANK_DEPTH,
    // SIMU_READ_PORT, SIMU_WRITE_PORT, BANK_PORT_NUM + SRAM_BANKS,
    // BANK_PORT_NUM, BANK_HIGH_READ_PORT_NUM, event_engine);
    DynamicBandwidthRamRow<sc_bv<SRAM_BITWIDTH>, SRAM_BANKS> *ram_array;

    HardwareTaskConfig *systolic_config;
    HardwareTaskConfig *other_config;
    DummyDCache *dummy_dcache;

    sc_signal<bool> systolic_done;
    sc_signal<bool> systolic_start;

    SC_HAS_PROCESS(WorkerCore);
    WorkerCore(const sc_module_name &n, int s_cid, Event_engine *event_engine);
    ~WorkerCore();
};


class WorkerCoreExecutor : public sc_module {
public:
    int cid;
    bool prim_refill; // 是否通过原语重填的方式实现循环
    int loop_cnt;     // 如果开启prim_refill，表明现在是第几个循环
    int send_global_mem; // [yicheng] todo

    /* ------------------PRIM----------------------- */
    sc_signal<bool> prim_block;     // 用于指示当前运行的原语是否正在执行
    sc_buffer<CORE_PRIM> pres_prim; // 用于指示当前运行的是哪一个原语

    // 原语执行完毕之后，利用该event将prim_block切换回unblock状态，使执行流继续
    sc_event ev_block;

    sc_event ev_send;
    sc_event ev_para_send;
    sc_event ev_recv;
    sc_event ev_comp;
    sc_event ev_send_helper;
    sc_event ev_systolic;

    Msg send_buffer; // 每一次调用write helper，从这里获取要发送的msg

    queue<Msg> recv_buffer; // 接收recv原语的buffer，用于重排
    queue<Msg>
        start_data_buffer; // 专门用于存放start
                           // data的buffer，和普通的recv_buffer作区分。这么做主要是因为host发送给core时没有握手信号。
    queue<Msg> ack_buffer;

    bool send_done; // 并行策略：send和recv并行
    bool send_last_packet;
    bool comp_done;                     // 并行策略：comp和send并行
    deque<prim_base *> prim_queue;      // 用于存储所有需要依次执行的原语
    queue<prim_base *> send_para_queue; // 并行策略：send和recv并行

    /* ----------------Config----------------------- */
    HardwareTaskConfig *systolic_config;
    HardwareTaskConfig *other_config;

    /* ----------------Hardware--------------------- */
    sc_in<bool> systolic_done_i;
    sc_out<bool> systolic_start_o;

    /* ----------------Handshake-------------------- */
    // 握手信号buffer
    vector<Msg> request_buffer;

    /* ----------------SendHelper------------------- */
    sc_time present_time = sc_time(0, SC_NS);
    int send_helper_write; // 用于指示send
                           // helper是要向data_sent_o写入true还是false

    /* ----------------PD complex------------------- */
    vector<Stage> *batchInfo;
    vector<bool> decode_done;

    /* --------------------------------------------- */

    // 向router传递：是否可以向core传递信息
    sc_out<bool> core_busy_o;

    // 传递数据的真正信道
    sc_in<sc_bv<256>> channel_i;
    sc_out<sc_bv<256>> channel_o;
    queue<sc_bv<256>> buffer_i; // 输入的缓存队列

    // 告知数据已经发送，通道使能信号
    sc_in<bool> data_sent_i;
    sc_out<bool> data_sent_o;

    // 通道未满的握手信号
    sc_in<bool> channel_avail_i;

    Event_engine *event_engine;
    
    NB_GlobalMemIF *nb_global_mem_socket;

#if USE_NB_DRAMSYS == 1
    NB_DcacheIF *nb_dcache_socket;
#else
    DcacheCore *dcache_socket;
#endif
#if USE_L1L2_CACHE == 1
    L1Cache *core_lv1_cache;
    // Processor *cache_processor;
    GPUNB_dcacheIF *gpunb_dcache_if;
    GpuPosLocator *gpu_pos_locator;
#else
#endif
    mem_access_unit *mem_access_port;
    high_bw_mem_access_unit *high_bw_mem_access_port;

    // sram相关
    int *sram_addr;                    // 用于记录当前sram可分配的起始地址
    sc_event *start_nb_dram_event;     // 用于启动非阻塞dram访存
    sc_event *end_nb_dram_event;       // 非阻塞sram访存结束标志
    sc_event *start_nb_gpu_dram_event; // 用于启动非阻塞gpu dram访存
    sc_event *end_nb_gpu_dram_event;   // 非阻塞gpu dram访存结束标志
    sc_event *start_global_mem_event;  // 用于启动global memory访存
    sc_event *end_global_mem_event;    // global memory访存结束标志
    SramPosLocator *sram_pos_locator; // 记录sram中数据的位置，label(string)-int
    AddrDatapassLabel
        *next_datapass_label; // 记录sram中数据的标签，这个变量由set
                              // sram修改，并由紧接着的comp原语读取并使用

    SC_HAS_PROCESS(WorkerCoreExecutor);
    WorkerCoreExecutor(const sc_module_name &n, int s_cid,
                       Event_engine *event_engine);
    ~WorkerCoreExecutor();

    void init_global_mem();

    void worker_core_execute();
    void switch_prim_block();
    void poll_buffer_i(); // 每个时钟周期，将发送进core的数据包统一转移到input
                          // buffer中，实现发送和处理逻辑的解耦

    void send_logic();
    void send_para_logic();
    void recv_logic();
    void task_logic();

    void send_helper(); // 同时在send和recv中被调用
    void call_systolic_array();

    bool atomic_helper_lock(sc_time try_time, int status);

    prim_base *parse_prim(sc_bv<128> buffer);

    void end_of_elaboration();
};