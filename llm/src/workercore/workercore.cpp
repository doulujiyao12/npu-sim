#include "systemc.h"
#include <deque>
#include <iostream>
#include <queue>
#include <string>
#include <typeinfo>

#include "defs/const.h"
#include "defs/global.h"
#include "link/nb_global_memif_v2.h"
#include "memory/dram/GPUNB_DcacheIF.h"
#include "memory/gpu/GPU_L1L2_Cache.h"
#include "memory/sram/Mem_access_unit.h"
#include "prims/comp_prims.h"
#include "prims/moe_prims.h"
#include "prims/norm_prims.h"
#include "prims/pd_prims.h"
#include "prims/base.h"
#include "trace/Event_engine.h"
#include "utils/memory_utils.h"
#include "utils/msg_utils.h"
#include "utils/pe_utils.h"
#include "utils/prim_utils.h"
#include "utils/print_utils.h"
#include "utils/system_utils.h"
#include "workercore/workercore.h"

using namespace std;

// workercore
WorkerCore::WorkerCore(const sc_module_name &n, int s_cid,
                       Event_engine *event_engine, string dram_config_name)
    : sc_module(n), cid(s_cid), event_engine(event_engine) {
    // systolic_config = new HardwareTaskConfig();
    // other_config = new HardwareTaskConfig();
    dcache = new DCache(sc_gen_unique_name("dcache"), cid, (int)cid / GRID_X,
                        (int)cid % GRID_X, this->event_engine, dram_config_name,
                        "../DRAMSys/configs");
    cout << "Workercore " << cid << " initialize: dram_string "
         << dram_config_name << endl;
    cout << " MaxAddr "
         << dcache->dramSysWrapper->dramsys->getAddressDecoder().maxAddress();
    auto sram_bitw = GetCoreHWConfig(cid).sram_bitwidth;
    ram_array = new DynamicBandwidthRamRow<sc_bv<SRAM_BITWIDTH>, SRAM_BANKS>(
        sc_gen_unique_name("ram_array"), 0,
        MAX_SRAM_SIZE * 8 / sram_bitw / SRAM_BANKS, SIMU_READ_PORT,
        SIMU_WRITE_PORT, BANK_PORT_NUM + SRAM_BANKS, BANK_PORT_NUM,
        BANK_HIGH_READ_PORT_NUM, event_engine);
    temp_ram_array =
        new DynamicBandwidthRamRow<sc_bv<SRAM_BITWIDTH>, SRAM_BANKS>(
            sc_gen_unique_name("temp_ram_array"), 0,
            MAX_SRAM_SIZE * 8 / sram_bitw / SRAM_BANKS, SIMU_READ_PORT,
            SIMU_WRITE_PORT, BANK_PORT_NUM + SRAM_BANKS, BANK_PORT_NUM,
            BANK_HIGH_READ_PORT_NUM, event_engine);

    executor = new WorkerCoreExecutor(sc_gen_unique_name("workercore-exec"),
                                      cid, this->event_engine);
    // executor->MaxDramAddr =
    //     dcache->dramSysWrapper->dramsys->getAddressDecoder().maxAddress();
    executor->MaxDramAddr =
        dcache->dramSysWrapper->dramsys->getMemSpec().memorySizeBytes;
    executor->defaultDataLength =
        dcache->dramSysWrapper->dramsys->getMemSpec().defaultBytesPerBurst;
    if (use_gpu == false) {
        dram_aligned = executor->defaultDataLength;
    }
    assert(dataset_words_per_tile <
           dcache->dramSysWrapper->dramsys->getMemSpec().memorySizeBytes);
    g_dram_kvtable[cid] =
        new DramKVTable(executor->MaxDramAddr, (uint64_t)50 * 1024 * 1024, 20);
    // executor->systolic_config = systolic_config;
    // executor->other_config = other_config;
    // dummy_dcache =  new DummyDCache("dcache");
#if USE_NB_DRAMSYS == 1
    executor->nb_dcache_socket->socket.bind(dcache->socket);
    // executor->dcache_socket->isocket.bind(dummy_dcache->target_socket);
#else
    executor->dcache_socket->isocket.bind(dcache->socket);
    // executor->nb_dcache_socket->socket.bind(dummy_dcache->target_socket);
#endif
    executor->mem_access_port->mem_read_port(*ram_array);
    executor->mem_access_port->mem_write_port(*ram_array);
    executor->high_bw_mem_access_port->mem_read_port(*ram_array);

    executor->temp_mem_access_port->mem_read_port(*temp_ram_array);
    executor->temp_mem_access_port->mem_write_port(*temp_ram_array);
    executor->high_bw_temp_mem_access_port->mem_read_port(*temp_ram_array);

    // systolic = new SystolicArray(sc_gen_unique_name("systolic-array"),
    //                              this->event_engine, systolic_config);

    // executor->systolic_done_i(systolic_done);
    // systolic->systolic_done_o(systolic_done);
    // executor->systolic_start_o(systolic_start);
    // systolic->systolic_start_i(systolic_start);
}

WorkerCore::~WorkerCore() {
    delete executor;
    delete dcache;
    // delete systolic;
    // delete systolic_config;
    // delete other_config;
    delete ram_array;
    delete temp_ram_array;
}

