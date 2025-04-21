#include "assert.h"
#include "defs/global.h"
#include "monitor/monitor.h"
#include "systemc.h"
#include "trace/Event_engine.h"
#include "utils/file_utils.h"
#include "utils/simple_flags.h"
#include "utils/system_utils.h"
#include <iostream>

#include <SFML/Graphics.hpp>
using namespace std;

#define CHECK_C cout << total_cycle << endl;
Define_bool_opt("--help", g_flag_help, false, "show these help information");
Define_bool_opt("--node-mode", g_flag_node, false, "whether to sim in a node");
Define_string_opt("--config-file", g_flag_config_file, "../llm/test/config_gpt2_small.json", "config file");
Define_string_opt("--ttf-file", g_flag_ttf, "../font/NotoSansDisplay-Bold.ttf", "font ttf file");
Define_bool_opt("--use-dramsys", g_flag_dramsys, true, "whether to use DRAMSys");
Define_float_opt("--comp_util", g_flag_comp_util, 0.7, "computation and memory overlap");
Define_int64_opt("--MAC_SIZE", g_flag_mac_size, 128, "MAC size");

// ----------------------------------------------------------------------------
// all the individual layers' forward and backward passes
// B = batch_size, T = sequence_length, C = channels, V = vocab_size


// gpt2 架构

// https://www.bilibili.com/read/cv36513074/
// https://zhuanlan.zhihu.com/p/108231904


void layernorm_forward(float *out, float *inp, float *weight, float *bias, int B, int T, int C, ExuConfig tile_exu, int &total_cycle) {
    // reference:
    // https://pytorch.org/docs/stable/generated/torch.nn.LayerNorm.html
    // https://zhuanlan.zhihu.com/p/650231190 对最后一维特征做归一化
    // both inp and out are (B,T,C) of the activations+
    // mean and rstd are (B,T) buffers, to be used later in backward pass
    // at each position (b,t) of the input, the C-dimensional vector
    // of activations gets normalized, then scaled and shifted

    int cycle = 0;
    if (tile_exu.type == MAC_Array) {
        cycle = B * T * (8 * C + 5) / (2 * tile_exu.x_dims * tile_exu.y_dims);
    }

    float eps = 1e-5f;
    for (int b = 0; b < B; b++) {
        for (int t = 0; t < T; t++) {
            // seek to the input position inp[b,t,:]
            float *x = inp + b * T * C + t * C;
            // calculate the mean
            float m = 0.0f;
            for (int i = 0; i < C; i++) {
                m += x[i]; // compute: C
            }
            // mean
            m = m / C; // compute: 1
            // calculate the variance (without any bias correction)
            float v = 0.0f;
            for (int i = 0; i < C; i++) {
                float xshift = x[i] - m; // compute: C
                v += xshift * xshift;    // compute: 2C
            }
            v = v / C; // compute: 1
            // calculate the rstd (reciprocal standard deviation)
            // 标准差倒数
            float s = 1.0f / sqrtf(v + eps); // compute: 3
            // seek to the output position in out[b,t,:]
            float *out_bt = out + b * T * C + t * C;
            for (int i = 0; i < C; i++) {
                float n = (s * (x[i] - m));        // normalize // compute: 2C
                float o = n * weight[i] + bias[i]; // scale and shift // compute: 2C
                out_bt[i] = o;                     // write
            }
            // cache the mean and rstd for the backward pass later
            // mean[b * T + t] = m;
            // rstd[b * T + t] = s;
        }
    }

    cout << "layernorm" << endl;

    total_cycle += cycle;
}

void matmul_forward_naive(float *out, const float *inp, const float *weight, const float *bias, int B, int T, int C, int OC) {
// the most naive implementation of matrix multiplication
// this serves as an algorithmic reference, and as a fallback for
// unfriendly input shapes inside matmul_forward(), below.
#pragma omp parallel for collapse(2)
    for (int b = 0; b < B; b++) {
        for (int t = 0; t < T; t++) {
            int bt = b * T + t;
            for (int o = 0; o < OC; o++) {
                float val = (bias != NULL) ? bias[o] : 0.0f;
                for (int i = 0; i < C; i++) {
                    val += inp[bt * C + i] * weight[o * C + i];
                }
                out[bt * OC + o] = val;
            }
        }
    }
}

