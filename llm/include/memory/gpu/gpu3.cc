#define SC_INCLUDE_DYNAMIC_PROCESSES
#include <cstring>
#include <iostream>
#include <map>
#include <sc_spawn.h>
#include <systemc.h>
#include <vector>

using namespace sc_core;
// Scope types for memory operations
enum class Scope {
    CTA, // Thread-block scope
    GPU, // Device-wide scope
    NONE // Non-scoped (default to GPU scope per table 10.19)
};

// Memory operation types
enum class MemOp {
    LOAD,         // Regular load
    STORE,        // Regular store
    ACQUIRE_LOAD, // Load with acquire semantics
    RELEASE_STORE // Store with release semantics
};

// Cache block states
enum class BlockState {
    I, // Invalid
    V, // Valid (not owned)
    O  // Owned
};

// Message types between L1 and L2
enum class MessageType {
    GET_V,          // Get value
    GET_O,          // Get ownership
    WRITE_BACK,     // Write back data
    REQ_WRITE_BACK, // Request write-back
    ACK,            // Acknowledgment
    DATA,           // Data response
    SEND_DATA
};

// Memory request/response message
struct CacheMessage {
    uint64_t address;
    MessageType type;
    Scope scope;
    int requester_id; // ID of requesting L1 cache
    uint8_t *data;    // Pointer to data
    bool is_dirty;    // Dirty flag for the block

    CacheMessage() : address(0), type(MessageType::GET_V), scope(Scope::NONE), requester_id(-1), data(nullptr), is_dirty(false) {}

    CacheMessage(uint64_t addr, MessageType t, Scope s, int req_id, uint8_t *d = nullptr, bool dirty = false) : address(addr), type(t), scope(s), requester_id(req_id), data(d), is_dirty(dirty) {}
    // 添加输出流运算符重载
    friend std::ostream &operator<<(std::ostream &os, const CacheMessage &msg) {
        os << "CacheMessage{addr=0x" << std::hex << msg.address << std::dec << ", type=" << static_cast<int>(msg.type) << ", scope=" << static_cast<int>(msg.scope) << ", req_id=" << msg.requester_id
           << ", dirty=" << msg.is_dirty << "}";
        return os;
    }
};

// Cache block structure
struct CacheBlock {
    uint64_t tag;
    uint8_t *data;
    BlockState state;
    bool dirty;

    CacheBlock() : tag(0), data(nullptr), state(BlockState::I), dirty(false) {}
};

// Constants
// DAHU address 的粒度是64 Byte
const int BLOCK_SIZE = 64;           // Cache block size in bytes Cacheline
const int L1_SIZE = 32 * 1024;       // 32KB L1 cache
const int L2_SIZE = 1 * 1024 * 1024; // 1MB L2 cache
const int NUM_L1_CACHES = 4;         // Number of L1 caches (CTAs/SMs)

// L1 Cache Controller
class L1CacheController : public sc_module {
public:
    // Ports
    sc_port<sc_fifo_in_if<CacheMessage>> l2_to_l1_port;       // From L2
    sc_port<sc_fifo_in_if<CacheMessage>> l2_to_l1_ctrl_port;  // From L2
    sc_port<sc_fifo_out_if<CacheMessage>> l1_to_l2_port;      // To L2
    sc_port<sc_fifo_out_if<CacheMessage>> l1_to_l2_data_port; // To L2
    SC_HAS_PROCESS(L1CacheController);

    L1CacheController(sc_module_name name, int id) : sc_module(name), l1_id(id) {
        // Initialize cache structure
        num_blocks = L1_SIZE / BLOCK_SIZE;
        blocks.resize(num_blocks);
        for (int i = 0; i < num_blocks; i++) {
            blocks[i].data = new uint8_t[BLOCK_SIZE];
            memset(blocks[i].data, 0, BLOCK_SIZE);
        }

        // Register SystemC processes
        SC_THREAD(responseHandler);

        std::cout << "L1 Cache " << l1_id << " initialized with " << num_blocks << " blocks" << std::endl;
    }

