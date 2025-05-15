#include <atomic>
#include <vector>

#include "monitor/config_helper_core.h"
#include "monitor/config_helper_gpu.h"
#include "monitor/config_helper_pd.h"
#include "monitor/mem_interface.h"
#include "prims/comp_prims.h"
#include "prims/norm_prims.h"
#include "utils/file_utils.h"
#include "utils/msg_utils.h"
#include "utils/prim_utils.h"

MemInterface::MemInterface(const sc_module_name &n, Event_engine *event_engine,
                           const char *config_name, const char *font_ttf)
    : event_engine(event_engine) {

    cout << "SIMULATION MODE: " << SYSTEM_MODE << endl;

    if (SYSTEM_MODE == SIM_DATAFLOW)
        config_helper = new config_helper_core(config_name, font_ttf);
    else if (SYSTEM_MODE == SIM_GPU)
        config_helper = new config_helper_gpu(config_name, font_ttf);
    else if (SYSTEM_MODE == SIM_PD)
        config_helper =
            new config_helper_pd(config_name, font_ttf, &ev_req_handler);

    //[yicheng] 先写个简单的，之后改
    for (int i = 0; i < config_helper->coreconfigs.size(); i++) {
        if (config_helper->coreconfigs[i].send_global_mem != -1) {
            if (has_global_mem.size() >= 1) {
                assert(false && "Only one core can send global memory");
            } else {
                has_global_mem.push_back(
                    config_helper->coreconfigs[i]
                        .id); // 记录其id，之后将此id与global memory接起来
            }
        }
    }

    init();
}

MemInterface::MemInterface(const sc_module_name &n, Event_engine *event_engine,
                           config_helper_base *input_config)
    : event_engine(event_engine), config_helper(input_config) {
    init();
}

void MemInterface::init() {
    host_data_sent_i = new sc_in<bool>[GRID_X];
    host_data_sent_o = new sc_out<bool>[GRID_X];

    host_channel_i = new sc_in<sc_bv<256>>[GRID_X];
    host_channel_o = new sc_out<sc_bv<256>>[GRID_X];

    host_channel_avail_i = new sc_in<bool>[GRID_X];

    write_buffer = new queue<Msg>[GRID_X];

    phase = PRO_CONF;

    g_recv_ack_cnt = 0;
    g_recv_done_cnt = 0;

    SC_THREAD(write_helper);
    sensitive << ev_write;
    dont_initialize();

    SC_THREAD(distribute_config);
    sensitive << start_i.pos() << ev_dis_config;
    dont_initialize();

    SC_THREAD(distribute_data)
    sensitive << ev_dis_data;
    dont_initialize();

    SC_THREAD(distribute_start_data);
    sensitive << ev_dis_start;
    dont_initialize();

    SC_THREAD(catch_host_data_sent_i);
    for (int i = 0; i < GRID_X; i++) {
        sensitive << host_data_sent_i[i].pos();
    }
    dont_initialize();

    SC_THREAD(recv_helper);
    sensitive << ev_recv_helper;
    dont_initialize();

    SC_THREAD(recv_ack);
    sensitive << ev_recv_ack;
    dont_initialize();

    SC_THREAD(recv_done);
    sensitive << ev_recv_done;
    dont_initialize();

    SC_THREAD(req_handler);
    sensitive << ev_req_handler;
    dont_initialize();

    SC_THREAD(switch_phase);
    sensitive << ev_switch_phase;
    dont_initialize();

    flow_id = 0;
};

MemInterface::~MemInterface() {
    delete[] host_data_sent_i;
    delete[] host_data_sent_o;
    delete[] host_channel_avail_i;
    delete[] host_channel_i;
    delete[] host_channel_o;

    delete[] write_buffer;

    delete config_helper;
}