void matmul_forward_cycle(int tile_id, float *out, const float *inp, const float *weight, const float *bias, int B, int T, int C, int OC, ExuConfig tile_exu, int out_addr, int inp_addr,
                          int weight_addr, int bias_addr, DATATYPE datatype, int &total_cycle) {
    // most of the running time is spent here and in matmul_backward
    // therefore, the implementation below is very mildly optimized
    // this function is otherwise identical to that of matmul_forward_naive()
    // OC is short for "output channels"
    // inp is (B,T,C), weight is (OC, C), bias is (OC)
    // out will be (B,T,OC)

    // make sure the tiled loop will be correct or fallback to naive version

    u_int64_t dram_addr_tile = tile_id * dataset_words_per_tile;

    u_int64_t out_global_addr = dram_addr_tile + out_addr;

    u_int64_t inp_global_addr = dram_addr_tile + inp_addr;

    u_int64_t weight_global_addr = dram_addr_tile + weight_addr;

    u_int64_t bias_global_addr = dram_addr_tile + bias_addr;

    // 4bytes in a word

    // TODO vector load

    u_int64_t dram_time = 0;
    int data_byte = 0;

    // TODO discard
    u_int64_t time_fetched = 0;
    u_int64_t time_prefetched = 0;
    u_int64_t prefetch_tag = 0;

    if (datatype == INT8) {
        data_byte = 1;
    } else if (datatype == FP16) {
        data_byte = 2;
    }


    u_int64_t in_dcacheline = 0;
    for (int b = 0; b < B; b++) {
        for (int t = 0; t < T; t++) {
            for (int c = 0; c < C; c++) {

                in_dcacheline = (inp_global_addr >> dcache_words_in_line_log2) >> 2;
                //(int tX,int tY, u_int64_t dram_addr, u_int64_t timer,
                // u_int64_t & time_fetched, u_int64_t & time_prefetched,
                // u_int64_t & prefetch_tag, bool prefetch){

                dram_time += check_dcache(0, 0, in_dcacheline << (dcache_words_in_line_log2 + 2), total_cycle + dram_time, time_fetched, time_prefetched, prefetch_tag, false);
                inp_global_addr += data_byte;
                // printf("out_global_addr: %d  dataset_words_per_tile: %d \n",
                // out_global_addr, dataset_words_per_tile);
#ifdef ASSERT
                assert(inp_global_addr < dataset_words_per_tile);

#endif
            }
        }
    }

    printf("input matmul_forward_cycle %ld \n", dram_time);


    u_int64_t weight_dcacheline = 0;

    for (int oc = 0; oc < OC; oc++) {
        for (int c = 0; c < C; c++) {

            weight_dcacheline = (weight_global_addr >> dcache_words_in_line_log2) >> 2;
            //(int tX,int tY, u_int64_t dram_addr, u_int64_t timer, u_int64_t &
            // time_fetched, u_int64_t & time_prefetched, u_int64_t &
            // prefetch_tag, bool prefetch){

            dram_time += check_dcache(0, 0, weight_dcacheline << (dcache_words_in_line_log2 + 2), total_cycle + dram_time, time_fetched, time_prefetched, prefetch_tag, false);
            weight_global_addr += data_byte;
            // printf("out_global_addr: %d  dataset_words_per_tile: %d \n",
            // out_global_addr, dataset_words_per_tile);
#ifdef ASSERT
            assert(weight_global_addr < dataset_words_per_tile);

#endif
        }
    }


    printf("weight matmul_forward_cycle %ld \n", dram_time);


    u_int64_t bias_dcacheline = 0;

    for (int oc = 0; oc < OC; oc++) {


        bias_dcacheline = (bias_global_addr >> dcache_words_in_line_log2) >> 2;
        //(int tX,int tY, u_int64_t dram_addr, u_int64_t timer, u_int64_t &
        // time_fetched, u_int64_t & time_prefetched, u_int64_t & prefetch_tag,
        // bool prefetch){

        dram_time += check_dcache(0, 0, bias_dcacheline << (dcache_words_in_line_log2 + 2), total_cycle + dram_time, time_fetched, time_prefetched, prefetch_tag, false);
        bias_global_addr += data_byte;
        // printf("out_global_addr: %d  dataset_words_per_tile: %d \n",
        // out_global_addr, dataset_words_per_tile);
#ifdef ASSERT
        assert(bias_global_addr < dataset_words_per_tile);

#endif
    }


    printf("bias matmul_forward_cycle %ld \n", dram_time);


    // 4bytes in a word

    u_int64_t cycle = 0;
    if (tile_exu.type == MAC_Array) {
        cycle = B * T * C * OC * 2 / (2 * tile_exu.x_dims * tile_exu.y_dims);
    }


    u_int64_t overlap_time = 0;


    if (dram_time > cycle) {
        overlap_time = dram_time;

    } else {
        overlap_time = cycle;
    }


    u_int64_t out_dcacheline = 0;

    for (int b = 0; b < B; b++) {
        for (int t = 0; t < T; t++) {
            for (int oc = 0; oc < OC; oc++) {

                out_dcacheline = (out_global_addr >> dcache_words_in_line_log2) >> 2;
                //(int tX,int tY, u_int64_t dram_addr, u_int64_t timer,
                // u_int64_t & time_fetched, u_int64_t & time_prefetched,
                // u_int64_t & prefetch_tag, bool prefetch){

                overlap_time += check_dcache(0, 0, out_dcacheline << (dcache_words_in_line_log2 + 2), total_cycle + overlap_time, time_fetched, time_prefetched, prefetch_tag, false);
                out_global_addr += data_byte;
                // printf("out_global_addr: %d  dataset_words_per_tile: %d \n",
                // out_global_addr, dataset_words_per_tile);
#ifdef ASSERT
                assert(out_global_addr < dataset_words_per_tile);

#endif
            }
        }
    }

    printf("output matmul_forward_cycle %ld \n", overlap_time);


    const int LOOP_UNROLL = 8;
    if (B * T % LOOP_UNROLL != 0) {
        matmul_forward_naive(out, inp, weight, bias, B, T, C, OC);

        cout << "matmul_forward_naive" << endl;
        return;
    }

// collapse the B and T loops into one and turn it into a strided loop.
// then we can tile the inner loop, and reuse the loaded weight LOOP_UNROLL many
// times
#pragma omp parallel for
    for (int obt = 0; obt < B * T; obt += LOOP_UNROLL) {
        for (int o = 0; o < OC; o++) {
            // we'll keep LOOP_UNROLL many results in registers
            float result[LOOP_UNROLL];
            // initialize the bias, if it exists
            for (int ibt = 0; ibt < LOOP_UNROLL; ibt++) {
                result[ibt] = (bias != NULL) ? bias[o] : 0.0f;
            }
            // inner loops. Because we do LOOP_UNROLL steps of inner bt, we can
            // cache the value of weight[i + o * C] and reuse it. we compile
            // with -Ofast, so the compiler will turn the inner loop into FMAs
            for (int i = 0; i < C; i++) {
                float w = weight[i + o * C];
                for (int ibt = 0; ibt < LOOP_UNROLL; ibt++) {
                    int bt = obt + ibt;
                    result[ibt] += inp[bt * C + i] * w; // compute: 2C*OC
                }
            }
            // write back results to main memory
            for (int ibt = 0; ibt < LOOP_UNROLL; ibt++) {
                int bt = obt + ibt;
                out[bt * OC + o] = result[ibt];
            }
        }
    }

    cout << "matmul" << endl;

    total_cycle += overlap_time;
}


