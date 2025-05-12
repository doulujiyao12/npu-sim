#include <atomic>
#include <vector>

#include "monitor/config_helper_core.h"
#include "monitor/config_helper_gpu.h"
#include "monitor/config_helper_pd.h"
#include "monitor/mem_interface.h"
#include "prims/comp_prims.h"
#include "prims/norm_prims.h"
#include "utils/file_utils.h"
#include "utils/msg_utils.h"
#include "utils/prim_utils.h"

#include "link/chip_global_memory.h"


class GlobalMemInterface {
public:
    GlobalMemInterface::GlobalMemInterface(const sc_module_name &n, Event_engine *event_engine,
                        const char *config_name, const char *font_ttf);

    void init();

    ChipGlobalMemory *chipGlobalMemory;

};