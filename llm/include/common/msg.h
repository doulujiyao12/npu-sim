#pragma once
#include "systemc.h"

#include "defs/enums.h"
#include "macros/macros.h"

// 以下为数据包相关
class Msg {
public:
    bool is_end;       // 是否为最后一个包
    MSG_TYPE msg_type; // 消息类型
    int seq_id;        // 包序号
    int des;           // 目标id
    int offset;        // 目标地址偏移
    int tag_id;        // send & recv对应的tag编号
    int source;        // 发送此msg的core id
    int length;        // 真实数据的长度，避免end包覆盖
    bool refill;       // 在end包中表示是否需要refill
    sc_bv<128> data;

    Msg(bool e, MSG_TYPE m, int seq, int des, int offset, int tag, int length,
        sc_bv<128> d)
        : is_end(e),
          msg_type(m),
          seq_id(seq),
          des(des),
          offset(offset),
          tag_id(tag),
          length(length),
          data(d) {}
    Msg(bool e, MSG_TYPE m, int s, int des, sc_bv<128> d)
        : is_end(e), msg_type(m), seq_id(s), des(des), data(d) {}
    Msg(MSG_TYPE m, int des, int tag, int source)
        : msg_type(m), des(des), tag_id(tag), source(source) {} // 用于REQ和ACK
    Msg(MSG_TYPE m, int des, int source)
        : msg_type(m), des(des), source(source) {} // 用于DONE
    Msg() {
        seq_id = -1;
        des = -1;
        is_end = false;
        msg_type = CONFIG;
        data = sc_bv<128>(0x1);
    }

    bool operator<(const Msg &other) const { return other.seq_id < seq_id; }
};
