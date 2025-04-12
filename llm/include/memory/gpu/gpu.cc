#include <iostream>
#include <map>
#include <string>
#include <systemc.h>
#include <vector>

// Cache line states for LRCC protocol
enum class CacheLineState {
    INVALID, // Line is not present in cache
    VALID,   // Line is present in cache, but not owned
    OWNED,   // Line is owned exclusively by this cache
    MODIFIED // Line is modified and needs to be written back
};

// Memory operation types
enum class MemOpType {
    LOAD,           // Regular load
    STORE,          // Regular store
    ACQUIRE_LOAD,   // Load with acquire semantics
    RELEASE_STORE,  // Store with release semantics
    INVALIDATE,     // Invalidate a cache line
    INVALIDATE_ALL, // Invalidate all valid cache lines
    WRITEBACK_ALL,  // Write back all dirty cache lines
    PRINT           // Special operation for debugging
};

// Cache line structure
struct CacheLine {
    uint64_t address;     // Physical address
    uint64_t data;        // Data stored
    CacheLineState state; // State of the cache line
    int owner_id;         // ID of the owner SM (if in L2)

    CacheLine() : address(0), data(0), state(CacheLineState::INVALID), owner_id(-1) {}
};

// Memory operation structure
struct MemoryOperation {
    uint64_t address; // Memory address
    uint64_t data;    // Data (for stores)
    MemOpType type;   // Type of operation
    int sm_id;        // ID of the requesting SM
    int cta_id;       // CTA ID that this operation belongs to

    MemoryOperation() : address(0), data(0), type(MemOpType::LOAD), sm_id(0), cta_id(0) {}

    MemoryOperation(uint64_t addr, uint64_t d, MemOpType t, int smid, int ctaid) : address(addr), data(d), type(t), sm_id(smid), cta_id(ctaid) {}
};

// Main memory
class Memory : public sc_module {
public:
    SC_HAS_PROCESS(Memory);

    sc_port<sc_fifo_in_if<MemoryOperation>> mem_port;
    sc_port<sc_fifo_out_if<MemoryOperation>> response_port;

    std::map<uint64_t, uint64_t> memory;

    Memory(sc_module_name name) : sc_module(name) { SC_THREAD(process_requests); }

    void process_requests() {
        while (true) {
            MemoryOperation op;
            mem_port->read(op);

            if (op.type == MemOpType::STORE || op.type == MemOpType::RELEASE_STORE) {
                memory[op.address] = op.data;
                std::cout << "Memory: Store of " << op.data << " to address 0x" << std::hex << op.address << std::dec << std::endl;
            } else {
                op.data = memory.count(op.address) ? memory[op.address] : 0;
                std::cout << "Memory: Load from address 0x" << std::hex << op.address << std::dec << " returned " << op.data << std::endl;
                response_port->write(op);
            }
        }
    }
};

// L1 Cache
class L1Cache : public sc_module {
public:
    SC_HAS_PROCESS(L1Cache);

    int sm_id;

    sc_port<sc_fifo_in_if<MemoryOperation>> from_sm_port;
    sc_port<sc_fifo_out_if<MemoryOperation>> to_sm_port;
    sc_port<sc_fifo_out_if<MemoryOperation>> to_l2_port;
    sc_port<sc_fifo_in_if<MemoryOperation>> from_l2_port;

    std::map<uint64_t, CacheLine> cache;

    L1Cache(sc_module_name name, int id) : sc_module(name), sm_id(id) {
        SC_THREAD(process_sm_requests);
        SC_THREAD(process_l2_requests);
    }

    void process_sm_requests() {
        while (true) {
            MemoryOperation op;
            from_sm_port->read(op);

            // Set SM ID for the operation
            op.sm_id = sm_id;

            // Process based on operation type
            switch (op.type) {
            case MemOpType::LOAD:
                handle_load(op);
                break;
            case MemOpType::STORE:
                handle_store(op);
                break;
            case MemOpType::ACQUIRE_LOAD:
                handle_acquire_load(op);
                break;
            case MemOpType::RELEASE_STORE:
                handle_release_store(op);
                break;
            default:
                to_l2_port->write(op);
                break;
            }
        }
    }

