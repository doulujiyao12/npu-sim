#include "utils/msg_utils.h"

sc_bv<256> serialize_msg(Msg msg) {
    sc_bv<256> serialized_msg;

    int pos = 0;

    serialized_msg.range(pos + M_D_IS_END - 1, pos) =
        sc_bv<M_D_IS_END>(msg.is_end);
    pos += M_D_IS_END;
    serialized_msg.range(pos + M_D_MSG_TYPE - 1, pos) =
        sc_bv<M_D_MSG_TYPE>(int(msg.msg_type));
    pos += M_D_MSG_TYPE;
    serialized_msg.range(pos + M_D_SEQ_ID - 1, pos) =
        sc_bv<M_D_SEQ_ID>(msg.seq_id);
    pos += M_D_SEQ_ID;
    serialized_msg.range(pos + M_D_DES - 1, pos) = sc_bv<M_D_DES>(msg.des);
    pos += M_D_DES;
    serialized_msg.range(pos + M_D_OFFSET - 1, pos) =
        sc_bv<M_D_OFFSET>(msg.offset);
    pos += M_D_OFFSET;
    serialized_msg.range(pos + M_D_TAG_ID - 1, pos) =
        sc_bv<M_D_TAG_ID>(msg.tag_id);
    pos += M_D_TAG_ID;
    serialized_msg.range(pos + M_D_SOURCE - 1, pos) =
        sc_bv<M_D_SOURCE>(msg.source);
    pos += M_D_SOURCE;
    serialized_msg.range(pos + M_D_LENGTH - 1, pos) =
        sc_bv<M_D_LENGTH>(msg.length);
    pos += M_D_LENGTH;
    serialized_msg.range(pos + M_D_REFILL - 1, pos) =
        sc_bv<M_D_REFILL>(msg.refill);
    pos += M_D_REFILL;
    serialized_msg.range(pos + M_D_DATA - 1, pos) = msg.data;
    pos += M_D_DATA;
    serialized_msg.range(255, pos) = sc_bv<32>(0);

    return serialized_msg;
}

Msg deserialize_msg(sc_bv<256> buffer) {
    Msg msg;

    int pos = 0;

    msg.is_end = buffer.range(pos + M_D_IS_END - 1, pos).to_uint64(),
    pos += M_D_IS_END;
    msg.msg_type =
        MSG_TYPE(buffer.range(pos + M_D_MSG_TYPE - 1, pos).to_uint64()),
    pos += M_D_MSG_TYPE;
    msg.seq_id = buffer.range(pos + M_D_SEQ_ID - 1, pos).to_uint64(),
    pos += M_D_SEQ_ID;
    msg.des = buffer.range(pos + M_D_DES - 1, pos).to_uint64(), pos += M_D_DES;
    msg.offset = buffer.range(pos + M_D_OFFSET - 1, pos).to_uint64(),
    pos += M_D_OFFSET;
    msg.tag_id = buffer.range(pos + M_D_TAG_ID - 1, pos).to_uint64(),
    pos += M_D_TAG_ID;
    msg.source = buffer.range(pos + M_D_SOURCE - 1, pos).to_uint64(),
    pos += M_D_SOURCE;
    msg.length = buffer.range(pos + M_D_LENGTH - 1, pos).to_uint64(),
    pos += M_D_LENGTH;
    msg.refill = buffer.range(pos + M_D_REFILL - 1, pos).to_uint64(),
    pos += M_D_REFILL;
    msg.data = buffer.range(pos + M_D_DATA - 1, pos);
    
    return msg;
}

MSG_TYPE get_msg_type(sc_bv<256> buffer) {
    int pos = 0;
    pos += M_D_IS_END;

    return MSG_TYPE(buffer.range(pos + M_D_MSG_TYPE - 1, pos).to_uint64());
}

int get_msg_des_id(sc_bv<256> buffer) {
    int pos = 0;
    pos += M_D_IS_END + M_D_MSG_TYPE + M_D_SEQ_ID;

    return buffer.range(pos + M_D_DES - 1, pos).to_uint64();
}

int get_msg_tag_id(sc_bv<256> buffer) {
    int pos = 0;
    pos += M_D_IS_END + M_D_MSG_TYPE + M_D_SEQ_ID + M_D_DES + M_D_OFFSET;

    return MSG_TYPE(buffer.range(pos + M_D_TAG_ID - 1, pos).to_uint64());
}