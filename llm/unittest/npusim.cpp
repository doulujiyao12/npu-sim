#include "assert.h"
#include "defs/global.h"
#include "defs/spec.h"
#include "monitor/monitor.h"
#include "systemc.h"
#include "trace/Event_engine.h"
#include "utils/print_utils.h"
#include "utils/simple_flags.h"
#include "utils/system_utils.h"
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>

// 假设 json.hpp 文件在当前目录或包含路径中
#include <nlohmann/json.hpp>
#include <string>

#include <SFML/Graphics.hpp>
using namespace std;

Define_bool_opt("--help", g_flag_help, false, "show these help information");

Define_string_opt("--workload-config", g_flag_workload_config,
                  "../llm/test/workload_config/gpu/pd_serving.json",
                  "workload config file");
Define_string_opt("--hardware-config", g_flag_hardware_config,
                  "../llm/test/hardware_config/core_4x4.json",
                  "hardware config file");
Define_string_opt("--simulation-config", g_flag_simulation_config,
                  "../llm/test/simulation_config/default_spec.json",
                  "simulation config file");

Define_float_opt("--comp-util", g_flag_comp_util, 0.7,
                 "computation and memory overlap");
Define_int64_opt("--trace-window", g_flag_trace_window, 2, "Trace window size");
Define_bool_opt("--gpu_cachelog", g_gpu_clog, false,
                "whether log gpu cache");
Define_int64_opt("--gpu_dram_bw", g_gpu_bw, 512,
                 "GPU bandwidth"); 
Define_int64_opt("--df_dram_bw", g_dram_bw, 8, 
                 "Dataflow per core bandwidth");
Define_int64_opt("--dram_burst_byte", g_dram_burst_byte, 2048,
                 "gpu dram burst byte");
Define_bool_opt("--gpu_inner", g_inner, false,
                "inner matmul or outer matmul, default outer");
Define_int64_opt("--dram_aligned", g_dram_aligned, 64, "gpu dram aligned");
Define_string_opt("--gpu_dram_config", g_gpu_dram_config,
                  "../DRAMSys/configs/hbm3-example.json", "gpu dram config");
Define_bool_opt("--use_gpu", g_use_gpu, false, "w/o use gpu mode");
Define_float_opt("--beha_dram_util", g_beha_dram_util, 0.7,
                 "dram bandwidth utilization in beha dram");
Define_int64_opt("--gpu_B", g_gpu_B, 1, "gpu batch size");

Define_int64_opt("--verbose-level", g_verbose_level, 1,
                 "verbose-level"); 


/**
 * 修改JSON文件中的nbrOfDevices值
 * @param inputPath 原JSON文件路径
 * @param outputPath 新JSON文件路径
 * @param x 要设置的nbrOfDevices值
 * @return 成功返回true，失败返回false
 */
bool modifyNbrOfDevices(const std::string &inputPath,
                        const std::string &outputPath, int x) {
    // 读取原始 JSON 文件
    std::ifstream infile(inputPath);
    if (!infile.is_open()) {
        std::cerr << "无法打开输入文件: " << inputPath << std::endl;
        return false;
    }

    json j;
    try {
        infile >> j;
    } catch (json::parse_error &e) {
        std::cerr << "JSON 解析错误: " << e.what() << std::endl;
        return false;
    }
    infile.close();

    // 修改 nbrOfDevices 的值
    j["memspec"]["memarchitecturespec"]["nbrOfDevices"] = x;

    // 写入新文件
    std::ofstream outfile(outputPath);
    if (!outfile.is_open()) {
        std::cerr << "无法创建输出文件: " << outputPath << std::endl;
        return false;
    }

    try {
        outfile << j.dump(4); // 格式化输出，缩进4个空格
    } catch (const std::exception &e) {
        std::cerr << "写入文件时发生错误: " << e.what() << std::endl;
        return false;
    }

    outfile.close();
    return true;
}