void MemInterface::end_of_simulation() {

    // 美观的打印输出
    print_bar(40);
    std::cout << "| " << std::left << std::setw(20) << "CoreConfig"
              << "| " << std::right << std::setw(15) << "Util.   (Byte) |\n";
    print_bar(40);
    for (int i = 0; i < config_helper->coreconfigs.size(); i++) {
        CoreConfig *c = &config_helper->coreconfigs[i];
        int total_utilization = 0;
        for (auto work : c->worklist) {
            for (auto prim : work.prims_in_loop) {
                if (prim) { // 确保指针非空
                    total_utilization += prim->sram_utilization(prim->datatype);
                }
            }
        }
        // 打印当前CoreConfig的总SRAM利用率
        print_row("CoreConfig " + std::to_string(i), total_utilization);
    }
    print_bar(40);
}

void MemInterface::end_of_elaboration() {
    // set signals
    for (int i = 0; i < GRID_X; i++) {
        host_data_sent_o[i].write(false);
    }
}

void MemInterface::distribute_config() {
    while (true) {
        event_engine->add_event(this->name(), "Sending Config", "B",
                                Trace_event_util());

        if (SYSTEM_MODE == SIM_PD)
            ((config_helper_pd *)config_helper)->iter_start();

        config_helper->fill_queue_config(write_buffer);

        // 检查write_buffer是否为空，如果为空则直接跳过发送阶段（PD模式）
        bool writable = false;
        for (int i = 0; i < GRID_X; i++) {
            if (write_buffer[i].size()) {
                writable = true;
                break;
            }
        }
        cout << "writable " << writable << endl;

        // 发送开始书写信号
        if (writable) {
            ev_write.notify(CYCLE, SC_NS);
            wait(write_done.posedge_event());
            cout << sc_time_stamp() << ": Mem Interface: config sent done.\n";
            event_engine->add_event(this->name(), "Sending Config", "E",
                                    Trace_event_util());

            // 使用唯一的flow ID替换名称
            flow_id++;
            std::string flow_name = "flow_" + std::to_string(flow_id);
            event_engine->add_event(this->name(), "Sending Config", "s",
                                    Trace_event_util(flow_name),
                                    sc_time(0, SC_NS), 100);
        }

        wait();
    }
}

void MemInterface::distribute_data() {
    while (true) {
        cout << "2\n";
        event_engine->add_event(this->name(), "Send Weight Data", "B",
                                Trace_event_util());

        config_helper->fill_queue_data(write_buffer);

        // 发送开始书写信号
        ev_write.notify(CYCLE, SC_NS);
        wait(write_done.posedge_event());
        event_engine->add_event(this->name(), "Send Weight Data", "E",
                                Trace_event_util());
        // 使用唯一的flow ID替换名称
        flow_id++;
        std::string flow_name = "flow_" + std::to_string(flow_id);
        event_engine->add_event(this->name(), "Send Weight Data", "s",
                                Trace_event_util(flow_name), sc_time(0, SC_NS),
                                100);

        cout << "Mem Interface: data sent done.\n";
        wait();
    }
}

void MemInterface::distribute_start_data() {
    while (true) {
        event_engine->add_event(this->name(), "Send Input Data", "B",
                                Trace_event_util());

        config_helper->fill_queue_start(write_buffer);

        ev_write.notify(CYCLE, SC_NS);
        wait(write_done.posedge_event());
        event_engine->add_event(this->name(), "Send Input Data", "E",
                                Trace_event_util());

        cout << "Mem Interface: start data sent done.\n";
        wait();
    }
}

void MemInterface::recv_helper() {
    while (true) {
        for (int i = 0; i < GRID_X; i++) {
            if (host_data_sent_i[i].read()) {
                sc_bv<256> d = host_channel_i[i].read();
                Msg m = deserialize_msg(d);

                if (m.msg_type == ACK) {
                    cout << "ACK from " << m.source << endl;
                    g_temp_ack_msg.push_back(m);
                    ev_recv_ack.notify(0, SC_NS);
                }

                else if (m.msg_type == DONE) {
                    cout << "DONE from " << m.source << endl;
                    g_temp_done_msg.push_back(m);
                    ev_recv_done.notify(0, SC_NS);
                }

                ev_recv_helper.notify(CYCLE, SC_NS);
            }
        }

        wait();
    }
}

