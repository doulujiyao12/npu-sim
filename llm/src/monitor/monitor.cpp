#include "monitor/monitor.h"

Monitor::Monitor(const sc_module_name &n, Event_engine *event_engine, const char *config_name, const char *font_ttf) : sc_module(n), event_engine(event_engine) {
    memInterface = new MemInterface("mem-interface", this->event_engine, config_name, font_ttf);
    init();
}

Monitor::Monitor(const sc_module_name &n, Event_engine *event_engine, config_helper_base *input_config) : sc_module(n), event_engine(event_engine) {
    memInterface = new MemInterface("mem-interface", this->event_engine, input_config);
    init();
}


Monitor::~Monitor() {
    delete[] core_busy;
    delete[] rc_channel;
    delete[] rc_data_sent;
    for (int i = 0; i < DIRECTIONS; i++) {
        delete[] channel[i];
        delete[] channel_avail[i];
        delete[] data_sent[i];
    }

    delete[] host_channel_avail;
    delete[] host_data_sent_i;
    delete[] host_data_sent_o;
    delete[] host_channel_i;
    delete[] host_channel_o;

    delete routerMonitor;
    delete workerCores;
    delete memInterface;
}

void Monitor::init(){
    routerMonitor = new RouterMonitor("router-monitor", this->event_engine);
    // memInterface = new MemInterface("mem-interface", this->event_engine, config_name, font_ttf);
    workerCores = new WorkerCore *[GRID_SIZE];
    for (int i = 0; i < GRID_SIZE; i++) {
        workerCores[i] = new WorkerCore(sc_gen_unique_name("workercore"), i, this->event_engine);
    }

#if USE_L1L2_CACHE == 1
    // GPU
    vector<L1Cache *> l1caches;
    vector<GPUNB_dcacheIF *> processors;
    for (int i = 0; i < GRID_SIZE; i++) {
        l1caches.push_back(workerCores[i]->executor->core_lv1_cache);
        processors.push_back(workerCores[i]->executor->gpunb_dcache_if);

    }
    cacheSystem = new L1L2CacheSystem("l1l2-cache_system", GRID_SIZE, l1caches, processors);
#else
#endif

    memInterface->start_i(star);
    memInterface->preparations_done_o(config_done);
    preparations_done_i(config_done);
    start_o(star);

    // bind ports to signals
    core_busy = new sc_signal<bool>[GRID_SIZE];
    rc_channel = new sc_signal<sc_bv<256>>[GRID_SIZE];
    rc_data_sent = new sc_signal<bool>[GRID_SIZE];

    host_channel_avail = new sc_signal<bool>[GRID_X];
    host_data_sent_i = new sc_signal<bool>[GRID_X];
    host_data_sent_o = new sc_signal<bool>[GRID_X];
    host_channel_i = new sc_signal<sc_bv<256>>[GRID_X];
    host_channel_o = new sc_signal<sc_bv<256>>[GRID_X];

    for (int i = 0; i < DIRECTIONS; i++) {
        channel[i] = new sc_signal<sc_bv<256>>[GRID_SIZE];
        channel_avail[i] = new sc_signal<bool>[GRID_SIZE];
        data_sent[i] = new sc_signal<bool>[GRID_SIZE];
    }

    // host & router
    for (int i = 0; i < GRID_X; i++) {
        int rid = i * GRID_X; // 边缘core的id
        RouterUnit *ru = routerMonitor->routers[rid];

        memInterface->host_channel_avail_i[i](host_channel_avail[i]);
        (*ru->host_channel_avail_o)(host_channel_avail[i]);
        memInterface->host_data_sent_i[i](host_data_sent_i[i]);
        (*ru->host_data_sent_o)(host_data_sent_i[i]);
        memInterface->host_data_sent_o[i](host_data_sent_o[i]);
        (*ru->host_data_sent_i)(host_data_sent_o[i]);
        memInterface->host_channel_i[i](host_channel_i[i]);
        (*ru->host_channel_o)(host_channel_i[i]);
        memInterface->host_channel_o[i](host_channel_o[i]);
        (*ru->host_channel_i)(host_channel_o[i]);
    }

    // core & router
    for (int i = 0; i < GRID_SIZE; i++) {
        RouterUnit *ru = routerMonitor->routers[i];
        WorkerCoreExecutor *wc = workerCores[i]->executor;

        ru->core_busy_i(core_busy[i]);
        wc->core_busy_o(core_busy[i]);

        ru->channel_avail_o[CENTER](channel_avail[CENTER][i]);
        wc->channel_avail_i(channel_avail[CENTER][i]);
        ru->data_sent_o[CENTER](data_sent[CENTER][i]);
        wc->data_sent_i(data_sent[CENTER][i]);
        ru->data_sent_i[CENTER](rc_data_sent[i]);
        wc->data_sent_o(rc_data_sent[i]);

        ru->channel_o[CENTER](channel[CENTER][i]);
        wc->channel_i(channel[CENTER][i]);
        ru->channel_i[CENTER](rc_channel[i]);
        wc->channel_o(rc_channel[i]);
    }

    // router & router
    for (int j = 0; j < GRID_SIZE; j++) {
        RouterUnit *pos = routerMonitor->routers[j];

        for (int i = 0; i < DIRECTIONS - 1; i++) {
            Directions input_dir = get_oppose_direction(Directions(i));
            int input_source = get_input_source(Directions(i), j);

            pos->channel_o[i](channel[i][j]);
            pos->channel_i[i](channel[input_dir][input_source]);

            pos->channel_avail_o[i](channel_avail[i][j]);
            pos->channel_avail_i[i](channel_avail[input_dir][input_source]);

            pos->data_sent_o[i](data_sent[i][j]);
            pos->data_sent_i[i](data_sent[input_dir][input_source]);
        }
    }

    cout << "Components initialize complete, prepare to start.\n";

    SC_THREAD(start_simu);
}

void Monitor::start_simu() {
    // Msg t;
    // t.des = 0;
    // t.msg_type = DATA;
    // t.is_end = true;

    // 开始分发配置
    start_o.write(true);
    wait(preparations_done_i.posedge_event());

    // 开始发送数据
    // memInterface->clear_write_buffer();
    // memInterface->write_buffer[0].push(Msg(START, 0, 0));
    // memInterface->ev_write.notify(CYCLE, SC_NS);
}