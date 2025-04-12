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

    sc_event ev_recv_helper;
    sc_event ev_recv_ack;
    sc_event ev_recv_done;
    int g_recv_ack_cnt;
    int g_recv_done_cnt;

    /* -----------------Write helper---------------------- */
    // 由write_helper进行统一写入，此信号指示是否开始写
    sc_event ev_write;
    sc_signal<bool> write_done;
    queue<Msg> *write_buffer;
    /* --------------------------------------------------- */

    Event_engine *event_engine;
    int flow_id;

    SC_HAS_PROCESS(MemInterface);
    MemInterface(const sc_module_name &n, Event_engine *event_engine, const char *config_name, const char *font_ttf);
    ~MemInterface();

    void distribute_config();
    void distribute_data();
    void distribute_start_data();

    void recv_helper();
    void recv_ack();
    void recv_done();

    void write_helper();
    void switch_phase();

    void clear_write_buffer();

    void end_of_elaboration();
    void end_of_simulation() override;
};