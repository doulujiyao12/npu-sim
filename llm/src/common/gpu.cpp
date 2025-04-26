#include "common/memory.h"

void GpuPosLocator::addPair(const std::string &key, GpuPosKey value) {
    value.pos = addr_top;
    data_map[key] = value;
    addr_top += value.size;
}

void GpuPosLocator::findPair(std::string &key, int &result) {
    auto it = data_map.find(key);
    if (it != data_map.end()) {
        result = it->second.pos;
    } else {
        result = 0;
        cout << "[GpuPosLocator]: failed to find key " << key << " in GPU memory.\n";
    }
}

void GpuPosLocator::findPair(std::string &key, GpuPosKey &result) {
    auto it = data_map.find(key);
    if (it != data_map.end()) {
        result = it->second;
    } else {
        result = GpuPosKey();
        cout << "[GpuPosLocator]: failed to find key " << key << " in GPU memory.\n";
    }
}

void GpuPosLocator::clearAll() {
    data_map.clear();
}