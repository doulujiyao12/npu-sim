// NOTE: WARN: this file is deprecated. this file is used for configs which use
// layer as the minimal unit. now we change into core as the minimal unit.


// #pragma once
// #include "config_helper_base.h"

// class Config_helper : public config_helper_base {
// public:
//     vector<LayerConfig> layers; // 读入config文件之后，优先读入这里
//     map<int, vector<int>> layer_sm; // 记录所有layer的拆分合并信息

//     Config_helper(string filename);

//     CoreConfig *find_config(int id);
//     bool judge_is_end(int i);

//     void generate_prims(int i);
//     comp_base *parse_prim(json j);
//     void calculate_address(bool do_loop);

//     void print_self();

//     void fill_queue_start(queue<Msg> *q);
// };

// void Config_helper::print_self() {
//     for (auto core : coreconfigs) {
//         cout << "[Core " << core.id << "]\n";

//         cout << "\tCore prims: \n";
//         for (auto work : core.worklist) {
//             for (auto prim : work.prims_in_loop) {
//                 prim->print_self("\t\t");
//             }
//         }
//     }

//     cout <<
//     "\n\n------------------------------------------------------------\n\n";

//     for (auto core : coreconfigs) {
//         cout << "[Core " << core.id << "]\n";

//         cout << "\tCore cast: \n";
//         for (auto work : core.worklist) {
//             for (auto cast : work.cast) {
//                 cout << "\t-> " << cast.dest << ", weight = " << cast.weight
//                 << (cast.loopout == FALSE ? (" (loopout: FALSE)") :
//                 (cast.loopout == TRUE ? (" (loopout: TRUE)") : ( "( loopout:
//                 BOTH)"))) << endl;
//             }
//             cout << "\tWork recv_cnt: " << work.recv_cnt << endl;
//         }
//     }
// }

// Config_helper::Config_helper(string filename) {
//     json j;
//     // namespace fs = std::filesystem;
//     // std::cout << "当前工作目录: " << fs::current_path() << std::endl;
//     ifstream jfile(filename);
//     jfile >> j;

//     // 收集相关参数
//     auto vt = j["vars"];
//     for (auto v : vt.items()) {
//         vtable.push_back(make_pair(v.key(), v.value()));
//     }

//     auto st = j["source"];
//     for (auto s : st) {
//         source_info.push_back(make_pair(s["dest"], find_var(s["size"])));
//     }

//     // 处理layer信息
//     auto lt = j["layers"];
//     for (int i = 0; i < lt.size(); i++) {
//         LayerConfig layer = lt[i];

//         // 配置layer的计算原语
//         layer.prim = parse_prim(lt[i]);

//         // 配置layer的循环信息
//         if (lt[i].contains("loop")) {
//             layer.loop = find_var(lt[i]["loop"]);
//         } else {
//             layer.loop = 1;
//         }

//         if (lt[i].contains("repeat")) {
//             layer.repeat = find_var(lt[i]["repeat"]);
//         } else {
//             layer.repeat = 1;
//         }

//         layers.push_back(layer);
//     }

//     // 从这里开始将layer划分到core上，首先决定所有的layer拆分和合并（CTODO:
//     目前只考虑拆分） int core_cnt = 0; bool sm_flag[layers.size()]; for (int
//     i = 0; i < layers.size(); i++) {
//         sm_flag[i] = false;

//         LayerConfig layer = layers[i];
//         comp_base *prim = layer.prim;
//         if (layer.split != NO_SPLIT) {
//             // 进行拆分
//             sm_flag[i] = true;
//             // 查看是matmul还是conv
//             if (typeid(*prim) == typeid(Matmul_f)) {
//                 Matmul_f *mp = (Matmul_f *)prim;