void matmul_forward(float *out, const float *inp, const float *weight, const float *bias, int B, int T, int C, int OC, ExuConfig tile_exu, int &total_cycle) {
    // most of the running time is spent here and in matmul_backward
    // therefore, the implementation below is very mildly optimized
    // this function is otherwise identical to that of matmul_forward_naive()
    // OC is short for "output channels"
    // inp is (B,T,C), weight is (OC, C), bias is (OC)
    // out will be (B,T,OC)

    // make sure the tiled loop will be correct or fallback to naive version

    int cycle = 0;
    if (tile_exu.type == MAC_Array) {
        cycle = B * T * C * OC * 2 / (2 * tile_exu.x_dims * tile_exu.y_dims);
    }


    const int LOOP_UNROLL = 8;
    if (B * T % LOOP_UNROLL != 0) {
        matmul_forward_naive(out, inp, weight, bias, B, T, C, OC);

        cout << "matmul_forward_naive" << endl;
        return;
    }

// collapse the B and T loops into one and turn it into a strided loop.
// then we can tile the inner loop, and reuse the loaded weight LOOP_UNROLL many
// times
#pragma omp parallel for
    for (int obt = 0; obt < B * T; obt += LOOP_UNROLL) {
        for (int o = 0; o < OC; o++) {
            // we'll keep LOOP_UNROLL many results in registers
            float result[LOOP_UNROLL];
            // initialize the bias, if it exists
            for (int ibt = 0; ibt < LOOP_UNROLL; ibt++) {
                result[ibt] = (bias != NULL) ? bias[o] : 0.0f;
            }
            // inner loops. Because we do LOOP_UNROLL steps of inner bt, we can
            // cache the value of weight[i + o * C] and reuse it. we compile
            // with -Ofast, so the compiler will turn the inner loop into FMAs
            for (int i = 0; i < C; i++) {
                float w = weight[i + o * C];
                for (int ibt = 0; ibt < LOOP_UNROLL; ibt++) {
                    int bt = obt + ibt;
                    result[ibt] += inp[bt * C + i] * w; // compute: 2C*OC
                }
            }
            // write back results to main memory
            for (int ibt = 0; ibt < LOOP_UNROLL; ibt++) {
                int bt = obt + ibt;
                out[bt * OC + o] = result[ibt];
            }
        }
    }

    cout << "matmul" << endl;

    total_cycle += cycle;
}

