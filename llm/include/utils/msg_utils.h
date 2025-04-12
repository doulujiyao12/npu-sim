#pragma once
#include "common/msg.h"

// 此文件用于消息（数据包）相关的工具函数

sc_bv<256> serialize_msg(Msg msg);
Msg deserialize_msg(sc_bv<256> buffer);

MSG_TYPE get_msg_type(sc_bv<256> buffer);
int get_msg_des_id(sc_bv<256> buffer);
int get_msg_tag_id(sc_bv<256> buffer);