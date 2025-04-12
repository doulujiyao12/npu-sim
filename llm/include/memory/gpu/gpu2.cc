#include <iostream>
#include <map>
#include <queue>
#include <string>
#include <systemc.h>
#include <vector>

// Scope definitions
enum Scope { CTA_SCOPE, GPU_SCOPE };

// Memory operation types
enum OpType { LOAD, STORE, ACQUIRE, RELEASE };

// Block states for L1 cache
enum L1State {
    L1_I, // Invalid
    L1_V, // Valid (read-only)
    L1_O  // Owned (read-write)
};

// Block states for L2 cache
enum L2State {
    L2_I, // Invalid
    L2_V, // Valid
    L2_O  // Owned
};

// Message types between L1 and L2
enum MessageType {
    GET_V,          // Get Value request
    GET_O,          // Get Ownership request
    DATA_RESPONSE,  // Data response
    WRITE_BACK,     // Write back data
    ACK,            // Acknowledgment
    REQ_WRITE_BACK, // Request write back
    INVALIDATE      // Invalidate request
};

// Message structure for cache coherence communication
struct Message {
    MessageType type;
    int address;
    int value;
    int source_id;
    int dest_id;
    bool is_dirty;
    Scope scope;

    Message(MessageType t, int addr, int val = 0, int src = -1, int dst = -1, bool dirty = false, Scope s = GPU_SCOPE)
        : type(t), address(addr), value(val), source_id(src), dest_id(dst), is_dirty(dirty), scope(s) {}

    void print() const {
        std::cout << "Message[" << "type=" << type << ", addr=" << address << ", value=" << value << ", src=" << source_id << ", dst=" << dest_id << ", dirty=" << is_dirty
                  << ", scope=" << (scope == CTA_SCOPE ? "CTA" : "GPU") << "]" << std::endl;
    }
};

// Memory request structure
struct MemoryRequest {
    OpType type;
    int address;
    int value;
    Scope scope;
    int processor_id;

    MemoryRequest(OpType t, int addr, int val = 0, Scope s = GPU_SCOPE, int pid = -1) : type(t), address(addr), value(val), scope(s), processor_id(pid) {}
};

// Forward declarations
class L1Cache;
class L2Cache;

// Communication channels
sc_fifo<Message *> l1_to_l2_channel;
sc_fifo<Message *> l2_to_l1_channel;
sc_fifo<Message *> l2_to_mem_channel;
sc_fifo<Message *> mem_to_l2_channel;

// Block structure for cache
struct CacheBlock {
    int address;
    int value;
    bool valid;
    bool dirty;
    int owner; // For L2 cache, tracking which L1 owns the block

    CacheBlock() : address(-1), value(0), valid(false), dirty(false), owner(-1) {}
};

// L1 Cache Controller
class L1Cache : public sc_module {
public:
    sc_port<sc_fifo_out_if<Message *>> to_l2;
    sc_port<sc_fifo_in_if<Message *>> from_l2;

    SC_HAS_PROCESS(L1Cache);

    L1Cache(sc_module_name name, int id, int num_blocks, L2Cache *l2_cache) : sc_module(name), cache_id(id), l2(l2_cache) {

        // Initialize cache blocks
        blocks.resize(num_blocks);
        states.resize(num_blocks, L1_I);

        SC_THREAD(process_memory_requests);
        SC_THREAD(process_incoming_messages);
    }

