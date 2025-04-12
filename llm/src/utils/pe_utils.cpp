#include "stdarg.h"

#include "macros/macros.h"
#include "utils/pe_utils.h"

bool PE_is_sum_start(int peid) { return peid / PE_GRID_X == 0; }

bool PE_is_weight_end(int peid) { return peid / PE_GRID_X == PE_GRID_X - 1; }

bool PE_is_data_end(int peid) { return peid % PE_GRID_X == PE_GRID_X - 1; }

void test_fill_data(int batch, int count, ...) {
    va_list args;
    va_start(args, count);

    for (int i = 0; i < count; i++) {
        float *a = va_arg(args, float *);
        int size_a = va_arg(args, int);
        if (i == 0) {
            for (int j = 0; j < batch; j++) {
                for (int k = 0; k < size_a; k++) {
                    a[j * size_a + k] = k;
                }
            }
        } else {
            for (int j = 0; j < size_a; j++) {
                a[j] = j;
            }
        }
    }

    va_end(args);
}