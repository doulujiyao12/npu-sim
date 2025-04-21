#pragma once
#include "systemc.h"

#include "common/memory.h"
#include "prims/prim_base.h"

class gpu_base : public prim_base {
public:
    int grid_x;
    int grid_y;
    int block_x;
    int block_y;

    int inp_size;
    int out_size;

    bool mock;
    int req_sm;

    void parse_compose(json j);

    virtual void parse_json(json j) = 0;
    virtual void initialize() = 0;
    virtual gpu_base *clone() = 0;
};