void generateAddressMapping(int n, const std::string &outputFilename) {
    // ROW_BIT 占 15 位，从 n+12 开始，最高位是 n+26，必须 <= 34
    json addressmapping;

    int byte_start = 0;
    int column_start = n;
    int bank_start = n + 22;
    int bankgroup_start = n + 24;
    int pseudo_start = n + 26;
    int row_start = n + 7;

    // BYTE_BIT
    std::vector<int> byte_bits;
    for (int i = 0; i < n; ++i)
        byte_bits.push_back(i);
    addressmapping["BYTE_BIT"] = byte_bits;

    // COLUMN_BIT (7 bits)
    std::vector<int> column_bits;
    for (int i = 0; i < 7; ++i)
        column_bits.push_back(column_start + i);
    addressmapping["COLUMN_BIT"] = column_bits;

    // BANK_BIT (2 bits)
    std::vector<int> bank_bits = {bank_start, bank_start + 1};
    addressmapping["BANK_BIT"] = bank_bits;

    // BANKGROUP_BIT (2 bits)
    std::vector<int> bankgroup_bits = {bankgroup_start, bankgroup_start + 1};
    addressmapping["BANKGROUP_BIT"] = bankgroup_bits;

    // PSEUDOCHANNEL_BIT (1 bit)
    addressmapping["PSEUDOCHANNEL_BIT"] = std::vector<int>{pseudo_start};

    // ROW_BIT (15 bits)
    std::vector<int> row_bits;
    for (int i = 0; i < 15; ++i) {
        int bit = row_start + i;
        row_bits.push_back(bit);
    }
    addressmapping["ROW_BIT"] = row_bits;

    // 写入文件
    json root;
    root["addressmapping"] = addressmapping;

    std::ofstream outFile(outputFilename);
    if (!outFile.is_open()) {
        std::cerr << "Error: Cannot write to file " << outputFilename
                  << std::endl;
        return;
    }
    outFile << std::setw(4) << root << std::endl;
    outFile.close();

    std::cout << "Address mapping with n=" << n << " saved to "
              << outputFilename << std::endl;
}


void generateDFAddressMapping(int n, const std::string &outputFilename) {
    // ROW_BIT 占 15 位，从 n+12 开始，最高位是 n+26，必须 <= 34
    json addressmapping;

    int byte_start = 0;
    int column_start = n;
    int bank_start = n + 27;
    int bankgroup_start = n + 25;
    int row_start = n + 10;

    // BYTE_BIT
    std::vector<int> byte_bits;
    for (int i = 0; i < n; ++i)
        byte_bits.push_back(i);
    addressmapping["BYTE_BIT"] = byte_bits;

    // COLUMN_BIT (10 bits)
    std::vector<int> column_bits;
    for (int i = 0; i < 10; ++i)
        column_bits.push_back(column_start + i);
    addressmapping["COLUMN_BIT"] = column_bits;

    // BANK_BIT (2 bits)
    std::vector<int> bank_bits = {bank_start, bank_start + 1};
    addressmapping["BANK_BIT"] = bank_bits;

    // BANKGROUP_BIT (2 bits)
    std::vector<int> bankgroup_bits = {bankgroup_start, bankgroup_start + 1};
    addressmapping["BANKGROUP_BIT"] = bankgroup_bits;

    // ROW_BIT (15 bits)
    std::vector<int> row_bits;
    for (int i = 0; i < 15; ++i) {
        int bit = row_start + i;
        row_bits.push_back(bit);
    }
    addressmapping["ROW_BIT"] = row_bits;

    // 写入文件
    json root;
    root["addressmapping"] = addressmapping;

    std::ofstream outFile(outputFilename);
    if (!outFile.is_open()) {
        std::cerr << "Error: Cannot write to file " << outputFilename
                  << std::endl;
        return;
    }
    outFile << std::setw(4) << root << std::endl;
    outFile.close();

    std::cout << "Address mapping with n=" << n << " saved to "
              << outputFilename << std::endl;
}


/**
 * @brief 根据给定的 nbrOfDevices 值生成一个 JSON 文件。
 *
 * @param A 要写入 nbrOfDevices 字段的整数值。
 * @param filename 要生成的 JSON 文件的名称（可选，默认为 "output.json"）。
 * @return bool 如果文件成功生成则返回 true，否则返回 false。
 */
bool generateGPUCacheJsonFile(int numDevices,
                              const std::string &filename = "output.json") {
    try {
        // 1. 创建 JSON 对象结构并填充初始数据
        json j = {
            {"memspec",
             {{"memarchitecturespec",
               {{"burstLength", 4},
                {"dataRate", 2},
                {"nbrOfBankGroups", 4},
                {"nbrOfBanks", 16},
                {"nbrOfColumns", 128},
                {"nbrOfPseudoChannels", 2},
                {"nbrOfRows", 32768},
                {"width", 64},
                // 注意：这里初始化为 32，但会被参数 A 覆盖
                {"nbrOfDevices", 32},
                {"nbrOfChannels", 1}

               }},
              {"memoryId", "https://www.computerbase.de/2019-05/"
                           "amd-memory-tweak-vram-oc/#bilder"},
              {"memoryType", "HBM2"},
              {"memtimingspec", {{"CCDL", 3},  {"CCDS", 2},    {"CKE", 8},
                                 {"DQSCK", 1}, {"FAW", 16},    {"PL", 0},
                                 {"RAS", 28},  {"RC", 42},     {"RCDRD", 12},
                                 {"RCDWR", 6}, {"REFI", 3900}, {"REFISB", 244},
                                 {"RFC", 220}, {"RFCSB", 96},  {"RL", 17},
                                 {"RP", 14},   {"RRDL", 6},    {"RRDS", 4},
                                 {"RREFD", 8}, {"RTP", 5},     {"RTW", 18},
                                 {"WL", 7},    {"WR", 14},     {"WTRL", 9},
                                 {"WTRS", 4},  {"XP", 8},      {"XS", 216},
                                 {"tCK", 1000}}}}}};

        // 2. 使用传入的参数 A 替换 nbrOfDevices 的值
        j["memspec"]["memarchitecturespec"]["nbrOfDevices"] = numDevices;

        // 3. 打开文件流用于写入
        std::ofstream outFile(filename);
        if (!outFile.is_open()) {
            std::cerr << "错误：无法打开文件 " << filename << " 进行写入。"
                      << std::endl;
            return false;
        }

        // 4. 将 JSON 对象序列化为格式化的字符串并写入文件
        // setw(4) 用于美化输出，使其具有缩进
        outFile << j.dump(4);
        outFile.close();

        std::cout << "JSON 文件已成功生成: " << filename << std::endl;
        return true;

    } catch (const std::exception &e) {
        std::cerr << "生成 JSON 文件时发生异常: " << e.what() << std::endl;
        return false;
    }
}

