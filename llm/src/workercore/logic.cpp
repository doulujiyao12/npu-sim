#include "systemc.h"
#include <deque>
#include <iostream>
#include <queue>
#include <string>
#include <typeinfo>

#include "defs/const.h"
#include "defs/global.h"
#include "link/nb_global_memif_v2.h"
#include "memory/dram/GPUNB_DcacheIF.h"
#include "memory/gpu/GPU_L1L2_Cache.h"
#include "memory/sram/Mem_access_unit.h"
#include "prims/base.h"
#include "prims/comp_prims.h"
#include "prims/moe_prims.h"
#include "prims/norm_prims.h"
#include "prims/pd_prims.h"
#include "trace/Event_engine.h"
#include "utils/memory_utils.h"
#include "utils/msg_utils.h"
#include "utils/pe_utils.h"
#include "utils/prim_utils.h"
#include "utils/print_utils.h"
#include "utils/system_utils.h"
#include "workercore/workercore.h"


void WorkerCoreExecutor::send_logic() {
    while (true) {
        Send_prim *prim = (Send_prim *)prim_queue.front();

        prim->data_packet_id = 0;
        bool job_done = false; // 结束内圈循环的标志

        cout << "[SEND START] Core " << cid << ": running send "
             << GetEnumSendType(prim->type) << ", destination " << prim->des_id
             << ", tag " << prim->tag_id << ", max packet " << prim->max_packet
             << " at " << sc_time_stamp() << endl;

        while (true) {
            bool need_long_wait = false;
            int roofline_packets = 1;

            if (atomic_helper_lock(sc_time_stamp(), 0))
                ev_send_helper.notify(0, SC_NS);

            // SEND_DATA, SEND_ACK, SEND_REQ
            if (prim->type == SEND_DATA) {
#if ROUTER_PIPE == 1
                while (job_done != true) {
#endif
                    // [发送方] 正常发送数据
                    prim->data_packet_id++;

                    bool is_end_packet =
                        prim->data_packet_id == prim->max_packet;
                    int length = is_end_packet ? prim->end_length : M_D_DATA;

#if USE_BEHA_NOC == 1
                    is_end_packet = true;
                    roofline_packets = prim->max_packet;
#endif

                    int delay = 0;
                    TaskCoreContext context = generate_context(this);
                    // 因为send taskCoreDefault 会delay 所以 ev_send_helper
                    // 有机会发出去 然后wc 里面又要wait 一个cycle ev_send_helper
                    // 一低一高
                    delay = prim->taskCoreDefault(context);

                    if (!channel_avail_i.read())
                        wait(ev_channel_avail_i);

                    Msg temp_msg = Msg(is_end_packet, MSG_TYPE::DATA,
                                       prim->data_packet_id, prim->des_id, 0,
                                       prim->tag_id, length, sc_bv<128>(0x1));
                    temp_msg.roofline_packets_ = roofline_packets;
                    send_buffer = temp_msg;

                    // send_helper_write = 3;
                    atomic_helper_lock(sc_time_stamp(), 3);
                    ev_send_helper.notify(0, SC_NS);

                    cout << "Core " << cid << ": send " << send_buffer.seq_id_
                         << " to " << send_buffer.des_ << " at "
                         << sc_time_stamp() << endl;

                    if (is_end_packet) {
                        cout << "Core " << cid
                             << " max_packet: " << prim->max_packet << " "
                             << send_buffer.is_end_ << endl;

                        job_done = true;
                    }

                    need_long_wait = true;
#if ROUTER_PIPE == 1
                }
#endif
            }

            else if (prim->type == SEND_REQ) {
                // [发送方] 发送一个req包，发送完之后结束此原语，进入 RECV_ACK
                if (!channel_avail_i.read())
                    wait(ev_channel_avail_i);

                send_buffer =
                    Msg(MSG_TYPE::REQUEST, prim->des_id, prim->tag_id, cid);

                send_helper_write = 3;
                ev_send_helper.notify(0, SC_NS);

                cout << sc_time_stamp() << ": Worker " << cid << ": REQ to "
                     << prim->des_id << " sent.\n";

                job_done = true;
            }

            else if (prim->type == SEND_DONE) {
                // [执行核]
                // 在计算图的汇节点执行完毕之后，给host发送一份DONE数据包，标志任务完成
                if (!channel_avail_i.read())
                    wait(ev_channel_avail_i);

                send_buffer = Msg(MSG_TYPE::DONE, GRID_SIZE, cid);

                if (SYSTEM_MODE == SIM_PD || SYSTEM_MODE == SIM_PDS) {
                    for (int i = 0; i < core_context->decode_done_.size();
                         i++) {
                        send_buffer.data_.range(i, i) =
                            sc_bv<1>(core_context->decode_done_[i]);
                    }
                }

                send_helper_write = 3;
                ev_send_helper.notify(0, SC_NS);

                cout << sc_time_stamp() << ": Worker " << cid
                     << ": DONE sent.\n";

                job_done = true;
            }

            else {
                // unimplemented
                cout << sc_time_stamp() << ": Worker " << cid
                     << ": unimplemented SEND_PRIM.\n";

                sc_stop();
            }

            wait(roofline_packets * CYCLE, SC_NS);

            if (job_done) {
                cout << "[SEND DONE] Core " << cid << ": running send "
                     << GetEnumSendType(prim->type) << " done at "
                     << sc_time_stamp() << "\n";
                break;
            }
        }

        ev_block.notify(CYCLE, SC_NS);
        wait();
    }
}