    void process_memory_request(const MemoryRequest &req) {
        // Find block in cache
        int block_idx = find_block(req.address);

        switch (req.type) {
        case LOAD: {
            if (block_idx >= 0 && (states[block_idx] == L1_V || states[block_idx] == L1_O)) {
                // Cache hit, return value
                std::cout << "L1-" << cache_id << ": Load hit for addr " << req.address << std::endl;
            } else {
                // Cache miss, send GetV to L2
                std::cout << "L1-" << cache_id << ": Load miss for addr " << req.address << std::endl;
                send_get_v(req.address, req.scope);
            }
            break;
        }
        case STORE: {
            if (block_idx >= 0 && states[block_idx] == L1_O) {
                // Cache hit and owned, update block
                std::cout << "L1-" << cache_id << ": Store hit for addr " << req.address << std::endl;
                blocks[block_idx].value = req.value;
                blocks[block_idx].dirty = true;
            } else {
                // Cache miss or not owned, send GetO to L2
                std::cout << "L1-" << cache_id << ": Store miss for addr " << req.address << std::endl;
                send_get_o(req.address, req.scope);
            }
            break;
        }
        case ACQUIRE: {
            if (req.scope == CTA_SCOPE) {
                // CTA-scoped acquire
                if (block_idx >= 0 && (states[block_idx] == L1_V || states[block_idx] == L1_O)) {
                    // Cache hit, no need to invalidate anything
                    std::cout << "L1-" << cache_id << ": CTA-scoped acquire hit for addr " << req.address << std::endl;
                } else {
                    // Cache miss, send GetV to L2
                    std::cout << "L1-" << cache_id << ": CTA-scoped acquire miss for addr " << req.address << std::endl;
                    send_get_v(req.address, CTA_SCOPE);
                }
            } else {
                // GPU-scoped acquire
                std::cout << "L1-" << cache_id << ": GPU-scoped acquire for addr " << req.address << std::endl;
                // Send GetV to L2
                send_get_v(req.address, GPU_SCOPE);
                // Will trigger self-invalidation of non-owned blocks upon
                // response
            }
            break;
        }
        case RELEASE: {
            if (req.scope == GPU_SCOPE) {
                // GPU-scoped release
                if (block_idx >= 0) {
                    // If block is dirty, write back to L2
                    if (blocks[block_idx].dirty) {
                        std::cout << "L1-" << cache_id << ": GPU-scoped release, writing back dirty block " << req.address << std::endl;
                        send_write_back(blocks[block_idx].address, blocks[block_idx].value);
                        // Update ownership if needed
                        if (states[block_idx] == L1_V) {
                            states[block_idx] = L1_O;
                        }
                    }
                }

                // Write back all dirty non-owned blocks
                write_back_dirty_non_owned_blocks();
            }
            // For CTA-scoped release, no action needed
            break;
        }
        }
    }

    void process_memory_requests() {
        // This would normally process from a request queue
        // For simplicity, we'll just have example requests here
        wait(10, SC_NS);

        // Example: Processor 0 issues a load
        MemoryRequest load_req(LOAD, 0x100, 0, GPU_SCOPE, cache_id);
        process_memory_request(load_req);

        wait(10, SC_NS);

        // Example: Processor 0 issues an acquire
        MemoryRequest acq_req(ACQUIRE, 0x200, 0, GPU_SCOPE, cache_id);
        process_memory_request(acq_req);

        wait(10, SC_NS);

        // Example: Processor 0 issues a store
        MemoryRequest store_req(STORE, 0x200, 42, GPU_SCOPE, cache_id);
        process_memory_request(store_req);

        wait(10, SC_NS);

        // Example: Processor 0 issues a release
        MemoryRequest rel_req(RELEASE, 0x200, 0, GPU_SCOPE, cache_id);
        process_memory_request(rel_req);
    }

    void process_incoming_messages() {
        while (true) {
            Message *msg = from_l2->read();

            switch (msg->type) {
            case DATA_RESPONSE: {
                int block_idx = allocate_block(msg->address);
                blocks[block_idx].value = msg->value;
                blocks[block_idx].valid = true;
                blocks[block_idx].dirty = false;

                if (msg->scope == GPU_SCOPE) {
                    // For GPU-scoped GetV, self-invalidate all valid non-owned
                    // blocks except the block just received
                    self_invalidate_non_owned_blocks(msg->address);
                }

                // Update state based on message
                if (msg->type == GET_O || msg->dest_id == cache_id) {
                    states[block_idx] = L1_O;
                } else {
                    states[block_idx] = L1_V;
                }

                std::cout << "L1-" << cache_id << ": Received data for addr " << msg->address << ", value=" << msg->value << ", state=" << (states[block_idx] == L1_O ? "OWNED" : "VALID") << std::endl;
                break;
            }

            case REQ_WRITE_BACK: {
                // L2 is requesting a write-back
                int block_idx = find_block(msg->address);
                if (block_idx >= 0 && blocks[block_idx].dirty) {
                    // Write back the requested block
                    send_write_back(blocks[block_idx].address, blocks[block_idx].value);

                    // Write back all dirty non-owned blocks if needed
                    if (states[block_idx] == L1_O && msg->is_dirty) {
                        write_back_dirty_non_owned_blocks();
                    }

                    // Update state
                    if (msg->type == INVALIDATE) {
                        states[block_idx] = L1_I;
                        blocks[block_idx].valid = false;
                    } else {
                        blocks[block_idx].dirty = false;
                    }
                }
                break;
            }

            case INVALIDATE: {
                // Invalidate request from L2
                int block_idx = find_block(msg->address);
                if (block_idx >= 0) {
                    std::cout << "L1-" << cache_id << ": Invalidating block " << msg->address << std::endl;
                    states[block_idx] = L1_I;
                    blocks[block_idx].valid = false;
                }
                break;
            }

            default:
                std::cout << "L1-" << cache_id << ": Received unknown message type" << std::endl;
                break;
            }

            delete msg;
        }
    }

private:
    int cache_id;
    std::vector<CacheBlock> blocks;
    std::vector<L1State> states;
    L2Cache *l2;

