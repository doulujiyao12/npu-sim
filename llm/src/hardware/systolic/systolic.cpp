#include "hardware/systolic/systolic.h"

// systolice array
SystolicArray::SystolicArray(const sc_module_name &n,
                             Event_engine *event_engine,
                             HardwareTaskConfig *config)
    : sc_module(n), event_engine(event_engine), config(config) {
    elements = new PE *[PE_GRID_SIZE];
    for (int i = 0; i < PE_GRID_SIZE; i++) {
        elements[i] = new PE(sc_gen_unique_name("pe"), i, this->event_engine);
    }

    interface = new Systolic_interface(sc_gen_unique_name("sys-interface"),
                                       this->event_engine, this->config);
    interface->elements = elements;

    data_c = new sc_signal<float>[PE_GRID_SIZE];
    weight_c = new sc_signal<float>[PE_GRID_SIZE];
    psum_c = new sc_signal<float>[PE_GRID_SIZE];
    for (int i = 0; i < 2; i++) {
        data_sent[i] = new sc_signal<bool>[PE_GRID_SIZE];
    }
    psum_sent_back = new sc_signal<bool>[PE_GRID_X];

    // 连接端口
    // data PE之间的data连接
    for (int i = 0; i < PE_GRID_SIZE; i++) {
        if (i % PE_GRID_X == 0) {
            (interface->data_o)[i / PE_GRID_X](data_c[i]);
            (*(elements[i])->data_i)(data_c[i]);
        } else {
            (*(elements[i - 1])->data_o)(data_c[i]);
            (*(elements[i])->data_i)(data_c[i]);
        }
    }

    // weight
    // weight 在 PE 之间的连接
    for (int i = 0; i < PE_GRID_SIZE; i++) {
        if (i / PE_GRID_X == 0) {
            (interface->weight_o)[i % PE_GRID_X](weight_c[i]);
            (*(elements[i])->weight_i)(weight_c[i]);
        } else {
            (*(elements[i - PE_GRID_X])->weight_o)(weight_c[i]);
            (*(elements[i])->weight_i)(weight_c[i]);
        }
    }

    // psum
    // 最后一行作为输出 psum的输出
    for (int i = 0; i < PE_GRID_SIZE; i++) {
        if (i / PE_GRID_X == PE_GRID_X - 1) {
            (*(elements[i])->psum_o)(psum_c[i]);
            (interface->psum_i)[i % PE_GRID_X](psum_c[i]);
        } else {
            (*(elements[i])->psum_o)(psum_c[i]);
            (*(elements[i + PE_GRID_X])->psum_i)(psum_c[i]);
        }
    }

    // data_sent
    for (int i = 0; i < PE_GRID_SIZE; i++) {
        // data_sent_o[0] 对应左侧输入的data 使能
        if (i % PE_GRID_X == 0) {
            (interface->data_sent_o)[0][i / PE_GRID_X](data_sent[0][i]);
            ((elements[i])->data_sent_i[0])(data_sent[0][i]);
        } else {
            (*(elements[i - 1])->data_sent_right_o)(data_sent[0][i]);
            ((elements[i])->data_sent_i[0])(data_sent[0][i]);
        }
        // data_sent_o[1] 对应上册输入的 weight 使能

        if (i / PE_GRID_X == 0) {
            (interface->data_sent_o)[1][i % PE_GRID_X](data_sent[1][i]);
            ((elements[i])->data_sent_i[1])(data_sent[1][i]);
        } else {
            (*(elements[i - PE_GRID_X])->data_sent_down_o)(data_sent[1][i]);
            ((elements[i])->data_sent_i[1])(data_sent[1][i]);
        }
        // psum 的 输出使能
        if (i / PE_GRID_X == PE_GRID_X - 1) {
            (interface->data_sent_i)[i % PE_GRID_X](
                psum_sent_back[i % PE_GRID_X]);
            (*(elements[i])->data_sent_down_o)(psum_sent_back[i % PE_GRID_X]);
        }
    }

    SC_THREAD(systolic_execute);
    sensitive << systolic_start_i;
    dont_initialize();
}

void SystolicArray::systolic_execute() {
    while (true) {
        interface->ev_exec.notify(SC_ZERO_TIME);
        wait(interface->work_done.posedge_event());

        systolic_done_o.write(true);
        wait(CYCLE, SC_NS);
        systolic_done_o.write(false);

        wait();
    }
}

SystolicArray::~SystolicArray() {
    delete interface;
    for (int i = 0; i < PE_GRID_SIZE; i++) {
        delete elements[i];
    }
    delete[] elements;
    delete[] data_c;
    delete[] weight_c;
    delete[] psum_c;
    for (int i = 0; i < 2; i++) {
        delete[] data_sent[i];
    }
    delete[] psum_sent_back;
}

// systolic interface
Systolic_interface::Systolic_interface(const sc_module_name &n,
                                       Event_engine *event_engine,
                                       HardwareTaskConfig *config)
    : sc_module(n), event_engine(event_engine), config(config) {
    for (int i = 0; i < PE_GRID_X; i++) {
        psum_count[i] = 0;
    }

    SC_THREAD(systolic_interface_execute);
    sensitive << ev_exec;
    dont_initialize();

    SC_THREAD(receive_psum);
    sensitive << ev_next_trigger_psum;
    dont_initialize();

    SC_THREAD(trigger_next_psum);
    for (int i = 0; i < PE_GRID_X; i++) {
        sensitive << data_sent_i[i];
    }
    dont_initialize();
}

