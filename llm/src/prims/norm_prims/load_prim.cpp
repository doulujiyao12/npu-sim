#include "systemc.h"

#include "prims/base.h"
#include "prims/norm_prims.h"
#include "utils/prim_utils.h"

REGISTER_PRIM(Load_prim);

void Load_prim::printSelf() { cout << "<Load_prim>\n"; }

void Load_prim::deserialize(sc_bv<128> buffer) {}

sc_bv<128> Load_prim::serialize() {
    sc_bv<128> d;
    d.range(7, 0) = sc_bv<8>(PrimFactory::getInstance().getPrimId(name));

    return d;
}
int Load_prim::taskCoreDefault(TaskCoreContext &context) { return 0; }