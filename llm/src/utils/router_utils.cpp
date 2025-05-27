#include "utils/router_utils.h"
#include "defs/global.h"
#include "macros/macros.h"
#include <queue>
#include <fstream>

Directions get_oppose_direction(Directions dir) {
    switch (dir) {
    case WEST:
        return EAST;
    case EAST:
        return WEST;
    case NORTH:
        return SOUTH;
    case SOUTH:
        return NORTH;
    default:
        return CENTER;
    }
}

int get_input_source(Directions dir, int pos) {
    int x = pos % GRID_X;
    int y = pos / GRID_X;

    switch (dir) {
    case WEST:
        x = (x - 1 + GRID_X) % GRID_X;
        break;
    case NORTH:
        y = (y + 1) % GRID_X;
        break;
    case EAST:
        x = (x + 1) % GRID_X;
        break;
    case SOUTH:
        y = (y - 1 + GRID_X) % GRID_X;
        break;
    default:
        return 0;
    }

    return y * GRID_X + x;
}

bool is_margin_core(int id) { return id % GRID_X == 0; }

int decide_next_hop(int id) {
    // 这个函数仅供测试使用
    // if (id / GRID_X % 2 == 0) return id+GRID_X;
    // if (id % 32 == 31) return id+1;
    // return id-GRID_X+1;

    return id + 1;
}

Directions get_next_hop(int des, int pos) {
    // 从pos发往des的下一个方向, 先X后Y
    if (is_margin_core(pos) && des == GRID_SIZE)
        return HOST;
    if (des == GRID_SIZE)
        return WEST; // CTODO: fix this

    int dx = des % GRID_X;
    int dy = des / GRID_X;

    int xx = pos % GRID_X;
    int yy = pos / GRID_X;

    if (dx == xx && dy == yy)
        return CENTER;
    else if (dx != xx) {
        if (dx > xx) {
            return EAST;
        } else {
            return WEST;
        }
    } else {
        if (dy > yy) {
            return NORTH;
        } else {
            return SOUTH;
        }
    }
}

Directions get_next_hop_r(int des, int pos) {
    // 从pos发往des的下一个方向, 先Y后X
    if (is_margin_core(pos) && des == GRID_SIZE)
        return HOST;
    if (des == GRID_SIZE)
        return WEST; // CTODO: fix this

    int dx = des % GRID_X;
    int dy = des / GRID_X;

    int xx = pos % GRID_X;
    int yy = pos / GRID_X;

    if (dx == xx && dy == yy)
        return CENTER;
    else if (dy != yy) {
        if (dy > yy) {
            return NORTH;
        } else {
            return SOUTH;
        }
    } else {
        if (dx > xx) {
            return EAST;
        } else {
            return WEST;
        }
    }
}