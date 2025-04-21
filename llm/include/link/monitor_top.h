#pragma once
#include "systemc.h"

#include "../router/router.h"
#include "../workercore/workercore.h"
#include "monitor/mem_interface.h"
#include "trace/Event_engine.h"
#include "link/chip_global_memory.h"
#include "link/config_top.h"

#include "link/base_component.h"

using namespace std;

class TopConfig;
class TopMonitor : public BaseComponent, public sc_module {
public:
    TopMonitor(sc_module_name name, Event_engine *event_engine, TopConfig *config, std::string font_ttf);
    TopMonitor(sc_module_name name, Event_engine *event_engine, std::string config_file, std::string font_ttf);
    ~TopMonitor() = default;

    TopConfig *config;
    std::string font_ttf;

    Event_engine *event_engine;

    std::vector<BaseComponent *> components;

    Type getType() const override {
        return Type::TYPE_TOP;
    }
 
private:
    void init();
};