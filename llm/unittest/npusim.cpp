#include "assert.h"
#include "defs/global.h"
#include "monitor/monitor.h"
#include "systemc.h"
#include "trace/Event_engine.h"
#include "utils/file_utils.h"
#include "utils/simple_flags.h"
#include "utils/system_utils.h"
#include <ctime>
#include <iostream>
#include <filesystem>
#include <iostream>

#include <SFML/Graphics.hpp>
using namespace std;

#define CHECK_C cout << total_cycle << endl;
Define_bool_opt("--help", g_flag_help, false, "show these help information");
Define_bool_opt("--node-mode", g_flag_node, false, "whether to sim in a node");
Define_string_opt("--config-file", g_flag_config_file,
                  "../llm/test/config_gpt2_small.json", "config file");
Define_string_opt("--core-config-file", g_flag_core_config_file,
                  "../llm/test/core_4x4.json", "core config file");
Define_string_opt("--ttf-file", g_flag_ttf, "../font/NotoSansDisplay-Bold.ttf",
                  "font ttf file");
Define_bool_opt("--use-dramsys", g_flag_dramsys, true,
                "whether to use DRAMSys");
Define_float_opt("--comp-util", g_flag_comp_util, 0.7,
                 "computation and memory overlap");
Define_int64_opt("--MAC-SIZE", g_flag_mac_size, 128, "MAC size");
Define_int64_opt("--trace-window", g_flag_trace_window, 2, "Trace window size");
Define_int64_opt("--sram-max", g_flag_max_sram, 8388608,
                 "Max SRAM size"); // 3145728
Define_int64_opt("--verbose-level", g_verbose_level, 1,
                 "verbose-level"); // 3145728
// ----------------------------------------------------------------------------
// all the individual layers' forward and backward passes
// B = batch_size, T = sequence_length, C = channels, V = vocab_size


// gpt2 架构

// https://www.bilibili.com/read/cv36513074/
// https://zhuanlan.zhihu.com/p/108231904

void delete_core_log_files() {
    const std::string current_dir = ".";  // Current working directory
    try {
        for (const auto& entry : std::filesystem::directory_iterator(current_dir)) {
            if (entry.is_regular_file() && 
                entry.path().filename().string().find("core_") == 0 &&
                entry.path().extension() == ".log") {
                std::filesystem::remove(entry.path());
                std::cout << "Deleted log file: " << entry.path().filename().string() << std::endl;
            }
        }
    } catch (const std::filesystem::filesystem_error& e) {
        std::cerr << "Error deleting log files: " << e.what() << std::endl;
    }
}
int sc_main(int argc, char *argv[]) {
    clock_t start = clock();

    srand((unsigned)time(NULL));
    std::cout.setf(std::ios::unitbuf);

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
        cout << "unknown option(s): " << content.c_str() << endl;
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
    MAX_SRAM_SIZE = g_flag_max_sram;
    verbose_level = g_verbose_level;
    delete_core_log_files();

    init_grid(g_flag_config_file.c_str(), g_flag_core_config_file.c_str());
    init_global_members();

    init_dram_areas();
    initialize_cache_structures();
    init_perf_counters();

    Event_engine *event_engine =
        new Event_engine("event-engine", g_flag_trace_window);
    Monitor monitor("monitor", event_engine, g_flag_config_file.c_str(),
                    g_flag_ttf.c_str());
    sc_trace_file *tf = sc_create_vcd_trace_file("Cchip_1");
    sc_clock clk("clk", CYCLE, SC_NS);
    // sc_trace(tf, clk, "clk");
    // sc_trace(tf, monitor.memInterface->host_data_sent_i[0],
    // "monitor.memInterface->host_data_sent_i[0]"); sc_trace(tf,
    // monitor.memInterface->host_data_sent_i[1],
    // "monitor.memInterface->host_data_sent_i[1]"); sc_trace(tf,
    // monitor.memInterface->host_data_sent_i[2],
    // "monitor.memInterface->host_data_sent_i[2]"); sc_trace(tf,
    // monitor.memInterface->host_data_sent_i[3],
    // "monitor.memInterface->host_data_sent_i[3]");
    sc_start();

    destroy_dram_areas();
    destroy_cache_structures();
    // event_engine->dump_traced_file();
    sc_close_vcd_trace_file(tf);

    system_cleanup();
    close_log_files();

    clock_t end = clock();
    cout << "花费了" << (double)(end - start) / CLOCKS_PER_SEC << "秒" << endl;

    delete event_engine;
    return 0;
}