    ~L1CacheController() {
        for (int i = 0; i < num_blocks; i++) {
            delete[] blocks[i].data;
        }
    }

    // Public method to process memory operations from GPU threads
    void processMemOp(uint64_t address, MemOp op, Scope scope, uint8_t *data = nullptr) {
        uint64_t block_addr = address & ~(BLOCK_SIZE - 1); // Align to block boundary
        int index = findBlock(block_addr);

        std::cout << "L1-" << l1_id << ": Processing "
                  << (op == MemOp::LOAD           ? "LOAD"
                      : op == MemOp::STORE        ? "STORE"
                      : op == MemOp::ACQUIRE_LOAD ? "ACQUIRE_LOAD"
                                                  : "RELEASE_STORE")
                  << " @ 0x" << std::hex << address << std::dec << " scope="
                  << (scope == Scope::CTA   ? "CTA"
                      : scope == Scope::GPU ? "GPU"
                                            : "NONE")
                  << std::endl;

        switch (op) {
        case MemOp::LOAD:
        case MemOp::ACQUIRE_LOAD:
            handleLoad(block_addr, index, op, scope);
            break;

        case MemOp::STORE:
        case MemOp::RELEASE_STORE:
            handleStore(block_addr, index, op, scope, data);
            break;
        }
    }

private:
    int l1_id;                      // ID of this L1 cache
    std::vector<CacheBlock> blocks; // Cache blocks
    int num_blocks;

    // Find a block in the cache (-1 if not found)
    int findBlock(uint64_t address) {
        for (int i = 0; i < num_blocks; i++) {
            if (blocks[i].tag == address && blocks[i].state != BlockState::I) {
                return i;
            }
        }
        return -1;
    }

    // Find a replacement victim
    int getVictimBlock() {
        // First look for invalid blocks
        // DAHU invalid LRU celue
        // First look for invalid blocks
        for (int i = 0; i < num_blocks; i++) {
            if (blocks[i].state == BlockState::I) {
                return i;
            }
        }
        // If no invalid block, use simple LRU (just pick first non-owned for
        // simplicity)
        for (int i = 0; i < num_blocks; i++) {
            if (blocks[i].state == BlockState::V) {
                return i;
            }
        }

        // If no invalid block, use simple LRU (just pick first one for
        // simplicity)
        return 0;
    }

    // 处理 O 状态块的驱逐
    void handleOwnedEviction(int index) {
        if (blocks[index].state == BlockState::O) {
            std::cout << "L1-" << l1_id << ": Evicting owned block 0x" << std::hex << blocks[index].tag << std::dec << std::endl;

            // Write back all dirty non-owned blocks
            for (int i = 0; i < num_blocks; i++) {
                if (blocks[i].state == BlockState::V && blocks[i].dirty) {
                    writeBack(blocks[i].tag, i);
                }
            }


            writeBack(blocks[index].tag, index);


            // 更新状态为 I
            blocks[index].state = BlockState::I;
        }
    }