void WorkerCoreExecutor::send_para_logic() {
    while (true) {
        while (send_para_queue.size()) {
            PrimBase *prim = send_para_queue.front();
            send_para_queue.pop();

            if (typeid(*prim) == typeid(Send_prim)) {
                ((Send_prim *)prim)->data_packet_id = 0;
                cout << "Core " << cid << " going para send\n";
                event_engine->add_event(
                    "Core " + ToHexString(cid), "Send_prim", "B",
                    Trace_event_util(
                        "Send_prim" +
                        GetEnumSendType(
                            dynamic_cast<Send_prim *>(prim)->type)));
            } else if (typeid(*prim) == typeid(Recv_prim)) {
                cout << "Core " << cid << " going para recv\n";
                event_engine->add_event(
                    "Core " + ToHexString(cid), "Recv_prim", "B",
                    Trace_event_util(
                        "Recv_prim" +
                        GetEnumRecvType(
                            dynamic_cast<Recv_prim *>(prim)->type)));
            }

            bool job_done = false; // 结束内圈循环的标志

            while (true) {
#if ROUTER_PIPE == 0
                if (atomic_helper_lock(sc_time_stamp(), 0))
                    ev_send_helper.notify(0, SC_NS);
#else
                while (atomic_helper_lock(sc_time_stamp(), 0) == false) {
                    wait(CYCLE, SC_NS);
                }

                ev_send_helper.notify(0, SC_NS);
#endif

                if (job_done)
                    break;

                // SEND_DATA, SEND_ACK, SEND_REQ
                if (typeid(*prim) == typeid(Send_prim) &&
                    ((Send_prim *)prim)->type == SEND_DATA) {
                    // [发送方] 正常发送数据，数据从DRAM中获取
                    Send_prim *s_prim = (Send_prim *)prim;

                    // atomic_helper_lock 其实是为了表示上锁
#if ROUTER_PIPE == 1
                    while (job_done == false) {
                        if (channel_avail_i.read() &&
                            atomic_helper_lock(sc_time_stamp(), 1)) {
#else
                    if (channel_avail_i.read() &&
                        atomic_helper_lock(sc_time_stamp(), 1)) {
#endif
                            ev_send_helper.notify(0, SC_NS);

                            s_prim->data_packet_id++;

                            bool is_end_packet =
                                s_prim->data_packet_id == s_prim->max_packet;
                            int length = M_D_DATA;
                            if (is_end_packet) {
                                length = s_prim->end_length;
#if ROUTER_PIPE == 0
                                while (!send_last_packet)
                                    wait(ev_send_last_packet);

#else
                            while (!send_last_packet) {
                                atomic_helper_lock(sc_time_stamp(), 0, true);
                                wait(ev_send_last_packet);
                                while (atomic_helper_lock(sc_time_stamp(), 0) ==
                                       false) {
                                    wait(CYCLE, SC_NS);
                                }
                            }
#endif
                                send_last_packet = false;
                            }
#if ROUTER_PIPE == 1
                            send_buffer = Msg(
                                s_prim->data_packet_id == s_prim->max_packet,
                                MSG_TYPE::DATA, s_prim->data_packet_id,
                                s_prim->des_id, 0, s_prim->tag_id, length,
                                sc_bv<128>(0x1));
#else
                        send_buffer =
                            Msg(s_prim->data_packet_id == s_prim->max_packet,
                                MSG_TYPE::DATA, s_prim->data_packet_id,
                                s_prim->des_id, 0, s_prim->tag_id, length,
                                sc_bv<128>(0x1));
#endif
                            int delay = 0;
                            TaskCoreContext context = generate_context(this);
                            delay = prim->taskCoreDefault(context);
#if ROUTER_PIPE == 1
                            atomic_helper_lock(sc_time_stamp(), 0, true);
#else
                        atomic_helper_lock(sc_time_stamp(), 2);
#endif
                            ev_send_helper.notify(0, SC_NS);

                            if (s_prim->data_packet_id == s_prim->max_packet) {
                                job_done = true;
                                cout << "Core " << cid
                                     << " max_packet: " << s_prim->max_packet
                                     << " " << send_buffer.is_end_ << endl;
                            }
                        }
#if ROUTER_PIPE == 1
                        else {
                            cout << "Core " << cid << " "
                                 << channel_avail_i.read() << endl;

                            if (send_helper_write == 1) {
                                send_helper_write = 0;
                            }

                            wait(CYCLE, SC_NS);
                            atomic_helper_lock(sc_time_stamp(), 0);
                            // cout << "Core " << channel_avail_i.read() <<
                            // endl;
                        }
                    }
#endif
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
                    if (msg_buffer_[MSG_TYPE::ACK].size()) {
                        // 接收到数据包
                        Msg m = msg_buffer_[MSG_TYPE::ACK].front();
                        msg_buffer_[MSG_TYPE::ACK].pop();

                        if (m.msg_type_ == ACK) {
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
                    "Core " + ToHexString(cid), "Send_prim", "E",
                    Trace_event_util(
                        "Send_prim" +
                        GetEnumSendType(
                            dynamic_cast<Send_prim *>(prim)->type)));
            } else {
                event_engine->add_event(
                    "Core " + ToHexString(cid), "Recv_prim", "E",
                    Trace_event_util(
                        "Recv_prim" +
                        GetEnumRecvType(
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
        // 已经接收到的end包数量，需要等于recv原语中的对应要求才能结束此原语
        int end_cnt = 0;
        // 在RECV_CONFIG中，接收到最后一个config包之后，需要等待发送ack
        bool wait_send = false;
        bool job_done = false;
        vector<sc_bv<128>> segments; // 单个原语配置的所有数据包

        cout << "[RECV] Core " << cid << ": running recv "
             << GetEnumRecvType(prim->type) << ", recv_cnt " << prim->recv_cnt
             << ", recv_tag " << prim->tag_id << endl;

        while (true) {
            bool need_long_wait = false;
            int roofline_packets = 1;

            if (atomic_helper_lock(sc_time_stamp(), 0))
                ev_send_helper.notify(0, SC_NS);

            if (prim->type == RECV_ACK) {
                // [发送方] 接收来自接收方的ack包，收到之后结束此原语，进入
                // SEND_DATA 或 SEND_SRAM
                while (!msg_buffer_[MSG_TYPE::ACK].size())
                    wait(ev_recv_msg_type_[MSG_TYPE::ACK]);

                // 接收到数据包
                Msg m = msg_buffer_[MSG_TYPE::ACK].front();
                msg_buffer_[MSG_TYPE::ACK].pop();

                if (m.msg_type_ == ACK) {
                    job_done = true;

                    cout << sc_time_stamp() << ": Worker " << cid
                         << ": received ACK packet.\n";
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
                while (!msg_buffer_[MSG_TYPE::P_DATA].size())
                    wait(ev_recv_msg_type_[MSG_TYPE::P_DATA]);

                temp = msg_buffer_[MSG_TYPE::P_DATA].front();
                msg_buffer_[MSG_TYPE::P_DATA].pop();

                // 复制到SRAM中
                // 如果是end包，则将recv_index归零，表示开始接收下一个core传来的数据（如果有的话）
                if (temp.is_end_) {
                    while (!atomic_helper_lock(sc_time_stamp(), 3) ||
                           !channel_avail_i.read()) {
                        wait(CYCLE, SC_NS);
                    }

                    // 这里是针对host data 和 start 包
                    cout << sc_time_stamp() << ": Worker " << cid
                         << ": received all prepare data.\n";

                    // 向host发送一个ack包
                    send_buffer =
                        Msg(MSG_TYPE::ACK, GRID_SIZE, prim->tag_id, cid);
                    ev_send_helper.notify(0, SC_NS);

                    cout << sc_time_stamp() << ": Worker " << cid
                         << " receive end packet: end_cnt " << end_cnt
                         << ", recv_cnt " << recv_cnt << ", max_recv "
                         << max_recv << endl;

                    job_done = true;
                }
            }

            else if (prim->type == RECV_DATA || prim->type == RECV_START) {
                // [接收方]
                // 接收消息，但是途中如果有新的REQ包进入，需要判断是否要回发ACK包
                // 如果recv_cnt等于0,说明无需接收包裹，直接开始comp即可
                if (prim->recv_cnt == 0)
                    job_done = true;
                else {
                    // 按照prim的tag进行判断。如果tag等同于cid，则优先查看start
                    // data buffer，再查看recv buffer
                    // 如果tag不等同于id，则不允许查看start data buffer
                    ev_prim_recv_notice.notify(0, SC_NS);
                    // cout << "Core " << cid << ": in RECV_START/RECV_DATA.\n";

                    Msg temp;
                    // 表示 当前周期该核有需要处理的msg 的recv包
                    if (prim->type == RECV_DATA) {
                        while (!msg_buffer_[MSG_TYPE::DATA].size())
                            wait(ev_recv_msg_type_[MSG_TYPE::DATA]);

                        temp = msg_buffer_[MSG_TYPE::DATA].front();
                    } else if (prim->type == RECV_START) {
                        while (!msg_buffer_[MSG_TYPE::S_DATA].size())
                            wait(ev_recv_msg_type_[MSG_TYPE::S_DATA]);

                        temp = msg_buffer_[MSG_TYPE::S_DATA].front();
                    }

                    if (prim->tag_id != cid && temp.tag_id_ != prim->tag_id)
                        ARGUS_EXIT("Core ", cid,
                                   " gets incompatible tag id: prim tag ",
                                   prim->tag_id, " with buffer top msg tag ",
                                   temp.tag_id_);

                    if (prim->type == RECV_DATA)
                        msg_buffer_[MSG_TYPE::DATA].pop();
                    else
                        msg_buffer_[MSG_TYPE::S_DATA].pop();

                    recv_cnt++;

                    if (temp.seq_id_ == 1 &&
                        (SYSTEM_MODE == SIM_DATAFLOW || SYSTEM_MODE == SIM_PD ||
                         SYSTEM_MODE == SIM_PDS)) {
                        // 在pos locator中添加一个kv，label是input_label
                        // 对于每一个核的第一算子的input来自与send
                        // 核的输出，并且已经会由router保存在sram上
                        AddrPosKey inp_key = AddrPosKey(*sram_addr, 0);
                        string input_label = INPUT_LABEL;

                        core_context->sram_pos_locator_->addPair(input_label,
                                                                 inp_key, true);
                    }

                    int delay = 0;
                    TaskCoreContext context = generate_context(this);
                    delay = prim->taskCoreDefault(context);

                    // cout << sc_time_stamp() << ": Worker " << cid
                    //      << ": received packet: " << temp.seq_id_ << endl;

                    // 如果是end包，则将recv_index归零，表示开始接收下一个core传来的数据（如果有的话）
                    if (temp.is_end_) {
                        end_cnt++;
                        max_recv += temp.seq_id_;

                        cout << sc_time_stamp() << ": Worker " << cid
                             << " receive end packet: end_cnt " << end_cnt
                             << ", recv_cnt " << recv_cnt << ", max_recv "
                             << max_recv
                             << ", roofline: " << temp.roofline_packets_
                             << endl;

                        // prim->recv_cnt 记录的是 receive 原语 需要接受的
                        // end 包的数量 多发一的实现 max_recv 表示当前 DATA
                        // 包 发送了多少个 package 数量
                        if (end_cnt == prim->recv_cnt && recv_cnt >= max_recv) {
                            // 收到了所有的数据，可以结束此原语，进入comp原语
                            // 无需更新pos_locator中的kv的size，由原语自己指定输入大小
                            job_done = true;
                        }
                    }

                    need_long_wait = true;
#if USE_BEHA_NOC == 1
                    roofline_packets = temp.roofline_packets_;
#endif
                }
            }

            else if (prim->type == RECV_CONF) {
                // [所有人]
                // 在模拟开始时接收配置，接收完毕之后发送一个ACK包给host，此原语需要对prim_queue进行压入，此原语执行完毕之后，进入RECV_DATA
                if (wait_send) {
                    while (!atomic_helper_lock(sc_time_stamp(), 3) ||
                           !channel_avail_i.read()) {
                        wait(CYCLE, SC_NS);
                    }

                    // 正在等待向host发送ack包
                    send_buffer =
                        Msg(MSG_TYPE::ACK, GRID_SIZE, prim->tag_id, cid);
                    ev_send_helper.notify(0, SC_NS);

                    cout << "[RECV] Core " << cid << ": received all CONFIG.\n";

                    job_done = true;
                } else {
                    while (!msg_buffer_[MSG_TYPE::CONFIG].size())
                        wait(ev_recv_msg_type_[MSG_TYPE::CONFIG]);

                    Msg m = msg_buffer_[MSG_TYPE::CONFIG].front();
                    msg_buffer_[MSG_TYPE::CONFIG].pop();

                    if (m.config_end_) {
                        cout << "[RECV] Core " << cid
                             << ": received CONFIG end, "
                             << PrimFactory::getInstance().getPrimType(
                                    m.data_.range(7, 0).to_uint64())
                             << endl;
                        segments.push_back(m.data_);
                        prim_queue.emplace_back(parse_prim(segments));
                        segments.clear();
                    } else {
                        cout << "[RECV] Core " << cid
                             << ": received CONFIG segment, "
                             << PrimFactory::getInstance().getPrimType(
                                    m.data_.range(7, 0).to_uint64())
                             << endl;
                        segments.push_back(m.data_);
                    }


                    // 检查是否为end config包，如果是，需要向host发送ack包
                    if (m.is_end_) {
                        this->prim_refill = m.refill_;
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
            wait(roofline_packets * CYCLE, SC_NS);

            ev_msg_process_end.notify();
            if (job_done)
                break;
        }

        ev_block.notify(CYCLE, SC_NS);
        wait();
    }
}

void WorkerCoreExecutor::task_logic() {
    while (true) {
        PrimBase *p = prim_queue.front();

        int delay = 0;
        TaskCoreContext context = generate_context(this);

        cout << "[PRIM] Core <\033[38;5;214m" << cid
             << "\033[0m>: PRIM NAME -----------------------: " << p->name
             << endl;
        delay = p->taskCoreDefault(context);
        wait(sc_time(delay, SC_NS));

        cout << "Core " << cid << ": task " << p->name << " done.\n";

        ev_block.notify(CYCLE, SC_NS);
        wait();
    }
}

void WorkerCoreExecutor::req_logic() {
    queue<int> ack_queue;

    while (true) {
        if (prim_queue.size()) {
            PrimBase *p = prim_queue.front();

            if (typeid(*p) == typeid(Recv_prim)) {
                Recv_prim *prim = (Recv_prim *)p;

                if ((prim->type == RECV_DATA || prim->type == RECV_START) &&
                    !msg_buffer_[MSG_TYPE::REQUEST].empty()) {
                    queue<Msg> temp;

                    while (!msg_buffer_[MSG_TYPE::REQUEST].empty()) {
                        auto &msg = msg_buffer_[MSG_TYPE::REQUEST].front();

                        if (msg.tag_id_ == prim->tag_id) {
                            ack_queue.push(msg.source_);
                        } else
                            temp.push(msg);

                        msg_buffer_[MSG_TYPE::REQUEST].pop();
                    }
                    msg_buffer_[MSG_TYPE::REQUEST] = move(temp);
                }

                // 发送ack包
                while (ack_queue.size()) {
                    while (!atomic_helper_lock(sc_time_stamp(), 3) ||
                           !channel_avail_i.read()) {
                        wait(CYCLE, SC_NS);
                    }

                    int des = ack_queue.front();
                    ack_queue.pop();

                    send_buffer = Msg(MSG_TYPE::ACK, des, des, cid);
                    ev_send_helper.notify(0, SC_NS);

                    cout << "Core " << cid << " sent ACK to " << des << endl;
                }
            }
        }

        wait();
    }
}