#pragma once
#include "common/msg.h"

// 消息（数据包）的工具函数
sc_bv<256> SerializeMsg(Msg msg);
Msg DeserializeMsg(sc_bv<256> buffer);