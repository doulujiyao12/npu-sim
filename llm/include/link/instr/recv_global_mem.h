#pragma once
#include "chip_instr.h"

class Recv_global_mem : public chip_instr_base{
public:

    Recv_global_mem(){
        name = "Recv_global_mem";
        instr_type = SEQ_EXEC;
        id = -1;
    }

    Recv_global_mem(int i){
        name = "Recv_global_mem";
        instr_type = SEQ_EXEC;
        id = i;
    }

    void parse_json(json j) override {
        seq = j.contains("seq") ? j["seq"].get<int>() : -1;
    }

    void print_self(std::string prefix) override{
        std::cout << prefix << "<Recv_global_mem>" << std::endl;
    }
};