    int find_block(int address) {
        for (size_t i = 0; i < blocks.size(); i++) {
            if (blocks[i].valid && blocks[i].address == address) {
                return i;
            }
        }
        return -1; // Not found
    }

    int allocate_block(int address) {
        // Simple replacement policy: find invalid block or overwrite a valid
        // one
        for (size_t i = 0; i < blocks.size(); i++) {
            if (!blocks[i].valid) {
                blocks[i].address = address;
                return i;
            }
        }

        // All blocks are valid, evict one (could be more sophisticated)
        int victim = 0;
        // If the victim is dirty, write it back
        if (blocks[victim].dirty) {
            send_write_back(blocks[victim].address, blocks[victim].value);
        }
        blocks[victim].address = address;
        return victim;
    }

    void send_get_v(int address, Scope scope) {
        Message *msg = new Message(GET_V, address, 0, cache_id, -1, false, scope);
        std::cout << "L1-" << cache_id << ": Sending GetV for addr " << address << " with scope " << (scope == CTA_SCOPE ? "CTA" : "GPU") << std::endl;
        to_l2->write(msg);
    }

    void send_get_o(int address, Scope scope) {
        Message *msg = new Message(GET_O, address, 0, cache_id, -1, false, scope);
        std::cout << "L1-" << cache_id << ": Sending GetO for addr " << address << " with scope " << (scope == CTA_SCOPE ? "CTA" : "GPU") << std::endl;
        to_l2->write(msg);
    }

    void send_write_back(int address, int value) {
        Message *msg = new Message(WRITE_BACK, address, value, cache_id, -1, true);
        std::cout << "L1-" << cache_id << ": Writing back addr " << address << ", value=" << value << std::endl;
        to_l2->write(msg);
    }

    void write_back_dirty_non_owned_blocks() {
        for (size_t i = 0; i < blocks.size(); i++) {
            if (blocks[i].valid && blocks[i].dirty && states[i] != L1_O) {
                send_write_back(blocks[i].address, blocks[i].value);
                blocks[i].dirty = false;
            }
        }
    }

    void self_invalidate_non_owned_blocks(int except_address) {
        for (size_t i = 0; i < blocks.size(); i++) {
            if (blocks[i].valid && states[i] != L1_O && blocks[i].address != except_address) {
                // Write back if dirty
                if (blocks[i].dirty) {
                    send_write_back(blocks[i].address, blocks[i].value);
                }
                // Invalidate
                std::cout << "L1-" << cache_id << ": Self-invalidating addr " << blocks[i].address << std::endl;
                states[i] = L1_I;
                blocks[i].valid = false;
            }
        }
    }
};

// L2 Cache Controller
class L2Cache : public sc_module {
public:
    sc_port<sc_fifo_in_if<Message *>> from_l1;
    sc_port<sc_fifo_out_if<Message *>> to_l1;
    sc_port<sc_fifo_out_if<Message *>> to_mem;
    sc_port<sc_fifo_in_if<Message *>> from_mem;

    SC_HAS_PROCESS(L2Cache);

    L2Cache(sc_module_name name, int num_blocks, int num_l1_caches) : sc_module(name), num_l1s(num_l1_caches) {

        // Initialize cache blocks
        blocks.resize(num_blocks);
        states.resize(num_blocks, L2_I);

        SC_THREAD(process_incoming_messages);
    }

