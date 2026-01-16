#include <getopt.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

#include <parser-spef.hpp>

#include "Read_train/Read.h"
#include "Tree/inc/build_tree.h"
#include "Tree/inc/calc_delay.h"
#include "Tree/inc/io.h"

using namespace std;
namespace fs = std::filesystem;

#ifndef PROJECT_ROOT
#define PROJECT_ROOT "."
#endif

// 需要本地调试时可打开（会使用默认路径和 group）
// #define __DEBUG__

extern unordered_map<string, vector<Input_info>> netlist_info;

// 与 course.cpp 对齐：把 [ ] 变成 _，避免 netlist_info 的 key 对不上
static inline string NormalizeName(string name)
{
    for (char &c : name) {
        if (c == '[' || c == ']') c = '_';
    }
    return name;
}

static inline double EdgeR(const Topology &topo, int u, int v)
{
    if (u < 0 || v < 0 || u >= (int)topo.nodes.size() || v >= (int)topo.nodes.size()) return 0.0;
    for (const auto &pr : topo.nodes[u].neighbors) {
        if (pr.first == v) return pr.second;
    }
    return 0.0;
}

// D2M：由 (m1, m2) 计算 50% 延时（ps）。注意：这里返回 t/ln2，与 src/calc_delay.cpp 的实现保持一致。
static inline double DelayFromMoments(double m1_ps, double m2_ps2)
{
    if (m1_ps <= 0.0) return 0.0;

    const double ln2 = std::log(2.0);
    const double single_pole_delay = m1_ps;

    const double s1 = m1_ps;
    const double s2 = 0.5 * m2_ps2;
    const double p = s1 * s1 - s2;
    const double disc = s1 * s1 - 4.0 * p;
    if (p <= 0.0 || disc <= 0.0) return single_pole_delay;

    const double sqrt_disc = std::sqrt(disc);
    double tau1 = 0.5 * (s1 + sqrt_disc);
    double tau2 = 0.5 * (s1 - sqrt_disc);

    if (tau1 <= 0.0 || tau2 <= 0.0 || std::abs(tau1 - tau2) < 1e-12) return single_pole_delay;

    auto v = [&](double t) {
        double e1 = std::exp(-t / tau1);
        double e2 = std::exp(-t / tau2);
        return 1.0 - (tau1 * e1 - tau2 * e2) / (tau1 - tau2);
    };
    auto dv = [&](double t) {
        double e1 = std::exp(-t / tau1);
        double e2 = std::exp(-t / tau2);
        return (e1 - e2) / (tau1 - tau2);
    };

    const double target = 0.5;

    // Newton
    double t = single_pole_delay;
    for (int iter = 0; iter < 20; ++iter) {
        double f = v(t) - target;
        double df = dv(t);
        if (std::abs(df) < 1e-15) break;
        double t_new = t - f / df;
        if (!(t_new > 0.0) || !std::isfinite(t_new)) break;
        if (std::abs(t_new - t) < 1e-6) return t_new / ln2;
        t = t_new;
    }

    // Bisection
    double lo = 0.0;
    double hi = 5.0 * single_pole_delay;
    if (hi <= 0.0) hi = 5.0 * m1_ps;
    double vlo = v(lo) - target;
    double vhi = v(hi) - target;
    if (vlo * vhi > 0.0) return single_pole_delay;

    for (int i = 0; i < 60; ++i) {
        double mid = 0.5 * (lo + hi);
        double vm = v(mid) - target;
        if (std::abs(vm) < 1e-6) return mid / ln2;
        if (vlo * vm < 0.0) {
            hi = mid;
            vhi = vm;
        } else {
            lo = mid;
            vlo = vm;
        }
    }
    return 0.5 * (lo + hi) / ln2;
}

static void WriteHeader(ofstream &csv)
{
    csv << "net_name,out_real_name,real_input_name";

    // base 34
    csv << ",num_stages,rho,Elmore_ps,subtree_Elmore_ps";
    for (int i = 1; i <= 10; ++i) csv << ",R" << i;
    for (int i = 1; i <= 10; ++i) csv << ",C_ds" << i;
    for (int i = 1; i <= 10; ++i) csv << ",StageDelay" << i;

    // delay
    csv << ",std_ps,err_ps,rel_err,metric_type,metric_err";

    // path
    csv << ",branch_R_ohm,branch_hops,input_total_deg";

    // moment
    csv << ",second_moment_ps2,d2m_delay_ps";

    // subtree
    csv << ",extra_R_after_input,extra_C_after_input";

    // geom
    csv << ",pin_x,pin_y,drv_x,drv_y,dx,dy,dist_L1,dist_L2";

    // net stats
    csv << ",max_R_dist_all,max_R_dist_inputs,avg_R_dist_inputs"
        << ",r_segments,rc_nodes,leaf_nodes,branch_nodes,total_R"
        << ",ground_cap_sum,input_count_net";

    csv << "\n";
}

