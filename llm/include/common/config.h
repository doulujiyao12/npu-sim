#pragma once
#include <vector>

#include "defs/enums.h"
#include "prims/comp_base.h"
#include "prims/prim_base.h"

class Cast {
public:
    int dest;
    int tag;
    int weight;
    int addr;
    bool critical;
    LOOP_TYPE loopout;

    Cast() {}
    Cast(int dest) : dest(dest), tag(tag) {
        weight = 1;
        addr = -1;
        critical = false;
        loopout = BOTH;
    }
};

void from_json(const json &j, Cast &c);

class CoreJob {
public:
    vector<Cast> cast;

    int recv_cnt;
    int recv_tag;

    vector<prim_base *> prims;
    vector<prim_base *> prims_last_loop;
    vector<prim_base *> prims_in_loop;

    int loop;

    void print_self();
    CoreJob() {}
    CoreJob(int recv_cnt, int recv_tag, int loop)
        : recv_cnt(recv_cnt), recv_tag(recv_tag), loop(loop) {
        Cast new_cast;
        new_cast.dest = -1;
        cast.push_back(new_cast);
    }
};

void from_json(const json &j, CoreJob &c);


class CoreConfig {
public:
    int id;

    bool prim_refill; // 是否通过原语重填的方式实现循环
    int prim_copy;    // 是否需要复制其他核的计算原语
    int send_global_mem; //是否需要将计算结果发送给Global Mem

    int loop;
    int repeat;

    vector<CoreJob> worklist;

    void print_self();
};

void from_json(const json &j, CoreConfig &c);

class LayerConfig {
public:
    int id; // 全局的原语数组
    comp_base *prim;
    vector<Cast> cast;

    int loop;
    int repeat;

    SPLIT_TYPE split;
    int split_slice;
    int split_dim;
};

void from_json(const json &j, LayerConfig &c);

class StreamConfig {
public:
    int id;

    vector<prim_base *> prims;
    vector<pair<string, int>> sources;
};

void from_json(const json &j, StreamConfig &c);