// workercore executor
WorkerCoreExecutor::WorkerCoreExecutor(const sc_module_name &n, int s_cid,
                                       Event_engine *event_engine)
    : sc_module(n), cid(s_cid), event_engine(event_engine) {
    prim_refill = false;

    SC_THREAD(catch_channel_avail_i);
    sensitive << channel_avail_i.pos();
    dont_initialize();

    SC_THREAD(catch_data_sent_i);
    sensitive << data_sent_i.pos();
    dont_initialize();

    SC_THREAD(next_write_clear);
    sensitive << ev_next_write_clear;
    dont_initialize();

    SC_THREAD(switch_prim_block);
    sensitive << ev_block;

    SC_THREAD(worker_core_execute);

    SC_THREAD(send_logic);
    sensitive << ev_send;
    dont_initialize();

    SC_THREAD(send_para_logic);
    sensitive << ev_para_send;
    dont_initialize();

    SC_THREAD(recv_logic);
    sensitive << ev_recv;
    dont_initialize();

    SC_THREAD(send_helper);
    sensitive << ev_send_helper;
    dont_initialize();

    SC_THREAD(task_logic);
    sensitive << ev_comp;
    dont_initialize();

    SC_THREAD(req_logic);
    sensitive << ev_recv_request << ev_prim_recv_notice;
    dont_initialize();

    SC_THREAD(call_systolic_array);
    sensitive << ev_systolic;
    dont_initialize();

    SC_THREAD(poll_buffer_i);
    sram_addr = new int(0);

    // 初始化PrimCoreContext
    core_context = new PrimCoreContext(cid);
    core_context->gpu_pos_locator_ = gpu_pos_locator;

    send_done = true;
    send_last_packet = false;
    
    start_global_mem_event = new sc_event();
    end_global_mem_event = new sc_event();
    start_nb_dram_event = new sc_event();
    start_nb_gpu_dram_event = new sc_event();
    start_sram_event = new sc_event();
    end_sram_event = new sc_event();

    end_nb_dram_event = new sc_event();
    end_nb_gpu_dram_event = new sc_event();

    sram_writer = new SRAMWriteModule("sram_writer", end_sram_event);
#if USE_NB_DRAMSYS == 1
    nb_dcache_socket =
        new NB_DcacheIF(cid, sc_gen_unique_name("nb_dcache"),
                        start_nb_dram_event, end_nb_dram_event, event_engine);
#else
    dcache_socket = new DcacheCore(sc_gen_unique_name("dcache"), event_engine);
#endif
#if USE_L1L2_CACHE == 1
    core_lv1_cache = new L1Cache(("l1_cache_" + to_string(cid)).c_str(), cid,
                                 L1CACHESIZE, L1CACHELINESIZE, 4, 8);
    gpunb_dcache_if = new GPUNB_dcacheIF(sc_gen_unique_name("nb_dcache_if"),
                                         cid, start_nb_gpu_dram_event,
                                         end_nb_gpu_dram_event, event_engine);
#else
#endif
    mem_access_port = new mem_access_unit(sc_gen_unique_name("mem_access_unit"),
                                          event_engine);
    high_bw_mem_access_port = new high_bw_mem_access_unit(
        sc_gen_unique_name("high_bw_mem_access_unit"), event_engine);
    temp_mem_access_port = new mem_access_unit(
        sc_gen_unique_name("temp_mem_access_unit"), event_engine);
    high_bw_temp_mem_access_port = new high_bw_mem_access_unit(
        sc_gen_unique_name("high_bw_temp_mem_access_unit"), event_engine);
}

void WorkerCoreExecutor::init_global_mem() {
    nb_global_mem_socket = new NB_GlobalMemIF(
        sc_gen_unique_name("nb_global_mem"), start_global_mem_event,
        end_global_mem_event, event_engine);
}

void WorkerCoreExecutor::end_of_elaboration() {
    // 在构造函数之后设置信号的初始值
    data_sent_o.write(false);
    core_busy_o.write(false);
}

