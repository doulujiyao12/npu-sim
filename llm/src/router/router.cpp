#include "router/router.h"

RouterMonitor::RouterMonitor(const sc_module_name &n,
                             Event_engine *event_engine)
    : sc_module(n), event_engine(event_engine) {
    // WEAK: we now assume there are only 1*GRID_X tiles
    routers = new RouterUnit *[GRID_SIZE];
    for (int i = 0; i < GRID_SIZE; i++) {
        routers[i] =
            new RouterUnit(sc_gen_unique_name("router"), i, this->event_engine);
    }
}

RouterMonitor::~RouterMonitor() {
    // free routers
    for (int i = 0; i < GRID_SIZE; i++) {
        delete (routers[i]);
    }
    delete (routers);
}

RouterUnit::RouterUnit(const sc_module_name &n, int rid,
                       Event_engine *event_engine)
    : sc_module(n), rid(rid), event_engine(event_engine) {
    host_data_sent_i = nullptr;
    host_data_sent_o = nullptr;
    host_channel_i = nullptr;
    host_channel_o = nullptr;
    host_buffer_i = nullptr;
    host_buffer_o = nullptr;
    host_channel_avail_o = nullptr;

    // 初始全部通道均未上锁
    for (int i = 0; i < DIRECTIONS; i++) {
        input_lock[i] = 0;
        input_lock_ref[i] = 0;
        output_lock[i] = -1;
        output_lock_ref[i] = 0;
    }

    if (is_margin_core(rid)) {
        host_buffer_i = new queue<sc_bv<256>>;
        host_buffer_o = new queue<sc_bv<256>>;
        host_channel_i = new sc_in<sc_bv<256>>;
        host_channel_o = new sc_out<sc_bv<256>>;
        host_data_sent_i = new sc_in<bool>;
        host_data_sent_o = new sc_out<bool>;
        host_channel_avail_o = new sc_out<bool>;
    }

    SC_THREAD(trans_next_trigger);
    sensitive << data_sent_i[WEST].pos() << data_sent_i[EAST].pos()
              << data_sent_i[CENTER].pos() << data_sent_i[SOUTH].pos()
              << data_sent_i[NORTH].pos();
    if (is_margin_core(rid))
        sensitive << host_data_sent_i->pos();
    sensitive << channel_avail_i[WEST].pos() << channel_avail_i[EAST].pos()
              << channel_avail_i[SOUTH].pos() << channel_avail_i[NORTH].pos();
    sensitive << core_busy_i.neg();
    dont_initialize();

    SC_THREAD(router_execute);
    sensitive << need_next_trigger;
    dont_initialize();
}

void RouterUnit::end_of_elaboration() {
    // set signals
    for (int i = 0; i < DIRECTIONS; i++) {
        channel_avail_o[i].write(true);
        data_sent_o[i].write(false);
    }

    if (is_margin_core(rid)) {
        host_channel_avail_o->write(true);
        host_data_sent_o->write(false);
    }
}

