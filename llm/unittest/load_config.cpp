#include "assert.h"
#include "defs/global.h"
#include "monitor/monitor.h"
#include "systemc.h"
#include "trace/Event_engine.h"
#include "utils/file_utils.h"
#include "utils/simple_flags.h"
#include "utils/system_utils.h"
#include "link/monitor_top.h"
#include <iostream>

#include "link/config_top.h"

#include <SFML/Graphics.hpp>
using namespace std;

#define CHECK_C cout << total_cycle << endl;
Define_bool_opt("--help", g_flag_help, false, "show these help information");
Define_bool_opt("--node-mode", g_flag_node, false, "whether to sim in a node");
Define_string_opt("--config-file", g_flag_config_file, "../llm/test/config_gpt2_small_cluster.json", "config file");
Define_string_opt("--ttf-file", g_flag_ttf, "../font/NotoSansDisplay-Bold.ttf", "font ttf file");
Define_bool_opt("--use-dramsys", g_flag_dramsys, true, "whether to use DRAMSys");
Define_float_opt("--comp_util", g_flag_comp_util, 0.7, "computation and memory overlap");
Define_int64_opt("--MAC_SIZE", g_flag_mac_size, 128, "MAC size");

int sc_main(int argc, char *argv[]){
    
    std::cout << "[TEST] Loading Config" << std::endl;
    use_node = false;
    use_DramSys = false;

    simple_flags::parse_args(argc, argv);
    if (!simple_flags::get_unknown_flags().empty()) {
        string content;
        for (auto it : simple_flags::get_unknown_flags()) {
            content += "'" + it + "', ";
        }
        content.resize(content.size() - 2); // remove last ', '
        content.append(".");
        cout << "unknown option(s): " << content.c_str();
        return -1;
    }

    if (g_flag_help) {
        simple_flags::print_args_info();
        return 0;
    }

    if (g_flag_dramsys) {
        use_DramSys = true;
    }

    if (g_flag_node) {
        use_node = true;
    }

    comp_util = g_flag_comp_util;

    tile_exu.x_dims = g_flag_mac_size;
    tile_exu.y_dims = g_flag_mac_size;
    tile_sfu.x_dims = g_flag_mac_size * 16;

    init_grid(g_flag_config_file.c_str());
    init_global_members();
    init_dram_areas();
    initialize_cache_structures();
    init_perf_counters();
    
    TopConfig *top_config = new TopConfig(g_flag_config_file.c_str(), g_flag_ttf.c_str());
    // TopConfig top_config(g_flag_config_file.c_str());
    top_config->print_self();

    Event_engine *event_engine = new Event_engine("event-engine");
    TopMonitor *top_monitor = new TopMonitor("top_monitor", event_engine, top_config, g_flag_ttf.c_str());
    sc_trace_file *tf = sc_create_vcd_trace_file("Cchip_1");    
    sc_clock clk("clk", CYCLE, SC_NS);

    sc_start();
    
    // destroy_dram_areas();
    // destroy_cache_structures();
    // sc_close_vcd_trace_file(tf);
    // system_cleanup();
    

    delete event_engine;
    delete top_config;
    return 0;
}