#pragma once
#include "systemc.h"
#include "trace/Event_engine.h"

class PE : public sc_module {
public:
    int peid;
    float weight; // weight is static

    Event_engine *event_engine;

    sc_event need_next_trigger;

    sc_in<bool> *data_sent_i;
    sc_out<bool> *data_sent_down_o;
    sc_out<bool> *data_sent_right_o;
    sc_in<float> *data_i;
    sc_out<float> *data_o;

    // CTODO: delete this
    sc_in<float> *weight_i;
    sc_out<float> *weight_o;

    sc_in<float> *psum_i;
    sc_out<float> *psum_o;

    SC_HAS_PROCESS(PE);
    PE(const sc_module_name &n, int s_peid, Event_engine *event_engine);
    ~PE() {}

    void step_in();
    void trans_next_trigger();
};