void MemInterface::recv_ack() {
    while (true) {
        event_engine->add_event(this->name(), "Waiting Recv Ack", "B",
                                Trace_event_util());

        for (auto m : g_temp_ack_msg) {
            int cid = m.source;
            cout << sc_time_stamp()
                 << ": Mem Interface: received ack packet from " << cid
                 << ". total " << g_recv_ack_cnt + 1 << "/"
                 << config_helper->coreconfigs.size() << ".\n";

            g_recv_ack_cnt++;
        }
        g_temp_ack_msg.clear();

        event_engine->add_event(this->name(), "Waiting Recv Ack", "E",
                                Trace_event_util());

        switch (SYSTEM_MODE) {
        case SIM_DATAFLOW:
        case SIM_GPU:
            if (g_recv_ack_cnt >= config_helper->coreconfigs.size()) {
                ev_switch_phase.notify(CYCLE, SC_NS);

                // 使用唯一的flow ID替换名称
                std::string flow_name = "flow_" + std::to_string(flow_id);
                event_engine->add_event(this->name(), "Waiting Recv Ack", "f",
                                        Trace_event_util(flow_name),
                                        sc_time(0, SC_NS), 100, "e");
                cout << "Mem Interface: received all ack packets.\n";

                g_recv_ack_cnt = 0;
            }
            break;
        case SIM_PD:
            if (g_recv_ack_cnt >=
                ((config_helper_pd *)config_helper)->coreStatus.size()) {
                g_recv_ack_cnt = 0;
                ev_dis_start.notify(CYCLE, SC_NS);
            }
            break;
        }

        wait();
    }
}

void MemInterface::recv_done() {
    while (true) {
        event_engine->add_event(this->name(), "Waiting Core busy", "B",
                                Trace_event_util());

        for (auto m : g_temp_done_msg) {
            int cid = m.source;
            cout << sc_time_stamp()
                 << ": Mem Interface: received done packet from " << cid
                 << ", total " << g_recv_done_cnt + 1 << ".\n";

            g_recv_done_cnt++;
            g_done_msg.push_back(m);
        }
        g_temp_done_msg.clear();

        event_engine->add_event(this->name(), "Waiting Core busy", "E",
                                Trace_event_util());

        // Declare variables outside the switch
        config_helper_gpu *helper = nullptr;
        prim_base *prim = nullptr;
        int core_inv = 0;

        switch (SYSTEM_MODE) {
        case SIM_DATAFLOW:
            if (g_recv_done_cnt >=
                config_helper->end_cores * config_helper->pipeline) {
                if (!config_helper->sequential ||
                    config_helper->seq_index ==
                        config_helper->source_info.size()) {
                    cout << "Mem Interface: all work done, end_core: "
                         << config_helper->end_cores
                         << ", recv_cnt: " << g_recv_done_cnt << endl;

                    g_recv_done_cnt = 0;
                    sc_stop();
                } else {
                    cout << "Mem Interface: one work done. "
                         << config_helper->seq_index << " of "
                         << config_helper->source_info.size() << ".\n";

                    g_recv_done_cnt = 0;
                    ev_switch_phase.notify(CYCLE, SC_NS);
                }
            }
            break;
        case SIM_GPU:
            helper = (config_helper_gpu *)config_helper;
            prim = helper->streams[0].prims[helper->gpu_index - 1];
            core_inv = ((gpu_base *)prim)->req_sm;

            if (core_inv >= GRID_SIZE)
                core_inv = GRID_SIZE;
            if (g_recv_done_cnt >= core_inv) {
                cout << "Mem Interface: one work done. " << helper->gpu_index
                     << " of " << helper->streams[0].prims.size() << endl;

                if (helper->gpu_index == helper->streams[0].prims.size()) {
                    cout << "Mem Interface: all work done.\n";
                    sc_stop();
                } else {
                    g_recv_done_cnt = 0;
                    ev_switch_phase.notify(CYCLE, SC_NS);
                }
            }
            break;
        case SIM_PD:
            if (g_recv_done_cnt >=
                ((config_helper_pd *)config_helper)->coreStatus.size()) {
                ((config_helper_pd *)config_helper)->iter_done(g_done_msg);

                g_done_msg.clear();
                g_recv_done_cnt = 0;
                ev_dis_config.notify(CYCLE, SC_NS);
            }
            break;
        }

        wait();
    }
}

