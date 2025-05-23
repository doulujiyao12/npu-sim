#include "utils/prim_utils.h"
#include "prims/comp_prims.h"
#include "prims/gpu_prims.h"
#include "prims/norm_prims.h"
#include "prims/pd_prims.h"
#include "prims/prim_base.h"
#include "systemc.h"

prim_base *new_prim(string type) {
    prim_base *prim = nullptr;

    if (type == "Dummy_p")
        prim = new Dummy_p();
    else if (type == "Layernorm_f")
        prim = new Layernorm_f();
    else if (type == "Attention_f")
        prim = new Attention_f();
    else if (type == "Conv_f")
        prim = new Conv_f();
    else if (type == "Gelu_f")
        prim = new Gelu_f();
    else if (type == "Matmul_f")
        prim = new Matmul_f();
    else if (type == "Relu_f")
        prim = new Relu_f();
    else if (type == "Max_pool")
        prim = new Max_pool();
    else if (type == "Residual_f")
        prim = new Residual_f();
    else if (type == "Batchnorm_f")
        prim = new Batchnorm_f();
    else if (type == "Load_prim")
        prim = new Load_prim();
    else if (type == "Merge_conv")
        prim = new Merge_conv();
    else if (type == "Merge_matmul")
        prim = new Merge_matmul();
    else if (type == "Receive_prim")
        prim = new Recv_prim();
    else if (type == "Relu_f")
        prim = new Relu_f();
    else if (type == "Send_prim")
        prim = new Send_prim();
    else if (type == "Split_conv")
        prim = new Split_conv();
    else if (type == "Split_matmul")
        prim = new Split_matmul();
    else if (type == "Store_prim")
        prim = new Store_prim();
    else if (type == "Attention_f_decode")
        prim = new Attention_f_decode();
    else if (type == "Attention_f_prefill")
        prim = new Attention_f_prefill();
    else if (type == "Matmul_f_decode")
        prim = new Matmul_f_decode();
    else if (type == "Matmul_f_prefill")
        prim = new Matmul_f_prefill();
    else if (type == "Set_addr")
        prim = new Set_addr();
    else if (type == "Clear_sram")
        prim = new Clear_sram();
    else if (type == "Matmul_f_gpu")
        prim = new Matmul_f_gpu();
    else if (type == "Attention_f_gpu")
        prim = new Attention_f_gpu();
    else if (type == "Gelu_f_gpu")
        prim = new Gelu_f_gpu();
    else if (type == "Layernorm_f_gpu")
        prim = new Layernorm_f_gpu();
    else if (type == "Residual_f_gpu")
        prim = new Residual_f_gpu();
    else if (type == "Matmul_f_pd")
        prim = new matmul_forward_pd();
    else if (type == "Send_global_memory")
        prim = new Send_global_memory();
    else if (type == "Recv_global_memory")
        prim = new Recv_global_memory();
    else if (type == "switch_data")
        prim = new switch_data();

    else {
        cout << "Parse config prim: Not Implemented.\n";

        sc_stop();
    }

    global_prim_stash.push_back(prim);

    return prim;
}

// 打印枚举的变量名
std::string get_send_type_name(SEND_TYPE type) {
    const std::unordered_map<SEND_TYPE, std::string> SEND_TYPE_NAMES = {
        {SEND_ACK, "SEND_ACK"},   {SEND_REQ, "SEND_REQ"},
        {SEND_DATA, "SEND_DATA"}, {SEND_SRAM, "SEND_SRAM"},
        {SEND_DONE, "SEND_DONE"},
    };

    auto it = SEND_TYPE_NAMES.find(type);
    if (it != SEND_TYPE_NAMES.end()) {
        return it->second; // 返回映射值
    }
    return "Unknown SEND_TYPE"; // 如果没找到，返回一个默认值
}

// 获取枚举的名称
std::string get_recv_type_name(RECV_TYPE type) {
    const std::unordered_map<RECV_TYPE, std::string> RECV_TYPE_NAMES = {
        {RECV_TYPE::RECV_CONF, "RECV_CONF"},
        {RECV_TYPE::RECV_ACK, "RECV_ACK"},
        {RECV_TYPE::RECV_FLAG, "RECV_FLAG"},
        {RECV_TYPE::RECV_DATA, "RECV_DATA"},
        {RECV_TYPE::RECV_SRAM, "RECV_SRAM"},
        {RECV_TYPE::RECV_WEIGHT, "RECV_WEIGHT"},
        {RECV_TYPE::RECV_START, "RECV_START"}
    };

    auto it = RECV_TYPE_NAMES.find(type);
    if (it != RECV_TYPE_NAMES.end()) {
        return it->second;
    }
    return "Unknown RECV_TYPE";
}