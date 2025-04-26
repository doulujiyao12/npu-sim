#include "prims/gpu_base.h"
#include "utils/system_utils.h"

void gpu_base::parse_compose(json j)
{
    auto &data_gx = j["grid_x"];
    if (data_gx.is_number_integer())
        grid_x = data_gx;
    else
        grid_x = find_var(j["grid_x"]);

    auto &data_gy = j["grid_y"];
    if (data_gy.is_number_integer())
        grid_y = data_gy;
    else
        grid_y = find_var(j["grid_y"]);

    auto &data_bx = j["block_x"];
    if (data_bx.is_number_integer())
        block_x = data_bx;
    else
        block_x = find_var(j["block_x"]);

    auto &data_by = j["block_y"];
    if (data_by.is_number_integer())
        block_y = data_by;
    else
        block_y = find_var(j["block_y"]);

    auto &data_sm = j["require_sm"];
    if (data_sm.is_number_integer())
        req_sm = data_sm;
    else
        req_sm = find_var(j["require_sm"]);
}