    void process_l2_requests() {
        while (true) {
            MemoryOperation op;
            from_l2_port->read(op);

            // Handle operations from L2
            switch (op.type) {
            case MemOpType::INVALIDATE:
                // Invalidate a specific cache line
                if (cache.find(op.address) != cache.end()) {
                    std::cout << "L1 Cache " << sm_id << ": Invalidating line 0x" << std::hex << op.address << std::dec << std::endl;
                    cache.erase(op.address);
                }
                break;

            case MemOpType::INVALIDATE_ALL:
                // Invalidate all valid cache lines (part of acquire)
                std::cout << "L1 Cache " << sm_id << ": Invalidating all VALID lines" << std::endl;
                for (auto it = cache.begin(); it != cache.end();) {
                    if (it->second.state == CacheLineState::VALID) {
                        it = cache.erase(it);
                    } else {
                        ++it;
                    }
                }
                break;

            case MemOpType::WRITEBACK_ALL:
                // Write back all dirty cache lines (part of release or acquire)
                std::cout << "L1 Cache " << sm_id << ": Writing back all MODIFIED lines" << std::endl;
                for (auto &entry : cache) {
                    if (entry.second.state == CacheLineState::MODIFIED) {
                        // Create a write-back operation
                        MemoryOperation wb_op(entry.first, entry.second.data, MemOpType::STORE, sm_id, op.cta_id);
                        to_l2_port->write(wb_op);

                        // Update state to VALID
                        entry.second.state = CacheLineState::VALID;
                    }
                }
                break;

            default:
                // Regular response from L2
                if (op.type == MemOpType::LOAD || op.type == MemOpType::ACQUIRE_LOAD) {
                    // Update cache for loads
                    CacheLine line;
                    line.address = op.address;
                    line.data = op.data;
                    line.state = CacheLineState::VALID;
                    cache[op.address] = line;

                    // Forward to SM
                    to_sm_port->write(op);
                }
                break;
            }
        }
    }

    void handle_load(MemoryOperation op) {
        if (cache.find(op.address) != cache.end()) {
            // Cache hit
            std::cout << "L1 Cache " << sm_id << ": Load HIT for address 0x" << std::hex << op.address << std::dec << std::endl;
            op.data = cache[op.address].data;
            to_sm_port->write(op);
        } else {
            // Cache miss, forward to L2
            std::cout << "L1 Cache " << sm_id << ": Load MISS for address 0x" << std::hex << op.address << std::dec << std::endl;
            to_l2_port->write(op);
        }
    }

    void handle_store(MemoryOperation op) {
        // Update in L1 cache
        std::cout << "L1 Cache " << sm_id << ": Store to address 0x" << std::hex << op.address << std::dec << std::endl;

        if (cache.find(op.address) != cache.end()) {
            cache[op.address].data = op.data;
            cache[op.address].state = CacheLineState::MODIFIED;
        } else {
            // Allocate in L1
            CacheLine line;
            line.address = op.address;
            line.data = op.data;
            line.state = CacheLineState::MODIFIED;
            cache[op.address] = line;
        }
    }

    void handle_acquire_load(MemoryOperation op) {
        std::cout << "L1 Cache " << sm_id << ": Acquire load for address 0x" << std::hex << op.address << std::dec << std::endl;

        // For acquire load:
        // 1. Write back all dirty cache lines
        for (auto &entry : cache) {
            if (entry.second.state == CacheLineState::MODIFIED) {
                // Create a write-back operation
                MemoryOperation wb_op(entry.first, entry.second.data, MemOpType::STORE, sm_id, op.cta_id);
                to_l2_port->write(wb_op);

                // Update state
                entry.second.state = CacheLineState::VALID;
            }
        }

        // 2. Forward the acquire load to L2
        to_l2_port->write(op);
    }

