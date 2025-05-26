#pragma once 

#include "memory/sram/High_mem_access_unit.h"
#include "memory/sram/Mem_access_unit.h"
// Forward-declare to avoid pulling in heavy headers
class GlobalMemInterface;

class TaskChipContext{
public:

    TaskChipContext(){};
    mem_access_unit *mau;
};

TaskChipContext generate_chip_context(GlobalMemInterface *global_mem_interface);