static inline double GetDelay(const std::vector<std::pair<std::string, double>> &delays,
                              const std::string &key)
{
    for (const auto &p : delays) {
        if (p.first == key) return p.second;
    }
    return -1.0;
}

int main(int argc, char **argv)
{
    int spef_num = 0;
    char *file_path = nullptr;
    string out_csv;

#ifdef __DEBUG__
    string default_file_path = "D:\\vscode\\Delay_calc\\Data";
    file_path = (char *)malloc((strlen(default_file_path.c_str()) + 1));
    strcpy(file_path, default_file_path.c_str());
    spef_num = 0;
#endif

    int option_index = 0;
    static struct option long_options[] = {
        {"file_path", required_argument, 0, 'f'},
        {"spef_num", required_argument, 0, 'm'},
        {"out_csv", required_argument, 0, 'o'},
        {0, 0, 0, 0}};

    int c;
    while ((c = getopt_long(argc, argv, "f:m:o:", long_options, &option_index)) != -1) {
        switch (c) {
        case 'f':
            file_path = (char *)malloc((strlen(optarg) + 1));
            strcpy(file_path, optarg);
            break;
        case 'm':
            spef_num = atoi(optarg);
            break;
        case 'o':
            out_csv = optarg;
            break;
        default:
            break;
        }
    }

    if (!file_path) {
        cerr << "Missing --file_path" << endl;
        return 1;
    }

    // 读取 netlist / delay / SPEF（与 course.cpp 统一）
    if (load_netlist_and_delay(file_path, spef_num) != 0) return 1;
    spef::Spef parser;
    if (load_spef(file_path, spef_num, parser) != 0) return 1;

    if (out_csv.empty()) {
        fs::path out_dir = fs::path(PROJECT_ROOT) / "py" / "feature_csv";
        std::error_code ec;
        fs::create_directories(out_dir, ec);
        out_csv = (out_dir / (string("path_features_group") + to_string(spef_num) + ".csv")).string();
    }

    ofstream csv(out_csv, ios::out | ios::trunc);
    if (!csv.is_open()) {
        cerr << "Failed to open " << out_csv << endl;
        return 1;
    }
    WriteHeader(csv);

    const double INF = numeric_limits<double>::infinity();
    // 单位约定：res=ohm，cap=fF，输出 ps
    const double pin_load_unit_factor = 1e3;   // 若 Input.pin_cap 单位与 caps 不同，可调整以换算到 fF
    const double cap_ff_to_ps_factor = 1e-3;   // R(ohm)*C(fF) -> ps 的系数

    for (auto &net : parser.nets) {
        string out_name;
        string out_real_name;
        vector<tuple<string, Input_info>> inputs;
        vector<Input_info> paths;
        unordered_map<string, pair<double, double>> pin_coord;

        bool missing_in_netlist = false;
        for (auto &connection : net.connections) {
            if (connection.coordinate.has_value()) {
                auto &t = *connection.coordinate;
                pin_coord[connection.name] = {std::get<0>(t), std::get<1>(t)};
            }

            if (connection.direction == spef::ConnectionDirection::OUTPUT) {
                out_name = connection.name;
                int index = connection.name.rfind(':');
                int id = stoi(connection.name.substr(1, index - 1));
                out_real_name = NormalizeName(parser.name_map[id] + '/' + connection.name.substr(index + 1));

                auto it = netlist_info.find(out_real_name);
                if (it == netlist_info.end()) missing_in_netlist = true;
                else paths = it->second;
            }
        }
        if (missing_in_netlist || out_name.empty()) continue;

        // driver 坐标（无坐标则 0）
        double drv_x = 0.0, drv_y = 0.0;
        {
            auto it_drv = pin_coord.find(out_name);
            if (it_drv != pin_coord.end()) {
                drv_x = it_drv->second.first;
                drv_y = it_drv->second.second;
            }
        }

        // 收集 inputs
        for (auto &connection : net.connections) {
            if (connection.direction != spef::ConnectionDirection::INPUT) continue;

            int index = connection.name.rfind(':');
            int id = stoi(connection.name.substr(1, index - 1));
            string input_real = NormalizeName(parser.name_map[id] + '/' + connection.name.substr(index + 1));

            for (auto &p : paths) {
                if (input_real == p.name) {
                    inputs.emplace_back(connection.name, p);
                    break;
                }
            }
        }
        if (inputs.empty()) continue;

        // 建树（与 course.cpp 统一的实现）
        Topology topo = BuildTopologyFromRess(net, out_name, inputs);
        FillCapsFromNet(net, topo, out_name, inputs);

        // 计算 Elmore（填充 parent_idx / subtree_cap / elmore_delay 等字段）
        // 注意：我们只调用一次 advanced 版本，用它来建立 parent_idx 等树信息。
        (void)ComputeElmoreDelays_advanced(net, out_name, inputs, topo, pin_load_unit_factor, cap_ff_to_ps_factor);

        // 直接复用“推理时的 ML 特征计算函数”来获取 base-34 特征（并保证 10 段向量特征是 pure cap）
        std::vector<MLFeatureDumpRow> ml_rows;
        ml_rows.reserve(inputs.size());
        auto adv_res = ComputeElmoreDelays_advanced(net, out_name, inputs, topo,
                                           pin_load_unit_factor, cap_ff_to_ps_factor);

        (void)ML_fix(out_name, inputs, topo,
                    pin_load_unit_factor, cap_ff_to_ps_factor,
                    /*feature_mode=*/34,
                    /*split_mode=*/true,
                    /*fit_mode=*/MODE_LINEAR,adv_res,
                    /*dump_rows=*/&ml_rows);

        std::unordered_map<std::string, const MLFeatureDumpRow*> ml_row_map;
        ml_row_map.reserve(ml_rows.size() * 2);
        for (auto &r : ml_rows) {
            ml_row_map.emplace(r.inp_name, &r);
        }

        const int root_idx = topo.base_for_special;
        if (root_idx < 0 || root_idx >= (int)topo.nodes.size()) continue;

        // net 级统计（度数）
        int rc_nodes = 0, leaf_nodes = 0, branch_nodes = 0;
        for (size_t i = 0; i < topo.nodes.size(); ++i) {
            int deg = (int)topo.nodes[i].neighbors.size();
            if (deg > 0) {
                rc_nodes++;
                if ((int)i != root_idx && deg == 1) leaf_nodes++;
                else if (deg >= 3) branch_nodes++;
            }
        }

        // children（以 parent_idx 为树方向：child.parent_idx = parent）
        vector<vector<int>> children(topo.nodes.size());
        for (int v = 0; v < (int)topo.nodes.size(); ++v) {
            int p = topo.nodes[v].parent_idx;
            if (p >= 0 && p < (int)topo.nodes.size()) children[p].push_back(v);
        }

        // root → all 的电阻距离（树上唯一路径即可）
        vector<double> dist_R(topo.nodes.size(), INF);
        dist_R[root_idx] = 0.0;
        // 简单 DFS
        vector<int> stack;
        stack.push_back(root_idx);
        while (!stack.empty()) {
            int u = stack.back();
            stack.pop_back();
            for (int v : children[u]) {
                double r_uv = EdgeR(topo, u, v);
                dist_R[v] = dist_R[u] + r_uv;
                stack.push_back(v);
            }
        }

        double max_R_dist_all = 0.0;
        for (double d : dist_R) {
            if (d < INF / 2 && d > max_R_dist_all) max_R_dist_all = d;
        }

        double max_R_dist_inputs = 0.0;
        double sum_R_dist_inputs = 0.0;
        int cnt_R_dist_inputs = 0;
        for (const auto &inp : inputs) {
            int idx = -1;
            try {
                idx = MapNodeNameToIndex(get<0>(inp), out_name, inputs, topo.base_for_special, topo.name_to_idx);
            } catch (...) {
                idx = -1;
            }
            if (idx >= 0 && idx < (int)dist_R.size() && dist_R[idx] < INF / 2) {
                double br = dist_R[idx];
                sum_R_dist_inputs += br;
                cnt_R_dist_inputs++;
                max_R_dist_inputs = max(max_R_dist_inputs, br);
            }
        }
        double avg_R_dist_inputs = (cnt_R_dist_inputs > 0) ? (sum_R_dist_inputs / cnt_R_dist_inputs) : 0.0;

        // 统计项
        const int r_segments = (int)net.ress.size();
        const double total_R_net = topo.total_resistor;
        const double sum_ground_cap = topo.total_capacitance;

        // 预计算：input pin 的 load cap（用于 m2 的 cap*m1 汇总、以及 subtree_elmore 的 path-only 后缀 C）
        unordered_map<int, double> sink_load_cap;
        sink_load_cap.reserve(inputs.size() * 2);
        for (const auto &inp : inputs) {
            try {
                int idx = MapNodeNameToIndex(get<0>(inp), out_name, inputs, topo.base_for_special, topo.name_to_idx);
                sink_load_cap[idx] = get<1>(inp).pin_cap * pin_load_unit_factor;
            } catch (...) {
            }
        }

        // extra_R/C_after_input：input 节点之后的子树额外 RC
        vector<double> extra_R_sub(topo.nodes.size(), 0.0);
        vector<double> extra_C_sub(topo.nodes.size(), 0.0);
        {
            function<void(int)> dfs_sub = [&](int u) {
                double sumR = 0.0;
                double sumC = 0.0;
                for (int v : children[u]) {
                    dfs_sub(v);
                    double r_uv = EdgeR(topo, u, v);
                    sumR += r_uv + extra_R_sub[v];
                    sumC += topo.nodes[v].ground_cap + extra_C_sub[v];
                }
                extra_R_sub[u] = sumR;
                extra_C_sub[u] = sumC;
            };
            dfs_sub(root_idx);
        }

        // m2（ps^2）：subtree_cap_mul_m1 的后序 + m2 的前序
        vector<double> subtree_cap_mul_m1(topo.nodes.size(), 0.0);
        vector<double> m2_ps2(topo.nodes.size(), 0.0);
        {
            function<void(int)> dfs_cap_m1 = [&](int u) {
                double sum = 0.0;
                if (u != root_idx) {
                    sum += topo.nodes[u].ground_cap * topo.nodes[u].elmore_delay;
                    auto it = sink_load_cap.find(u);
                    if (it != sink_load_cap.end()) sum += it->second * topo.nodes[u].elmore_delay;
                }
                for (int v : children[u]) {
                    dfs_cap_m1(v);
                    sum += subtree_cap_mul_m1[v];
                }
                subtree_cap_mul_m1[u] = sum;
            };
            dfs_cap_m1(root_idx);

            function<void(int)> dfs_m2 = [&](int u) {
                for (int v : children[u]) {
                    double r_uv = EdgeR(topo, u, v);
                    m2_ps2[v] = m2_ps2[u] + 2.0 * r_uv * subtree_cap_mul_m1[v] * cap_ff_to_ps_factor;
                    dfs_m2(v);
                }
            };
            m2_ps2[root_idx] = 0.0;
            dfs_m2(root_idx);
        }

        // 对每个 input 输出一行
        for (const auto &inp : inputs) {
            const string &inp_name = get<0>(inp);
            const auto &info = get<1>(inp);

            int pin_idx = -1;
            try {
                pin_idx = MapNodeNameToIndex(inp_name, out_name, inputs, topo.base_for_special, topo.name_to_idx);
            } catch (...) {
                pin_idx = -1;
            }
            if (pin_idx < 0 || pin_idx >= (int)topo.nodes.size()) continue;
            if (topo.nodes[pin_idx].parent_idx == -1) continue; // 与 root 不连通

            // root→pin 路径（用 parent_idx 回溯）
            vector<int> path_nodes;
            int cur = pin_idx;
            while (cur != -1) {
                path_nodes.push_back(cur);
                if (cur == root_idx) break;
                cur = topo.nodes[cur].parent_idx;
            }
            if (path_nodes.empty() || path_nodes.back() != root_idx) continue;
            reverse(path_nodes.begin(), path_nodes.end());
            if (path_nodes.size() < 2) continue;

            // ===== base-34 特征：直接取 ML_fix(...) 的 dump（确保与推理一致） =====
            auto it_ml = ml_row_map.find(inp_name);
            if (it_ml == ml_row_map.end()) continue;
            const std::vector<double> &feats = it_ml->second->feats;
            if ((int)feats.size() < 4) continue;

            int num_stages = (int)std::llround(feats[0]);
            double rho = feats[1];
            double elmore_ps = feats[2];
            double subtree_elmore_ps = feats[3];

            // path_total_res 仍按原逻辑重新算一遍，供“path”特征使用（与 ML_fix 内部一致的 EdgeR 取值）
            double path_total_res = 0.0;
            {
                int stages_calc = (int)path_nodes.size() - 1;
                for (int k = 0; k < stages_calc; ++k) {
                    int u = path_nodes[k];
                    int v = path_nodes[k + 1];
                    path_total_res += EdgeR(topo, u, v);
                }

                double rho_calc = (topo.total_resistor > 1e-9) ? (path_total_res / topo.total_resistor) : 0.0;
                if (std::abs(rho_calc - rho) > 1e-6) {
                    std::cerr << "[warn] rho mismatch: net=" << net.name
                              << " out=" << out_name
                              << " inp=" << inp_name
                              << " rho(calc)=" << rho_calc
                              << " rho(ml)=" << rho
                              << "\n";
                }
            }

            // 10 段向量特征：纯下游电容版本（由 ML_fix 计算）
            double R_stage[10] = {0};
            double C_stage[10] = {0};
            double SD_stage[10] = {0};
            if ((int)feats.size() >= 34) {
                int idx = 4;
                for (int i = 0; i < 10; ++i) R_stage[i] = feats[idx++];
                for (int i = 0; i < 10; ++i) C_stage[i] = feats[idx++];
                for (int i = 0; i < 10; ++i) SD_stage[i] = feats[idx++];
            }

            // 基础三列
            csv << net.name << "," << out_real_name << "," << info.name;

            // base 34
            csv << "," << num_stages << "," << rho << "," << elmore_ps << "," << subtree_elmore_ps;
            for (int i = 0; i < 10; ++i) csv << "," << R_stage[i];
            for (int i = 0; i < 10; ++i) csv << "," << C_stage[i];
            for (int i = 0; i < 10; ++i) csv << "," << SD_stage[i];

            // delay 相关（注意：训练时不要把 std/err 当输入特征）
            {
                double std_ps = info.delay * 1000.0;
                double err_ps = elmore_ps - std_ps;
                double rel_err = (std_ps != 0.0) ? (err_ps / std_ps) : 0.0;

                const double DELAY_THRESHOLD_PS = 50.0;
                bool metric_is_rel = (std_ps >= DELAY_THRESHOLD_PS);
                string metric_type = metric_is_rel ? "REL" : "ABS";
                double metric_err = metric_is_rel ? std::abs(rel_err) : std::abs(err_ps);

                csv << "," << std_ps << "," << err_ps << "," << rel_err << "," << metric_type << "," << metric_err;
            }

            // path
            {
                int input_total_deg = (int)topo.nodes[pin_idx].neighbors.size();
                csv << "," << path_total_res << "," << num_stages << "," << input_total_deg;
            }

            // moment
            {
                double second_moment_ps2 = (pin_idx >= 0 && pin_idx < (int)m2_ps2.size()) ? m2_ps2[pin_idx] : 0.0;
                double d2m_delay_ps = DelayFromMoments(elmore_ps, second_moment_ps2);
                csv << "," << second_moment_ps2 << "," << d2m_delay_ps;
            }

            // subtree
            {
                double extra_R_after_input = (pin_idx >= 0 && pin_idx < (int)extra_R_sub.size()) ? extra_R_sub[pin_idx] : 0.0;
                double extra_C_after_input = (pin_idx >= 0 && pin_idx < (int)extra_C_sub.size()) ? extra_C_sub[pin_idx] : 0.0;
                csv << "," << extra_R_after_input << "," << extra_C_after_input;
            }

            // geom
            {
                double pin_x = 0.0, pin_y = 0.0;
                auto it_xy = pin_coord.find(inp_name);
                if (it_xy != pin_coord.end()) {
                    pin_x = it_xy->second.first;
                    pin_y = it_xy->second.second;
                }
                double dx = pin_x - drv_x;
                double dy = pin_y - drv_y;
                double dist_L1 = std::fabs(dx) + std::fabs(dy);
                double dist_L2 = std::sqrt(dx * dx + dy * dy);
                csv << "," << pin_x << "," << pin_y << "," << drv_x << "," << drv_y
                    << "," << dx << "," << dy << "," << dist_L1 << "," << dist_L2;
            }

            // net
            {
                csv << "," << max_R_dist_all << "," << max_R_dist_inputs << "," << avg_R_dist_inputs
                    << "," << r_segments << "," << rc_nodes << "," << leaf_nodes << "," << branch_nodes
                    << "," << total_R_net << "," << sum_ground_cap << "," << (int)inputs.size();
            }

            csv << "\n";
        }
    }

    if (file_path) {
        free(file_path);
        file_path = nullptr;
    }
    return 0;
}