void Systolic_interface::distribute_weight(int batch, int h_index,
                                           int w_index) {
    // 将weight分发到各个PE, x和y代表weight的区块索引
    for (int i = 0; i < PE_GRID_SIZE; i++) {
        int row = h_index * PE_GRID_X + i / PE_GRID_X;
        int col = w_index * PE_GRID_X + i % PE_GRID_X;

        float *weight_ptr = weight + row * weight_w + col;
        elements[i]->weight = *weight_ptr;
        cout << *weight_ptr << " ";
    }
    cout << endl;
}

void Systolic_interface::systolic_interface_execute() {
    while (true) {
        work_done.write(false);

        if (config->hardware == SYSTOLIC_MATMUL) {
            // 从config中获取数据
            input = config->data[0];
            output = config->data[1];
            weight = config->data[2];
            float *bias = config->data[3];

            batch_size = config->args[0];
            data_h = config->args[1];
            data_w = config->args[2];
            weight_w = config->args[3];
            weight_h = data_w;

            cout << "batch_size: " << batch_size << ", data_h: " << data_h
                 << ", data_w: " << data_w << ", weight_h: " << weight_h
                 << ", weight_w: " << weight_w << ".\n";

            int weight_slice_h = weight_h / PE_GRID_X;
            int weight_slice_w = weight_w / PE_GRID_X;
            int data_slice_h = data_h / PE_GRID_X;
            int data_slice_w = data_w / PE_GRID_X;
            // period_b/h 也会被 receive_psum 共享确定输出的数据地址
            for (period_b = 0; period_b < batch_size; period_b++) {
                for (period_h = 0; period_h < weight_slice_h; period_h++) {
                    data_period_w = period_h;
                    for (period_w = 0; period_w < weight_slice_w; period_w++) {
                        cout << "Sys[INFO]: at batch " << period_b
                             << ", weight_h " << period_h << ", weight_w "
                             << period_w << ".\n";
                        distribute_weight(period_b, period_h, period_w);
                        // 输出的行切块，
                        for (data_period_h = 0; data_period_h < data_slice_h;
                             data_period_h++) {
                            cout << "Sys[INFO]: at data_h " << data_period_h
                                 << ".\n";
                            // 发送数据
                            int index[PE_GRID_X];
                            // 脉动 shift delay
                            for (int i = 0; i < PE_GRID_X; i++) {
                                index[i] = -i;
                            }

                            bool flag = true;
                            while (flag) {
                                flag = false;
                                for (int i = 0; i < PE_GRID_X; i++) {
                                    data_sent_o[0][i].write(false);
                                    if (index[i] < 0 || index[i] >= PE_GRID_X) {
                                        // do nothing
                                    } else {
                                        flag = true;
                                        float num =
                                            input[period_b * data_h * data_w +
                                                  (data_period_h * PE_GRID_X +
                                                   i) *
                                                      data_w +
                                                  data_period_w * PE_GRID_X +
                                                  index[i]];
                                        data_o[i].write(num);
                                        data_sent_o[0][i].write(true);

                                        // cout << "Sys[SEND]: at batch " <<
                                        // period_b << ", weight_h " << period_h
                                        // << ", weight_w " << period_w << ",
                                        // data_h " << data_period_h << ", send
                                        // data " << num << " to row " << i <<
                                        // ".\n";
                                    }
                                    index[i]++;
                                }

                                wait(CYCLE, SC_NS);
                            }

                            // 等待记录psum到output
                            // 当前weight 对应的输出数据都算完了
                            wait(recv_psum_block.negedge_event());
                        }
                    }
                }
            }
        }

        else {
            cout << "Systolic_interface: Unknown hardware task.\n";
            sc_stop();
        }

        work_done.write(true);
        wait();
    }
}

void Systolic_interface::receive_psum() {
    while (true) {
        bool flag = true;
        recv_psum_block.write(true);

        for (int i = 0; i < PE_GRID_X; i++) {
            if (psum_count[i] >= PE_GRID_X) {
                continue;
            }

            if (data_sent_i[i].read()) {
                flag = false;
                float psum = psum_i[i].read();
                output[period_b * data_h * weight_w +
                       (data_period_h * PE_GRID_X + psum_count[i]) * weight_w +
                       period_w * PE_GRID_X + i] += psum;
                cout << period_b << " " << period_h << " " << period_w << " "
                     << data_period_h << " " << i << " " << psum_count[i] << " "
                     << psum << endl;
                psum_count[i]++;

                cout << "Sys[RECV]: output["
                     << period_b * data_h * weight_w +
                            (data_period_h * PE_GRID_X + psum_count[i] - 1) *
                                weight_w +
                            period_w * PE_GRID_X + i
                     << "] += " << psum << ".\n";
            }
        }

        if (flag) {
            recv_psum_block.write(false);
            cout << "+++++++++++++++++++++++++++++++++++++\n";
            for (int i = 0; i < PE_GRID_X; i++) {
                psum_count[i] = 0;
            }
        } else
            ev_next_trigger_psum.notify(CYCLE, SC_NS);

        wait();
    }
}

void Systolic_interface::trigger_next_psum() {
    while (true) {
        ev_next_trigger_psum.notify(CYCLE, SC_NS);
        wait();
    }
}