    // Handle load operations (including acquire)
    void handleLoad(uint64_t address, int index, MemOp op, Scope scope) {
        bool is_acquire = (op == MemOp::ACQUIRE_LOAD);

        if (index == -1) {
            // Cache miss
            index = getVictimBlock();

            // 如果受害块是 O 状态，需要特殊处理
            if (blocks[index].state == BlockState::O) {
                handleOwnedEviction(index);
            }
            // 如果受害块是 V 状态且脏，写回
            else if (blocks[index].state == BlockState::V && blocks[index].dirty) {
                writeBack(blocks[index].tag, index);
                blocks[index].state = BlockState::I;
            } else {
                blocks[index].state = BlockState::I;
            }

            // Send GetV to L2
            CacheMessage req(address, MessageType::GET_V, scope, l1_id);
            l1_to_l2_port->write(req);
            std::cout << "L1-" << l1_id << ": Sent GetV for 0x" << std::hex << address << std::dec << std::endl;

            // Wait for response from L2 (simplified - actual code would use
            // events)
            CacheMessage resp;
            l2_to_l1_port->read(resp);

            // Update cache block
            blocks[index].tag = address;
            blocks[index].state = BlockState::V;
            blocks[index].dirty = false;
            if (resp.data) {
                memcpy(blocks[index].data, resp.data, BLOCK_SIZE);
            }

            std::cout << "L1-" << l1_id << ": Block 0x" << std::hex << address << std::dec << " loaded to index " << index << std::endl;

            // For GPU/non-scoped acquire, handle self-invalidation
            if (is_acquire && (scope == Scope::GPU || scope == Scope::NONE)) {
                selfInvalidateNonOwned(index);
            }
        } else {
            // Cache hit
            if (is_acquire && (scope == Scope::GPU || scope == Scope::NONE) && blocks[index].state == BlockState::V) {
                // Need to get fresh value and self-invalidate
                CacheMessage req(address, MessageType::GET_V, scope, l1_id);
                l1_to_l2_port->write(req);

                // Wait for response
                CacheMessage resp;
                l2_to_l1_port->read(resp);

                // Update cache block
                if (resp.data) {
                    memcpy(blocks[index].data, resp.data, BLOCK_SIZE);
                }

                // Self-invalidate other non-owned blocks
                selfInvalidateNonOwned(index);
            }

            std::cout << "L1-" << l1_id << ": Read hit for 0x" << std::hex << address << std::dec << " at index " << index << std::endl;
        }
    }

    // Handle store operations (including release)
    void handleStore(uint64_t address, int index, MemOp op, Scope scope, uint8_t *data) {
        bool is_release = (op == MemOp::RELEASE_STORE);

        if (index == -1) {
            // Cache miss
            index = getVictimBlock();

            // 如果受害块是 O 状态，需要特殊处理
            if (blocks[index].state == BlockState::O) {
                handleOwnedEviction(index);
            }
            // 如果受害块是 V 状态且脏，写回
            else if (blocks[index].state == BlockState::V && blocks[index].dirty) {
                writeBack(blocks[index].tag, index);
                blocks[index].state = BlockState::I;
            } else {
                blocks[index].state = BlockState::I;
            }

            // For GPU/non-scoped release, get ownership
            if (is_release && (scope == Scope::GPU || scope == Scope::NONE)) {
                CacheMessage req(address, MessageType::GET_O, scope, l1_id);
                l1_to_l2_port->write(req);
                std::cout << "L1-" << l1_id << ": Sent GetO for 0x" << std::hex << address << std::dec << std::endl;

                // Wait for response
                CacheMessage resp;
                l2_to_l1_port->read(resp);

                // Update cache block
                blocks[index].tag = address;
                blocks[index].state = BlockState::O;
                if (resp.data) {
                    memcpy(blocks[index].data, resp.data, BLOCK_SIZE);
                }
            } else {
                // For regular stores or CTA-scoped release, just get value
                CacheMessage req(address, MessageType::GET_V, scope, l1_id);
                l1_to_l2_port->write(req);
                std::cout << "L1-" << l1_id << ": Sent GetV for 0x" << std::hex << address << std::dec << std::endl;

                // Wait for response
                CacheMessage resp;
                l2_to_l1_port->read(resp);

                // Update cache block
                blocks[index].tag = address;
                blocks[index].state = BlockState::V;
                if (resp.data) {
                    memcpy(blocks[index].data, resp.data, BLOCK_SIZE);
                }
            }

            // Update data and mark as dirty
            if (data) {
                memcpy(blocks[index].data, data, BLOCK_SIZE);
            }
            blocks[index].dirty = true;

            std::cout << "L1-" << l1_id << ": Block 0x" << std::hex << address << std::dec << " loaded to index " << index << " for store" << std::endl;
        } else {
            // Cache hit
            if (is_release && (scope == Scope::GPU || scope == Scope::NONE) && blocks[index].state == BlockState::V) {
                // Need to get ownership for GPU/non-scoped release
                CacheMessage req(address, MessageType::GET_O, scope, l1_id);
                l1_to_l2_port->write(req);

                // Wait for response
                CacheMessage resp;
                l2_to_l1_port->read(resp);

                // Update block state
                blocks[index].state = BlockState::O;
            }

            // Update data and mark as dirty
            if (data) {
                memcpy(blocks[index].data, data, BLOCK_SIZE);
            }
            blocks[index].dirty = true;

            std::cout << "L1-" << l1_id << ": Write hit for 0x" << std::hex << address << std::dec << " at index " << index << std::endl;
        }
    }