int sc_main(int argc, char *argv[]) {
    clock_t start = clock();

    srand((unsigned)time(NULL));
    std::cout.setf(std::ios::unitbuf);

    // 解析参数
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

    gpu_inner = g_inner;

    comp_util = g_flag_comp_util;
    verbose_level = g_verbose_level;
    dram_aligned = g_dram_aligned;
    gpu_dram_config = g_gpu_dram_config;
    gpu_clog = g_gpu_clog;

    gpu_bw = g_gpu_bw;
    g_default_dram_bw = g_dram_bw;
    use_gpu = g_use_gpu;
    beha_dram_util = g_beha_dram_util;
    gpu_B = g_gpu_B;

    modifyNbrOfDevices(
        "../DRAMSys/configs/memspec/JEDEC_4Gb_DDR4-1866_8bit_A.json",
        "../DRAMSys/configs/memspec/JEDEC_4Gb_DDR4-1866_8bit_DF.json",
        g_default_dram_bw);
    int bytecount_df = static_cast<int>(log2(g_dram_bw));
    generateDFAddressMapping(
        bytecount_df,
        "../DRAMSys/configs/addressmapping/am_ddr4_8x4Gbx8_df.json");

    if (g_gpu_bw == 512 || g_gpu_bw == 1024 || g_gpu_bw == 256 ||
        g_gpu_bw == 128 || g_gpu_bw == 64) {
        int numDevices = 32 * g_gpu_bw / 512; // 每个设备 32 个通道
        int bytecount = static_cast<int>(log2(g_gpu_bw)) - 1;

        cout << "GPU BW: " << DRAM_BURST_BYTE << " GB/s" << endl;
        generateGPUCacheJsonFile(numDevices,
                                 "../DRAMSys/configs/memspec/HBM2_GPU.json");
        generateAddressMapping(
            bytecount, "../DRAMSys/configs/addressmapping/am_hbm2_gpu.json");
        gpu_dram_config = "../DRAMSys/configs/gpu_hbm2.json";
    } else if (!SPEC_USE_BEHA_DRAM) {
        assert(false && "gpu bandwidth must be 512 1024 256");
    }

    // 清理所有上一次运行后产生的log文件
    DeleteCoreLogFiles();
    DeleteMemoryLogFiles();

    // 收集所有配置文件，统一解析
    g_workload_config = g_flag_workload_config;
    g_hardware_config = g_flag_hardware_config;
    g_simulation_config = g_flag_simulation_config;
    InitGrid(g_workload_config, g_hardware_config, g_simulation_config);
    InitGlobalMembers();

    // init_dram_areas();
    // initialize_cache_structures();

    Event_engine *event_engine =
        new Event_engine("event-engine", g_flag_trace_window);
    Monitor monitor("monitor", event_engine, g_workload_config.c_str());
    sc_trace_file *tf = sc_create_vcd_trace_file("Cchip_1");
    sc_clock clk("clk", CYCLE, SC_NS);

    sc_start();

    // destroy_dram_areas();
    // destroy_cache_structures();
    // event_engine->dump_traced_file();
    sc_close_vcd_trace_file(tf);

    SystemCleanup();
    close_log_files();

    clock_t end = clock();
    cout << "花费了" << (double)(end - start) / CLOCKS_PER_SEC << "秒" << endl;
    ofstream outfile("simulation_result_df_pd.txt", ios::app);
    if (outfile.is_open()) {
        outfile << "花费了" << (double)(end - start) / CLOCKS_PER_SEC << "秒"
                << endl;
        outfile.close();
    } else {
        cout << "Error: Unable to open file for writing timestamp." << endl;
    }
    delete event_engine;
    return 0;
}