void WorkerCoreExecutor::worker_core_execute() {
    while (true) {
        PrimBase *p = nullptr; // 下一个要执行的原语

        if (prim_queue.size() == 0) {
            // 队列中没有指令，意味着现在是初始状态或者所有原语都被执行完了（假设所有原语只做一轮），默认作recv，直到config发进来
            p = new Recv_prim(RECV_TYPE::RECV_CONF);
            prim_queue.emplace_front(p);
        } else {
            p = prim_queue.front();
        }
        cout << "[PRIM] Core <" << cid
             << ">: PRIM NAME -----------------------: " << p->name << endl;

        // NOTE:
        // send原语和recv原语和其他计算原语不同，需要涉及core中信号的处理，所以需要在core这个文件内部处理相关逻辑，否则会出现依赖问题。
        // 需要等待 switch_prim_block 将 prim_block 置为 false，然后再执行
        // switch_prim_block 收到 ev_block 触发 ev_block 在 send_logic 和
        // recv_logic 中触发

        if (typeid(*p) == typeid(Send_prim)) {
            // 触发 send_logic
#if SR_PARA == 0
            ev_send.notify(CYCLE, SC_NS);
            event_engine->add_event(
                "Core " + ToHexString(cid), "Send_prim", "B",
                Trace_event_util(
                    "Send_prim" +
                    GetEnumSendType(dynamic_cast<Send_prim *>(p)->type)));
            wait(prim_block.negedge_event());
            event_engine->add_event(
                "Core " + ToHexString(cid), "Send_prim", "E",
                Trace_event_util(
                    "Send_prim" +
                    GetEnumSendType(dynamic_cast<Send_prim *>(p)->type)));
#else
            while (!send_done)
                wait(CYCLE, SC_NS);

            // send 模块处理的四条指令
            while ((typeid(*p) == typeid(Recv_prim) &&
                    ((Recv_prim *)p)->type == RECV_ACK) ||
                   (typeid(*p) == typeid(Send_prim) &&
                    ((Send_prim *)p)->type == SEND_DATA) ||
                   (typeid(*p) == typeid(Send_prim) &&
                    ((Send_prim *)p)->type == SEND_REQ) ||
                   (typeid(*p) == typeid(Send_prim) &&
                    ((Send_prim *)p)->type == SEND_DONE)) {
                prim_queue.pop_front();
                send_para_queue.push(p);
                if (prim_refill) {
                    prim_queue.emplace_back(p);
                }
                if (!prim_queue.size())
                    break;
                // 这里会pop出来RECV_ACK
                p = prim_queue.front();
                cout << "Core " << cid << " push!!!!\n";
            }

            send_done = false;

            // 触发 send_logic
            ev_para_send.notify(CYCLE, SC_NS);
            continue;
#endif
        } else if (typeid(*p) == typeid(Recv_prim)) {
            ev_recv.notify(CYCLE, SC_NS);
            event_engine->add_event(
                "Core " + ToHexString(cid), "Receive_prim", "B",
                Trace_event_util(
                    "Receive_prim" +
                    GetEnumRecvType(dynamic_cast<Recv_prim *>(p)->type)));
            wait(prim_block.negedge_event());
            event_engine->add_event(
                "Core " + ToHexString(cid), "Receive_prim", "E",
                Trace_event_util(
                    "Receive_prim" +
                    GetEnumRecvType(dynamic_cast<Recv_prim *>(p)->type)));
        } else {
            // 检查队列中p的下一个原语是否还是计算原语
            bool last_comp = false;
            if (prim_queue.size() >= 2 &&
                prim_queue[1]->prim_type != COMP_PRIM &&
                prim_queue[1]->prim_type != PD_PRIM &&
                prim_queue[1]->prim_type != MOE_PRIM) {
                last_comp = true;
            }

            ev_comp.notify(CYCLE, SC_NS);
            event_engine->add_event("Core " + ToHexString(cid), "Comp_prim",
                                    "B", Trace_event_util(p->name));
            wait(prim_block.negedge_event());

            // 发送信号让send发送最后一个包
            if (last_comp) {
                send_last_packet = true;
                ev_send_last_packet.notify(CYCLE, SC_NS);
            }

            event_engine->add_event("Core " + ToHexString(cid), "Comp_prim",
                                    "E", Trace_event_util(p->name));
        }

        // 将原语重新填充到队列中
        if (prim_refill) {
            bool flag = false;
            if (typeid(*p) == typeid(Recv_prim)) {
                Recv_prim *rp = (Recv_prim *)p;
                if (rp->type == RECV_CONF || rp->type == RECV_WEIGHT) {
                    flag = true;
                }
            }

            if (!flag) {
                if (SYSTEM_MODE == SIM_DATAFLOW &&
                    typeid(*p) == typeid(Recv_prim)) {
                    Recv_prim *rp = (Recv_prim *)p;
                    if (rp->type == RECV_START) {
#if PIPELINE_MODE == 0
                        rp->type = RECV_DATA;
#endif
                    }
                }

                prim_queue.emplace_back(p);
            }
        }

        prim_queue.pop_front();
        wait(CYCLE, SC_NS);
    }
}

void WorkerCoreExecutor::switch_prim_block() {
    while (true) {
        prim_block.write(true);
        wait();

        prim_block.write(false);
        wait(CYCLE, SC_NS);
    }
}

// 指令被 RECV_CONF发送过来后，会在本地核实例化对应的指令类
PrimBase *WorkerCoreExecutor::parse_prim(sc_bv<128> buffer) {
    int type = buffer.range(7, 0).to_uint64();
    PrimBase *task = PrimFactory::getInstance().createPrim(type);

    task->deserialize(buffer);
    task->prim_context = core_context;

    return task;
}

void WorkerCoreExecutor::poll_buffer_i() {
    while (true) {
        if (!data_sent_i.read())
            wait(ev_data_sent_i);

        Msg m = DeserializeMsg(channel_i.read());
        switch (m.msg_type_) {
        case DATA:
            recv_buffer.push(m);
            ev_recv_data.notify(0, SC_NS);
            break;
        case P_DATA:
            recv_buffer.push(m);
            ev_recv_prepare_data.notify(0, SC_NS);
            break;
        case S_DATA:
            start_data_buffer.push(m);
            ev_recv_start_data.notify(0, SC_NS);
            break;
        case REQUEST:
            request_buffer.push_back(m);
            ev_recv_request.notify(0, SC_NS);
            break;
        case ACK:
            ack_buffer.push(m);
            ev_recv_ack.notify(0, SC_NS);
            break;
        case CONFIG:
            buffer_i.push(channel_i.read());
            ev_recv_config.notify(0, SC_NS);
            break;
        }

        if (buffer_i.size() >= MAX_BUFFER_PACKET_SIZE)
            core_busy_o.write(true);
        else
            core_busy_o.write(false);

        wait(CYCLE, SC_NS);
    }
}

