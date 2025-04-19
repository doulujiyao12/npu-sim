#pragma once
#include "systemc.h"

#include "memory/gpu/GPU_L1L2_Cache.h"

class L1L2CacheSystem : public sc_module {
public:
    // vector<Processor*> processors;
    // vector<L1Cache*> l1Caches;
    L2Cache *l2Cache;
    MainMemory *mainMemory;
    Bus *bus;

    L1L2CacheSystem(sc_module_name name, int numProcessors, vector<L1Cache *> l1caches, vector<Processor *> processors) : sc_module(name) {

        l2Cache = new L2Cache("l2_cache", 65536, 64, 8, 16);
        mainMemory = new MainMemory("main_memory");
        bus = new Bus("bus", numProcessors);

        // 连接组件
        for (int i = 0; i < numProcessors; i++) {
            cout << "r" << i << endl;
            processors[i]->cache_socket.bind(l1caches[i]->cpu_socket);
            l1caches[i]->bus_socket.bind(*bus->l1_sockets[i]);
            bus->addL1Cache(l1caches[i]);
        }

        bus->l2_socket.bind(l2Cache->bus_socket);
        l2Cache->mem_socket.bind(mainMemory->l2_socket);
    }
};