//                 // 将本层拆分
//                 vector<int> t_cores;
//                 // 按照split_slice的个数拆分成对应的coreconfigs
//                 // 这里是修改并增加计算原语
//                 for (int i = 0; i < layer.split_slice; i++) {
//                     // 首先生成拆分之后的新原语
//                     Matmul_f *cp = new Matmul_f();
//                     cp->B = mp->B, cp->T = mp->T, cp->C = mp->C, cp->OC =
//                     mp->OC;

//                     if (layer.split_dim == 1) cp->OC /= layer.split_slice;
//                     else if (layer.split_dim == 2) cp->C /=
//                     layer.split_slice;
//                     // CTODO: 需要将对应的权重数据按照维度切割

//                     cp->initialize();

//                     CoreConfig c;
//                     for (auto cast : layer.cast) c.cast.push_back(cast);
//                     c.prims.push_back(cp);
//                     c.loop = layer.loop;
//                     c.repeat = layer.repeat;

//                     coreconfigs.push_back(c);
//                     t_cores.push_back(core_cnt);
//                     core_cnt++;
//                 }

//                 layer_sm.insert(make_pair(layer.id, t_cores));
//             } else {
//                 // CTODO: conv
//             }
//         } else {
//             CoreConfig c;
//             for (auto cast : layer.cast) c.cast.push_back(cast);
//             c.prims.push_back(layer.prim);
//             c.loop = layer.loop;
//             c.repeat = layer.repeat;

//             coreconfigs.push_back(c);

//             //
//             记录layer的拆分合并情况，被划分到哪一个核上（这里的核编号是虚拟核编号，届时需要重新分配实际的物理核编号）
//             vector<int> t_cores;
//             t_cores.push_back(core_cnt);
//             layer_sm.insert(make_pair(layer.id, t_cores));

//             // cout << "push sm: " << layer.id << ": ";
//             // for (auto k : t_cores) cout << k << " ";
//             // cout << endl;
//             // 虚拟core id
//             core_cnt++;
//         }
//     }

//     // core_cnt代表在layer拆分合并完成之后一共用到了几个核
//     cout << "Altogether core count: " << core_cnt << endl;

//     // 随后将每一个虚拟核编号重新分配一个物理核编号上，保证较优的路由策略
//     // CTODO: 完善这个分配策略，目前使用简单的蛇形分配
//     // 实际物理的core id
//     int cur_core = 0;
//     // 虚拟的core id
//     int cntt = 0;
//     for (auto &core : coreconfigs) {
//         core.id = cur_core;
//         core.recv_tag = core.id;

//         cout << "core id map: " << cntt++ << " " << cur_core << endl;

//         if (cur_core%(2*GRID_X) < GRID_X) {
//             cur_core++;
//             if (cur_core%(2*GRID_X) == GRID_X)
//                 cur_core = cur_core-1+GRID_X;
//         } else {
//             cur_core--;
//             if (cur_core%(2*GRID_X) < GRID_X)
//                 cur_core = cur_core+1+GRID_X;
//         }
//     }

//     //
//     随后重新填写每一个core的cast，原先cast的dest填写的是layer编号，后来layer进行拆分合并之后，映射到了不同的core上，之后这些core又被分配了具体的物理编号
//     //
//     这里需要遍历所有的core，找到其所有的cast，将dest的layer编号改写为新的core物理编号。注意在layer进行拆分之后可能会产生一个layer对应多个core的情况。
//     map<int, int> ref_cnt;

//     for (auto &core : coreconfigs) {
//         vector<Cast> corecast = core.cast;
//         vector<Cast> newcast;
//         for (auto cast : corecast) {
//             int layer = cast.dest;

//             if (layer == -1) {
//                 Cast c = cast;
//                 newcast.push_back(c);
//                 continue;
//             }

//             vector<int> subs = layer_sm[layer];
//             for (auto sub : subs) {
//                 Cast c = cast;
//                 c.dest = coreconfigs[sub].id;
//                 ref_cnt[c.dest]++;
//                 newcast.push_back(c);
//             }
//         }

//         core.cast = newcast;
//     }

