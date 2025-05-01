#include "systemc.h"
#include <deque>
#include <iostream>
#include <queue>
#include <string>
#include <typeinfo>

#include "defs/const.h"
#include "defs/global.h"
#include "memory/dram/GPUNB_DcacheIF.h"
#include "memory/gpu/GPU_L1L2_Cache.h"
#include "memory/sram/Mem_access_unit.h"
#include "prims/comp_prims.h"
#include "prims/norm_prims.h"
#include "prims/prim_base.h"
#include "trace/Event_engine.h"
#include "utils/file_utils.h"
#include "utils/msg_utils.h"
#include "utils/pe_utils.h"
#include "utils/prim_utils.h"
#include "utils/system_utils.h"
#include "workercore/workercore.h"


using namespace std;

// workercore
WorkerCore::WorkerCore(const sc_module_name &n, int s_cid,
                       Event_engine *event_engine)
    : sc_module(n), cid(s_cid), event_engine(event_engine) {
    systolic_config = new HardwareTaskConfig();
    other_config = new HardwareTaskConfig();
    dcache = new DCache(sc_gen_unique_name("dcache"), (int)cid / GRID_X,
                        (int)cid % GRID_X, this->event_engine,
                        "../DRAMSys/configs/ddr4-example.json",
                        "../DRAMSys/configs");
    ram_array = new DynamicBandwidthRamRow<sc_bv<SRAM_BITWIDTH>, SRAM_BANKS>(
        sc_gen_unique_name("ram_array"), 0, BANK_DEPTH, SIMU_READ_PORT,
        SIMU_WRITE_PORT, BANK_PORT_NUM + SRAM_BANKS, BANK_PORT_NUM,
        BANK_HIGH_READ_PORT_NUM, event_engine);
    executor = new WorkerCoreExecutor(sc_gen_unique_name("workercore-exec"),
                                      cid, this->event_engine);
    executor->systolic_config = systolic_config;
    executor->other_config = other_config;
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

    systolic = new SystolicArray(sc_gen_unique_name("systolic-array"),
                                 this->event_engine, systolic_config);

    executor->systolic_done_i(systolic_done);
    systolic->systolic_done_o(systolic_done);
    executor->systolic_start_o(systolic_start);
    systolic->systolic_start_i(systolic_start);
}

WorkerCore::~WorkerCore() {
    delete executor;
    delete dcache;
    delete systolic;
    delete systolic_config;
    delete other_config;
    delete ram_array;
}

// workercore executor
WorkerCoreExecutor::WorkerCoreExecutor(const sc_module_name &n, int s_cid,
                                       Event_engine *event_engine)
    : sc_module(n), cid(s_cid), event_engine(event_engine) {
    prim_refill = false;

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

    SC_THREAD(call_systolic_array);
    sensitive << ev_systolic;
    dont_initialize();

    SC_THREAD(poll_buffer_i);
    sram_addr = new int(0);
    send_done = true;
    send_last_packet = false;
    loop_cnt = 1;
    start_nb_dram_event = new sc_event();
    start_nb_gpu_dram_event = new sc_event();
    end_nb_dram_event = new sc_event();
    end_nb_gpu_dram_event = new sc_event();
    next_datapass_label = new AddrDatapassLabel();
    sram_pos_locator = new SramPosLocator(s_cid);
#if USE_NB_DRAMSYS == 1
    nb_dcache_socket =
        new NB_DcacheIF(sc_gen_unique_name("nb_dcache"), start_nb_dram_event,
                        end_nb_dram_event, event_engine);
#else
    dcache_socket = new DcacheCore(sc_gen_unique_name("dcache"), event_engine);
#endif
#if USE_L1L2_CACHE == 1
    core_lv1_cache = new L1Cache(("l1_cache_" + to_string(cid)).c_str(), cid,
                                 8192, 64, 4, 8);
    gpunb_dcache_if = new GPUNB_dcacheIF(sc_gen_unique_name("nb_dcache_if"),
                                         cid, start_nb_gpu_dram_event,
                                         end_nb_gpu_dram_event, event_engine);
    // cache_processor = new Processor(("processor_" + to_string(cid)).c_str(),
    // cid * 1000);
#else
#endif
    mem_access_port = new mem_access_unit(sc_gen_unique_name("mem_access_unit"),
                                          event_engine);
    high_bw_mem_access_port = new high_bw_mem_access_unit(
        sc_gen_unique_name("high_bw_mem_access_unit"), event_engine);
}

void WorkerCoreExecutor::end_of_elaboration() {
    // 在构造函数之后设置信号的初始值
    data_sent_o.write(false);
    core_busy_o.write(false);
}

