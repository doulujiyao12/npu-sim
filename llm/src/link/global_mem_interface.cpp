#include <atomic>
#include <vector>

#include "link/global_mem_interface.h"


GlobalMemInterface::GlobalMemInterface(const sc_module_name &n, Event_engine *event_engine,
                                       const char *config_name, const char *font_ttf) {
    init();
}

GlobalMemInterface::GlobalMemInterface() {
    init();
}

void GlobalMemInterface::init() {

    chipGlobalMemory = new ChipGlobalMemory(sc_gen_unique_name("chip-global-memory"), "../DRAMSys/configs/ddr4-example.json", "../DRAMSys/configs");
    
}