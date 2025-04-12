#include "systemc.h"

#include "memory/dram/Dcachecore.h"
#include "prims/comp_base.h"
#include "prims/comp_prims.h"

void Split_conv::print_self(string prefix) {
    cout << prefix << "<split_conv>\n";
    cout << prefix << "\tslice: " << slice << ", new_H: " << new_H << endl;
    cout << prefix << "\tout_size: " << out_size << " , inp_size: " << inp_size << ", previous_inp_size: " << p_inp_size << endl;
    cout << prefix << "\toutput_offset: " << out_offset << ", input_offset: " << inp_offset << endl;
}

void Split_conv::parse_json(json j) {
    if (j.contains("dram_address")) {
        parse_address(j["dram_address"]);
    }

    if (j.contains("sram_address")) {
        parse_sram_label(j["sram_address"]);
    }
}

void Split_conv::parse_conv(Conv_f *p) {
    W = p->W, H = p->H, C = p->C, B = p->B;
    pX = p->pX, pY = p->pY;
    S = p->sY, K = p->kY;

    inp_size = W * H * C * B;
    p_inp_size = W * H * C * B;

    int oH = (H + 2 * pY - K) / S + 1;
    int pH = oH / slice;
    new_H = (pH - 1) * S + K;

    out_size = B * C * new_H * (W + 2 * pX) * slice;

    out_dim.push_back(B);
    out_dim.push_back(C);
    out_dim.push_back(new_H);
    out_dim.push_back(W + 2 * pX);
}

void Split_conv::deserialize(sc_bv<128> buffer) {
    inp_offset = buffer.range(23, 8).to_uint64();
    out_offset = buffer.range(39, 24).to_uint64();
    W = buffer.range(55, 40).to_uint64();
    H = buffer.range(71, 56).to_uint64();
    C = buffer.range(79, 72).to_uint64();
    pX = buffer.range(87, 80).to_uint64();
    pY = buffer.range(95, 88).to_uint64();
    S = buffer.range(99, 96).to_uint64();
    K = buffer.range(107, 100).to_uint64();

    slice = buffer.range(111, 108).to_uint64();
    B = buffer.range(115, 112).to_uint64();
}

sc_bv<128> Split_conv::serialize() {
    sc_bv<128> d;
    d.range(7, 0) = sc_bv<8>(0xe);
    d.range(23, 8) = sc_bv<16>(inp_offset);
    d.range(39, 24) = sc_bv<16>(out_offset);
    d.range(55, 40) = sc_bv<16>(W);
    d.range(71, 56) = sc_bv<16>(H);
    d.range(79, 72) = sc_bv<8>(C);
    d.range(87, 80) = sc_bv<8>(pX);
    d.range(95, 88) = sc_bv<8>(pY);
    d.range(99, 96) = sc_bv<4>(S);
    d.range(107, 100) = sc_bv<8>(K);

    d.range(111, 108) = sc_bv<4>(slice);
    d.range(115, 112) = sc_bv<4>(B);

    return d;
}
int Split_conv::task_core(TaskCoreContext &context) { return 0; }
int Split_conv::task() {


#if DUMMY == 1


#else


    float *dram_start = (float *)(dram_array[cid]);
    float *input = dram_start + inp_offset;
    float *output = dram_start + out_offset;

    std::vector<float> padded_input(B * C * (H + 2 * pY) * (W + 2 * pX), 0);

    for (int b = 0; b < B; ++b) {
        for (int c = 0; c < C; ++c) {
            for (int h = 0; h < H; ++h) {
                for (int w = 0; w < W; ++w) {
                    padded_input[b * C * (H + 2 * pY) * (W + 2 * pX) + c * (H + 2 * pY) * (W + 2 * pX) + (h + pY) * (W + 2 * pX) + (w + pX)] = input[b * C * H * W + c * H * W + h * W + w];
                }
            }
        }
    }

    int offset = 0;
    int new_W = W + 2 * pX;
    for (int s = 0; s < slice; s++) {
        for (int b = 0; b < B; b++) {
            for (int c = 0; c < C; c++) {
                for (int h = 0; h < new_H; h++) {
                    for (int w = 0; w < new_W; w++) {
                        // CTODO: finish this
                        int inp_h = new_H * s - (s - 1) * (K - S) + h;
                        output[offset++] = padded_input[b * C * (H + 2 * pY) * (W + 2 * pX) + c * (H + 2 * pY) * (W + 2 * pX) + inp_h * (W + 2 * pX) + w];
                    }
                }
            }
        }
    }
#endif
    return 0;
}

int Split_conv::sram_utilization(DATATYPE datatype) { return 0; }