    void handle_release_store(MemoryOperation op) {
        std::cout << "L1 Cache " << sm_id << ": Release store to address 0x" << std::hex << op.address << std::dec << std::endl;

        // For release store:
        // 1. First, write back all dirty cache lines
        for (auto &entry : cache) {
            if (entry.second.state == CacheLineState::MODIFIED) {
                // Create a write-back operation
                MemoryOperation wb_op(entry.first, entry.second.data, MemOpType::STORE, sm_id, op.cta_id);
                to_l2_port->write(wb_op);

                // Update state
                entry.second.state = CacheLineState::VALID;
            }
        }

        // 2. Then, execute the release store itself
        to_l2_port->write(op);

        // 3. Update the L1 cache - set OWNED state for this line
        if (cache.find(op.address) != cache.end()) {
            cache[op.address].data = op.data;
            cache[op.address].state = CacheLineState::OWNED;
        } else {
            // Allocate in L1
            CacheLine line;
            line.address = op.address;
            line.data = op.data;
            line.state = CacheLineState::OWNED;
            cache[op.address] = line;
        }
    }
};

// L2 Cache
class L2Cache : public sc_module {
public:
    SC_HAS_PROCESS(L2Cache);

    std::vector<sc_port<sc_fifo_in_if<MemoryOperation>> *> from_l1_ports;
    std::vector<sc_port<sc_fifo_out_if<MemoryOperation>> *> to_l1_ports;
    sc_port<sc_fifo_out_if<MemoryOperation>> to_memory_port;
    sc_port<sc_fifo_in_if<MemoryOperation>> from_memory_port;

    std::map<uint64_t, CacheLine> cache;
    int num_sms;

    L2Cache(sc_module_name name, int sms) : sc_module(name), num_sms(sms) {
        // Create ports for each SM
        for (int i = 0; i < num_sms; i++) {
            from_l1_ports.push_back(new sc_port<sc_fifo_in_if<MemoryOperation>>);
            to_l1_ports.push_back(new sc_port<sc_fifo_out_if<MemoryOperation>>);
        }

        SC_THREAD(process_requests);
    }

    ~L2Cache() {
        for (auto port : from_l1_ports)
            delete port;
        for (auto port : to_l1_ports)
            delete port;
    }

    void process_requests() {
        while (true) {
            // Round-robin check of all L1 ports
            for (int i = 0; i < num_sms; i++) {
                if (from_l1_ports[i]->num_available() > 0) {
                    MemoryOperation op;
                    from_l1_ports[i]->read(op);

                    // Process based on operation type
                    switch (op.type) {
                    case MemOpType::LOAD:
                        handle_load(op, i);
                        break;
                    case MemOpType::STORE:
                        handle_store(op, i);
                        break;
                    case MemOpType::ACQUIRE_LOAD:
                        handle_acquire_load(op, i);
                        break;
                    case MemOpType::RELEASE_STORE:
                        handle_release_store(op, i);
                        break;
                    default:
                        std::cerr << "L2 Cache: Unhandled operation type" << std::endl;
                        break;
                    }
                }
            }

            // Wait a bit before checking again
            wait(1, SC_NS);
        }
    }

    void handle_load(MemoryOperation op, int src_sm) {
        std::cout << "L2 Cache: Load from SM " << src_sm << " for address 0x" << std::hex << op.address << std::dec << std::endl;

        if (cache.find(op.address) != cache.end()) {
            // Cache hit in L2
            std::cout << "L2 Cache: HIT for address 0x" << std::hex << op.address << std::dec << std::endl;
            op.data = cache[op.address].data;
            to_l1_ports[src_sm]->write(op);
        } else {
            // Cache miss, fetch from memory
            std::cout << "L2 Cache: MISS for address 0x" << std::hex << op.address << std::dec << ", fetching from memory" << std::endl;
            to_memory_port->write(op);
            MemoryOperation response;
            from_memory_port->read(response);

            // Add to L2 cache
            CacheLine line;
            line.address = response.address;
            line.data = response.data;
            line.state = CacheLineState::VALID;
            cache[response.address] = line;

            to_l1_ports[src_sm]->write(response);
        }
    }

