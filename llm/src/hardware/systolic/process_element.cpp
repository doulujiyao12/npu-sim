#include "hardware/systolic/process_element.h"
#include "macros/macros.h"
#include "utils/pe_utils.h"

PE::PE(const sc_module_name &n, int s_peid, Event_engine *event_engine) : sc_module(n), event_engine(event_engine) {
    peid = s_peid;

    data_sent_i = new sc_in<bool>[2];
    data_sent_down_o = new sc_out<bool>;
    if (!PE_is_data_end(peid)) {
        data_sent_right_o = new sc_out<bool>;
    }

    data_i = new sc_in<float>;
    if (!PE_is_data_end(peid)) {
        data_o = new sc_out<float>;
    }

    // CTODO: delete this
    weight_i = new sc_in<float>;
    if (!PE_is_weight_end(peid))
        weight_o = new sc_out<float>;

    if (!PE_is_sum_start(peid))
        psum_i = new sc_in<float>;
    psum_o = new sc_out<float>;

    SC_THREAD(trans_next_trigger);
    sensitive << data_sent_i[0].pos() << data_sent_i[1].pos();
    dont_initialize();

    SC_THREAD(step_in);
    sensitive << need_next_trigger;
    dont_initialize();
}

void PE::trans_next_trigger() {
    while (true) {
        need_next_trigger.notify(CYCLE, SC_NS);

        wait();
    }
}

void PE::step_in() {
    while (true) {
        bool trigger_flag = false;
        if (!PE_is_data_end(peid))
            data_sent_right_o->write(false);
        data_sent_down_o->write(false);

        if (data_sent_i[0].read() || data_sent_i[1].read()) {
            trigger_flag = true;

            float data = data_i->read();
            float psum;
            if (PE_is_sum_start(peid)) {
                psum = 0.0;
            } else {
                psum = psum_i->read();
            }

            cout << "PE " << peid << ": weight " << weight << ", data " << data << ", psum " << psum << "->";

            psum += data * weight;

            cout << psum << endl;

            if (!PE_is_data_end(peid)) {
                data_o->write(data);
                data_sent_right_o->write(true);
            }

            // if (!PE_is_weight_end(peid)) {
            //     weight_o->write(weight);
            //     data_sent_down_o->write(true);
            // }

            psum_o->write(psum);
            data_sent_down_o->write(true);
        }

        if (trigger_flag)
            need_next_trigger.notify(CYCLE, SC_NS);

        wait();
    }
}