//     // 改写所有core的recv_cnt
//     for (auto &core : coreconfigs) {
//         core.recv_cnt = ref_cnt[core.id];
//         if (core.recv_cnt == 0) core.recv_cnt++;
//     }

//     // 随后在这里重新推入split和merge原语

//     for (int i = 0; i < layers.size(); i++) {
//         if (layers[i].split == NO_SPLIT) continue;

//         comp_base *prim = layers[i].prim;
//         comp_base *split_prim = nullptr;
//         comp_base *merge_prim = nullptr;
//         // 这里是在需要split的计算原语前后增加 split 和 merge 原语
//         // DTODO连续两个需要split 计算原语
//         if (typeid(*prim) == typeid(Matmul_f)) {
//             Matmul_f *mp = (Matmul_f *) prim;
//             split_prim = new Split_matmul();
//             merge_prim = new Merge_matmul();

//             Split_matmul *sp = (Split_matmul *) split_prim;
//             sp->dim = layers[i].split_dim;
//             sp->slice = layers[i].split_slice;
//             sp->parse_matmul(mp);

//             Merge_matmul *vp = (Merge_matmul *) merge_prim;
//             vp->dim = layers[i].split_dim;
//             vp->slice = layers[i].split_slice;
//             vp->parse_matmul(mp);

//             if (sp->dim == 1) {
//                 for (auto c : layer_sm[i-1]) {
//                     // split
//                     coreconfigs[c].prims.push_back(sp);
//                 }
//                 for (auto c : layer_sm[i+1]) {
//                     // merge
//                     vector<prim_base *> new_prims;
//                     new_prims.push_back(vp);
//                     for (auto p : coreconfigs[c].prims) {
//                         new_prims.push_back(p);
//                     }

//                     coreconfigs[c].prims = new_prims;
//                 }
//             } else if (sp->dim == 2) {
//                 for (auto c : layer_sm[i-1]) {
//                     // split
//                     coreconfigs[c].prims.push_back(sp);
//                     for (auto cast : coreconfigs[c].cast) {
//                         cast.weight = sp->slice;
//                     }
//                 }
//                 for (auto c : layer_sm[i+1]) {
//                     // merge
//                     vector<prim_base *> new_prims;
//                     new_prims.push_back(vp);
//                     for (auto p : coreconfigs[c].prims) {
//                         new_prims.push_back(p);
//                     }

//                     coreconfigs[c].prims = new_prims;
//                 }
//             }
//         } else {
//             // CTODO: conv
//         }
//     }

//     // 得到一个CoreConfig的map，生成相应的收发原语和地址

//     for (int i = 0; i < coreconfigs.size(); i++) {
//         generate_prims(i);
//     }

//     // 再去重新填写send的收发地址
//     // prims_in_loop 的地址
//     calculate_address(true);
//     // prims_last_loop 的地址
//     calculate_address(false);

//     print_self();
// }

// comp_base *Config_helper::parse_prim(json j) {
//     comp_base *p = nullptr;
//     string type = j.at("type");

//     if (type == "Dummy_p") p = new Dummy_p();
//     else if (type == "Layernorm_f") p = new Layernorm_f();
//     else if (type == "Attention_f") p = new Attention_f();
//     else if (type == "Conv_f") p = new Conv_f();
//     else if (type == "Max_pool") p = new Max_pool();
//     else if (type == "Gelu_f") p = new Gelu_f();
//     else if (type == "Matmul_f") p = new Matmul_f();
//     else if (type == "Relu_f") p = new Relu_f();
//     else if (type == "Residual_f") p = new Residual_f();

//     else {
//         cout << "Parse config prim: Not Implemented.\n";

//         sc_stop();
//     }

//     p->parse_json(j.at("var"));

//     return p;
// }

// CoreConfig *Config_helper::find_config(int id) {
//     for (auto &config : coreconfigs) {
//         if (config.id == id) {
//             return &config;
//         }
//     }

//     cout << "Failed to find config.\n";
//     return &coreconfigs[0];
// }

