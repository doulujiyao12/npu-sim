#pragma once
#include <vector>

#include "common/pd.h"
#include "monitor/config_helper_base.h"

using namespace std;

class config_helper_pd : public config_helper_base {
public:
    json json_template;
    vector<CoreStatus> coreStatus;
    vector<RequestRecord> requestRecords;
    int decode_done; // 收到decode的eof完成次数
    vector<Msg> temp_config; // 存放所有还没有发出去的config
    vector<queue<int>> idle_decode; // 由于超过credit而需要被stall的decode

    // 模型配置
    int heads;
    double eof_chance;
    int model_stage;

    config_helper_pd(string filename, string font_ttf, int config_chip_id = 0);

    config_helper_pd *clone() const override {
        return new config_helper_pd(*this);
    }

    void generate_prims(int i);
    void calculate_address(bool do_loop);

    void print_self();

    void random_core(string font_ttf);

    void fill_queue_start(queue<Msg> *q);
    void fill_queue_config(queue<Msg> *q);

    void iter_start(); // 填充原语，发送在meminterface完成
    void iter_done(vector<Msg> done_msg);
};