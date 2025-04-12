#include "systemc.h"

#include "common/memory.h"
#include "defs/global.h"
#include "prims/comp_base.h"
#include "prims/norm_prims.h"
#include "utils/system_utils.h"

void Set_Sram::print_self(string prefix) {
    cout << prefix << "<set_sram>\n";
    cout << prefix << "\tSram_addr: " << sram_addr << endl;
}

int Set_Sram::sram_utilization(DATATYPE datatype) { return 0; }

void Set_Sram::parse_json(json j) { sram_addr = find_var(j["sram_addr"]); }

void Set_Sram::deserialize(sc_bv<128> buffer) {
    sram_addr = buffer.range(31, 8).to_uint64();
    datatype = (DATATYPE)buffer.range(33, 32).to_uint64();

    int offset = 34;
    for (int i = 0; i < MAX_SPLIT_NUM; i++) {
        datapass_label->indata[i] = g_sram_label_table.findRecord(buffer.range(offset + 7, offset).to_uint64());
        offset += 8;
    }
    datapass_label->outdata = g_sram_label_table.findRecord(buffer.range(offset + 7, offset).to_uint64());
}

sc_bv<128> Set_Sram::serialize() {
    sc_bv<128> d;
    d.range(7, 0) = sc_bv<8>(0xd1);
    d.range(31, 8) = sc_bv<24>(sram_addr);
    d.range(33, 32) = sc_bv<2>(datatype);

    int offset = 34;
    for (int i = 0; i < MAX_SPLIT_NUM; i++) {
        d.range(offset + 7, offset) = sc_bv<8>(g_sram_label_table.addRecord(datapass_label->indata[i]));
        offset += 8;
    }
    d.range(offset + 7, offset) = sc_bv<8>(g_sram_label_table.addRecord(datapass_label->outdata));

    return d;
}
int Set_Sram::task_core(TaskCoreContext &context) {
    // std::cout << "Set Sram333 " << sram_addr << " to " << context.sram_addr
    // << endl;
    //  CTODO: 取消注释
    //  *(context.sram_addr) = sram_addr;
    //  将datapass_label的内容复制到target中
    for (int i = 0; i < MAX_SPLIT_NUM; i++) {
        target->indata[i] = datapass_label->indata[i];
    }
    target->outdata = datapass_label->outdata;

    char format_label[100];
    sprintf(format_label, "#%d", context.loop_cnt);
    string label_suffix = format_label;
    for (int i = 0; i < MAX_SPLIT_NUM; i++) {
        if (target->indata[i] != UNSET_LABEL && target->indata[i] != DRAM_LABEL) {
            target->indata[i] += label_suffix;
        }
    }
    target->outdata += label_suffix;

    return 0;
}
int Set_Sram::task() { return 0; }