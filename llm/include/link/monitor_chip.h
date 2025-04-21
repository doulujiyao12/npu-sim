#pragma once
#include "systemc.h"

#include "../router/router.h"
#include "../workercore/workercore.h"
#include "monitor/mem_interface.h"
#include "trace/Event_engine.h"
#include "link/chip_global_memory.h"
#include "link/config_top.h"

#include "link/base_component.h"
#include "link/monitor_top.h"
#include "trace/Event_engine.h"
#include "monitor/monitor.h"

using namespace std;

class ChipMonitor : public BaseComponent, public sc_module {
public:
    ChipMonitor(const sc_module_name &name, Event_engine *event_engine, BaseConfig *config, std::string font_ttf);
    ~ChipMonitor() = default;

    BaseConfig *config;

    std::string filename;
    std::string font_ttf;

    Type getType() const override {
        return Type::TYPE_CHIP;
    }

    Event_engine *event_engine;
    Monitor *monitor;

    void start_simu();

private:
    void init();
};