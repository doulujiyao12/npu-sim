#include "link/monitor_top.h"
#include "link/config_top.h"
#include "link/monitor_chip.h"
#include "monitor/monitor.h"


TopMonitor::TopMonitor(sc_module_name name, Event_engine *event_engine,
                       TopConfig *config, std::string font_ttf)
    : sc_module(name),
      config(config),
      event_engine(event_engine),
      font_ttf(font_ttf) {
    init();
}

TopMonitor::TopMonitor(sc_module_name name, Event_engine *event_engine,
                       std::string config_file, std::string font_ttf)
    : sc_module(name), event_engine(event_engine), font_ttf(font_ttf) {
    config = new TopConfig(config_file, font_ttf);
    // TopMonitor(name, config);
    init();
}

void TopMonitor::init() {
    // 初始化组件
    assert(config != nullptr && "config is nullptr");

    for (auto config_ptr : config->component_) {
        switch (config_ptr->getType()) {
        case BaseConfig::TYPE_CHIP:
            // BaseComponent *base_component = new ChipMonitor("chip_monitor",
            // event_engine, config_ptr, font_ttf);
            components.push_back(new ChipMonitor("chip_monitor", event_engine,
                                                 config_ptr, font_ttf));
            break;
        default:
            assert(0 && "not implemented yet");
            break;
        }
    }
}