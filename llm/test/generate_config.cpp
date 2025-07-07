#include <fstream>
#include <iostream>

using namespace std;

int main() {
    int TP, tag;
    cout << "输入TP大小：";
    cin >> TP;
    tag = 120;

    int start_core, group;
    start_core = TP;
    cout << "输入总共需要生成的组数：";
    cin >> group;

    cout << "文件将输出在gen_config.json中\n";

    fstream f;
    f.open("gen_config.json", ios::out);

    for (int t = 1; t < TP - 1; t++) {
        f << "{";
        f << "\"id\": " << t + 1 << ",";
        f << "\"prim_copy\": 1,";
        f << "\"worklist\": [";
        f << "{";
        f << "\"recv_cnt\": 1,";
        f << "\"cast\": [";
        f << "{";
        f << "\"dest\": " << 0 << ",";
        f << "\"tag\": " << tag << "}";
        f << "]";
        f << "},";
        f << "{";
        f << "\"recv_cnt\": 1,";
        f << "\"cast\": [";
        f << "{";
        f << "\"dest\": " << 0 << ",";
        f << "\"tag\": " << tag + 1 << "}";
        f << "]";
        f << "}";
        f << "]";
        f << "},";
    }

    for (int g = 0; g < group - 1; g++) {
        // 先生成主core
        f << "{";
        f << "\"id\": " << start_core + g * TP << ",";
        f << "\"prim_copy\": 0,";
        f << "\"worklist\": [";
        f << "{";
        f << "\"recv_cnt\": 1,";
        f << "\"cast\": [";
        for (int i = 0; i < TP - 1; i++) {
            f << "{";
            f << "\"dest\": " << start_core + g * TP + i + 1;
            f << "}";
            if (i != TP - 2) {
                f << ",";
            }
        }
        f << "]";
        f << "},";
        f << "{";
        f << "\"recv_cnt\": 0,";
        f << "\"cast\": []";
        f << "},";
        f << "{";
        f << "\"recv_cnt\": " << TP - 1 << ",";
        f << "\"recv_tag\": " << tag + 2 * (g + 1) << ",";
        f << "\"cast\": [";
        for (int i = 0; i < TP - 1; i++) {
            f << "{";
            f << "\"dest\": " << start_core + g * TP + i + 1;
            f << "}";
            if (i != TP - 2) {
                f << ",";
            }
        }
        f << "]";
        f << "},";
        f << "{";
        f << "\"recv_cnt\": 0,";
        f << "\"cast\": []";
        f << "},";
        f << "{";
        f << "\"recv_cnt\": " << TP - 1 << ",";
        f << "\"recv_tag\": " << tag + 2 * (g + 1) + 1 << ",";
        f << "\"cast\": [";
        f << "{";
        f << "\"dest\": "
          << ((g != group - 2) ? (start_core + (g + 1) * TP) : -1) << ",";
        f << "\"critical\": true";
        f << "}";
        f << "]";
        f << "}";
        f << "]";
        f << "},";

        // 再生成次core
        for (int t = 0; t < TP - 1; t++) {
            f << "{";
            f << "\"id\": " << start_core + g * TP + t + 1 << ",";
            f << "\"prim_copy\": 1,";
            f << "\"worklist\": [";
            f << "{";
            f << "\"recv_cnt\": 1,";
            f << "\"cast\": [";
            f << "{";
            f << "\"dest\": " << start_core + g * TP << ",";
            f << "\"tag\": " << tag + 2 * (g + 1) << "}";
            f << "]";
            f << "},";
            f << "{";
            f << "\"recv_cnt\": 1,";
            f << "\"cast\": [";
            f << "{";
            f << "\"dest\": " << start_core + g * TP << ",";
            f << "\"tag\": " << tag + 2 * (g + 1) + 1 << "}";
            f << "]";
            f << "}";
            f << "]";
            f << "}";

            if (t != TP - 2 || g != group - 2) {
                f << ",";
            }
        }
    }
}