void attention_forward(float *out, float *preatt, float *att, float *inp, int B, int T, int C, int NH, ExuConfig tile_exu, int &total_cycle) {
    // input is (B, T, 3C) holding the query, key, value (Q, K, V) vectors
    // preatt, att are (B, NH, T, T). NH = number of heads, T = sequence length
    // that holds the pre-attention and post-attention scores (used in backward)
    // output is (B, T, C)
    // attention is the only layer that mixes information across time
    // every other operation is applied at every (b,t) position independently
    // (and of course, no layer mixes information across batch)
    int C3 = C * 3;
    int hs = C / NH; // head size
    float scale = 1.0 / sqrtf(hs);

    int cycle = 0;
    // calculate cycles
    if (tile_exu.type == MAC_Array) {
        cycle = (B * NH * T * (T - 1) / 2 * (4 * hs + 5)) / (2 * tile_exu.x_dims * tile_exu.y_dims);
    }

#pragma omp parallel for collapse(3)
    for (int b = 0; b < B; b++) {
        for (int t = 0; t < T; t++) {
            for (int h = 0; h < NH; h++) {
                float *query_t = inp + b * T * C3 + t * C3 + h * hs;
                float *preatt_bth = preatt + b * NH * T * T + h * T * T + t * T;
                float *att_bth = att + b * NH * T * T + h * T * T + t * T;

                // pass 1: calculate query dot key and maxval
                float maxval = -10000.0f; // TODO something better
                // 这里已经是causal了
                for (int t2 = 0; t2 <= t; t2++) {
                    float *key_t2 = inp + b * T * C3 + t2 * C3 + h * hs + C; // +C because it's key

                    // (query_t) dot (key_t2)
                    float val = 0.0f;
                    for (int i = 0; i < hs; i++) {
                        val += query_t[i] * key_t2[i]; // compute: B*NH*T*(T-1)/2*(2*hs+1)
                    }
                    val *= scale;
                    if (val > maxval) {
                        maxval = val;
                    }
                    // 没有减去maxval的att的值
                    preatt_bth[t2] = val;
                }

                // pass 2: calculate the exp and keep track of sum
                // maxval is being calculated and subtracted only for numerical
                // stability
                float expsum = 0.0f;
                for (int t2 = 0; t2 <= t; t2++) {
                    float expv = expf(preatt_bth[t2] - maxval); // compute: B*NH*T*(T-1)/2*3
                    expsum += expv;
                    // 减去 maxval 后的 指数数值
                    att_bth[t2] = expv;
                }
                // 分母的指数和
                float expsum_inv = expsum == 0.0f ? 0.0f : 1.0f / expsum;

                // pass 3: normalize to get the softmax
                for (int t2 = 0; t2 < T; t2++) {
                    if (t2 <= t) {
                        att_bth[t2] *= expsum_inv; // compute: B*NH*T*(T-1)/2
                    } else {
                        // causal attention mask. not strictly necessary to set
                        // to zero here only doing this explicitly for debugging
                        // and checking to PyTorch
                        att_bth[t2] = 0.0f;
                    }
                }

                // pass 4: accumulate weighted values into the output of
                // attention
                float *out_bth = out + b * T * C + t * C + h * hs;
                for (int i = 0; i < hs; i++) {
                    out_bth[i] = 0.0f;
                }
                for (int t2 = 0; t2 <= t; t2++) {
                    float *value_t2 = inp + b * T * C3 + t2 * C3 + h * hs + C * 2; // +C*2 because it's value
                    float att_btht2 = att_bth[t2];
                    for (int i = 0; i < hs; i++) {
                        out_bth[i] += att_btht2 * value_t2[i]; // compute: B*NH*T*(T-1)/2*2*hs
                    }
                }
            }
        }
    }

    cout << "attention" << endl;

    total_cycle += cycle;
}