    // Write back a dirty block
    void writeBack(uint64_t address, int index) {
        std::cout << "L1-" << l1_id << ": Writing back 0x" << std::hex << address << std::dec << std::endl;

        CacheMessage req(address, MessageType::WRITE_BACK, Scope::NONE, l1_id, blocks[index].data, true);
        l1_to_l2_port->write(req);

        // Wait for acknowledgement
        CacheMessage resp;
        l2_to_l1_port->read(resp);

        blocks[index].dirty = false;
    }

    // Self-invalidate all non-owned blocks except the given one
    void selfInvalidateNonOwned(int exclude_index) {
        std::cout << "L1-" << l1_id << ": Self-invalidating non-owned blocks" << std::endl;


        // Then invalidate all non-owned blocks
        for (int i = 0; i < num_blocks; i++) {
            if (i != exclude_index && blocks[i].state == BlockState::V && !blocks[i].dirty) {
                blocks[i].state = BlockState::I;
                std::cout << "L1-" << l1_id << ": Invalidated block " << i << " (0x" << std::hex << blocks[i].tag << std::dec << ")" << std::endl;
            }
        }

        // First write back all dirty non-owned blocks
        for (int i = 0; i < num_blocks; i++) {
            if (i != exclude_index && blocks[i].state == BlockState::V && blocks[i].dirty) {
                writeBack(blocks[i].tag, i);
            }
        }
    }

    // Thread to handle responses from L2
    void responseHandler() {
        while (true) {
            CacheMessage msg;
            l2_to_l1_ctrl_port->read(msg);

            // Handle message based on type
            switch (msg.type) {
            case MessageType::REQ_WRITE_BACK: {
                // L2 requesting write-back of all dirty non-owned blocks
                uint64_t requested_addr = msg.address;
                int requested_idx = findBlock(requested_addr);

                // Write back all dirty non-owned blocks
                for (int i = 0; i < num_blocks; i++) {
                    if (blocks[i].state == BlockState::V && blocks[i].dirty) {
                        writeBack(blocks[i].tag, i);
                    }
                }

                // Write back the requested block if dirty
                if (requested_idx != -1 && blocks[requested_idx].state == BlockState::O) {
                    // writeBack(requested_addr, requested_idx);

                    CacheMessage req(requested_addr, MessageType::SEND_DATA, Scope::NONE, l1_id, blocks[requested_idx].data, true);
                    l1_to_l2_data_port->write(req);

                    // If it was owned, downgrade to valid
                    if (blocks[requested_idx].state == BlockState::O) {
                        blocks[requested_idx].state = BlockState::V;
                    }
                }


                break;
            }

            case MessageType::DATA:
            case MessageType::ACK:
                // These are handled in handleLoad/handleStore functions
                break;

            default:
                std::cerr << "L1-" << l1_id << ": Unexpected message type from L2" << std::endl;
                break;
            }
        }
    }
};

// L2 Cache Controller
class L2CacheController : public sc_module {
public:
    // Ports for communication with L1 caches
    sc_port<sc_fifo_in_if<CacheMessage>> l1_to_l2_ports[NUM_L1_CACHES];
    sc_port<sc_fifo_in_if<CacheMessage>> l1_to_l2_data_ports[NUM_L1_CACHES];
    sc_port<sc_fifo_out_if<CacheMessage>> l2_to_l1_ports[NUM_L1_CACHES];
    sc_port<sc_fifo_out_if<CacheMessage>> l2_to_l1_ctrl_ports[NUM_L1_CACHES];
    SC_HAS_PROCESS(L2CacheController);