// bool Config_helper::judge_is_end(int i) {
//     // 判断一个核是否是汇节点
//     CoreConfig *c = &coreconfigs[i];
//     for (auto cast : c->cast) {
//         if (cast.dest == -1) return true;
//     }

//     return false;
// }

// void Config_helper::generate_prims(int i) {
//     // 处理单个核的原语，将其放入Coreconfig.prims中
//     CoreConfig *c = &coreconfigs[i];
//     //汇聚节点 1 core
//     bool is_end = judge_is_end(i); // 是不是计算图中的汇节点
//     if (is_end) end_cores++;

//     // 先生成loop中的原语
//     // 首先是recv，对应 RECV_DATA
//     c->prims_in_loop.push_back(new Recv_prim(RECV_DATA, c->recv_tag,
//     c->recv_cnt));

//     // 然后是comp，直接推c中的对应队列即可
//     for (auto prim : c->prims)
//         c->prims_in_loop.push_back(prim);

//     // 最后是send，如果是多播的话需要加入多个send原
//     // 这里的发送地址和接收地址先不填，等到后续统一填
//     // 按照cast 广播的方式添加对应数量的 send 原语数量
//     for (int j = 0; j < c->cast.size(); j++) {
//         auto ca = c->cast[j];
//         if (ca.loopout == TRUE) continue;

//         int dest = ca.dest;
//         int tag = dest;

//         c->prims_in_loop.push_back(new Send_prim(SEND_REQ, dest, tag));
//         c->prims_in_loop.push_back(new Recv_prim(RECV_ACK));
//         c->prims_in_loop.push_back(new Send_prim(SEND_DATA, dest, tag));
//     }

//     // 再生成最后一个loop的原语
//     c->prims_last_loop.push_back(new Recv_prim(RECV_DATA, c->recv_tag,
//     c->recv_cnt));
// map<int, int> delta_offset; // 用于记录每一个核的接收地址偏移

//     for (auto prim : c->prims) {
//         c->prims_last_loop.push_back(prim);
//     }

//     if (is_end) {
//         c->prims_last_loop.push_back(new Send_prim(SEND_DONE));
//         return;
//     }

//     for (int j = 0; j < c->cast.size(); j++) {
//         auto ca = c->cast[j];
//         if (ca.loopout == FALSE) continue;

//         int dest = ca.dest;
//         int tag = dest;

//         c->prims_last_loop.push_back(new Send_prim(SEND_REQ, dest, tag));
//         c->prims_last_loop.push_back(new Recv_prim(RECV_ACK));
//         c->prims_last_loop.push_back(new Send_prim(SEND_DATA, dest, tag));
//     }
// }

// void Config_helper::calculate_address(bool do_loop) {
//     // 遍历每一个核的prim，填写其中原语的收发地址
//     for (int i = 0; i < coreconfigs.size(); i++) {
//         // 初始化
//         // id 表示实际的core id
//         delta_offset[coreconfigs[i].id] = 0;
//     }

//     // 首先填写comp原语
//     for (int i = 0; i < coreconfigs.size(); i++) {
//         vector<prim_base *> *v = nullptr;
//         if (do_loop) v = &(coreconfigs[i].prims_in_loop);
//         else v = &(coreconfigs[i].prims_last_loop);

//         for (int j = v->size()-1; j >= 0; j--) {
//             auto p = (*v)[j];
//             if (!is_comp_prim(p)) continue;

//             comp_base *cp = (comp_base *) p;

//             // 从后往前填写
//             if (j < v->size()-1 && !is_comp_prim((*v)[j+1])) {
//                 // 如果是最后执行的原语，out不在下一个in的内部
//                 cp->out_offset = 0;
//                 cp->inp_offset = cp->out_size;
//             } else {
//                 // out在下一个原语in的开头，in在下一个原语in的结尾

//                 comp_base *next_cp = (comp_base *) (*v)[j+1];
//                 cp->out_offset = next_cp->inp_offset;