// 从write_buffer里面取出数据，发送到host
void MemInterface::write_helper() {
    while (true) {
        write_done.write(false);
        cout << "Mem Interface: start to write\n";

        // 立刻将buffer中的内容复制到本地，并清空全局buffer
        queue<Msg> temp_buffer[GRID_X];
        for (int i = 0; i < GRID_X; i++) {
            while (write_buffer[i].size()) {
                Msg t = write_buffer[i].front();
                temp_buffer[i].push(t);
                write_buffer[i].pop();
            }
        }

        while (true) {
            bool stop_flag = true;
            for (int i = 0; i < GRID_X; i++) {
                host_data_sent_o[i].write(false);
                if (!temp_buffer[i].size()) {
                    continue;
                }

                stop_flag = false;
                if (host_channel_avail_i[i].read() == false)
                    continue;

                // send data
                Msg t = temp_buffer[i].front();
                temp_buffer[i].pop();
                host_channel_o[i].write(serialize_msg(t));
                host_data_sent_o[i].write(true);
                // cout << "SEND DATA to: " << t.des << ",seq: " << t.seq_id
                //      << endl;
            }

            if (stop_flag)
                break;

            wait(CYCLE, SC_NS);
        }

        cout << "Mem Interface: write done\n";
        write_done.write(true);

        wait();
    }
}

void MemInterface::req_handler() {
    while (true) {
        if (SYSTEM_MODE != SIM_PD) {
            cout << "[ERROR] Request handler can only be used in PD mode.\n";
            sc_stop();
        }
        cout << "sssssssssssssssssss\n";
        config_helper_pd *pd = (config_helper_pd *)config_helper;
        for (int i = 0; i < pd->arrival_time.size(); i++) {
            cout << pd->arrival_time[i] << " ss\n";
            sc_time next_time(pd->arrival_time[i], SC_NS);
            if (next_time < sc_time_stamp()) {
                cout << "[ERROR] Be sure all reqs come in sequentially.\n";
                sc_stop();
            }

            wait(next_time - sc_time_stamp());
            ev_dis_config.notify(0, SC_NS);
        }

        wait();
    }
}

void MemInterface::catch_host_data_sent_i() {
    while (true) {
        ev_recv_helper.notify(CYCLE, SC_NS);

        wait();
    }
}

void MemInterface::switch_phase() {
    while (true) {
        if (phase == PRO_CONF) {
            phase = PRO_DATA;
            cout << sc_time_stamp() << ": Mem Interface: switch to P_DATA.\n";
            ev_dis_data.notify(0, SC_NS);
        } else if (phase == PRO_DATA) {
            phase = PRO_START;
            cout << sc_time_stamp() << ": Mem Interface: switch to P_START.\n";
            ev_dis_start.notify(0, SC_NS);
        } else if (phase == PRO_START) {
            cout << sc_time_stamp() << ": Mem Interface: continue P_START.\n";
            ev_dis_start.notify(0, SC_NS);
        }
        wait();
    }
}


void MemInterface::clear_write_buffer() {
    for (int i = 0; i < GRID_X; i++) {
        while (!write_buffer[i].empty())
            write_buffer[i].pop();
    }
}