    L2CacheController(sc_module_name name) : sc_module(name) {
        // Initialize cache structure
        num_blocks = L2_SIZE / BLOCK_SIZE;
        blocks.resize(num_blocks);
        for (int i = 0; i < num_blocks; i++) {
            blocks[i].data = new uint8_t[BLOCK_SIZE];
            memset(blocks[i].data, 0, BLOCK_SIZE);
        }

        // 为每个L1 cache创建一个处理线程
        for (int i = 0; i < NUM_L1_CACHES; i++) {
            sc_spawn(sc_bind(&L2CacheController::handleL1Requests, this, i));
        }

        std::cout << "L2 Cache initialized with " << num_blocks << " blocks" << std::endl;
    }

    ~L2CacheController() {
        for (int i = 0; i < num_blocks; i++) {
            delete[] blocks[i].data;
        }
    }

private:
    std::vector<CacheBlock> blocks; // Cache blocks
    int num_blocks;
    std::map<uint64_t, int> owner_map; // Maps block address to L1 owner ID (-1 if no owner)

    // Find a block in the cache (-1 if not found)
    int findBlock(uint64_t address) {
        for (int i = 0; i < num_blocks; i++) {
            if (blocks[i].tag == address && blocks[i].state != BlockState::I) {
                return i;
            }
        }
        return -1;
    }

    // Find a replacement victim
    int getVictimBlock() {
        // First look for invalid blocks
        for (int i = 0; i < num_blocks; i++) {
            if (blocks[i].state == BlockState::I) {
                return i;
            }
        }
        // If no invalid block, use simple LRU (just pick first non-owned for
        // simplicity)
        for (int i = 0; i < num_blocks; i++) {
            if (blocks[i].state == BlockState::V) {
                return i;
            }
        }
        // If all blocks are owned, handle eviction special case
        return 0;
    }

    // 每个L1 cache的请求处理线程
    void handleL1Requests(int l1_id) {
        while (true) {
            CacheMessage msg;
            l1_to_l2_ports[l1_id]->read(msg); // 阻塞直到有数据
            // 打印L1请求信息
            std::cout << "L2: Received request from L1-" << l1_id << " type=" << static_cast<int>(msg.type) << " addr=0x" << std::hex << msg.address << std::dec
                      << " scope=" << static_cast<int>(msg.scope) << std::endl;
            handleL1Request(msg, l1_id);
            wait(SC_ZERO_TIME); // 让其他进程有机会运行
        }
    }

    // // Thread to handle requests from L1 caches
    // void requestHandler() {
    //     while (true) {
    //         // Check for pending requests from all L1 caches
    //         for (int l1_id = 0; l1_id < NUM_L1_CACHES; l1_id++) {
    //             // DAHU 去掉死循环
    //             if (l1_to_l2_ports[l1_id]->num_available() > 0) {
    //                 CacheMessage msg;
    //                 l1_to_l2_ports[l1_id]->read(msg);

    //                 handleL1Request(msg, l1_id);
    //             }
    //         }

    //         // Yield to allow other processes to run
    //         wait(SC_ZERO_TIME);
    //     }
    // }

    // Handle a request from an L1 cache
    void handleL1Request(const CacheMessage &msg, int l1_id) {
        uint64_t address = msg.address;
        int block_idx = findBlock(address);

        switch (msg.type) {
        case MessageType::GET_V:
            handleGetV(address, block_idx, l1_id, msg.scope);
            break;

        case MessageType::GET_O:
            handleGetO(address, block_idx, l1_id, msg.scope);
            break;

        case MessageType::WRITE_BACK:
            handleWriteBack(address, block_idx, l1_id, msg.data);
            break;

        default:
            std::cerr << "L2: Unexpected message type from L1-" << l1_id << std::endl;
            break;
        }
    }