    void process_incoming_messages() {
        while (true) {
            Message *msg = from_l1->read();

            switch (msg->type) {
            case GET_V: {
                handle_get_v(msg);
                break;
            }

            case GET_O: {
                handle_get_o(msg);
                break;
            }

            case WRITE_BACK: {
                handle_write_back(msg);
                break;
            }

            default:
                std::cout << "L2: Received unknown message type" << std::endl;
                break;
            }

            delete msg;
        }
    }

private:
    int num_l1s;
    std::vector<CacheBlock> blocks;
    std::vector<L2State> states;

    int find_block(int address) {
        for (size_t i = 0; i < blocks.size(); i++) {
            if (blocks[i].valid && blocks[i].address == address) {
                return i;
            }
        }
        return -1; // Not found
    }

    int allocate_block(int address) {
        // Simple replacement policy: find invalid block or overwrite a valid
        // one
        for (size_t i = 0; i < blocks.size(); i++) {
            if (!blocks[i].valid) {
                blocks[i].address = address;
                return i;
            }
        }

        // All blocks are valid, evict one (could be more sophisticated)
        int victim = 0;
        // If the victim is owned, need to request write-back from owner
        if (states[victim] == L2_O) {
            request_write_back(blocks[victim].address, blocks[victim].owner);
        }
        blocks[victim].address = address;
        return victim;
    }

    void handle_get_v(Message *msg) {
        int block_idx = find_block(msg->address);

        if (block_idx >= 0 && blocks[block_idx].valid) {
            // Block is in L2 cache
            if (states[block_idx] == L2_O && blocks[block_idx].owner != msg->source_id) {
                // Block is owned by another L1, need to request write-back if
                // it's a GPU-scoped acquire
                if (msg->scope == GPU_SCOPE) {
                    request_write_back(blocks[block_idx].address, blocks[block_idx].owner);
                    // After write-back, block will be in state V
                    states[block_idx] = L2_V;
                }
            }

            // Send data to requesting L1
            Message *response = new Message(DATA_RESPONSE, blocks[block_idx].address, blocks[block_idx].value, -1, msg->source_id, false, msg->scope);
            to_l1->write(response);

            std::cout << "L2: Sent data for addr " << msg->address << " to L1-" << msg->source_id << std::endl;
        } else {
            // Block not in L2, fetch from memory
            fetch_from_memory(msg->address, msg->source_id, msg->scope, false);
        }
    }

    void handle_get_o(Message *msg) {
        int block_idx = find_block(msg->address);

        if (block_idx >= 0 && blocks[block_idx].valid) {
            // Block is in L2 cache
            if (states[block_idx] == L2_O && blocks[block_idx].owner != msg->source_id) {
                // Block is owned by another L1, request write-back
                request_write_back(blocks[block_idx].address, blocks[block_idx].owner);
            }

            // Update ownership
            states[block_idx] = L2_O;
            blocks[block_idx].owner = msg->source_id;

            // Send data to requesting L1
            Message *response = new Message(DATA_RESPONSE, blocks[block_idx].address, blocks[block_idx].value, -1, msg->source_id, false, msg->scope);
            to_l1->write(response);

            std::cout << "L2: Sent data for addr " << msg->address << " to L1-" << msg->source_id << " with ownership" << std::endl;
        } else {
            // Block not in L2, fetch from memory
            fetch_from_memory(msg->address, msg->source_id, msg->scope, true);
        }
    }

    void handle_write_back(Message *msg) {
        int block_idx = find_block(msg->address);

        if (block_idx < 0) {
            // Allocate a new block if not found
            block_idx = allocate_block(msg->address);
        }

        // Update block
        blocks[block_idx].value = msg->value;
        blocks[block_idx].valid = true;
        blocks[block_idx].dirty = true;

        std::cout << "L2: Received write-back for addr " << msg->address << " from L1-" << msg->source_id << std::endl;

        // If this is a owned block and the write-back is from the owner, change
        // state
        if (states[block_idx] == L2_O && blocks[block_idx].owner == msg->source_id) {
            states[block_idx] = L2_V;
            blocks[block_idx].owner = -1;
        }
    }