void WorkerCoreExecutor::send_logic() {
    while (true) {
        Send_prim *prim = (Send_prim *)prim_queue.front();

        prim->data_packet_id = 0;
        bool job_done = false; // 结束内圈循环的标志

        cout << "[SEND START] Core " << cid << ": running send "
             << GetEnumSendType(prim->type) << ", destination " << prim->des_id
             << ", tag " << prim->tag_id << ", max packet " << prim->max_packet
             << " at " << sc_time_stamp() << endl;

        while (true) {
            bool need_long_wait = false;

            if (atomic_helper_lock(sc_time_stamp(), 0))
                ev_send_helper.notify(0, SC_NS);

            // SEND_DATA, SEND_ACK, SEND_REQ
            if (prim->type == SEND_DATA) {
#if ROUTER_PIPE == 1
                while (job_done != true) {
#endif
                    // [发送方] 正常发送数据
                    prim->data_packet_id++;

                    bool is_end_packet =
                        prim->data_packet_id == prim->max_packet;
                    int length = M_D_DATA;
                    if (is_end_packet) {
                        length = prim->end_length;
                    }

                    int delay = 0;
                    TaskCoreContext context = generate_context(this);
                    // 因为send taskCoreDefault 会delay 所以 ev_send_helper
                    // 有机会发出去 然后wc 里面又要wait 一个cycle ev_send_helper
                    // 一低一高
                    delay = prim->taskCoreDefault(context);

                    if (!channel_avail_i.read())
                        wait(ev_channel_avail_i);

                    send_buffer =
                        Msg(prim->data_packet_id == prim->max_packet,
                            MSG_TYPE::DATA, prim->data_packet_id, prim->des_id,
                            0, prim->tag_id, length, sc_bv<128>(0x1));

                    // send_helper_write = 3;
                    atomic_helper_lock(sc_time_stamp(), 3);
                    ev_send_helper.notify(0, SC_NS);

                    if (prim->data_packet_id == prim->max_packet) {
                        cout << "Core " << cid
                             << " max_packet: " << prim->max_packet << " "
                             << send_buffer.is_end_ << endl;

                        job_done = true;
                    }

                    need_long_wait = true;
#if ROUTER_PIPE == 1
                }
#endif
            }

            else if (prim->type == SEND_REQ) {
                // [发送方] 发送一个req包，发送完之后结束此原语，进入 RECV_ACK
                if (!channel_avail_i.read())
                    wait(ev_channel_avail_i);

                send_buffer =
                    Msg(MSG_TYPE::REQUEST, prim->des_id, prim->tag_id, cid);

                send_helper_write = 3;
                ev_send_helper.notify(0, SC_NS);

                cout << sc_time_stamp() << ": Worker " << cid << ": REQ to "
                     << prim->des_id << " sent.\n";

                job_done = true;
            }

            else if (prim->type == SEND_DONE) {
                // [执行核]
                // 在计算图的汇节点执行完毕之后，给host发送一份DONE数据包，标志任务完成
                if (!channel_avail_i.read())
                    wait(ev_channel_avail_i);

                send_buffer = Msg(MSG_TYPE::DONE, GRID_SIZE, cid);

                if (SYSTEM_MODE == SIM_PD || SYSTEM_MODE == SIM_PDS) {
                    for (int i = 0; i < core_context->decode_done_.size();
                         i++) {
                        send_buffer.data_.range(i, i) =
                            sc_bv<1>(core_context->decode_done_[i]);
                    }
                }

                send_helper_write = 3;
                ev_send_helper.notify(0, SC_NS);

                cout << sc_time_stamp() << ": Worker " << cid
                     << ": DONE sent.\n";

                job_done = true;
            }

            else {
                // unimplemented
                cout << sc_time_stamp() << ": Worker " << cid
                     << ": unimplemented SEND_PRIM.\n";

                sc_stop();
            }

            if (job_done) {
                cout << "[SEND DONE] Core " << cid << ": running send "
                     << GetEnumSendType(prim->type) << " done at "
                     << sc_time_stamp() << "\n";
                break;
            }


            wait(CYCLE, SC_NS);
            if (need_long_wait) {
                wait((CORE_ACC_PAYLOAD - 1) * 2, SC_NS);
            }
        }

        ev_block.notify(CYCLE, SC_NS);
        wait();
    }
}