    void handle_store(MemoryOperation op, int src_sm) {
        std::cout << "L2 Cache: Store from SM " << src_sm << " for address 0x" << std::hex << op.address << std::dec << std::endl;

        // Regular store - update L2 if present
        if (cache.find(op.address) != cache.end()) {
            cache[op.address].data = op.data;

            // If this cache line was owned by someone else, invalidate their
            // copy
            if (cache[op.address].state == CacheLineState::OWNED && cache[op.address].owner_id != op.sm_id) {

                std::cout << "L2 Cache: Invalidating owner SM " << cache[op.address].owner_id << "'s copy of address 0x" << std::hex << op.address << std::dec << std::endl;

                // Send invalidation to previous owner
                MemoryOperation inv_op = op;
                inv_op.type = MemOpType::INVALIDATE;
                to_l1_ports[cache[op.address].owner_id]->write(inv_op);
            }

            // Update ownership
            cache[op.address].state = CacheLineState::OWNED;
            cache[op.address].owner_id = op.sm_id;
        } else {
            // Allocate in L2
            CacheLine line;
            line.address = op.address;
            line.data = op.data;
            line.state = CacheLineState::OWNED;
            line.owner_id = op.sm_id;
            cache[op.address] = line;
        }

        // Write through to memory
        to_memory_port->write(op);
    }

    void handle_acquire_load(MemoryOperation op, int src_sm) {
        std::cout << "L2 Cache: Acquire load from SM " << src_sm << " for address 0x" << std::hex << op.address << std::dec << std::endl;

        // Determine if this is a CTA-scope or GPU-scope synchronization
        bool is_cta_scope = false;

        if (cache.find(op.address) != cache.end() && cache[op.address].state == CacheLineState::OWNED) {

            // Check if owned by same SM or same CTA
            if (cache[op.address].owner_id == op.sm_id) {
                is_cta_scope = true;
                std::cout << "L2 Cache: CTA-scope synchronization detected (same SM)" << std::endl;
            } else if (op.cta_id != -1 && op.cta_id == op.cta_id) {
                // Same CTA, different SM
                is_cta_scope = true;
                std::cout << "L2 Cache: CTA-scope synchronization detected (same CTA)" << std::endl;
            } else {
                // GPU-scope synchronization
                std::cout << "L2 Cache: GPU-scope synchronization detected" << std::endl;

                // Get the current owner to write back all dirty cache lines
                MemoryOperation writeback_op = op;
                writeback_op.type = MemOpType::WRITEBACK_ALL;
                to_l1_ports[cache[op.address].owner_id]->write(writeback_op);

                // Wait for writeback to complete (simplified)
                wait(5, SC_NS);

                // Now the requesting SM needs to invalidate its valid cache
                // lines
                MemoryOperation invalidate_op = op;
                invalidate_op.type = MemOpType::INVALIDATE_ALL;
                to_l1_ports[src_sm]->write(invalidate_op);

                // Wait for invalidation to complete (simplified)
                wait(5, SC_NS);
            }
        }

        // Process the acquire load similar to a regular load
        handle_load(op, src_sm);
    }

    void handle_release_store(MemoryOperation op, int src_sm) {
        std::cout << "L2 Cache: Release store from SM " << src_sm << " for address 0x" << std::hex << op.address << std::dec << std::endl;

        // For a release store, the SM needs to write back all dirty lines first
        MemoryOperation writeback_op = op;
        writeback_op.type = MemOpType::WRITEBACK_ALL;
        to_l1_ports[src_sm]->write(writeback_op);

        // Wait for writeback to complete (simplified)
        wait(5, SC_NS);

        // Then handle the actual store operation and mark ownership
        handle_store(op, src_sm);

        // Mark this cache line as owned by the releasing SM
        if (cache.find(op.address) != cache.end()) {
            cache[op.address].state = CacheLineState::OWNED;
            cache[op.address].owner_id = op.sm_id;
        }
    }
};

// Streaming Multiprocessor
class SM : public sc_module {
public:
    SC_HAS_PROCESS(SM);

    int sm_id;
    int cta_id;