#define GELU_SCALING_FACTOR sqrtf(2.0f / M_PI)
void gelu_forward(float *out, float *inp, int N, ExuConfig tile_exu, int &total_cycle) {
    // (approximate) GeLU elementwise non-linearity in the MLP block of
    // Transformer
    int cycle = 0;
    // calculate cycles
    if (tile_exu.type == MAC_Array) {
        cycle = (11 * N) / (2 * tile_exu.x_dims * tile_exu.y_dims);
    }

    for (int i = 0; i < N; i++) {
        float x = inp[i];
        float cube = 0.044715f * x * x * x;                                   // compute: 3N
        out[i] = 0.5f * x * (1.0f + tanhf(GELU_SCALING_FACTOR * (x + cube))); // compute: (2+2+4)N
    }

    cout << "gelu" << endl;

    total_cycle += cycle;
}

void residual_forward(float *out, float *inp1, float *inp2, int N, ExuConfig tile_exu, int &total_cycle) {
    int cycle = 0;
    // calculate cycles
    if (tile_exu.type == MAC_Array) {
        cycle = N / (2 * tile_exu.x_dims * tile_exu.y_dims);
    }

    for (int i = 0; i < N; i++) {
        out[i] = inp1[i] + inp2[i]; // compute: N
    }

    cout << "residual" << endl;

    total_cycle += cycle;
}

void softmax_forward(float *probs, float *logits, int B, int T, int V, int Vp, ExuConfig tile_exu, int &total_cycle) {
    // output: probs are (B,T,Vp) of the probabilities (sums to 1.0 in each b,t
    // position) input: logits is (B,T,Vp) of the unnormalized log probabilities
    // Vp is the padded vocab size (for efficiency), V is the "real" vocab size
    // example: Vp is 50304 and V is 50257
    int cycle = 0;
    // calculate cycles
    if (tile_exu.type == MAC_Array) {
        cycle = (B * T * 4 * V) / (2 * tile_exu.x_dims * tile_exu.y_dims);
    }

#pragma omp parallel for collapse(2)
    for (int b = 0; b < B; b++) {
        for (int t = 0; t < T; t++) {
            // probs <- softmax(logits)
            float *logits_bt = logits + b * T * Vp + t * Vp;
            float *probs_bt = probs + b * T * Vp + t * Vp;

            // maxval is only calculated and subtracted for numerical stability
            float maxval = -10000.0f; // TODO something better
            for (int i = 0; i < V; i++) {
                if (logits_bt[i] > maxval) {
                    maxval = logits_bt[i];
                }
            }
            float sum = 0.0f;
            for (int i = 0; i < V; i++) {
                probs_bt[i] = expf(logits_bt[i] - maxval); // compute: B*T*3V
                sum += probs_bt[i];
            }
            // note we only loop to V, leaving the padded dimensions
            for (int i = 0; i < V; i++) {
                probs_bt[i] /= sum; // compute: B*T*V
            }
            // for extra super safety we may wish to include this too,
            // forcing the probabilities here to be zero, but it shouldn't
            // matter
            for (int i = V; i < Vp; i++) {
                probs_bt[i] = 0.0f;
            }
        }
    }

    cout << "softmax" << endl;

    total_cycle += cycle;
}

void crossentropy_forward(float *losses, float *probs, int *targets, int B, int T, int Vp, ExuConfig tile_exu, int &total_cycle) {
    // output: losses is (B,T) of the individual losses at each position
    // input: probs are (B,T,Vp) of the probabilities
    // input: targets is (B,T) of integers giving the correct index in logits
    int cycle = 0;
    // calculate cycles
    if (tile_exu.type == MAC_Array) {
        cycle = (B * T) / (2 * tile_exu.x_dims * tile_exu.y_dims);
    }

    for (int b = 0; b < B; b++) {
        for (int t = 0; t < T; t++) {
            // loss = -log(probs[target])
            float *probs_bt = probs + b * T * Vp + t * Vp;
            int ix = targets[b * T + t];
            losses[b * T + t] = -logf(probs_bt[ix]); // compute: B*T
        }
    }

    cout << "crossentropy" << endl;

    total_cycle += cycle;
}

