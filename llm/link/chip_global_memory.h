#pragma once
#include <systemc>
#include <tlm>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>

#include "../dramsys/dramsys_wrapper.h"
#include "../mem/multiport_ram_array.h"
#include "../trace_engine/Event_engine.h"
#include "nlohmann/json.hpp"


#include <vector>

// 不应该用MultiportRamArray，因为这个类是用来模拟SRAM的，而global memory是DRAM
//  GlobalMemory -> DramSysWrapper -> DramSys

class ChipGlobalMemory : public sc_module {
public:
    int cid;

    tlm_utils::simple_target_socket<ChipGlobalMemory> socket;             // Receive
    tlm_utils::simple_initiator_socket<ChipGlobalMemory> initiatorSocket; // Send

    ::DRAMSys::Config::Configuration globalMemoryConfig;

    SC_HAS_PROCESS(ChipGlobalMemory);

    ChipGlobalMemory(const sc_module_name &n, std::string_view configuration, std::string_view resource_directory)
        : socket("chip_global_target_socket"), initiatorSocket("chip_global_initiator_socket"), testConfig(::DRAMSys::Config::from_path(configuration, resource_directory)) {
        dramSysWrapper = new gem5::memory::DRAMSysWrapper("GlobalDRAMSysWrapper", testConfig, false);
        initiatorSocket.bind(dramSysWrapper->tSocket);

        socket.register_b_transport(this, &ChipGlobalMemory::b_transport);
        socket.register_nb_transport_fw(this, &ChipGlobalMemory::nb_transport_fw);
        initiatorSocket.register_nb_transport_bw(this, &ChipGlobalMemory::nb_transport_bw);
    }

    ~ChipGlobalMemory() { delete dramSysWrapper; }

    void b_transport(tlm_generic_payload &trans, sc_time &delay) { initiatorSocket->b_transport(trans, delay); }

    tlm::tlm_sync_enum nb_transport_fw(tlm::tlm_generic_payload &trans, tlm::tlm_phase &phase, sc_core::sc_time &delay) { return initiatorSocket->nb_transport_fw(trans, phase, delay); }

    tlm::tlm_sync_enum nb_transport_bw(tlm::tlm_generic_payload &payload, tlm::tlm_phase &phase, sc_time &bwDelay) { return socket->nb_transport_bw(payload, phase, bwDelay); }
};