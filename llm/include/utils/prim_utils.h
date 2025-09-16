#pragma once
#include "systemc.h"
#include <unordered_map>
#include <functional>

#include "prims/comp_prims.h"
#include "prims/gpu_prims.h"
#include "prims/norm_prims.h"
#include "prims/prim_base.h"
#include "utils/print_utils.h"

class PrimFactory {
public:
    using CreatorFunc = std::function<PrimBase*()>;
    
    static PrimFactory& getInstance() {
        static PrimFactory instance;
        return instance;
    }
    
    void registerPrim(const std::string& type, CreatorFunc creator) {
        creators_[type] = creator;
    }
    
    PrimBase *createPrim(const std::string& type) {
        auto it = creators_.find(type);
        if (it != creators_.end()) {
            g_prim_stash.push_back(it->second());
            return it->second();
        }
        
        ARGUS_EXIT("Unregistered primitive type ", type);
    }
    
private:
    std::unordered_map<std::string, CreatorFunc> creators_;
};

// 所有原语的注册函数
#define REGISTER_PRIM(prim_type, name) \
    static bool registered_##prim_type = []() { \
        PrimFactory::getInstance().registerPrim(name, []() { return new prim_type(); }); \
        return true; \
    }();