void WorkerCoreExecutor::worker_core_execute() {
    while (true) {
        prim_base *p = nullptr; // 下一个要执行的原语

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
                "Core " + toHexString(cid), "Send_prim", "B",
                Trace_event_util(
                    "Send_prim" +
                    get_send_type_name(dynamic_cast<Send_prim *>(p)->type)));
            wait(prim_block.negedge_event());
            event_engine->add_event(
                "Core " + toHexString(cid), "Send_prim", "E",
                Trace_event_util(
                    "Send_prim" +
                    get_send_type_name(dynamic_cast<Send_prim *>(p)->type)));
#else
            while (!send_done) {
                wait(CYCLE, SC_NS);
            }
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
            // cout << cid << " recv, type " << ((Recv_prim*)p)->type << ".\n";
            ev_recv.notify(CYCLE, SC_NS);
            event_engine->add_event(
                "Core " + toHexString(cid), "Receive_prim", "B",
                Trace_event_util(
                    "Receive_prim" +
                    get_recv_type_name(dynamic_cast<Recv_prim *>(p)->type)));
            wait(prim_block.negedge_event());
            event_engine->add_event(
                "Core " + toHexString(cid), "Receive_prim", "E",
                Trace_event_util(
                    "Receive_prim" +
                    get_recv_type_name(dynamic_cast<Recv_prim *>(p)->type)));
        } else {
            // 检查队列中p的下一个原语是否还是计算原语
            bool last_comp = false;
            if (prim_queue.size() >= 2 && !is_comp_prim(prim_queue[1])) {
                last_comp = true;
            }

            ev_comp.notify(CYCLE, SC_NS);
            event_engine->add_event("Core " + toHexString(cid), "Comp_prim",
                                    "B", Trace_event_util(p->name));
            wait(prim_block.negedge_event());

            // 发送信号让send发送最后一个包
            if (last_comp) {
                send_last_packet = true;
            }

            event_engine->add_event("Core " + toHexString(cid), "Comp_prim",
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
        wait(
            CYCLE,
            SC_NS); // 等待一个时钟周期后立刻将prim_block置为true，由于没有对其上升沿的检测，所以是可行的
    }
}
// 指令被 RECV_CONF发送过来后，会在本地核实例化对应的指令类
prim_base *WorkerCoreExecutor::parse_prim(sc_bv<128> buffer) {
    prim_base *task = nullptr;
    int type = buffer.range(7, 0).to_uint64();

    switch (type) {
    case 0x1:
        task = new Layernorm_f();
        break;
    case 0x2:
        task = new Matmul_f();
        break;
    case 0x3:
        task = new Attention_f();
        break;
    case 0x4:
        task = new Gelu_f();
        break;
    case 0x5:
        task = new Residual_f();
        break;
    case 0x6:
        task = new Send_prim();
        break;
    case 0x7:
        task = new Recv_prim();
        break;
    case 0x8:
        task = new Load_prim();
        break;
    case 0x9:
        task = new Store_prim();
        break;
    case 0xa:
        task = new Conv_f();
        break;
    case 0xb:
        task = new Relu_f();
        break;
    case 0xc:
        task = new Split_matmul();
        break;
    case 0xd:
        task = new Merge_matmul();
        break;
    case 0xe:
        task = new Split_conv();
        break;
    case 0xf:
        task = new Merge_conv();
        break;
    case 0x10:
        task = new Batchnorm_f();
        break;
    case 0x11:
        task = new Matmul_f_decode();
        break;
    case 0x12:
        task = new Attention_f_decode();
        break;
    case 0x13:
        task = new Max_pool();
        break;
    case 0x14:
        task = new Matmul_f_prefill();
        break;
    case 0xd0:
        task = new Dummy_p();
        break;
    case 0xd1:
        task = new Set_addr(this->next_datapass_label);
        break;
    case 0xd2:
        task = new Clear_sram();
        break;
    case 0xe0:
        task = new Matmul_f_gpu();
        break;
    case 0xe1:
        task = new Attention_f_gpu();
        break;
    case 0xe2:
        task = new Gelu_f_gpu();
        break;
    case 0xe3:
        task = new Residual_f_gpu();
        break;
    case 0xe4:
        task = new Layernorm_f_gpu();
        break;
    default:
        cout << "Unknown prim: " << type << ".\n";
        break;
    }

    task->deserialize(buffer);
    task->cid = cid;

    if (is_comp_prim(task)) {
        comp_base *comp = (comp_base *)task;
        comp->sram_pos_locator = sram_pos_locator;
        comp->sram_pos_locator->cid = cid;
    } else if (is_gpu_prim(task)) {
        gpu_base *gpu = (gpu_base *)task;
        gpu->gpu_pos_locator = gpu_pos_locator;
    }

    return task;
}