    // Handle GetV (get value) request
    void handleGetV(uint64_t address, int block_idx, int requester_id, Scope scope) {
        std::cout << "L2: Handling GetV from L1-" << requester_id << " for 0x" << std::hex << address << std::dec << std::endl;
        // state = I
        if (block_idx == -1) {
            // Cache miss - fetch from memory
            // DAHU i
            block_idx = getVictimBlock();

            // If victim block is owned, need to handle eviction
            if (blocks[block_idx].state == BlockState::O) {
                handleOwnedEviction(block_idx);
            } else if (blocks[block_idx].state == BlockState::V) {
                handleValidEviction(block_idx);
            } else {
                handleMemAcquire(block_idx);
            }

            // Simulate fetching from memory
            blocks[block_idx].tag = address;
            blocks[block_idx].state = BlockState::V;
            blocks[block_idx].dirty = false;
            // DAHU acquire mem
            std::cout << "L2: Fetched 0x" << std::hex << address << std::dec << " from memory" << std::endl;
        }
        // state = O
        // Check if the block is owned by another L1
        int owner_id = -1;
        if (owner_map.find(address) != owner_map.end()) {
            owner_id = owner_map[address];
        }

        if (owner_id != -1 && owner_id != requester_id) {
            // Request write-back from owner
            CacheMessage req(address, MessageType::REQ_WRITE_BACK, Scope::NONE, -1);
            l2_to_l1_ctrl_ports[owner_id]->write(req);

            std::cout << "L2: Requested write-back from L1-" << owner_id << " for 0x" << std::hex << address << std::dec << std::endl;

            // Wait for write-back (simplified)
            CacheMessage resp;
            l1_to_l2_data_ports[owner_id]->read(resp);

            // Update the block with latest data
            if (resp.data) {
                memcpy(blocks[block_idx].data, resp.data, BLOCK_SIZE);
            }
            blocks[block_idx].dirty = false;
            blocks[block_idx].state = BlockState::V;
        }
        // state = V
        // Send data to requester
        CacheMessage resp(address, MessageType::DATA, Scope::NONE, -1, blocks[block_idx].data);
        l2_to_l1_ports[requester_id]->write(resp);

        std::cout << "L2: Sent data for 0x" << std::hex << address << std::dec << " to L1-" << requester_id << std::endl;
    }

    // Handle GetO (get ownership) request
    void handleGetO(uint64_t address, int block_idx, int requester_id, Scope scope) {
        std::cout << "L2: Handling GetO from L1-" << requester_id << " for 0x" << std::hex << address << std::dec << std::endl;
        // state = I
        if (block_idx == -1) {
            // Cache miss - fetch from memory
            block_idx = getVictimBlock();

            // If victim block is owned, need to handle eviction
            if (blocks[block_idx].state == BlockState::O) {
                handleOwnedEviction(block_idx);
            } else if (blocks[block_idx].state == BlockState::V) {
                handleValidEviction(block_idx);
            } else {
                handleMemAcquire(block_idx);
            }

            // Simulate fetching from memory
            blocks[block_idx].tag = address;
            blocks[block_idx].state = BlockState::O;
            blocks[block_idx].dirty = false;

            std::cout << "L2: Fetched 0x" << std::hex << address << std::dec << " from memory" << std::endl;
        }
        // state = O
        // Check if the block is owned by another L1
        int owner_id = -1;
        if (owner_map.find(address) != owner_map.end()) {
            owner_id = owner_map[address];
        }

        if (owner_id != -1 && owner_id != requester_id) {
            // Request write-back from current owner
            CacheMessage req(address, MessageType::REQ_WRITE_BACK, Scope::NONE, -1);
            l2_to_l1_ctrl_ports[owner_id]->write(req);

            std::cout << "L2: Requested write-back from L1-" << owner_id << " for 0x" << std::hex << address << std::dec << std::endl;

            // Wait for write-back (simplified)
            CacheMessage resp;
            l1_to_l2_data_ports[owner_id]->read(resp);

            // Update the block with latest data
            if (resp.data) {
                memcpy(blocks[block_idx].data, resp.data, BLOCK_SIZE);
            }
            blocks[block_idx].dirty = false;
        }
        // state = V
        // Update ownership
        owner_map[address] = requester_id;
        blocks[block_idx].state = BlockState::O;

        // Send data to requester
        CacheMessage resp(address, MessageType::DATA, Scope::NONE, -1, blocks[block_idx].data);
        l2_to_l1_ports[requester_id]->write(resp);

        std::cout << "L2: Transferred ownership of 0x" << std::hex << address << std::dec << " to L1-" << requester_id << std::endl;
    }

