#pragma once
#include "systemc.h"

#include "common/config.h"
#include "common/msg.h"

using namespace std;

class config_helper_base {
public:
    int end_cores;

    vector<pair<int, int>> source_info; // 记录计算图开始需要推入才能触发的data
    vector<CoreConfig> coreconfigs;     // 记录所有核的工作配置，包括所有原语
    map<int, int> delta_offset;         // 用于记录每一个核的接收地址偏移

    int pipeline;    // 是否进行input连续输入，从而增加pipeline并行度，数值大小为需要连续进行的pipe段数
    bool sequential; // 是否进行顺序执行，即等待上一个done信号受到之后再进行下一个start
                     // data的发送（目前仅测试使用）
    int seq_index;   // 正在发放第几个start data包（仅在sequential为true时使用）

    virtual void fill_queue_config(queue<Msg> *q) = 0;
    virtual void fill_queue_start(queue<Msg> *q) = 0;
    void fill_queue_data(queue<Msg> *q);

    bool judge_is_end_core(int i);
    bool judge_is_end_work(CoreJob work);

    virtual void generate_prims(int i) = 0;
    void calculate_address(bool do_loop);

    virtual void print_self() = 0;
};