#pragma once
#include "prims/comp_prims.h"
#include "prims/gpu_prims.h"
#include "prims/norm_prims.h"
#include "prims/prim_base.h"
#include "systemc.h"

bool is_comp_prim(prim_base *p);
bool is_gpu_prim(prim_base *p);
bool is_pd_prim(prim_base *p);
prim_base *new_prim(string type);

// 打印枚举的变量名
std::string get_send_type_name(SEND_TYPE type);

// 获取枚举的名称
std::string get_recv_type_name(RECV_TYPE type);