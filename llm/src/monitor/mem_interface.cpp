#include <atomic>
#include <vector>

#include "monitor/config_helper_core.h"
#include "monitor/config_helper_gpu.h"
#include "monitor/config_helper_gpu_pd.h"
#include "monitor/config_helper_pd.h"
#include "monitor/config_helper_pds.h"
#include "monitor/mem_interface.h"
#include "prims/comp_prims.h"
#include "prims/norm_prims.h"
#include "utils/print_utils.h"
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
    else if (SYSTEM_MODE == SIM_PDS)
        config_helper =
            new config_helper_pds(config_name, font_ttf, &ev_req_handler);
    else if (SYSTEM_MODE == SIM_GPU_PD)
        config_helper =
            new config_helper_gpu_pd(config_name, font_ttf, &ev_req_handler);

    init();
}

MemInterface::MemInterface(const sc_module_name &n, Event_engine *event_engine,
                           config_helper_base *input_config)
    : event_engine(event_engine), config_helper(input_config) {
    init();
}

void MemInterface::init() {

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
    host_data_sent_i = new sc_in<bool>[GRID_X];
    host_data_sent_o = new sc_out<bool>[GRID_X];

    host_channel_i = new sc_in<sc_bv<256>>[GRID_X];
    host_channel_o = new sc_out<sc_bv<256>>[GRID_X];

    host_channel_avail_i = new sc_in<bool>[GRID_X];

    write_buffer = new queue<Msg>[GRID_X];

    phase = PRO_CONF;

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
    cout << "Mem Interface delete\n";
    delete[] host_data_sent_i;
    delete[] host_data_sent_o;
    delete[] host_channel_avail_i;
    delete[] host_channel_i;
    delete[] host_channel_o;

    delete[] write_buffer;

    // delete config_helper;
}

void MemInterface::end_of_simulation() {

    // 美观的打印输出
    PrintBar(40);
    std::cout << "| " << std::left << std::setw(20) << "CoreConfig"
              << "| " << std::right << std::setw(15) << "Util.   (Byte) |\n";
    PrintBar(40);
    for (int i = 0; i < config_helper->coreconfigs.size(); i++) {
        CoreConfig *c = &config_helper->coreconfigs[i];
        int total_utilization = 0;
        for (auto work : c->worklist) {
            for (auto prim : work.prims_in_loop) {
                if (prim && prim->prim_type & PRIM_TYPE::NPU_PRIM) { // 确保指针非空
                    total_utilization +=
                        ((NpuBase *)prim)->sramUtilization(prim->datatype, c->id);
                }
            }
        }
        // 打印当前CoreConfig的总SRAM利用率
        PrintRow("CoreConfig " + std::to_string(i), total_utilization);
    }
    PrintBar(40);
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
        else if (SYSTEM_MODE == SIM_PDS) {
            config_helper_pds *helper = (config_helper_pds *)config_helper;
            if (helper->wait_schedule_p)
                helper->iter_start(JOB_PREFILL);
            if (helper->wait_schedule_d)
                helper->iter_start(JOB_DECODE);
        } else if (SYSTEM_MODE == SIM_GPU_PD) 
            ((config_helper_gpu_pd *)config_helper)->iter_start();

        config_helper->fill_queue_config(write_buffer);

        // 检查write_buffer是否为空，如果为空则直接跳过发送阶段（PD模式）
        bool writable = false;
        for (int i = 0; i < GRID_X; i++) {
            if (write_buffer[i].size()) {
                writable = true;
                break;
            }
        }

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
                Msg m = DeserializeMsg(d);

                if (m.msg_type_ == ACK) {
                    cout << "ACK from " << m.source_ << endl;
                    config_helper->g_temp_ack_msg.push_back(m);
                    ev_recv_ack.notify(0, SC_NS);
                }

                else if (m.msg_type_ == DONE) {
                    cout << "DONE from " << m.source_ << endl;
                    config_helper->g_temp_done_msg.push_back(m);
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
        sc_event *notify_event = nullptr;
        switch (SYSTEM_MODE) {
        case SIM_DATAFLOW:
        case SIM_GPU:
            notify_event = &ev_switch_phase;
            break;
        case SIM_PD:
        case SIM_PDS:
        case SIM_GPU_PD:
            notify_event = &ev_dis_start;
            break;
        }

        config_helper->parse_ack_msg(event_engine, flow_id, notify_event);

        wait();
    }
}

void MemInterface::recv_done() {
    while (true) {
        sc_event *notify_event = nullptr;
        switch (SYSTEM_MODE) {
        case SIM_DATAFLOW:
            notify_event = nullptr;
            break;
        case SIM_GPU:
            notify_event = &ev_switch_phase;
            break;
        case SIM_PD:
        case SIM_PDS:
        case SIM_GPU_PD:
            notify_event = &ev_dis_config;
            break;
        }

        config_helper->parse_done_msg(event_engine, notify_event);

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
                host_channel_o[i].write(SerializeMsg(t));
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
        if (SYSTEM_MODE != SIM_PD && SYSTEM_MODE != SIM_PDS &&
            SYSTEM_MODE != SIM_GPU_PD) {
            cout << "[ERROR] Request handler can only be used in PD mode or "
                    "PDS mode.\n";
            sc_stop();
        }

        if (SYSTEM_MODE == SIM_PD) {
            config_helper_pd *pd = (config_helper_pd *)config_helper;
            for (int i = 0; i < pd->arrival_time.size(); i++) {
                sc_time next_time(pd->arrival_time[i], SC_NS);
                if (next_time < sc_time_stamp()) {
                    cout << "[ERROR] Be sure all reqs come in sequentially.\n";
                    sc_stop();
                }

                wait(next_time - sc_time_stamp());
                ev_dis_config.notify(0, SC_NS);
            }
        } else if (SYSTEM_MODE == SIM_PDS) {
            config_helper_pds *pd = (config_helper_pds *)config_helper;
            for (int i = 0; i < pd->arrival_time.size(); i++) {
                sc_time next_time(pd->arrival_time[i], SC_NS);
                if (next_time < sc_time_stamp()) {
                    cout << "[ERROR] Be sure all reqs come in sequentially.\n";
                    sc_stop();
                }

                wait(next_time - sc_time_stamp());
                ev_dis_config.notify(0, SC_NS);
            }
        } else if (SYSTEM_MODE == SIM_GPU_PD) {
            config_helper_gpu_pd *pd = (config_helper_gpu_pd *)config_helper;
            for (int i = 0; i < pd->arrival_time.size(); i++) {
                sc_time next_time(pd->arrival_time[i], SC_NS);
                if (next_time < sc_time_stamp()) {
                    cout << "[ERROR] Be sure all reqs come in sequentially.\n";
                    sc_stop();
                }

                wait(next_time - sc_time_stamp());
                ev_dis_config.notify(0, SC_NS);
            }
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