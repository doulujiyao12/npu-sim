#pragma once
#include "systemc.h"

#include "common/system.h"
#include "hardware/systolic/process_element.h"
#include "trace/Event_engine.h"

class Systolic_interface;

class SystolicArray : public sc_module {
public:
    PE **elements;
    Systolic_interface *interface;

    Event_engine *event_engine;
    HardwareTaskConfig *config;

    sc_signal<bool> *data_sent[2];
    sc_signal<bool> *psum_sent_back;
    sc_signal<float> *data_c;
    sc_signal<float> *weight_c;
    sc_signal<float> *psum_c;

    sc_out<bool> systolic_done_o;
    sc_in<bool> systolic_start_i;

    SC_HAS_PROCESS(SystolicArray);
    SystolicArray(const sc_module_name &n, Event_engine *event_engine, HardwareTaskConfig *config);
    ~SystolicArray();

    void systolic_execute();
};


class Systolic_interface : public sc_module {
public:
    Event_engine *event_engine;
    HardwareTaskConfig *config;
    PE **elements;

    float *weight;
    float *input;
    float *output;
    int batch_size, data_h, data_w, weight_h, weight_w;
    int period_b, period_h, period_w;
    int data_period_h, data_period_w;

    int psum_count[PE_GRID_X]; // 记录每个接收通道已经接收到的psum的数量

    sc_event ev_exec;                // 被systolic调用，触发工作流
    sc_event ev_next_trigger_psum;   // 用于触发下一个psum的接收
    sc_signal<bool> recv_psum_block; // 在开始发送数据之后，等待其下降沿，表明psum已经全部接收完毕
    sc_signal<bool> work_done;

    sc_out<float> data_o[PE_GRID_X];        // 将数据发送到PE
    sc_out<float> weight_o[PE_GRID_X];      // 不被使用
    sc_in<float> psum_i[PE_GRID_X];         // 从PE接收psum
    sc_out<bool> data_sent_o[2][PE_GRID_X]; // 从左侧和上侧向PE发送数据的提示
    sc_in<bool> data_sent_i[PE_GRID_X];     // 从下侧接收PE发来数据的提示

    SC_HAS_PROCESS(Systolic_interface);
    Systolic_interface(const sc_module_name &n, Event_engine *event_engine, HardwareTaskConfig *config);
    ~Systolic_interface() {}

    void systolic_interface_execute();
    void receive_psum();
    void trigger_next_psum();

    void distribute_weight(int batch, int h_index, int w_index);
};