void RouterUnit::router_execute() {
    while (true) {
        bool flag_trigger = false;

        // 将输出信号都设置为初始值false
        for (int i = 0; i < DIRECTIONS; i++) {
            channel_avail_o[i].write(false);
            data_sent_o[i].write(false);
        }

        // [input] 4方向+cores
        for (int i = 0; i < DIRECTIONS; i++) {
            if (data_sent_i[i].read()) {
                // move the data into the buffer
                // cout << sc_time_stamp() << ": Router " << rid << "getdata at
                // "
                //      << i << "\n";
                sc_bv<256> temp = channel_i[i].read();

                buffer_i[i].emplace(temp);

                // need trigger again
                flag_trigger = true;
            }
        }

        // [input] host
        // if is_margin_core
        if (host_buffer_i) {
            // host send data to core
            if (host_data_sent_i->read()) {
                // move the data into the buffer
                sc_bv<256> temp = host_channel_i->read();

                Msg tt = deserialize_msg(temp);
                // cout << sc_time_stamp() << ": Router " << rid
                //      << ": get des seqid " << tt.des << " " << tt.seq_id
                //      << " from host." << endl;

                host_buffer_i->emplace(temp);

                // need trigger again
                flag_trigger = true;
            }
        }

        // [output] 4方向
        for (int i = 0; i < DIRECTIONS - 1; i++) {
            // global update once
            data_sent_o[i].write(false);
            // 输出方向的buffer是否为满
            if (channel_avail_i[i].read() == false)
                continue;

            // shall not check when output buffer is empty
            // 对应输出的buffer非空
            if (!buffer_o[i].size())
                continue;

            sc_bv<256> temp = buffer_o[i].front();
            buffer_o[i].pop();

            Msg tt = deserialize_msg(temp);
            // cout << sc_time_stamp() << ": " << rid << ": output " << i <<
            // "\n";

            channel_o[i].write(temp);
            data_sent_o[i].write(true);

            // need trigger again
            flag_trigger = true;
        }

        // [output] host
        // if is_margin_core
        if (host_channel_i) {
            host_data_sent_o->write(false);
            // 输出到host方向上的buffer非空
            if (host_buffer_o->size()) {
                sc_bv<256> temp = host_buffer_o->front();
                host_buffer_o->pop();

                host_channel_o->write(temp);
                host_data_sent_o->write(true);

                // need trigger again
                flag_trigger = true;
            }
        }

        // [output] core
        // 输出到本地core内部的
        data_sent_o[CENTER].write(false);
        // 输出到本地core内的buffer非空
        if (buffer_o[CENTER].size()) {
            // core内部的接受队列是否满
            if (!core_busy_i.read()) {
                // move the data out of the buffer
                sc_bv<256> temp = buffer_o[CENTER].front();

                buffer_o[CENTER].pop();

                Msg tt = deserialize_msg(temp);

                channel_o[CENTER].write(temp);
                data_sent_o[CENTER].write(true);
                // cout << sc_time_stamp() << ": Router " << rid << ": send "
                //      << tt.seq_id << " to core.\n";
            }

            // need trigger again
            flag_trigger = true;
        }

        // [input -> output] host
        // host输入包 向 output 哪个方向输出
        if (host_channel_i && host_buffer_i->size()) {
            sc_bv<256> temp = host_buffer_i->front();
            int d = get_msg_des_id(temp);
            // 先x后y的路由
            Directions next = get_next_hop(d, rid);

            if (buffer_o[next].size() < MAX_BUFFER_PACKET_SIZE &&
                output_lock[next] == -1) {
                host_buffer_i->pop();
                buffer_o[next].emplace(temp);

                flag_trigger = true;
            }
        }

        // [input -> output] REQUEST
        // 检查req_queue中的所有元素。假设A向B发送req，则检查B->A的输出信道是否上锁。如果不上锁/或上锁且tag相同，且buffer未满，则可以搬运至输出信道。
        for (auto it = req_queue.begin(); it != req_queue.end();) {
            auto req = *it;
            int des = req.des;
            int source = req.source;
            Directions next = get_next_hop(des, source);

            if (output_lock[next] == -1 || output_lock[next] == req.tag_id) {
                cout << "[INFO] Router " << rid << ", checking req from "
                     << source << endl;
                if (buffer_o[CENTER].size() < MAX_BUFFER_PACKET_SIZE) {
                    it = req_queue.erase(it);
                    buffer_o[CENTER].emplace(serialize_msg(req));
                    flag_trigger = true;
                    continue;
                }
            }

            ++it;
        }

        // FIX input -> output 的仲裁
        // [input -> output] 4方向+core
        for (int i = 0; i < DIRECTIONS; i++) {
            if (!buffer_i[i].size())
                continue;

            sc_bv<256> temp = buffer_i[i].front();
            Msg m = deserialize_msg(temp);
            Directions out = get_next_hop(m.des, rid);

            // 是否能发送 不能发送的情况是上锁了以后，并且tag一样
            // 注意：REQUEST包若终点为本core，则会优先进入req_buffer；否则按照正常数据流转
            if (m.msg_type == REQUEST && m.des == rid) {
                buffer_i[i].pop();
                req_queue.push_back(m);

                cout << "[REQUEST] Router " << rid << " received REQ from "
                     << m.source << ", put into req_queue.\n";
                continue;
            }

            if (m.des != GRID_SIZE && output_lock[out] != -1 &&
                output_lock[out] !=
                    m.tag_id) // 如果不发往host，且目标通道上锁，且目标上锁tag不等同于自己的tag：continue
                continue;
            if (out == HOST &&
                host_buffer_o->size() >=
                    MAX_BUFFER_PACKET_SIZE) // 如果发往host，但通道已满：continue
                continue;
            else if (
                out != HOST &&
                buffer_o[out].size() >=
                    MAX_BUFFER_PACKET_SIZE) // 如果不发往host，但通道已满：continue
                continue;

            // cout << sc_time_stamp() << ": Router " << rid << ": "
            //      << " put into " << out << " id " << m.seq_id << endl;

            // [ACK] 非发往host的ACK包，需要上锁或者增加refcnt
            // FIX 上锁应该在第一个DATA 包
            if (m.msg_type == DATA && m.seq_id == 1 && m.des != GRID_SIZE &&
                m.source != GRID_SIZE) {
                // i 是 ACK 的进入方向，需要计算 ACK 的输出方向
                if (output_lock[out] == -1) {
                    // 上锁
                    output_lock[out] = m.tag_id;
                    output_lock_ref[out]++;
                    cout << sc_time_stamp() << " " << ": Router " << rid
                         << " lock: " << out << " " << output_lock[out] << " "
                         << output_lock_ref[out] << endl;
                } else if (output_lock[out] == m.tag_id) {
                    // 添加refcnt
                    // Two Ack 多发一 DATA 包 乱序 接受核的接受地址由 Send
                    // 包中地址决定
                    output_lock_ref[out]++;
                    // cout << sc_time_stamp() << " " << ": Router " << rid
                    //      << " addlock: " << out << " " << output_lock[out]
                    //      << " " << output_lock_ref[out] << endl;
                } else {
                    // 并非对应tag，不予通过
                    continue;
                }
            }

            // [DATA] 最后一个数据包，需要减少refcnt，如果refcnt为0,则解锁
            // DTODO
            // 排除了Config DATA 包，不会减少 lock
            // START DATA 包也不会上锁？
            if (m.msg_type == DATA && m.is_end && m.source != GRID_SIZE &&
                m.des != GRID_SIZE) {
                // i 是 data 的进入方向，需要计算 data 的输出方向
                out = get_next_hop(m.des, rid);

                output_lock_ref[out]--;

                cout << sc_time_stamp() << " " << ": Router " << rid
                     << " unlock: " << out << " " << output_lock[out] << " "
                     << output_lock_ref[out] << endl;

                if (output_lock_ref[out] < 0) {
                    cout << sc_time_stamp() << ": Router " << rid
                         << " output ref below zero.\n";
                    sc_stop();
                } else if (output_lock_ref[out] == 0) {
                    output_lock[out] = -1;
                }
            }

            // 发送
            if (out == HOST) {
                buffer_i[i].pop();
                host_buffer_o->emplace(temp);

                flag_trigger = true;
            } else {
                buffer_i[i].pop();
                buffer_o[out].emplace(temp);

                flag_trigger = true;
            }
        }

        // 检查是否有剩余的req，需要重复触发
        if (req_queue.size())
            flag_trigger = true;

        // [SIGNALS] 4方向
        for (int i = 0; i < DIRECTIONS; i++) {
            if (buffer_i[i].size() < MAX_BUFFER_PACKET_SIZE) {
                channel_avail_o[i].write(true);
            } else {
                channel_avail_o[i].write(false);
            }
        }

        // [SIGNALS] host
        if (host_channel_i) {
            if (host_buffer_i->size() < MAX_BUFFER_PACKET_SIZE) {
                host_channel_avail_o->write(true);
            } else {
                host_channel_avail_o->write(false);
            }
        }
#if ROUTER_LOOP == 1 
        cout << "Router " << rid << "flag_trigger " << flag_trigger << endl;
#endif 

        // trigger again
        if (flag_trigger)
            need_next_trigger.notify(CYCLE, SC_NS);

        wait();
    }
}

void RouterUnit::trans_next_trigger() {
    while (true) {
        // DAHU notify 0ns
        need_next_trigger.notify(CYCLE, SC_NS);
        wait();
    }
}

RouterUnit::~RouterUnit() {
    if (host_buffer_i) {
        delete host_buffer_i;
        delete host_buffer_o;
        delete host_channel_i;
        delete host_channel_o;
        delete host_data_sent_i;
        delete host_data_sent_o;
        delete host_channel_avail_o;
    }
}