    // Handle write-back request
    void handleWriteBack(uint64_t address, int block_idx, int l1_id, uint8_t *data) {
        std::cout << "L2: Handling write-back from L1-" << l1_id << " for 0x" << std::hex << address << std::dec << std::endl;
        // state = I
        if (block_idx == -1) {
            // Block not in L2, allocate it
            block_idx = getVictimBlock();

            // If victim block is owned, need to handle eviction
            if (blocks[block_idx].state == BlockState::O) {
                handleOwnedEviction(block_idx);
            } else if (blocks[block_idx].state == BlockState::V) {
                handleValidEviction(block_idx);
            } else {
                handleMemAcquire(block_idx);
            }

            blocks[block_idx].tag = address;
            blocks[block_idx].state = BlockState::V;
        }

        // Update block data
        if (data) {
            memcpy(blocks[block_idx].data, data, BLOCK_SIZE);
        }
        blocks[block_idx].dirty = true;
        // state = O
        // If this was the owner, clear ownership
        if (owner_map.find(address) != owner_map.end() && owner_map[address] == l1_id) {
            owner_map.erase(address);
            blocks[block_idx].state = BlockState::V;
            std::cout << "L2: Ownership of 0x" << std::hex << address << std::dec << " removed from L1-" << l1_id << std::endl;
        }
        // state = V
        // Send ACK to L1
        CacheMessage resp(address, MessageType::ACK, Scope::NONE, -1);
        l2_to_l1_ports[l1_id]->write(resp);
    }


    void handleMemAcquire(int block_idx) {}

    // Handle eviction of an owned block
    void handleValidEviction(int block_idx) {
        bool dirty = blocks[block_idx].dirty;

        if (dirty) {
            // DAHU MEM
        }
    }

    // Handle eviction of an owned block
    void handleOwnedEviction(int block_idx) {
        uint64_t address = blocks[block_idx].tag;
        int owner_id = owner_map[address];

        std::cout << "L2: Evicting owned block 0x" << std::hex << address << std::dec << " owned by L1-" << owner_id << std::endl;

        // Request write-back from owner
        CacheMessage req(address, MessageType::REQ_WRITE_BACK, Scope::NONE, -1);
        l2_to_l1_ctrl_ports[owner_id]->write(req);

        // Wait for write-back (simplified)
        CacheMessage resp;
        l1_to_l2_data_ports[owner_id]->read(resp);

        // Clear ownership
        owner_map.erase(address);

        // DAHU MEM
    }
};

// Top-level module to connect L1 and L2 caches
class GPUMemorySystem : public sc_module {
public:
    SC_HAS_PROCESS(GPUMemorySystem);

    GPUMemorySystem(sc_module_name name) : sc_module(name) {
        // Create L1 cache controllers
        for (int i = 0; i < NUM_L1_CACHES; i++) {
            std::string l1_name = "L1_Cache_" + std::to_string(i);
            l1_caches[i] = new L1CacheController(l1_name.c_str(), i);
        }

        // Create L2 cache controller
        l2_cache = new L2CacheController("L2_Cache");

        // Create FIFOs for communication
        for (int i = 0; i < NUM_L1_CACHES; i++) {
            l1_to_l2_fifos[i] = new sc_fifo<CacheMessage>(10);
            l1_to_l2_data_fifos[i] = new sc_fifo<CacheMessage>(10);
            l2_to_l1_fifos[i] = new sc_fifo<CacheMessage>(10);
            l2_to_l1_ctrl_fifos[i] = new sc_fifo<CacheMessage>(10);
            // Connect ports
            l1_caches[i]->l1_to_l2_port(*l1_to_l2_fifos[i]);
            l1_caches[i]->l1_to_l2_data_port(*l1_to_l2_data_fifos[i]);
            l1_caches[i]->l2_to_l1_port(*l2_to_l1_fifos[i]);
            l1_caches[i]->l2_to_l1_ctrl_port(*l2_to_l1_ctrl_fifos[i]);

            l2_cache->l1_to_l2_ports[i](*l1_to_l2_fifos[i]);
            l2_cache->l1_to_l2_data_ports[i](*l1_to_l2_data_fifos[i]);
            l2_cache->l2_to_l1_ports[i](*l2_to_l1_fifos[i]);
            l2_cache->l2_to_l1_ctrl_ports[i](*l2_to_l1_ctrl_fifos[i]);
        }

        // Register test process
        SC_THREAD(testLRCC);
    }

