#pragma once
#include "systemc.h"

#include "../router/router.h"
#include "../workercore/workercore.h"
#include "monitor/gpu_cache_system.h"
#include "monitor/mem_interface.h"
#include "trace/Event_engine.h"
// #include "../link/global_memory.h"
using namespace std;

class Monitor : public sc_module {
public:
    // signals
    sc_signal<bool> *core_busy;
    sc_signal<sc_bv<256>> *channel[DIRECTIONS];
    sc_signal<sc_bv<256>> *rc_channel;
    sc_signal<bool> *channel_avail[DIRECTIONS];
    sc_signal<bool> *data_sent[DIRECTIONS];
    sc_signal<bool> *rc_data_sent;

    sc_signal<bool> *host_channel_avail;
    sc_signal<bool> *host_data_sent_i;
    sc_signal<bool> *host_data_sent_o;
    sc_signal<sc_bv<256>> *host_channel_i;
    sc_signal<sc_bv<256>> *host_channel_o;

    sc_signal<bool> star;
    sc_signal<bool> config_done;
    sc_out<bool> start_o;
    sc_in<bool> preparations_done_i;

    // components
    RouterMonitor *routerMonitor;
    WorkerCore **workerCores;
    MemInterface *memInterface;

#if USE_L1L2_CACHE == 1
    L1L2CacheSystem *cacheSystem;
#else
#endif

    Event_engine *event_engine;

    SC_HAS_PROCESS(Monitor);
    Monitor(const sc_module_name &n, Event_engine *event_engine, const char *config_name, const char *font_ttf);
    ~Monitor();

    void start_simu();
};