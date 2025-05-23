#pragma once
#include "systemc.h"

#include "defs/global.h"
#include "monitor/config_helper_base.h"
#include "trace/Event_engine.h"

enum PRO_PHASE { PRO_CONF, PRO_DATA, PRO_START, PRO_DONE };

class MemInterface : public sc_module {
public:
    /* --------------------Configure--------------------- */
    config_helper_base *config_helper;
    /* -------------------------------------------------- */

    // ports
    sc_in<bool> *host_data_sent_i;
    sc_out<bool> *host_data_sent_o;

    sc_in<sc_bv<256>> *host_channel_i;
    sc_out<sc_bv<256>> *host_channel_o;

    sc_in<bool> *host_channel_avail_i;

    sc_in<bool> start_i;
    sc_out<bool> preparations_done_o;

    PRO_PHASE phase;
    // 所有host ack 包都已经收到了
    sc_event ev_switch_phase;
    // 可以下发 host data 包
    sc_event ev_dis_data;
    // 可以下发 host start 包
    sc_event ev_dis_start;
    // 可以下发一轮CONFIG
    sc_event ev_dis_config;

    sc_event ev_recv_helper;
    sc_event ev_recv_ack;
    sc_event ev_recv_done;
    int g_recv_ack_cnt;
    int g_recv_done_cnt;
    vector<Msg> g_done_msg; // 在PD模式中存储所有的done_msg

    sc_event ev_req_handler; // 在PD模式中用于在正确的时刻分发所有请求

    // 当recv_helper读到消息的时候，将读到的消息放入这两个数组中，随后通知对应的处理函数。
    // 如果让处理函数主动读，则由于delta
    // cycle的关系，会读不到传入的消息。所以需要这两个数组
    vector<Msg> g_temp_done_msg;
    vector<Msg> g_temp_ack_msg;

    /* -----------------Write helper---------------------- */
    // 由write_helper进行统一写入，此信号指示是否开始写
    sc_event ev_write;
    sc_signal<bool> write_done;
    queue<Msg> *write_buffer;
    /* --------------------------------------------------- */

    Event_engine *event_engine;
    int flow_id;
    vector<int> has_global_mem;

    SC_HAS_PROCESS(MemInterface);
    MemInterface(const sc_module_name &n, Event_engine *event_engine,
                 const char *config_name, const char *font_ttf);
    MemInterface(const sc_module_name &n, Event_engine *event_engine,
                 config_helper_base *input_config);
    ~MemInterface();

    void distribute_config();
    void distribute_data();
    void distribute_start_data();

    void recv_helper();
    void catch_host_data_sent_i();

    void recv_ack();
    void recv_done();

    void write_helper();
    void switch_phase();

    void req_handler(); // 仅PD模式使用

    void clear_write_buffer();

    void end_of_elaboration();
    void end_of_simulation() override;

private:
    void init();
};