void WorkerCoreExecutor::poll_buffer_i() {
    while (true) {
        if (data_sent_i.read()) {
            Msg m = deserialize_msg(channel_i.read());
            if (m.msg_type == DATA || m.msg_type == P_DATA) {
                recv_buffer.push(m);
            } else if (m.msg_type == S_DATA) {
                start_data_buffer.push(m);
            } else if (m.msg_type == REQUEST) {
                cout << "Core " << cid << " recv REQ\n";
                request_buffer.push_back(m);
            } else if (m.msg_type == ACK) {
                cout << "Core " << cid << " recv ACK\n";
                ack_buffer.push(m);
            } else {
                buffer_i.push(channel_i.read());
            }
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

        cout << "[SEND] Core " << cid << ": running send "
             << send_prim_type_to_string(prim->type) << ", destination "
             << prim->des_id << ", tag " << prim->tag_id << endl;

        while (true) {
            send_helper_write = false;
            // 根据 send_helper_write 选择是否向 Router 发送一个包
            // 如果不考虑send recv的并行的话，每次通过手动调整 send_helper_write
            // 如果后面都不需要send，则默认这里拉低
            ev_send_helper.notify(0, SC_NS);

            if (job_done)
                break;

            // SEND_DATA, SEND_ACK, SEND_REQ
            if (prim->type == SEND_DATA) {
                // DTODO
                // [发送方] 正常发送数据，数据从DRAM中获取
                if (channel_avail_i.read()) {
                    prim->data_packet_id++;

                    bool is_end_packet =
                        prim->data_packet_id == prim->max_packet;
                    int length = M_D_DATA;
                    if (is_end_packet) {
                        length = prim->end_length;
                    }

                    int delay = 0;
                    // DAHU
                    sc_bv<SRAM_BITWIDTH> msg_data_tmp;
                    // cout << sc_time_stamp() << ": Before Send Msg \n" ;

#if USE_NB_DRAMSYS == 1
                    NB_DcacheIF *nb_dcache =
                        this->nb_dcache_socket; // 实例化或获取 NB_DcacheIF
                                                // 对象
#else
                    DcacheCore *wc =
                        this->dcache_socket; // 实例化或获取 DcacheCore 对象
#endif
                    sc_event *s_nbdram =
                        this->start_nb_dram_event; // 实例化或获取
                                                   // start_nb_dram_event 对象
                    sc_event *e_nbdram =
                        this->end_nb_dram_event; // 实例化或获取
                                                 // end_nb_dram_event 对象
                    mem_access_unit *mau =
                        this->mem_access_port; // 实例化或获取 mem_access_unit
                                               // 对象
                    high_bw_mem_access_unit *hmau =
                        this->high_bw_mem_access_port; // 实例化或获取
                                                       // high_bw_mem_access_unit
                                                       // 对象
                    sc_bv<SRAM_BITWIDTH> msg_data =
                        msg_data_tmp; // 初始化 msg_data
                                      // int* sram_addr = this->sram_addr;
#if USE_NB_DRAMSYS == 1
                    // 创建类实例
                    TaskCoreContext context(mau, hmau, msg_data, sram_addr,
                                            s_nbdram, e_nbdram, nb_dcache);
#else
                    TaskCoreContext context(wc, mau, hmau, msg_data, sram_addr,
                                            s_nbdram, e_nbdram);
#endif

                    // std::cout << "msg_data (hex) before send: " <<
                    // msg_data.to_string(SC_HEX) << std::endl;
                    delay = prim->task_core(context);
                    // std::cout << "msg_data (hex) after send" <<
                    // context.msg_data.to_string(SC_HEX) << std::endl;

                    send_buffer =
                        Msg(prim->data_packet_id == prim->max_packet,
                            MSG_TYPE::DATA, prim->data_packet_id, prim->des_id,
                            prim->des_offset +
                                M_D_DATA * (prim->data_packet_id - 1),
                            prim->tag_id, length, msg_data);
                    // cout << sc_time_stamp() << ": After Send Msg \n" ;
                    send_helper_write = 3;
                    ev_send_helper.notify(0, SC_NS);

                    // cout << sc_time_stamp() << ": Worker " << cid << ": data
                    // packet " << prim->data_packet_id << " sent.\n";

                    if (prim->data_packet_id == prim->max_packet) {
                        job_done = true;
                        cout << "Core " << cid
                             << " max_packet: " << prim->max_packet << " "
                             << send_buffer.is_end << endl;
                    }
                }
            }

            else if (prim->type == SEND_REQ) {
                // [发送方] 发送一个req包，发送完之后结束此原语，进入 RECV_ACK
                if (channel_avail_i.read()) {
                    // 可以发送数据
                    send_buffer =
                        Msg(MSG_TYPE::REQUEST, prim->des_id, prim->tag_id, cid);

                    send_helper_write = 3;
                    ev_send_helper.notify(0, SC_NS);

                    cout << sc_time_stamp() << ": Worker " << cid << ": REQ to "
                         << prim->des_id << " sent.\n";

                    job_done = true;
                }
            }

            else if (prim->type == SEND_DONE) {
                // [执行核]
                // 在计算图的汇节点执行完毕之后，给host发送一份DONE数据包，标志任务完成
                if (channel_avail_i.read()) {
                    // 可以发送数据
                    send_buffer = Msg(MSG_TYPE::DONE, GRID_SIZE, cid);

                    if (SYSTEM_MODE == SIM_PD) {
                        for (int i = 0; i < decode_done.size(); i++) {
                            send_buffer.data.range(i, i) =
                                sc_bv<1>(decode_done[i]);
                        }
                    }

                    send_helper_write = 3;
                    ev_send_helper.notify(0, SC_NS);

                    cout << sc_time_stamp() << ": Worker " << cid
                         << ": DONE sent.\n";

                    job_done = true;
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

        ev_block.notify(CYCLE, SC_NS);
        wait();
    }
}

void WorkerCoreExecutor::send_para_logic() {
    while (true) {
        while (send_para_queue.size()) {
            prim_base *prim = send_para_queue.front();
            send_para_queue.pop();

            if (typeid(*prim) == typeid(Send_prim)) {
                ((Send_prim *)prim)->data_packet_id = 0;
                cout << "Core " << cid << " going para send\n";
                event_engine->add_event(
                    "Core " + toHexString(cid), "Send_prim", "B",
                    Trace_event_util(
                        "Send_prim" +
                        get_send_type_name(
                            dynamic_cast<Send_prim *>(prim)->type)));
            } else if (typeid(*prim) == typeid(Recv_prim)) {
                cout << "Core " << cid << " going para recv\n";
                event_engine->add_event(
                    "Core " + toHexString(cid), "Recv_prim", "B",
                    Trace_event_util(
                        "Recv_prim" +
                        get_recv_type_name(
                            dynamic_cast<Recv_prim *>(prim)->type)));
            }

            bool job_done = false; // 结束内圈循环的标志

            while (true) {
                // if (cid <= 1) cout << sc_time_stamp() << " core " << cid << "
                // here 1" << endl;
                if (atomic_helper_lock(sc_time_stamp(), 0))
                    ev_send_helper.notify(0, SC_NS);

                if (job_done)
                    break;

                // SEND_DATA, SEND_ACK, SEND_REQ
                if (typeid(*prim) == typeid(Send_prim) &&
                    ((Send_prim *)prim)->type == SEND_DATA) {
                    // [发送方] 正常发送数据，数据从DRAM中获取
                    Send_prim *s_prim = (Send_prim *)prim;
                    // if (cid == 20) cout << sc_time_stamp() << " core " << cid
                    // << " try to get status 1" << endl;
                    // atomic_helper_lock 其实是为了表示上锁
                    if (channel_avail_i.read() &&
                        atomic_helper_lock(sc_time_stamp(), 1)) {
                        ev_send_helper.notify(0, SC_NS);

                        s_prim->data_packet_id++;

                        bool is_end_packet =
                            s_prim->data_packet_id == s_prim->max_packet;
                        int length = M_D_DATA;
                        if (is_end_packet) {
                            length = s_prim->end_length;
                            while (!send_last_packet) {
                                wait(CYCLE, SC_NS);
                            }

                            send_last_packet = false;
                        }

                        int delay = 0;
                        // DAHU
                        sc_bv<SRAM_BITWIDTH> msg_data_tmp;

#if USE_NB_DRAMSYS == 1
                        NB_DcacheIF *nb_dcache =
                            this->nb_dcache_socket; // 实例化或获取
                                                    // NB_DcacheIF 对象
#else
                        DcacheCore *wc =
                            this->dcache_socket; // 实例化或获取 DcacheCore 对象
#endif
                        sc_event *s_nbdram =
                            this->start_nb_dram_event; // 实例化或获取
                                                       // start_nb_dram_event
                                                       // 对象
                        sc_event *e_nbdram =
                            this->end_nb_dram_event; // 实例化或获取
                                                     // end_nb_dram_event 对象
                        mem_access_unit *mau =
                            this->mem_access_port; // 实例化或获取
                                                   // mem_access_unit 对象
                        high_bw_mem_access_unit *hmau =
                            this->high_bw_mem_access_port; // 实例化或获取
                                                           // high_bw_mem_access_unit
                                                           // 对象
                        sc_bv<SRAM_BITWIDTH> msg_data =
                            msg_data_tmp; // 初始化 msg_data
                                          // int* sram_addr = this->sram_addr;
#if USE_NB_DRAMSYS == 1
                        // 创建类实例
                        TaskCoreContext context(mau, hmau, msg_data, sram_addr,
                                                s_nbdram, e_nbdram, nb_dcache);
#else
                        TaskCoreContext context(wc, mau, hmau, msg_data,
                                                sram_addr, s_nbdram, e_nbdram);
#endif

                        delay = prim->task_core(context);
                        // std::cout << "msg_data (hex) after send" <<
                        // context.msg_data.to_string(SC_HEX) << std::endl;

                        send_buffer =
                            Msg(s_prim->data_packet_id == s_prim->max_packet,
                                MSG_TYPE::DATA, s_prim->data_packet_id,
                                s_prim->des_id,
                                s_prim->des_offset +
                                    M_D_DATA * (s_prim->data_packet_id - 1),
                                s_prim->tag_id, length, msg_data);
                        // cout << sc_time_stamp() << ": After Send Msg \n" ;

                        atomic_helper_lock(sc_time_stamp(), 2);
                        ev_send_helper.notify(0, SC_NS);

                        // if (cid == 20) cout << sc_time_stamp() << ": Worker "
                        // << cid << ": data packet " << s_prim->data_packet_id
                        // << " sent.\n";

                        if (s_prim->data_packet_id == s_prim->max_packet) {
                            job_done = true;
                            cout << "Core " << cid
                                 << " max_packet: " << s_prim->max_packet << " "
                                 << send_buffer.is_end << endl;
                        }
                    }
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

                        if (m.msg_type == ACK) {
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
                    "Core " + toHexString(cid), "Send_prim", "E",
                    Trace_event_util(
                        "Send_prim" +
                        get_send_type_name(
                            dynamic_cast<Send_prim *>(prim)->type)));
            } else {
                event_engine->add_event(
                    "Core " + toHexString(cid), "Recv_prim", "E",
                    Trace_event_util(
                        "Recv_prim" +
                        get_recv_type_name(
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
        queue<int>
            ack_queue; // 需要发送ack的core id队列，需要全部发完才能结束此原语
        int end_cnt =
            0; // 已经接收到的end包数量，需要等于recv原语中的对应要求才能结束此原语
        bool wait_send =
            false; // 在RECV_CONFIG中，接收到最后一个config包之后，需要等待发送ack
        bool job_done = false;

        cout << "[RECV] Core " << cid << ": running recv "
             << recv_prim_type_to_string(prim->type) << ", recv_cnt "
             << prim->recv_cnt << ", recv_tag " << prim->tag_id << endl;

        while (true) {
            if (atomic_helper_lock(sc_time_stamp(), 0))
                ev_send_helper.notify(0, SC_NS);

            if (job_done) {
                break;
            }

            if (prim->type == RECV_ACK) {
                // [发送方] 接收来自接收方的ack包，收到之后结束此原语，进入
                // SEND_DATA 或 SEND_SRAM
                if (ack_buffer.size()) {
                    // 接收到数据包
                    Msg m = ack_buffer.front();
                    ack_buffer.pop();

                    if (m.msg_type == ACK) {
                        job_done = true;

                        cout << sc_time_stamp() << ": Worker " << cid
                             << ": received ACK packet.\n";
                    }
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
                bool has_msg = false;

                if (recv_buffer.size()) {
                    temp = recv_buffer.front();

                    if (prim->tag_id != cid && temp.tag_id != prim->tag_id) {
                        cout << "[WARN] Core " << cid
                             << " gets incompatible tag id: prim tag "
                             << prim->tag_id << " with buffer top msg tag "
                             << temp.tag_id << endl;
                        sc_stop();
                    }

                    recv_buffer.pop();
                    has_msg = true;
                }

                if (has_msg) {
                    // 复制到SRAM中

                    // 如果是end包，则将recv_index归零，表示开始接收下一个core传来的数据（如果有的话）
                    if (temp.is_end) {

                        while (!atomic_helper_lock(sc_time_stamp(), 3)) {
                            wait(CYCLE, SC_NS);
                        }

                        // 这里是针对host data 和 start 包
                        cout << sc_time_stamp() << ": Worker " << cid
                             << ": received all prepare data.\n";

                        // 向host发送一个ack包
                        send_buffer =
                            Msg(MSG_TYPE::ACK, GRID_SIZE, prim->tag_id, cid);

                        ev_send_helper.notify(0, SC_NS);

                        job_done = true;

                        cout << sc_time_stamp() << ": Worker " << cid
                             << " receive end packet: end_cnt " << end_cnt
                             << ", recv_cnt " << recv_cnt << ", max_recv "
                             << max_recv << endl;
                    }
                }
            }

            else if (prim->type == RECV_DATA) {
                // [接收方]
                // 接收消息，但是途中如果有新的REQ包进入，需要判断是否要回发ACK包

                // 如果recv_cnt等于0,说明无需接收包裹，直接开始comp即可
                if (prim->recv_cnt == 0) {
                    job_done = true;
                }

                else {
                    // 检查request_buffer，若有相同id的request，取出并发送ACK
                    if (request_buffer.size()) {
                        for (auto i = request_buffer.begin();
                             i != request_buffer.end();) {
                            if ((*i).tag_id == prim->tag_id) {
                                ack_queue.push((*i).source);
                                i = request_buffer.erase(i);
                            } else {
                                i++;
                            }
                        }
                    }

                    // 发送ack包
                    if (ack_queue.size() && channel_avail_i.read() &&
                        atomic_helper_lock(sc_time_stamp(), 3)) {
                        int des = ack_queue.front();
                        ack_queue.pop();

                        send_buffer =
                            Msg(MSG_TYPE::ACK, des, prim->tag_id, cid);

                        ev_send_helper.notify(0, SC_NS);
                        cout << "Core " << cid << " sent ACK to " << des
                             << endl;
                    }

                    // 按照prim的tag进行判断。如果tag等同于cid，则优先查看start
                    // data buffer，再查看recv buffer
                    // 如果tag不等同于id，则不允许查看start data buffer

                    Msg temp;
                    // 表示 当前周期该核有需要处理的msg 的recv包
                    bool has_msg = false;
                    if (prim->tag_id == cid && start_data_buffer.size()) {
                        temp = start_data_buffer.front();
                        start_data_buffer.pop();
                        has_msg = true;
                    }

                    else if (recv_buffer.size()) {
                        temp = recv_buffer.front();

                        if (prim->tag_id != cid &&
                            temp.tag_id != prim->tag_id) {
                            cout << "[WARN] Core " << cid
                                 << " gets incompatible tag id: prim tag "
                                 << prim->tag_id << " with buffer top msg tag "
                                 << temp.tag_id << endl;
                            sc_stop();
                        }

                        recv_buffer.pop();
                        has_msg = true;
                    }

                    sc_bv<SRAM_BITWIDTH> msg_data_tmp;

#if USE_NB_DRAMSYS == 1
                    TaskCoreContext context(
                        mem_access_port, high_bw_mem_access_port, msg_data_tmp,
                        sram_addr, start_nb_dram_event, end_nb_dram_event,
                        nb_dcache_socket);
#else
                    TaskCoreContext context(
                        dcache_socket, mem_access_port, high_bw_mem_access_port,
                        msg_data_tmp, sram_addr, start_nb_dram_event,
                        end_nb_dram_event);
#endif

                    if (has_msg) {
                        recv_cnt++;

                        if (temp.msg_type != MSG_TYPE::P_DATA) {
                            if (temp.seq_id == 1 &&
                                SYSTEM_MODE == SIM_DATAFLOW) {
                                // 在pos locator中添加一个kv，label是input_label
                                // 对于每一个核的第一算子的input来自与send
                                // 核的输出，并且已经会由router保存在sram上
                                AddrPosKey inp_key = AddrPosKey(*sram_addr, 0);
                                char format_label[100];
                                sprintf(format_label, "%s#%d", INPUT_LABEL,
                                        loop_cnt);
                                string input_label = format_label;

                                u_int64_t temp;
                                sram_pos_locator->addPair(input_label, inp_key);
                            }

                            int delay = 0;
                            delay = prim->task_core(context);
                        }

                        // 如果是end包，则将recv_index归零，表示开始接收下一个core传来的数据（如果有的话）
                        if (temp.is_end) {
                            end_cnt++;

                            max_recv += temp.seq_id;

                            cout << sc_time_stamp() << ": Worker " << cid
                                 << " receive end packet: end_cnt " << end_cnt
                                 << ", recv_cnt " << recv_cnt << ", max_recv "
                                 << max_recv << endl;


                            // prim->recv_cnt 记录的是 receive 原语 需要接受的
                            // end 包的数量 多发一的实现 max_recv 表示当前 DATA
                            // 包 发送了多少个 package 数量
                            if (end_cnt == prim->recv_cnt &&
                                recv_cnt >= max_recv) {
                                // 收到了所有的数据，可以结束此原语，进入comp原语
                                // 更新pos_locator中的kv的size
                                if (SYSTEM_MODE == SIM_DATAFLOW) {
                                    AddrPosKey inp_key;
                                    char format_label[100];
                                    sprintf(format_label, "%s#%d", INPUT_LABEL,
                                            loop_cnt);
                                    string input_label = format_label;

                                    sram_pos_locator->findPair(input_label,
                                                               inp_key);
                                    inp_key.size = max_recv * M_D_DATA;

                                    u_int64_t temp;
                                    sram_pos_locator->addPair(input_label,
                                                              inp_key);
                                }

                                job_done = true;
                            }
                        }
                    }
                }
            }

            else if (prim->type == RECV_CONF) {
                // [所有人]
                // 在模拟开始时接收配置，接收完毕之后发送一个ACK包给host，此原语需要对prim_queue进行压入，此原语执行完毕之后，进入RECV_DATA
                if (wait_send && atomic_helper_lock(sc_time_stamp(), 3)) {
                    // 正在等待向host发送ack包
                    send_buffer =
                        Msg(MSG_TYPE::ACK, GRID_SIZE, prim->tag_id, cid);

                    ev_send_helper.notify(0, SC_NS);

                    job_done = true;
                } else if (buffer_i.size()) {
                    Msg m = deserialize_msg(buffer_i.front());
                    buffer_i.pop();
                    prim_queue.emplace_back(parse_prim(m.data));

                    cout << sc_time_stamp() << ": Worker " << cid
                         << ": recv config " << m.seq_id << endl;

                    // 检查是否为end config包，如果是，需要向host发送ack包
                    if (m.is_end) {
                        this->prim_refill = m.refill;
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

            // 等待下一个时钟周期
            wait(CYCLE, SC_NS);
        }

        // 不能清空recv_buffer
        // while (recv_buffer.size()) recv_buffer.pop();

        ev_block.notify(CYCLE, SC_NS);
        wait();
    }
}

void WorkerCoreExecutor::task_logic() {
    while (true) {
        prim_base *p = prim_queue.front();

        int delay = 0;
        sc_bv<SRAM_BITWIDTH> msg_data_tmp;

#if USE_NB_DRAMSYS == 1
        NB_DcacheIF *nb_dcache =
            this->nb_dcache_socket; // 实例化或获取 NB_DcacheIF 对象
#else

        DcacheCore *wc = this->dcache_socket; // 实例化或获取 DcacheCore 对象
#endif
        sc_event *end_nb_gpu_dram_event =
            this->end_nb_gpu_dram_event; // 实例化或获取 end_nb_gpu_dram_event
                                         // 对象
        sc_event *start_nb_gpu_dram_event =
            this->start_nb_gpu_dram_event; // 实例化或获取
                                           // start_nb_gpu_dram_event 对象
        sc_event *s_nbdram =
            this->start_nb_dram_event; // 实例化或获取 start_nb_dram_event 对象
        sc_event *e_nbdram =
            this->end_nb_dram_event; // 实例化或获取 end_nb_dram_event 对象
        mem_access_unit *mau =
            this->mem_access_port; // 实例化或获取 mem_access_unit 对象
        high_bw_mem_access_unit *hmau =
            this->high_bw_mem_access_port; // 实例化或获取
                                           // high_bw_mem_access_unit 对象
        sc_bv<SRAM_BITWIDTH> msg_data =
            msg_data_tmp; // 初始化 msg_data
                          // int* sram_addr = &(p->sram_addr);
#if USE_L1L2_CACHE == 1
        // 创建类实例
        TaskCoreContext context(mau, hmau, msg_data, sram_addr, s_nbdram,
                                e_nbdram, nb_dcache, loop_cnt,
                                start_nb_gpu_dram_event, end_nb_gpu_dram_event);
#else
        TaskCoreContext context(wc, mau, hmau, msg_data, sram_addr, s_nbdram,
                                e_nbdram, loop_cnt);
#endif

        if (!p->use_hw || typeid(*p) != typeid(Matmul_f)) {
            if (typeid(*p) == typeid(Set_addr)) {
                // set sram 修改标签
                // 已经在 parse_prim 中设置了 datapass_label，这里do nothing
            } else if (is_comp_prim(p)) {
                // comp原语 读取标签
                comp_base *comp = (comp_base *)p;
                comp->datapass_label = *next_datapass_label;
            } else if (is_gpu_prim(p)) {
                cout << "socket " << cid << endl;
                context.gpunb_dcache_if = gpunb_dcache_if;
                context.cid = &cid;
                context.event_engine = event_engine;
                cout << "socket2 " << cid << endl;

                gpu_base *gpu = (gpu_base *)p;
                gpu->datapass_label = *next_datapass_label;
            } else if (typeid(*p) == typeid(Clear_sram)) {
                ((Clear_sram *)p)->sram_pos_locator = sram_pos_locator;
                ((Clear_sram *)p)->loop_cnt = &loop_cnt;
            }

            delay = p->task_core(context);
        } else if (typeid(*p) != typeid(Matmul_f)) {
            delay = p->task();
        }

        wait(sc_time(delay, SC_NS));

        sc_time start_time = sc_time_stamp();

        // CTODO: 现在仅适用于matmul
        if (typeid(*p) == typeid(Matmul_f) && p->use_hw) {
            HardwareTaskConfig *config = ((Matmul_f *)p)->generate_hw_config();
            systolic_config->args = config->args;
            systolic_config->data = config->data;
            systolic_config->hardware = config->hardware;

            // test only!! fill in data
            test_fill_data(systolic_config->args[0], 2,
                           systolic_config->data[0],
                           systolic_config->args[1] * systolic_config->args[2],
                           systolic_config->data[2],
                           systolic_config->args[2] * systolic_config->args[3]);

            for (int i = 0;
                 i < systolic_config->args[1] * systolic_config->args[3]; i++) {
                if (i % systolic_config->args[3] ==
                    systolic_config->args[3] - 1)
                    cout << endl;
            }

            cout << endl;

            // 进行工作
            ev_systolic.notify(0, SC_NS);

            // 等待工作完成
            while (systolic_done_i.read() == false) {
                wait(CYCLE, SC_NS);
            }

            // 计算overlap time
            sc_time end_time = sc_time_stamp();
            if ((end_time - start_time) < sc_time(delay, SC_NS)) {
                wait(sc_time(delay, SC_NS) - (end_time - start_time));
            }

            cout << systolic_config->args[1] << " " << systolic_config->args[3]
                 << endl;

            for (int i = 0;
                 i < systolic_config->args[1] * systolic_config->args[3]; i++) {
                cout << systolic_config->data[1][i] - 5 << " ";
                if (i % systolic_config->args[3] ==
                    systolic_config->args[3] - 1)
                    cout << endl;
            }

            cout << endl;
        }

        ev_block.notify(CYCLE, SC_NS);
        wait();
    }
}

/*
 在workercore executor中添加了一把锁，用于lock住write helper，
 因为同时运行send和recv原语会在同一个时钟周期内access write helper函数
 0是在 send
 的时，起到修改present_time的作用，并且如果当前没有锁的情况下（没有其他模块需要使用），默认拉低(当前模块也不需要使用)
 1表示拿到锁但是等待send原语的sram读，2表示send原语的sram读完可以发送
 3表示不需要读取sram的其他信号的发送，比如ack 和 done信号
 2和3都是可以发送 0和1不行
*/
bool WorkerCoreExecutor::atomic_helper_lock(sc_time try_time, int status) {
    // if (cid == 20 && status >= 1) cout << sc_time_stamp() << "core " << cid
    // << " try - pres & status & helper signal " << try_time << "-" <<
    // present_time << " & " << status << " & " << send_helper_write << endl; if
    // (cid == 20 && status >= 1) {
    //     Msg m = send_buffer;
    //     cout << "seq_id: " << m.seq_id << endl;
    // }

    bool res;

    if (try_time < present_time)
        res = false;

    if (try_time == present_time) {
        if (status == 0)
            return false;
        if (status == 1) { // send prepare
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
        present_time = try_time;
        // status 1 不会进入到这里，因为status 1 之前肯定会有 status 0 修改了
        // present_time
        if (status == 2) { // send ready
            if (send_helper_write == 1) {
                send_helper_write = 2;
                res = true;
            } else {
                res = false;
            }
        } else {
            // 这里应该只会进 status 0, 1 和 3 (除了返回给host ack
            // 因为while循环) 都会在0后处理，且不会有延迟，所以pre=try_time
            if (send_helper_write == 1)
                res = false;
            else {
                send_helper_write = status;
                res = true;
            }
        }
    }

    // if (cid <= 1) cout << "res: " << res << " send_helper_write: " <<
    // send_helper_write << endl;
    return res;
}

// 因为 send 模块和 receive 模块都需要触发
// 只要data_sent_o = true 就能发送
void WorkerCoreExecutor::send_helper() {
    while (true) {
        // if (cid <= 1) cout << sc_time_stamp() << ": core " << cid << "
        // send_helper switch " << send_helper_write << endl; 负责发送一个包

        if (send_helper_write >= 2) {
            channel_o.write(serialize_msg(send_buffer));
            data_sent_o.write(true);
        }

        else {
            data_sent_o.write(false);
        }

        wait();
    }
}

void WorkerCoreExecutor::call_systolic_array() {
    while (true) {
        if (systolic_config) {
            systolic_start_o.write(true);
            wait(CYCLE, SC_NS);
            systolic_start_o.write(false);
        }

        wait();
    }
}

WorkerCoreExecutor::~WorkerCoreExecutor() {
    delete sram_addr;
    delete sram_pos_locator;
    delete next_datapass_label;
#if USE_NB_DRAMSYS == 1
    delete nb_dcache_socket;
#else
    delete dcache_socket;
#endif
    delete mem_access_port;
    delete high_bw_mem_access_port;
    delete start_nb_dram_event;
    delete end_nb_dram_event;
}