    void fetch_from_memory(int address, int requester_id, Scope scope, bool assign_ownership) {
        // Send fetch request to memory
        Message *mem_req = new Message(GET_V, address, 0, -1, -1);
        to_mem->write(mem_req);

        std::cout << "L2: Fetching addr " << address << " from memory" << std::endl;

        // Wait for response from memory
        Message *mem_resp = from_mem->read();

        // Allocate block in L2
        int block_idx = allocate_block(address);
        blocks[block_idx].value = mem_resp->value;
        blocks[block_idx].valid = true;
        blocks[block_idx].dirty = false;

        // Set state and ownership
        if (assign_ownership) {
            states[block_idx] = L2_O;
            blocks[block_idx].owner = requester_id;
        } else {
            states[block_idx] = L2_V;
            blocks[block_idx].owner = -1;
        }

        // Forward data to requester
        Message *l1_resp = new Message(DATA_RESPONSE, address, mem_resp->value, -1, requester_id, false, scope);
        to_l1->write(l1_resp);

        std::cout << "L2: Sent data for addr " << address << " to L1-" << requester_id << (assign_ownership ? " with ownership" : "") << std::endl;

        delete mem_resp;
    }

    void request_write_back(int address, int owner_id) {
        // Send write-back request to owner
        Message *req = new Message(REQ_WRITE_BACK, address, 0, -1, owner_id, true);
        to_l1->write(req);

        std::cout << "L2: Requesting write-back for addr " << address << " from L1-" << owner_id << std::endl;

        // Note: In a real implementation, we would wait for the write-back
        // For simplicity, we'll assume it happens asynchronously
    }
};

// Memory module
class Memory : public sc_module {
public:
    sc_port<sc_fifo_in_if<Message *>> from_l2;
    sc_port<sc_fifo_out_if<Message *>> to_l2;

    SC_HAS_PROCESS(Memory);

    Memory(sc_module_name name) : sc_module(name) { SC_THREAD(process_requests); }

    void process_requests() {
        while (true) {
            Message *msg = from_l2->read();

            // Simulate memory access delay
            wait(100, SC_NS);

            // Handle request (for simplicity, we'll just return a default
            // value)
            int value = memory[msg->address];

            // Send response
            Message *response = new Message(DATA_RESPONSE, msg->address, value);
            to_l2->write(response);

            std::cout << "Memory: Sent data for addr " << msg->address << ", value=" << value << std::endl;

            delete msg;
        }
    }

    // Set a memory value
    void set_memory(int address, int value) { memory[address] = value; }

private:
    std::map<int, int> memory; // Simple memory model
};

// Top-level module
class LRCCSystem : public sc_module {
public:
    Memory *mem;
    L2Cache *l2;
    std::vector<L1Cache *> l1s;

    // Channels
    sc_fifo<Message *> l1_to_l2;
    sc_fifo<Message *> l2_to_l1;
    sc_fifo<Message *> l2_to_mem;
    sc_fifo<Message *> mem_to_l2;

    SC_HAS_PROCESS(LRCCSystem);

    LRCCSystem(sc_module_name name, int num_l1_caches) : sc_module(name), l1_to_l2(100), l2_to_l1(100), l2_to_mem(100), mem_to_l2(100) {

        // Create memory
        mem = new Memory("Memory");
        mem->from_l2(l2_to_mem);
        mem->to_l2(mem_to_l2);

        // Initialize some memory values
        mem->set_memory(0x100, 100);
        mem->set_memory(0x200, 200);

        // Create L2 cache
        l2 = new L2Cache("L2Cache", 16, num_l1_caches);
        l2->from_l1(l1_to_l2);
        l2->to_l1(l2_to_l1);
        l2->to_mem(l2_to_mem);
        l2->from_mem(mem_to_l2);

        // Create L1 caches
        for (int i = 0; i < num_l1_caches; i++) {
            std::string name = "L1Cache_" + std::to_string(i);
            L1Cache *l1 = new L1Cache(name.c_str(), i, 8, l2);
            l1->to_l2(l1_to_l2);
            l1->from_l2(l2_to_l1);
            l1s.push_back(l1);
        }

        SC_THREAD(monitor);
    }

    ~LRCCSystem() {
        delete mem;
        delete l2;
        for (auto l1 : l1s) {
            delete l1;
        }
    }

    void monitor() {
        // Monitor simulation (could add trace here)
        while (true) {
            wait(1000, SC_NS);
            std::cout << "Simulation time: " << sc_time_stamp() << std::endl;
        }
    }
};

int sc_main(int argc, char *argv[]) {
    // Create the LRCC system with 4 L1 caches
    LRCCSystem system("LRCC_System", 4);

    // Run simulation for 10000 ns
    sc_start(10000, SC_NS);

    return 0;
}