    sc_port<sc_fifo_out_if<MemoryOperation>> to_l1_port;
    sc_port<sc_fifo_in_if<MemoryOperation>> from_l1_port;

    // Test program to execute
    std::vector<MemoryOperation> program;

    SM(sc_module_name name, int id, int cta) : sc_module(name), sm_id(id), cta_id(cta) { SC_THREAD(execute); }

    void execute() {
        // Execute the test program
        for (auto &op : program) {
            // Set CTA ID
            op.cta_id = cta_id;

            // Handle print operation specially
            if (op.type == MemOpType::PRINT) {
                std::cout << "SM " << sm_id << " (CTA " << cta_id << ") Status Report" << std::endl;
                continue;
            }

            // Issue the memory operation
            to_l1_port->write(op);

            // Wait for response if it's a load
            if (op.type == MemOpType::LOAD || op.type == MemOpType::ACQUIRE_LOAD) {
                MemoryOperation response;
                from_l1_port->read(response);

                std::cout << "SM " << sm_id << " (CTA " << cta_id << ") received data: " << response.data << " from address: 0x" << std::hex << response.address << std::dec << std::endl;
            }

            // Add a delay
            wait(2, SC_NS);
        }

        std::cout << "SM " << sm_id << " (CTA " << cta_id << ") completed execution" << std::endl;
    }

    void set_program(const std::vector<MemoryOperation> &prog) { program = prog; }
};

// GPU with multiple SMs
class GPU : public sc_module {
public:
    int num_sms;
    std::vector<SM *> sms;
    std::vector<L1Cache *> l1_caches;
    L2Cache *l2_cache;
    Memory *memory;

    // FIFOs for communication
    std::vector<sc_fifo<MemoryOperation> *> sm_to_l1_fifos;
    std::vector<sc_fifo<MemoryOperation> *> l1_to_sm_fifos;
    std::vector<sc_fifo<MemoryOperation> *> l1_to_l2_fifos;
    std::vector<sc_fifo<MemoryOperation> *> l2_to_l1_fifos;
    sc_fifo<MemoryOperation> *l2_to_memory_fifo;
    sc_fifo<MemoryOperation> *memory_to_l2_fifo;

    GPU(sc_module_name name, int sms) : sc_module(name), num_sms(sms) {
        // Create L2 cache with the number of SMs
        l2_cache = new L2Cache("L2", num_sms);

        // Create memory
        memory = new Memory("Memory");

        // Create FIFOs between L2 and memory
        l2_to_memory_fifo = new sc_fifo<MemoryOperation>(10);
        memory_to_l2_fifo = new sc_fifo<MemoryOperation>(10);

        // Connect L2 to memory
        l2_cache->to_memory_port(*l2_to_memory_fifo);
        l2_cache->from_memory_port(*memory_to_l2_fifo);
        memory->mem_port(*l2_to_memory_fifo);
        memory->response_port(*memory_to_l2_fifo);

        // Create SMs and L1 caches
        for (int i = 0; i < num_sms; i++) {
            // Create SM and L1 cache with appropriate IDs
            sms.push_back(new SM(sc_gen_unique_name("SM"), i, i));
            l1_caches.push_back(new L1Cache(sc_gen_unique_name("L1"), i));

            // Create FIFOs
            sm_to_l1_fifos.push_back(new sc_fifo<MemoryOperation>(10));
            l1_to_sm_fifos.push_back(new sc_fifo<MemoryOperation>(10));
            l1_to_l2_fifos.push_back(new sc_fifo<MemoryOperation>(10));
            l2_to_l1_fifos.push_back(new sc_fifo<MemoryOperation>(10));

            // Connect SM to L1
            sms[i]->to_l1_port(*sm_to_l1_fifos[i]);
            sms[i]->from_l1_port(*l1_to_sm_fifos[i]);
            l1_caches[i]->from_sm_port(*sm_to_l1_fifos[i]);
            l1_caches[i]->to_sm_port(*l1_to_sm_fifos[i]);

            // Connect L1 to L2
            l1_caches[i]->to_l2_port(*l1_to_l2_fifos[i]);
            l1_caches[i]->from_l2_port(*l2_to_l1_fifos[i]);

            // Connect L2 ports to the L1 caches
            (*l2_cache->from_l1_ports[i])(*l1_to_l2_fifos[i]);
            (*l2_cache->to_l1_ports[i])(*l2_to_l1_fifos[i]);
        }
    }