int task_n() {
    GPT2Config config;

    initialize_cache_structures();

    init_perf_counters();

    ExuConfig tile_exu = {MAC_Array, 16, 16};

    // read in hyperparameters
    size_t maxT, V, Vp, L, NH, C; // size_t to prevent int overflow

    print_configuration(cout);

    config.max_seq_len = maxT = 1024;
    config.vocab_size = V = 32;
    config.num_layers = L = 32;
    config.num_heads = NH = 32;
    config.channels = C = 32;
    config.padded_vocab_size = Vp = 32;

    int B = 4;  // batch size 4 (i.e. 4 independent token sequences will be
                // trained on)
    int T = 64; // sequence length 64 (i.e. each sequence is 64 tokens long).
                // must be <= maxT, which is 1024 for GPT-2

    cout << "Hello, World!" << endl;
    u_int64_t time_fetched = 0;
    u_int64_t time_prefetched = 0;
    u_int64_t prefetch_tag = 0;
    //(int tX,int tY, u_int64_t dram_addr, u_int64_t timer, u_int64_t &
    // time_fetched, u_int64_t & time_prefetched, u_int64_t & prefetch_tag, bool
    // prefetch){
    check_dcache(0, 0, 1, 0, time_fetched, time_prefetched, prefetch_tag, false);

    float *l_qkv = (float *)mallocCheck(B * T * 3 * C * sizeof(float));
    float *l_ln1 = (float *)mallocCheck(B * T * C * sizeof(float));
    float *l_qkvw = (float *)mallocCheck(3 * C * C * sizeof(float));
    float *l_qkvb = (float *)mallocCheck(3 * C * sizeof(float));

    float *residual = (float *)mallocCheck(B * T * C * sizeof(float));

    float *l_ln1w = (float *)mallocCheck(C * sizeof(float));
    float *l_ln1b = (float *)mallocCheck(C * sizeof(float));

    float *l_atty = (float *)mallocCheck(B * T * C * sizeof(float));
    float *l_preatt = (float *)mallocCheck(B * NH * T * T * sizeof(float));
    float *l_att = (float *)mallocCheck(B * NH * T * T * sizeof(float));

    float *l_attproj = (float *)mallocCheck(B * T * C * sizeof(float));
    float *l_attprojw = (float *)mallocCheck(C * C * sizeof(float));
    float *l_attprojb = (float *)mallocCheck(C * sizeof(float));

    float *l_residual2 = (float *)mallocCheck(B * T * C * sizeof(float));

    float *l_ln2 = (float *)mallocCheck(B * T * C * sizeof(float));
    float *l_ln2_mean = (float *)mallocCheck(B * T * sizeof(float));
    float *l_ln2_rstd = (float *)mallocCheck(B * T * sizeof(float));
    float *l_ln2w = (float *)mallocCheck(C * sizeof(float));
    float *l_ln2b = (float *)mallocCheck(C * sizeof(float));

    float *l_fch = (float *)mallocCheck(B * T * 4 * C * sizeof(float));
    float *l_fch_gelu = (float *)mallocCheck(B * T * 4 * C * sizeof(float));
    float *l_fcw = (float *)mallocCheck(4 * C * C * sizeof(float));
    float *l_fcb = (float *)mallocCheck(4 * C * sizeof(float));
    float *l_fcprojw = (float *)mallocCheck(4 * C * C * sizeof(float));
    float *l_fcprojb = (float *)mallocCheck(C * sizeof(float));

    float *l_fcproj = (float *)mallocCheck(B * T * C * sizeof(float));
    float *l_residual3 = (float *)mallocCheck(B * T * C * sizeof(float));

    int total_cycle = 0;


    try {
        layernorm_forward(l_ln1, residual, l_ln1w, l_ln1b, B, T, C, tile_exu, total_cycle);
        CHECK_C
        matmul_forward_cycle(0, l_qkv, l_ln1, l_qkvw, l_qkvb, B, T, C, 3 * C, tile_exu, 0, 0, 0, 0, DATATYPE::INT8, total_cycle);
        CHECK_C
        attention_forward(l_atty, l_preatt, l_att, l_qkv, B, T, C, NH, tile_exu, total_cycle);
        CHECK_C
        matmul_forward(l_attproj, l_atty, l_attprojw, l_attprojb, B, T, C, C, tile_exu, total_cycle);
        CHECK_C
        residual_forward(l_residual2, residual, l_attproj, B * T * C, tile_exu, total_cycle);
        CHECK_C
        layernorm_forward(l_ln2, l_residual2, l_ln2w, l_ln2b, B, T, C, tile_exu, total_cycle);
        CHECK_C
        matmul_forward(l_fch, l_ln2, l_fcw, l_fcb, B, T, C, 4 * C, tile_exu, total_cycle);
        CHECK_C
        gelu_forward(l_fch_gelu, l_fch, B * T * 4 * C, tile_exu, total_cycle);
        CHECK_C
        matmul_forward(l_fcproj, l_fch_gelu, l_fcprojw, l_fcprojb, B, T, 4 * C, C, tile_exu, total_cycle);
        CHECK_C
        residual_forward(l_residual3, l_residual2, l_fcproj, B * T * C, tile_exu, total_cycle);
        CHECK_C
        // inp is (B,T,C), weight is (OC, C), bias is (OC)
        // out will be (B,T,OC)
    } catch (const std::exception &e) {
        std::cerr << "捕获到异常: " << e.what() << std::endl;
    }

    cout << "total cycles : " << total_cycle << endl;

    free(l_qkv);
    free(l_ln1);
    free(l_qkvw);
    free(l_qkvb);
    free(residual);
    free(l_ln1w);
    free(l_ln1b);
    free(l_atty);
    free(l_att);
    free(l_preatt);
    free(l_attproj);
    free(l_attprojb);
    free(l_attprojw);
    free(l_fcb);
    free(l_fch);
    free(l_fch_gelu);
    free(l_fcproj);
    free(l_fcprojb);
    free(l_fcprojw);
    free(l_fcw);
    free(l_residual2);
    free(l_residual3);
    free(l_ln2);
    free(l_ln2_mean);
    free(l_ln2_rstd);
    free(l_ln2b);
    free(l_ln2w);

    destroy_cache_structures();
    return 0;
}