void WorkerCoreExecutor::send_para_logic() {
    while (true) {
        while (send_para_queue.size()) {
            PrimBase *prim = send_para_queue.front();
            send_para_queue.pop();

            if (typeid(*prim) == typeid(Send_prim)) {
                ((Send_prim *)prim)->data_packet_id = 0;
                cout << "Core " << cid << " going para send\n";
                event_engine->add_event(
                    "Core " + ToHexString(cid), "Send_prim", "B",
                    Trace_event_util(
                        "Send_prim" +
                        GetEnumSendType(
                            dynamic_cast<Send_prim *>(prim)->type)));
            } else if (typeid(*prim) == typeid(Recv_prim)) {
                cout << "Core " << cid << " going para recv\n";
                event_engine->add_event(
                    "Core " + ToHexString(cid), "Recv_prim", "B",
                    Trace_event_util(
                        "Recv_prim" +
                        GetEnumRecvType(
                            dynamic_cast<Recv_prim *>(prim)->type)));
            }

            bool job_done = false; // 结束内圈循环的标志

            while (true) {
#if ROUTER_PIPE == 0
                if (atomic_helper_lock(sc_time_stamp(), 0))
                    ev_send_helper.notify(0, SC_NS);
#else
                while (atomic_helper_lock(sc_time_stamp(), 0) == false) {
#if ROUTER_LOOP == 1
                    cout << "Core " << cid << " wait for atomic_helper_lock"
                         << endl;
#endif
                    wait(CYCLE, SC_NS);
#if ROUTER_LOOP == 1
                    cout << "Core " << cid << " wake up" << endl;
#endif
                }
                ev_send_helper.notify(0, SC_NS);
#endif

                if (job_done)
                    break;

                // SEND_DATA, SEND_ACK, SEND_REQ
                if (typeid(*prim) == typeid(Send_prim) &&
                    ((Send_prim *)prim)->type == SEND_DATA) {
                    // [发送方] 正常发送数据，数据从DRAM中获取
                    Send_prim *s_prim = (Send_prim *)prim;

                    // atomic_helper_lock 其实是为了表示上锁
#if ROUTER_PIPE == 1
                    while (job_done == false) {
                        if (channel_avail_i.read() &&
                            atomic_helper_lock(sc_time_stamp(), 1)) {
#if ROUTER_LOOP == 1
                            cout << "Core " << cid
                                 << " wait for 746 atomic_helper_lock time "
                                 << sc_time_stamp() << endl;

#endif
                            // atomic_helper_lock(sc_time_stamp(), 1) always
                            // true unless 811
                            // atomic_helper_lock(sc_time_stamp(), 0);
#else
                    if (channel_avail_i.read() &&
                        atomic_helper_lock(sc_time_stamp(), 1)) {
#endif
                            ev_send_helper.notify(0, SC_NS);

                            s_prim->data_packet_id++;

                            bool is_end_packet =
                                s_prim->data_packet_id == s_prim->max_packet;
                            int length = M_D_DATA;
                            if (is_end_packet) {
                                length = s_prim->end_length;
#if ROUTER_PIPE == 0
                                while (!send_last_packet)
                                    wait(ev_send_last_packet);

#else
                            while (!send_last_packet) {
                                atomic_helper_lock(sc_time_stamp(), 0, true);
#if ROUTER_LOOP == 1
                                cout << "Core " << cid
                                     << " wait for 771 atomic_helper_lock"
                                     << endl;
#endif
                                wait(ev_send_last_packet);
                                while (atomic_helper_lock(sc_time_stamp(), 0) ==
                                       false) {
                                    wait(CYCLE, SC_NS);
                                }
                            }
#endif


                                send_last_packet = false;
                            }
#if ROUTER_PIPE == 1
                            send_buffer = Msg(
                                s_prim->data_packet_id == s_prim->max_packet,
                                MSG_TYPE::DATA, s_prim->data_packet_id,
                                s_prim->des_id, 0, s_prim->tag_id, length,
                                sc_bv<128>(0x1));

#endif

                            int delay = 0;
                            TaskCoreContext context = generate_context(this);
                            delay = prim->taskCoreDefault(context);
#if ROUTER_PIPE == 0
                            send_buffer = Msg(
                                s_prim->data_packet_id == s_prim->max_packet,
                                MSG_TYPE::DATA, s_prim->data_packet_id,
                                s_prim->des_id, 0, s_prim->tag_id, length,
                                sc_bv<128>(0x1));
#endif
#if ROUTER_PIPE == 1
                            atomic_helper_lock(sc_time_stamp(), 0, true);
#else
                        atomic_helper_lock(sc_time_stamp(), 2);
#endif
                            ev_send_helper.notify(0, SC_NS);

                            if (s_prim->data_packet_id == s_prim->max_packet) {
                                job_done = true;
                                cout << "Core " << cid
                                     << " max_packet: " << s_prim->max_packet
                                     << " " << send_buffer.is_end_ << endl;
                            }
                        }
#if ROUTER_PIPE == 1
                        else {
                            cout << "Core " << cid << " "
                                 << channel_avail_i.read() << endl;

                            if (send_helper_write == 1) {
                                send_helper_write = 0;
                            }

                            wait(CYCLE, SC_NS);
                            atomic_helper_lock(sc_time_stamp(), 0);
                            // cout << "Core " << channel_avail_i.read() <<
                            // endl;
                        }
                    }
#endif
                }

                else if (typeid(*prim) == typeid(Send_prim) &&
                         ((Send_prim *)prim)->type == SEND_REQ) {
                    Send_prim *s_prim = (Send_prim *)prim;
                    // [发送方] 发送一个req包，发送完之后结束此原语，进入
                    // RECV_ACK
                    if (channel_avail_i.read() &&
                        atomic_helper_lock(sc_time_stamp(), 3)) {
                        // 可以发送数据
                        send_buffer = Msg(MSG_TYPE::REQUEST, s_prim->des_id,
                                          s_prim->tag_id, cid);

                        ev_send_helper.notify(0, SC_NS);

                        cout << sc_time_stamp() << ": Worker " << cid
                             << ": REQ to " << s_prim->des_id << " sent.\n";

                        job_done = true;
                    }
                }

                else if (typeid(*prim) == typeid(Send_prim) &&
                         ((Send_prim *)prim)->type == SEND_DONE) {
                    Send_prim *s_prim = (Send_prim *)prim;
                    // [执行核]
                    // 在计算图的汇节点执行完毕之后，给host发送一份DONE数据包，标志任务完成
                    if (channel_avail_i.read() &&
                        atomic_helper_lock(sc_time_stamp(), 3)) {
                        // 可以发送数据
                        send_buffer = Msg(MSG_TYPE::DONE, GRID_SIZE, cid);

                        ev_send_helper.notify(0, SC_NS);

                        cout << sc_time_stamp() << ": Worker " << cid
                             << ": DONE sent.\n";

                        job_done = true;
                    }
                }

                else if (typeid(*prim) == typeid(Recv_prim) &&
                         ((Recv_prim *)prim)->type == RECV_ACK) {
                    // [发送方] 接收来自接收方的ack包，收到之后结束此原语，进入
                    // SEND_DATA 或 SEND_SRAM
                    if (ack_buffer.size()) {
                        // 接收到数据包
                        Msg m = ack_buffer.front();
                        ack_buffer.pop();

                        if (m.msg_type_ == ACK) {
                            job_done = true;

                            cout << sc_time_stamp() << ": Worker " << cid
                                 << ": received ACK packet.\n";
                        }
                    }
                }

                else {
                    // unimplemented
                    cout << sc_time_stamp() << ": Worker " << cid
                         << ": unimplemented SEND_PRIM.\n";

                    sc_stop();
                }

                // 等待下一个时钟周期
                wait(CYCLE, SC_NS);
            }

            if (typeid(*prim) == typeid(Send_prim)) {
                event_engine->add_event(
                    "Core " + ToHexString(cid), "Send_prim", "E",
                    Trace_event_util(
                        "Send_prim" +
                        GetEnumSendType(
                            dynamic_cast<Send_prim *>(prim)->type)));
            } else {
                event_engine->add_event(
                    "Core " + ToHexString(cid), "Recv_prim", "E",
                    Trace_event_util(
                        "Recv_prim" +
                        GetEnumRecvType(
                            dynamic_cast<Recv_prim *>(prim)->type)));
            }
        }

        send_done = true;
        wait();
    }
}

void WorkerCoreExecutor::recv_logic() {
    while (true) {
        Recv_prim *prim = (Recv_prim *)prim_queue.front();

        int recv_cnt = 0;
        int max_recv = 0;
        // 已经接收到的end包数量，需要等于recv原语中的对应要求才能结束此原语
        int end_cnt = 0;
        // 在RECV_CONFIG中，接收到最后一个config包之后，需要等待发送ack
        bool wait_send = false;
        bool job_done = false;

        cout << "[RECV] Core " << cid << ": running recv "
             << GetEnumRecvType(prim->type) << ", recv_cnt "
             << prim->recv_cnt << ", recv_tag " << prim->tag_id << endl;

        while (true) {
#if ROUTER_LOOP == 1
            cout << "Core " << cid << " wait for 944 atomic_helper_lock"
                 << endl;
#endif
            bool need_long_wait = false;

            if (atomic_helper_lock(sc_time_stamp(), 0))
                ev_send_helper.notify(0, SC_NS);

            if (prim->type == RECV_ACK) {
                // [发送方] 接收来自接收方的ack包，收到之后结束此原语，进入
                // SEND_DATA 或 SEND_SRAM
                if (!ack_buffer.size())
                    wait(ev_recv_ack);

                // 接收到数据包
                Msg m = ack_buffer.front();
                ack_buffer.pop();

                if (m.msg_type_ == ACK) {
                    job_done = true;

                    cout << sc_time_stamp() << ": Worker " << cid
                         << ": received ACK packet.\n";
                }
            }

            else if (prim->type == RECV_WEIGHT) {
                // [接收方]
                // 接收消息，但是途中如果有新的REQ包进入，需要判断是否要回发ACK包

                // 如果recv_cnt等于0,说明无需接收包裹，直接开始comp即可

                // 按照prim的tag进行判断。如果tag等同于cid，则优先查看start
                // data buffer，再查看recv buffer
                // 如果tag不等同于id，则不允许查看start data buffer

                Msg temp;
                // 表示 当前周期该核有需要处理的msg 的recv包
                if (!recv_buffer.size())
                    wait(ev_recv_prepare_data);

                temp = recv_buffer.front();
                // if (prim->tag_id != cid && temp.tag_id != prim->tag_id) {
                //     cout << "[WARN] Core " << cid
                //          << " gets incompatible tag id: prim tag "
                //          << prim->tag_id << " with buffer top msg tag "
                //          << temp.tag_id << endl;
                //     sc_stop();
                // }

                recv_buffer.pop();
                // 复制到SRAM中
                // 如果是end包，则将recv_index归零，表示开始接收下一个core传来的数据（如果有的话）
                if (temp.is_end_) {
                    while (!atomic_helper_lock(sc_time_stamp(), 3) ||
                           !channel_avail_i.read())
                        wait(CYCLE, SC_NS);

                    // 这里是针对host data 和 start 包
                    cout << sc_time_stamp() << ": Worker " << cid
                         << ": received all prepare data.\n";

                    // 向host发送一个ack包
                    send_buffer =
                        Msg(MSG_TYPE::ACK, GRID_SIZE, prim->tag_id, cid);
                    ev_send_helper.notify(0, SC_NS);

                    cout << sc_time_stamp() << ": Worker " << cid
                         << " receive end packet: end_cnt " << end_cnt
                         << ", recv_cnt " << recv_cnt << ", max_recv "
                         << max_recv << endl;

                    job_done = true;
                }
            }

            else if (prim->type == RECV_DATA || prim->type == RECV_START) {
                // [接收方]
                // 接收消息，但是途中如果有新的REQ包进入，需要判断是否要回发ACK包
                // 如果recv_cnt等于0,说明无需接收包裹，直接开始comp即可
                if (prim->recv_cnt == 0)
                    job_done = true;
                else {
                    // 按照prim的tag进行判断。如果tag等同于cid，则优先查看start
                    // data buffer，再查看recv buffer
                    // 如果tag不等同于id，则不允许查看start data buffer
                    ev_prim_recv_notice.notify(0, SC_NS);

                    Msg temp;
                    // 表示 当前周期该核有需要处理的msg 的recv包
                    if (prim->type == RECV_DATA) {
#if ROUTER_LOOP == 1
                        if (!recv_buffer.size()) {
                            cout << "Core " << cid
                                 << " wait for 1024 atomic_helper_lock" << endl;
                            wait(ev_recv_data);
                        }
#else
                        if (!recv_buffer.size())
                            wait(ev_recv_data);
#endif

                        temp = recv_buffer.front();
                    } else if (prim->type == RECV_START) {
                        if (!start_data_buffer.size())
                            wait(ev_recv_start_data);

                        temp = start_data_buffer.front();
                    }

                    if (prim->tag_id != cid && temp.tag_id_ != prim->tag_id) {
                        cout << "[WARN] Core " << cid
                             << " gets incompatible tag id: prim tag "
                             << prim->tag_id << " with buffer top msg tag "
                             << temp.tag_id_ << endl;
                        sc_stop();
                    }

                    if (prim->type == RECV_DATA)
                        recv_buffer.pop();
                    else
                        start_data_buffer.pop();

                    recv_cnt++;

                    if (temp.seq_id_ == 1 &&
                        (SYSTEM_MODE == SIM_DATAFLOW || SYSTEM_MODE == SIM_PD ||
                         SYSTEM_MODE == SIM_PDS)) {
                        // 在pos locator中添加一个kv，label是input_label
                        // 对于每一个核的第一算子的input来自与send
                        // 核的输出，并且已经会由router保存在sram上
                        AddrPosKey inp_key = AddrPosKey(*sram_addr, 0);
                        char format_label[100];
                        sprintf(format_label, "%s", INPUT_LABEL);
                        string input_label = format_label;

                        u_int64_t temp;
                        if (core_context->sram_pos_locator_->findPair(input_label, inp_key) ==
                            -1)
                            // TWO DRIVER
                            core_context->sram_pos_locator_->addPair(input_label, inp_key);
                    }

                    int delay = 0;
                    TaskCoreContext context = generate_context(this);
                    delay = prim->taskCoreDefault(context);

                    // 如果是end包，则将recv_index归零，表示开始接收下一个core传来的数据（如果有的话）
                    if (temp.is_end_) {
                        end_cnt++;
                        max_recv += temp.seq_id_;

                        cout << sc_time_stamp() << ": Worker " << cid
                             << " receive end packet: end_cnt " << end_cnt
                             << ", recv_cnt " << recv_cnt << ", max_recv "
                             << max_recv << endl;

                        // prim->recv_cnt 记录的是 receive 原语 需要接受的
                        // end 包的数量 多发一的实现 max_recv 表示当前 DATA
                        // 包 发送了多少个 package 数量
                        if (end_cnt == prim->recv_cnt && recv_cnt >= max_recv) {
                            // 收到了所有的数据，可以结束此原语，进入comp原语
                            // 更新pos_locator中的kv的size
                            if (SYSTEM_MODE == SIM_DATAFLOW ||
                                SYSTEM_MODE == SIM_PD ||
                                SYSTEM_MODE == SIM_PDS) {
                                AddrPosKey inp_key;
                                char format_label[100];
                                sprintf(format_label, "%s", INPUT_LABEL);
                                string input_label = format_label;
                                inp_key.size = 0; //+= max_recv * M_D_DATA;
                                core_context->sram_pos_locator_->findPair(input_label,
                                                           inp_key);


                                u_int64_t temp;
                                core_context->sram_pos_locator_->addPair(input_label, inp_key);
                            }

                            job_done = true;
                        }
                    }

                    need_long_wait = true;
                }
            }

            else if (prim->type == RECV_CONF) {
                // [所有人]
                // 在模拟开始时接收配置，接收完毕之后发送一个ACK包给host，此原语需要对prim_queue进行压入，此原语执行完毕之后，进入RECV_DATA
                if (wait_send) {
                    while (!atomic_helper_lock(sc_time_stamp(), 3) ||
                           !channel_avail_i.read())
                        wait(CYCLE, SC_NS);

                    // 正在等待向host发送ack包
                    send_buffer =
                        Msg(MSG_TYPE::ACK, GRID_SIZE, prim->tag_id, cid);
                    ev_send_helper.notify(0, SC_NS);

                    cout << "[RECV] Core " << cid << ": received all CONFIG.\n";

                    job_done = true;
                } else {
                    // cout << "[RECV] Core " << cid << ": received all
                    // CONFIG.\n";
                    if (!buffer_i.size())
                        wait(ev_recv_config);

                    Msg m = DeserializeMsg(buffer_i.front());
                    buffer_i.pop();
                    prim_queue.emplace_back(parse_prim(m.data_));

                    // cout << "Core " << cid << " recv config " << m.seq_id
                    //      << endl;

                    // 检查是否为end config包，如果是，需要向host发送ack包
                    if (m.is_end_) {
                        this->prim_refill = m.refill_;
                        wait_send = true;
                    }
                }
            }

            else {
                // unimplemented
                cout << sc_time_stamp() << ": Worker " << cid
                     << ": unimplemented RECV_PRIM.\n";

                sc_stop();
            }

            if (job_done)
                break;

            // 等待下一个时钟周期
            wait(CYCLE, SC_NS);
            if (need_long_wait) {
                wait((CORE_ACC_PAYLOAD - 1) * 2, SC_NS);
            }
        }

        ev_block.notify(CYCLE, SC_NS);
        wait();
    }
}

void WorkerCoreExecutor::task_logic() {
    while (true) {
        PrimBase *p = prim_queue.front();

        int delay = 0;
        TaskCoreContext context = generate_context(this);

        cout << "[PRIM] Core <\033[38;5;214m" << cid
             << "\033[0m>: PRIM NAME -----------------------: " << p->name
             << endl;
        delay = p->taskCoreDefault(context);
        wait(sc_time(delay, SC_NS));

        cout << "Core " << cid << ": task " << p->name << " done.\n";

        ev_block.notify(CYCLE, SC_NS);
        wait();
    }
}

void WorkerCoreExecutor::req_logic() {
    queue<int> ack_queue;

    while (true) {
        if (prim_queue.size()) {
            PrimBase *p = prim_queue.front();

            if (typeid(*p) == typeid(Recv_prim)) {
                Recv_prim *prim = (Recv_prim *)p;

                if ((prim->type == RECV_DATA || prim->type == RECV_START) &&
                    request_buffer.size()) {
                    for (auto i = request_buffer.begin();
                         i != request_buffer.end();) {
                        if ((*i).tag_id_ == prim->tag_id) {
                            ack_queue.push((*i).source_);
                            i = request_buffer.erase(i);
                        } else
                            i++;
                    }
                }

                // 发送ack包
                while (ack_queue.size()) {
                    while (!atomic_helper_lock(sc_time_stamp(), 3) ||
                           !channel_avail_i.read())
                        wait(CYCLE, SC_NS);

                    int des = ack_queue.front();
                    ack_queue.pop();

                    send_buffer = Msg(MSG_TYPE::ACK, des, des, cid);
                    ev_send_helper.notify(0, SC_NS);

                    cout << "Core " << cid << " sent ACK to " << des << endl;
                }
            }
        }

        wait();
    }
}

/*
 在workercore executor中添加了一把锁，用于lock住write helper，
 因为同时运行send和recv原语会在同一个时钟周期内access write helper函数
*/

/*
send_helper_write >= 2, that data_sent_o = true send a msg to router
send_helper_write < 2, that data_sent_o = false, reset signal

present_time the most recent time that the helper is try to lock

try = present_time some one has try to lock the helper before in the same time

status = 0, If a new cycle begins and no other module requires the helper, reset
send_helper_write to 0. pool down data_sent_o status = 1, 表示 准备执行send
taskCoreDefault （会有delay） 一般在status 0 之后 同一个周期内，行为和 0 一致

status = 2 表示send 从 sram 里面已经拿到数据了，可以开始发送了

status = 1 2 都只出现一次



*/
bool WorkerCoreExecutor::atomic_helper_lock(sc_time try_time, int status,
                                            bool force) {
    bool res;

    if (try_time < present_time)
        res = false;

    if (try_time == present_time) {
#if ROUTER_LOOP == 1
        cout << "Core " << cid << " try_time============: " << try_time
             << " present_time: " << present_time << " status: " << status
             << "send_helper_write " << send_helper_write << endl;
#endif
        if (status == 0) {

            if (force == true) {
                send_helper_write = status;
                return true;
            }
            return false;
        }

        if (status == 1) { // send prepare
            // status 1 只会出现在这里
            if (send_helper_write == 0) {
                send_helper_write = 1;
                res = true;
            } else {
                res = false;
            }
        }
        if (status == 2) { // send ready
            if (send_helper_write == 1) {
                send_helper_write = 2;
                res = true;
            } else {
                res = false;
            }
        }
        if (status == 3) { // other pass cond.
            if (send_helper_write == 0) {
                send_helper_write = 3;
                res = true;
            } else {
                res = false;
            }
        }
    }

    if (try_time > present_time) {
#if ROUTER_LOOP == 1
        cout << "Core " << cid << " try_time>>>>>>>>>>: " << try_time
             << " present_time: " << present_time << " status: " << status
             << "send_helper_write " << send_helper_write << endl;
#endif
        if (try_time - present_time < sc_time(CYCLE, SC_NS))
            return false;

        present_time = try_time;
        // status 1 不会进入到这里，因为status 1 之前肯定会有 status 0
        // 修改了 present_time
        if (status == 2) { // send ready
            if (send_helper_write == 1) {
                send_helper_write = 2;
                res = true;
            } else {
                res = false;
            }
        } else {
            // 这里应该只会进 status 0,
            // 1 和 3 ( 3 除了返回给host ack 因为while循环)
            // 都会在0后处理，且不会有延迟，所以pre=try_time
            // status 1 后面只能紧跟status 2
            // 防止status 3 把 原本 status 2 抢占 了
            if (send_helper_write == 1 && force == false)
                res = false;
            else {
                // 0 或者 3 是当前周期第一来的状态
                // 2 只会出现上面一种情况 成为当前周期第一来的状态

                send_helper_write = status;
                res = true;
            }
        }
    }

    return res;
}
// data_sent_o pos trigger router && later router can self trigger if
// data_sent_o is true 是否拉低不重要，只要 data_sent_o 是高就能发送
void WorkerCoreExecutor::send_helper() {
    while (true) {
#if ROUTER_PIPE == 1
        if (send_helper_write >= 1) {

#else
        if (send_helper_write >= 2) {
#endif
            channel_o.write(SerializeMsg(send_buffer));
            data_sent_o.write(true);
            ev_next_write_clear.notify(CYCLE, SC_NS);
        } else
            data_sent_o.write(false);

        wait();
    }
}

void WorkerCoreExecutor::call_systolic_array() {
    while (true) {
        // if (systolic_config) {
        //     systolic_start_o.write(true);
        //     wait(CYCLE, SC_NS);
        //     systolic_start_o.write(false);
        // }

        wait();
    }
}

void WorkerCoreExecutor::catch_channel_avail_i() {
    while (true) {
        ev_channel_avail_i.notify(CYCLE, SC_NS);

        wait();
    }
}

void WorkerCoreExecutor::next_write_clear() {
    while (true) {
        if (atomic_helper_lock(sc_time_stamp(), 0))
            ev_send_helper.notify(0, SC_NS);

        wait();
    }
}

void WorkerCoreExecutor::catch_data_sent_i() {
    while (true) {
        ev_data_sent_i.notify(CYCLE, SC_NS);

        wait();
    }
}

WorkerCoreExecutor::~WorkerCoreExecutor() {
    delete sram_addr;
#if USE_NB_DRAMSYS == 1
    delete nb_dcache_socket;
#else
    delete dcache_socket;
#endif
    delete mem_access_port;
    delete high_bw_mem_access_port;
    delete temp_mem_access_port;
    delete high_bw_temp_mem_access_port;
    delete start_nb_dram_event;
    delete end_nb_dram_event;
    delete start_sram_event;
    delete end_sram_event;
    delete start_global_mem_event;
    delete end_global_mem_event;
    delete sram_writer;
    delete g_dram_kvtable;
    delete g_dram_kvtable[cid];

    delete core_context;
}