//                 // inp_size 是 输入 input + 权重 data 的大小之和
//                 cp->inp_offset = next_cp->inp_offset+next_cp->inp_size;
//             }

//             // 需要将delta offset置于第一个原语in的开头
//             if (j > 0 && !is_comp_prim((*v)[j-1])) {
//                 // 用于记录每一个核的接收地址偏移
//                 delta_offset[coreconfigs[i].id] = cp->inp_offset;
//             }

//             cp->data_offset = cp->inp_offset + cp->p_inp_size;
//         }
//     }

//     for (int i = 0; i < coreconfigs.size(); i++) {
//         // 遍历每一个核中的send原语
//         vector<prim_base *> *v = nullptr;
//         if (do_loop) v = &(coreconfigs[i].prims_in_loop);
//         else v = &(coreconfigs[i].prims_last_loop);

//         // CTODO: 假设平均分配输出
//         int output_size = 0;
//         int output_offset = 0;
//         int index = 0;

//         if (!do_loop && judge_is_end(i)) continue; // 汇节点

//         // 拿到这个核的output size
//         for (int j = v->size()-1; j >= 0; j--) {
//             auto p = (*v)[j];
//             if (is_comp_prim(p)) {
//                 comp_base *cp = (comp_base *) p;
//                 output_size = cp->out_size;
//                 output_offset = cp->out_offset;
//                 break;
//             }
//         }
//         // cast send 原语每一个有不同的 des_offset
//         for (auto &prim : (*v)) {
//             if (typeid(*prim) == typeid(Send_prim)) {
//                 Send_prim *temp = (Send_prim *) prim;
//                 if (temp->type != SEND_DATA) continue;

//                 int weight = coreconfigs[i].cast[index].weight;
//                 int slice_size = (output_size%weight) ?
//                 (output_size/weight+1) : (output_size/weight); int
//                 slice_size_in_bit = slice_size * sizeof(float); int pkg_nums
//                 = (slice_size_in_bit%M_D_DATA) ?
//                 (slice_size_in_bit/M_D_DATA+1) :
//                 (slice_size_in_bit/M_D_DATA); int end_length =
//                 slice_size_in_bit-(pkg_nums-1)*M_D_DATA; index++;

//                 // local offset
//                 temp->local_offset = output_offset;
//                 if (weight > 1) output_offset += slice_size; // CTODO: fix
//                 this

//                 // max pkg nums
//                 temp->max_packet = pkg_nums;

//                 // des offset
//                 temp->des_offset = delta_offset[temp->des_id];
//                 delta_offset[temp->des_id] += slice_size;

//                 // end length
//                 temp->end_length = end_length;
//             }
//         }
//     }
// }

// // 发送host 给 模型起始节点发送的用户input数据
// void Config_helper::fill_queue_start(queue<Msg> *q) {
//     for (auto source : source_info) {
//         int i = layer_sm[source.first][0];
//         int size = source.second;

//         int index = coreconfigs[i].id / GRID_X;
//         int pkg_index = 0;
//         int send_offset = delta_offset[coreconfigs[i].id];
//         int send_size_in_bit = size * sizeof(float);
//         int pkg_num = (send_size_in_bit%M_D_DATA) ?
//         (send_size_in_bit/M_D_DATA+1) : (send_size_in_bit/M_D_DATA);

//         for (int j = 1; j <= pkg_num; j++) {
//             // CTODO: 拿到真正的数据
//             sc_bv<M_D_DATA> d(0x1);
//             int length = M_D_DATA;
//             bool is_end_packet = j == pkg_num;
//             if (is_end_packet) {
//                 length = size*sizeof(float)-M_D_DATA*(pkg_num-1);
//             }

//             Msg m = Msg(j==pkg_num, DATA, j+pkg_index, coreconfigs[i].id,
//             send_offset+M_D_DATA*(j-1), coreconfigs[i].recv_tag, length, d);
//             m.source = GRID_SIZE;
//             q[index].push(m);
//         }
//     }
// }