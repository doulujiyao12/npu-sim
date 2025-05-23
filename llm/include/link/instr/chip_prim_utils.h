#include "link/instr/chip_instr.h"
#include "link/instr/recv_global_mem.h"
#include "link/instr/print_msg.h"

chip_instr_base* new_chip_prim(string type){
    chip_instr_base* instr = nullptr;
    
    if(type == "Recv_global_memory"){
        instr = new Recv_global_mem();
    }else if(type == "Print_msg"){
        instr = new Print_msg();
    }
    // else if(type == "Send_global_memory"){

    // }

    else{
        assert(0 && "Chip Prim Not Implemented");
    }

    global_chip_prim_stash.push_back(instr);

    return instr;
}