    ~GPUMemorySystem() {
        for (int i = 0; i < NUM_L1_CACHES; i++) {
            delete l1_caches[i];
            delete l1_to_l2_fifos[i];
            delete l1_to_l2_data_fifos[i];
            delete l2_to_l1_fifos[i];
            delete l2_to_l1_ctrl_fifos[i];
        }
        delete l2_cache;
    }

    // Test process - simulates GPU threads performing memory operations
    void testLRCC() {
        // Test data
        uint8_t data1[BLOCK_SIZE] = {1};
        uint8_t data2[BLOCK_SIZE] = {2};

        // Test scenario:
        std::cout << "=== Starting LRCC Test ===" << std::endl;

        // 1. L1_0 does a CTA-scoped acquire load
        std::cout << "\n[Test] L1_0: CTA-scoped acquire load from 0x1000" << std::endl;
        l1_caches[0]->processMemOp(0x1000, MemOp::ACQUIRE_LOAD, Scope::CTA);

        // 2. L1_0 does a CTA-scoped release store
        std::cout << "\n[Test] L1_0: CTA-scoped release store to 0x1000" << std::endl;
        l1_caches[0]->processMemOp(0x1000, MemOp::RELEASE_STORE, Scope::CTA, data1);

        // 3. L1_1 does a GPU-scoped acquire load (will cause self-invalidation)
        std::cout << "\n[Test] L1_1: GPU-scoped acquire load from 0x1000" << std::endl;
        l1_caches[1]->processMemOp(0x1000, MemOp::ACQUIRE_LOAD, Scope::GPU);

        // 4. L1_1 does a GPU-scoped release store (will get ownership)
        std::cout << "\n[Test] L1_1: GPU-scoped release store to 0x1000" << std::endl;
        l1_caches[1]->processMemOp(0x1000, MemOp::RELEASE_STORE, Scope::GPU, data2);

        // 5. L1_0 does another GPU-scoped acquire load
        std::cout << "\n[Test] L1_0: GPU-scoped acquire load from 0x1000" << std::endl;
        l1_caches[0]->processMemOp(0x1000, MemOp::ACQUIRE_LOAD, Scope::GPU);

        std::cout << "\n=== LRCC Test Complete ===" << std::endl;

        // End simulation
        sc_stop();
    }

private:
    L1CacheController *l1_caches[NUM_L1_CACHES];
    L2CacheController *l2_cache;
    sc_fifo<CacheMessage> *l1_to_l2_fifos[NUM_L1_CACHES];
    sc_fifo<CacheMessage> *l1_to_l2_data_fifos[NUM_L1_CACHES];
    sc_fifo<CacheMessage> *l2_to_l1_fifos[NUM_L1_CACHES];
    sc_fifo<CacheMessage> *l2_to_l1_ctrl_fifos[NUM_L1_CACHES];
};

// Main function
// int sc_main(int argc, char *argv[]) {
//     // Create and start simulation
//     GPUMemorySystem gpu_memory_system("GPU_Memory_System");

//     std::cout << "Starting SystemC simulation..." << std::endl;
//     sc_start();
//     std::cout << "Simulation completed at " << sc_time_stamp() << std::endl;

//     return 0;
// }