    ~GPU() {
        delete l2_cache;
        delete memory;

        for (auto &sm : sms)
            delete sm;
        for (auto &l1 : l1_caches)
            delete l1;

        delete l2_to_memory_fifo;
        delete memory_to_l2_fifo;

        for (auto &fifo : sm_to_l1_fifos)
            delete fifo;
        for (auto &fifo : l1_to_sm_fifos)
            delete fifo;
        for (auto &fifo : l1_to_l2_fifos)
            delete fifo;
        for (auto &fifo : l2_to_l1_fifos)
            delete fifo;
    }

    void set_program(int sm_id, const std::vector<MemoryOperation> &program) {
        if (sm_id < num_sms) {
            sms[sm_id]->set_program(program);
        }
    }

    // Set CTA ID for an SM (to create different CTA arrangements for testing)
    void set_cta_id(int sm_id, int cta_id) {
        if (sm_id < num_sms) {
            sms[sm_id]->cta_id = cta_id;
        }
    }
};

// Main function that sets up test scenario
int sc_main(int argc, char *argv[]) {
    // Create GPU with 3 SMs
    GPU gpu("GPU", 3);

    // Set up CTA arrangement for testing:
    // SM0 and SM1 are in CTA 0, SM2 is in CTA 1
    gpu.set_cta_id(0, 0);
    gpu.set_cta_id(1, 0);
    gpu.set_cta_id(2, 1);

    // SM0 program - producer in CTA 0
    std::vector<MemoryOperation> sm0_program = {
        // Initialize data for CTA-scope sync test
        MemoryOperation(0x100, 42, MemOpType::STORE, 0,
                        0), // Store 42 to address 0x100

        // Signal with a release store to the flag for CTA-scope sync
        MemoryOperation(0x200, 1, MemOpType::RELEASE_STORE, 0,
                        0), // Set flag at 0x200 to 1

        // Wait some time
        MemoryOperation(0x300, 0, MemOpType::LOAD, 0, 0), // Dummy load to wait

        // Initialize data for GPU-scope sync test
        MemoryOperation(0x400, 99, MemOpType::STORE, 0,
                        0), // Store 99 to address 0x400

        // Signal with a release store to the flag for GPU-scope sync
        MemoryOperation(0x500, 1, MemOpType::RELEASE_STORE, 0,
                        0) // Set flag at 0x500 to 1
    };

    // SM1 program - consumer in CTA 0 (for CTA-scope sync)
    std::vector<MemoryOperation> sm1_program = {// Acquire load from flag (same CTA as SM0)
                                                MemoryOperation(0x200, 0, MemOpType::ACQUIRE_LOAD, 1, 0),

                                                // Read the data
                                                MemoryOperation(0x100, 0, MemOpType::LOAD, 1, 0),

                                                // Debug print
                                                MemoryOperation(0, 0, MemOpType::PRINT, 1, 0)};

    // SM2 program - consumer in CTA 1 (for GPU-scope sync)
    std::vector<MemoryOperation> sm2_program = {// Acquire load from flag (different CTA than SM0)
                                                MemoryOperation(0x500, 0, MemOpType::ACQUIRE_LOAD, 2, 1),

                                                // Read the data
                                                MemoryOperation(0x400, 0, MemOpType::LOAD, 2, 1),

                                                // Debug print
                                                MemoryOperation(0, 0, MemOpType::PRINT, 2, 1)};

    // Set the programs
    gpu.set_program(0, sm0_program);
    gpu.set_program(1, sm1_program);
    gpu.set_program(2, sm2_program);

    // Start simulation
    std::cout << "Starting simulation..." << std::endl;
    sc_start(100, SC_NS);
    std::cout << "Simulation complete." << std::endl;

    return 0;
}