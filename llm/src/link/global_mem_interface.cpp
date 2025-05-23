#include <atomic>
#include <vector>

#include "link/chip_config_helper.h"
#include "link/global_mem_interface.h"
#include "nlohmann/json.hpp"
#include <fstream>

GlobalMemInterface::GlobalMemInterface(const sc_module_name &n, Event_engine *event_engine,const char *config_name, const char *font_ttf) 
    : sc_module(n), event_engine(event_engine) {
    config_helper = new chip_config_helper(config_name, font_ttf, 0);
    init(); 
    
    // Load and queue all global interface primitives
    load_global_prims(config_name, font_ttf);
    
    // Launch instruction execution thread
    SC_THREAD(task_logic);
    dont_initialize();
    
    // Load any global_interface primitives from the config
    // load_global_prims(config_name);

    // load_global_prims(config_name, font_ttf);
}

GlobalMemInterface::GlobalMemInterface(const sc_module_name &n, Event_engine *event_engine, config_helper_base *input_config){
    assert(0);
}

GlobalMemInterface::GlobalMemInterface() {
    init();
}

void GlobalMemInterface::init() {

    chipGlobalMemory = new ChipGlobalMemory(sc_gen_unique_name("chip-global-memory"), "../DRAMSys/configs/ddr4-example.json", "../DRAMSys/configs");
}

void GlobalMemInterface::load_global_prims(const char *config_name, const char *font_ttf) {
    // Populate global_instrs and queue from config_helper
    global_instrs.clear();
    global_instrs_queue.clear();
    for (auto instr : config_helper->instr_list) {
        global_instrs.push_back(instr);
        global_instrs_queue.emplace_back(instr);
    }
}

void GlobalMemInterface::task_logic() {
    using namespace std;
    // Execute each global instruction in order
    while (!global_instrs_queue.empty()) {
        chip_instr_base* p = global_instrs_queue.front();
        global_instrs_queue.pop_front();
        // Dispatch based on instruction type
        if (typeid(*p) == typeid(Recv_global_mem)) {
            Recv_global_mem* prim = static_cast<Recv_global_mem*>(p);
            cout << "[GlobalMemInterface] Recv_global_mem seq=" << prim->seq << endl;
            // TODO: insert global memory receive logic here
        } else if (typeid(*p) == typeid(Print_msg)) {
            Print_msg* prim = static_cast<Print_msg*>(p);
            cout << "[GlobalMemInterface] Print_msg: " << prim->message << endl;
        } else {
            cout << "[GlobalMemInterface] Unknown instruction: " << p->name << endl;
        }
        // Throttle between instructions
        wait(CYCLE, SC_NS);
    }
    // No more instructions, suspend thread
    while (true) wait();
}