int sc_main(int argc, char *argv[]) {
    use_node = false;
    use_DramSys = false;

    simple_flags::parse_args(argc, argv);
    if (!simple_flags::get_unknown_flags().empty()) {
        string content;
        for (auto it : simple_flags::get_unknown_flags()) {
            content += "'" + it + "', ";
        }
        content.resize(content.size() - 2); // remove last ', '
        content.append(".");
        cout << "unknown option(s): " << content.c_str();
        return -1;
    }

    if (g_flag_help) {
        simple_flags::print_args_info();
        return 0;
    }

    if (g_flag_dramsys) {
        use_DramSys = true;
    }

    if (g_flag_node) {
        use_node = true;
    }

    comp_util = g_flag_comp_util;

    tile_exu.x_dims = g_flag_mac_size;
    tile_exu.y_dims = g_flag_mac_size;
    tile_sfu.x_dims = g_flag_mac_size * 16;

    init_grid(g_flag_config_file.c_str());
    init_global_members();

    init_dram_areas();
    initialize_cache_structures();
    init_perf_counters();

    Event_engine *event_engine = new Event_engine("event-engine");
    Monitor monitor("monitor", event_engine, g_flag_config_file.c_str(), g_flag_ttf.c_str());
    sc_trace_file *tf = sc_create_vcd_trace_file("Cchip_1");
    sc_clock clk("clk", CYCLE, SC_NS);
    // sc_trace(tf, clk, "clk");
    // sc_trace(tf, monitor.memInterface->host_data_sent_i[0],
    // "monitor.memInterface->host_data_sent_i[0]"); sc_trace(tf,
    // monitor.memInterface->host_data_sent_i[1],
    // "monitor.memInterface->host_data_sent_i[1]"); sc_trace(tf,
    // monitor.memInterface->host_data_sent_i[2],
    // "monitor.memInterface->host_data_sent_i[2]"); sc_trace(tf,
    // monitor.memInterface->host_data_sent_i[3],
    // "monitor.memInterface->host_data_sent_i[3]");
    sc_start();

    destroy_dram_areas();
    destroy_cache_structures();
    // event_engine->dump_traced_file();
    sc_close_vcd_trace_file(tf);

    system_cleanup();

    delete event_engine;
    return 0;
}