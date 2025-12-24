#include <getopt.h>
#include <parser-spef.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <queue>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "Read_train/Read.h"

using namespace std;

#ifndef PROJECT_ROOT
#define PROJECT_ROOT "."
#endif

namespace fs = std::filesystem;


// 需要本地调试时可打开（会使用默认路径和 group）
// #define __DEBUG__

extern unordered_map<string, vector<Input_info>> netlist_info;

struct NodeInfo {
    double ground_cap = 0.0; // 对地电容（耦合电容按一半分配）
};

struct Topology {
    vector<NodeInfo> nodes;
    int base_for_special = 0;               // output / inputs 的索引起点
    unordered_map<string, int> name_to_idx; // SPEF 节点名 -> 索引
};

static inline int MapNodeNameToIndex(const string &name,
                                     const string &out_name,
                                     const vector<tuple<string, Input_info>> &inputs,
                                     int base_for_special,
                                     unordered_map<string, int> &cache)
{
    auto it = cache.find(name);
    if (it != cache.end()) return it->second;

    if (name == out_name) {
        int idx = base_for_special;
        cache[name] = idx;
        return idx;
    }

    for (size_t i = 0; i < inputs.size(); ++i) {
        if (name == get<0>(inputs[i])) {
            int idx = base_for_special + 1 + static_cast<int>(i);
            cache[name] = idx;
            return idx;
        }
    }

    // 普通节点：取冒号后的数字
    int colon = static_cast<int>(name.rfind(':'));
    if (colon <= 0 || colon + 1 >= static_cast<int>(name.size())) {
        throw runtime_error("Invalid node format: missing ':'");
    }
    const string pin_part = name.substr(colon + 1);
    for (char ch : pin_part) {
        if (!isdigit(static_cast<unsigned char>(ch))) {
            throw runtime_error("Invalid node format: non-numeric pin");
        }
    }
    int idx = stoi(pin_part);
    cache[name] = idx;
    return idx;
}

static inline Topology BuildTopologyFromRess(const spef::Net &net,
                                             const string &out_name,
                                             const vector<tuple<string, Input_info>> &inputs)
{
    Topology topo;
    topo.base_for_special = static_cast<int>(net.ress.size() * 2);

    for (const auto &res : net.ress) {
        (void)get<2>(res);
        (void)MapNodeNameToIndex(get<0>(res), out_name, inputs, topo.base_for_special, topo.name_to_idx);
        (void)MapNodeNameToIndex(get<1>(res), out_name, inputs, topo.base_for_special, topo.name_to_idx);
    }

    int max_idx = topo.base_for_special + 1 + static_cast<int>(inputs.size());
    for (const auto &kv : topo.name_to_idx) max_idx = max(max_idx, kv.second);
    topo.nodes.assign(static_cast<size_t>(max_idx + 1), NodeInfo{});
    return topo;
}

static inline void FillCapsFromNet(const spef::Net &net,
                                   Topology &topo,
                                   const string &out_name,
                                   const vector<tuple<string, Input_info>> &inputs)
{
    for (const auto &cap : net.caps) {
        const string &n1 = get<0>(cap);
        const string &n2 = get<1>(cap);
        double cval = get<2>(cap);
        try {
            int idx1 = MapNodeNameToIndex(n1, out_name, inputs, topo.base_for_special, topo.name_to_idx);
            if (n2.empty()) {
                if (idx1 >= static_cast<int>(topo.nodes.size())) topo.nodes.resize(idx1 + 1);
                topo.nodes[idx1].ground_cap += cval;
            } else {
                int idx2 = MapNodeNameToIndex(n2, out_name, inputs, topo.base_for_special, topo.name_to_idx);
                int need = max(idx1, idx2) + 1;
                if (need > static_cast<int>(topo.nodes.size())) topo.nodes.resize(need);
                double half = cval * 0.5;
                topo.nodes[idx1].ground_cap += half;
                topo.nodes[idx2].ground_cap += half;
            }
        } catch (...) {
            // 节点名偶发不规范时，跳过该条 cap
        }
    }
}

static inline double EdgeR(const vector<vector<pair<int, double>>> &adj, int u, int v)
{
    for (const auto &pr : adj[u]) {
        if (pr.first == v) return pr.second;
    }
    return 0.0;
}

// D2M：由 (m1, m2) 计算 50% 延时（ps）。注意：这里返回 t/ln2，与你们 Tree/calc_delay.cpp 对齐。
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

struct FeatureSwitch {
    bool base = true;    // 原有 path-features（34 维）
    bool delay = true;   // std/误差相关
    bool moment = true;  // m2 + d2m_delay
    bool subtree = true; // extra_R/C_after_input
    bool path = true;    // branch_R/hops + input_total_deg
    bool geom = true;    // 坐标/距离
    bool net = true;     // net 级统计

    static FeatureSwitch AllOff()
    {
        FeatureSwitch s;
        s.base = s.delay = s.moment = s.subtree = s.path = s.geom = s.net = false;
        return s;
    }
};

static inline string ToLower(string s)
{
    for (auto &ch : s) ch = static_cast<char>(tolower(static_cast<unsigned char>(ch)));
    return s;
}

static inline vector<string> SplitTokens(const string &spec)
{
    vector<string> out;
    string cur;
    for (char c : spec) {
        if (c == ',' || c == '+' || c == ';' || c == ' ') {
            if (!cur.empty()) {
                out.push_back(cur);
                cur.clear();
            }
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

static inline FeatureSwitch ParseFeatureSwitch(const string &spec_raw)
{
    string spec = ToLower(spec_raw);
    if (spec.empty() || spec == "all") return FeatureSwitch{};

    if (spec == "baseline" || spec == "base") {
        FeatureSwitch s = FeatureSwitch::AllOff();
        s.base = true;
        return s;
    }

    FeatureSwitch s = FeatureSwitch::AllOff();
    for (const auto &tok_raw : SplitTokens(spec)) {
        string tok = ToLower(tok_raw);
        if (tok == "base" || tok == "baseline") s.base = true;
        else if (tok == "delay") s.delay = true;
        else if (tok == "moment" || tok == "m2") s.moment = true;
        else if (tok == "subtree") s.subtree = true;
        else if (tok == "path") s.path = true;
        else if (tok == "geom" || tok == "geo") s.geom = true;
        else if (tok == "net" || tok == "netstats") s.net = true;
    }

    // 如果全关（输入拼错等），回退到 all，避免输出空 CSV
    if (!(s.base || s.delay || s.moment || s.subtree || s.path || s.geom || s.net)) return FeatureSwitch{};
    return s;
}

static void WriteHeader(ofstream &csv, const FeatureSwitch &fs)
{
    csv << "net_name,out_real_name,real_input_name";

    if (fs.base) {
        csv << ",num_stages,rho,Elmore_ps,subtree_Elmore_ps";
        for (int i = 1; i <= 10; ++i) csv << ",R" << i;
        for (int i = 1; i <= 10; ++i) csv << ",C_ds" << i;
        for (int i = 1; i <= 10; ++i) csv << ",StageDelay" << i;
    }

    if (fs.delay) csv << ",std_ps,err_ps,rel_err,metric_type,metric_err";
    if (fs.path) csv << ",branch_R_ohm,branch_hops,input_total_deg";
    if (fs.moment) csv << ",second_moment_ps2,d2m_delay_ps";
    if (fs.subtree) csv << ",extra_R_after_input,extra_C_after_input";
    if (fs.geom) csv << ",pin_x,pin_y,drv_x,drv_y,dx,dy,dist_L1,dist_L2";

    if (fs.net) {
        csv << ",max_R_dist_all,max_R_dist_inputs,avg_R_dist_inputs"
            << ",r_segments,rc_nodes,leaf_nodes,branch_nodes,total_R"
            << ",ground_cap_sum,input_count_net";
    }

    csv << "\n";
}

int main(int argc, char **argv)
{
    int spef_num = 0;
    char *file_path = nullptr;
    string feat_spec = "all";
    string out_csv;

#ifdef __DEBUG__
    string default_file_path = "D:\\vscode\\Delay_calc\\Data";
    file_path = (char *)malloc((strlen(default_file_path.c_str()) + 1));
    strcpy(file_path, default_file_path.c_str());
    spef_num = 0;
    feat_spec = "all";
#endif

    int option_index = 0;
    static struct option long_options[] = {
        {"file_path", required_argument, 0, 'f'},
        {"spef_num", required_argument, 0, 'm'},
        {"feat", required_argument, 0, 't'},
        {"out_csv", required_argument, 0, 'o'},
        {0, 0, 0, 0}};

    int c;
    while ((c = getopt_long(argc, argv, "f:m:t:o:", long_options, &option_index)) != -1) {
        switch (c) {
        case 'f':
            file_path = (char *)malloc((strlen(optarg) + 1));
            strcpy(file_path, optarg);
            break;
        case 'm':
            spef_num = atoi(optarg);
            break;
        case 't':
            feat_spec = optarg;
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

    FeatureSwitch fs = ParseFeatureSwitch(feat_spec);

    // 读取 netlist / delay / SPEF
    string netlist_file = string(file_path) + "/netlist_info.txt";
    if (Read_netlist_file(netlist_file)) return 1;

    string delay_file = string(file_path) + "/delay_data/Group" + to_string(spef_num) + ".txt";
    if (Read_delay_file(delay_file)) {
        cerr << "读取 delay 文件失败" << endl;
        return 1;
    }

    string spef_file = string(file_path) + "/SPEF/Group" + to_string(spef_num) + ".spef";
    spef::Spef parser;
    if (!parser.read(spef_file)) {
        cerr << "读取 spef 文件失败: " << *parser.error << endl;
        return 1;
    }

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

    WriteHeader(csv, fs);

    const double INF = numeric_limits<double>::infinity();
    const double pin_load_unit_factor = 1e3;
    const double cap_ff_to_ps_factor = 1e-3;

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
                out_real_name = parser.name_map[id] + '/' + connection.name.substr(index + 1);

                auto it = netlist_info.find(out_real_name);
                if (it == netlist_info.end()) missing_in_netlist = true;
                else paths = it->second;
            }
        }
        if (missing_in_netlist || out_name.empty()) continue;

        // driver 坐标（无坐标则 0）
        double drv_x = 0.0, drv_y = 0.0;
        if (fs.geom) {
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
            string input_real = parser.name_map[id] + '/' + connection.name.substr(index + 1);

            for (auto &p : paths) {
                if (input_real == p.name) {
                    inputs.emplace_back(connection.name, p);
                    break;
                }
            }
        }
        if (inputs.empty()) continue;

        // 拓扑（节点索引 + ground cap 分配）
        Topology topo = BuildTopologyFromRess(net, out_name, inputs);
        FillCapsFromNet(net, topo, out_name, inputs);

        // 邻接表（带电阻）
        vector<vector<pair<int, double>>> adj(topo.nodes.size());
        int root_idx = -1;
        int r_segments = 0;
        double total_R_net = 0.0;

        try {
            for (const auto &res : net.ress) {
                const string &a = get<0>(res);
                const string &b = get<1>(res);
                double r = get<2>(res);
                int u = MapNodeNameToIndex(a, out_name, inputs, topo.base_for_special, topo.name_to_idx);
                int v = MapNodeNameToIndex(b, out_name, inputs, topo.base_for_special, topo.name_to_idx);
                if (u >= (int)adj.size() || v >= (int)adj.size()) continue;
                adj[u].push_back({v, r});
                adj[v].push_back({u, r});
                total_R_net += r;
                r_segments++;
            }
            root_idx = MapNodeNameToIndex(out_name, out_name, inputs, topo.base_for_special, topo.name_to_idx);
        } catch (...) {
            continue;
        }
        if (root_idx < 0 || root_idx >= (int)adj.size()) continue;

        // net 级统计（度数）
        int rc_nodes = 0, leaf_nodes = 0, branch_nodes = 0;
        if (fs.net) {
            for (size_t i = 0; i < adj.size(); ++i) {
                int deg = (int)adj[i].size();
                if (deg > 0) {
                    rc_nodes++;
                    if (deg == 1) leaf_nodes++;
                    else if (deg >= 3) branch_nodes++;
                }
            }
        }

        // ground cap 汇总（只保留 ground_cap_sum）
        double sum_ground_cap = 0.0;
        if (fs.net) {
            for (const auto &cap : net.caps) {
                if (get<1>(cap).empty()) sum_ground_cap += get<2>(cap);
            }
        }

        // Dijkstra：root → all
        vector<double> dist(adj.size(), INF);
        vector<int> parent(adj.size(), -1);
        using PQItem = pair<double, int>;
        priority_queue<PQItem, vector<PQItem>, greater<PQItem>> pq;

        dist[root_idx] = 0.0;
        pq.push({0.0, root_idx});

        while (!pq.empty()) {
            auto [d, u] = pq.top();
            pq.pop();
            if (d > dist[u]) continue;
            for (auto &pr : adj[u]) {
                int v = pr.first;
                double r = pr.second;
                double nd = d + r;
                if (nd < dist[v]) {
                    dist[v] = nd;
                    parent[v] = u;
                    pq.push({nd, v});
                }
            }
        }

        double max_R_dist_all = 0.0;
        for (double d : dist) {
            if (d < INF / 2 && d > max_R_dist_all) max_R_dist_all = d;
        }

        double max_R_dist_inputs = 0.0;
        double sum_R_dist_inputs = 0.0;
        int cnt_R_dist_inputs = 0;
        if (fs.net) {
            for (const auto &inp : inputs) {
                int idx = -1;
                try {
                    idx = MapNodeNameToIndex(get<0>(inp), out_name, inputs, topo.base_for_special, topo.name_to_idx);
                } catch (...) {
                    idx = -1;
                }
                if (idx >= 0 && idx < (int)dist.size() && dist[idx] < INF / 2) {
                    double br = dist[idx];
                    sum_R_dist_inputs += br;
                    cnt_R_dist_inputs++;
                    max_R_dist_inputs = max(max_R_dist_inputs, br);
                }
            }
        }
        double avg_R_dist_inputs = (cnt_R_dist_inputs > 0) ? (sum_R_dist_inputs / cnt_R_dist_inputs) : 0.0;

        // children（以 parent 为树方向）
        vector<vector<int>> children(adj.size());
        for (int v = 0; v < (int)adj.size(); ++v) {
            int p = parent[v];
            if (p >= 0) children[p].push_back(v);
        }

        // extra_R/C_after_input：input 节点之后的子树额外 RC
        vector<double> extra_R_sub(adj.size(), 0.0);
        vector<double> extra_C_sub(adj.size(), 0.0);
        if (fs.subtree) {
            function<void(int)> dfs_sub = [&](int u) {
                double sumR = 0.0;
                double sumC = 0.0;
                for (int v : children[u]) {
                    double r_uv = EdgeR(adj, u, v);
                    dfs_sub(v);
                    sumR += r_uv + extra_R_sub[v];
                    sumC += topo.nodes[v].ground_cap + extra_C_sub[v];
                }
                extra_R_sub[u] = sumR;
                extra_C_sub[u] = sumC;
            };
            dfs_sub(root_idx);
        }

        // sink load：input pin 的门负载
        unordered_map<int, double> sink_load_cap;
        for (const auto &inp : inputs) {
            try {
                int idx = MapNodeNameToIndex(get<0>(inp), out_name, inputs, topo.base_for_special, topo.name_to_idx);
                sink_load_cap[idx] = get<1>(inp).pin_cap * pin_load_unit_factor;
            } catch (...) {
            }
        }

        vector<double> node_total_cap(topo.nodes.size(), 0.0);
        for (size_t i = 0; i < topo.nodes.size(); ++i) {
            double load = 0.0;
            auto it = sink_load_cap.find((int)i);
            if (it != sink_load_cap.end()) load = it->second;
            node_total_cap[i] = topo.nodes[i].ground_cap + load;
        }

        // subtree cap
        vector<double> subtree_cap(topo.nodes.size(), 0.0);
        function<void(int)> dfs_post = [&](int u) {
            double sum = node_total_cap[u];
            for (int v : children[u]) {
                dfs_post(v);
                sum += subtree_cap[v];
            }
            subtree_cap[u] = sum;
        };
        dfs_post(root_idx);

        // Elmore m1（ps）
        vector<double> elmore(topo.nodes.size(), 0.0);
        function<void(int)> dfs_el = [&](int u) {
            for (int v : children[u]) {
                double r_uv = EdgeR(adj, u, v);
                elmore[v] = elmore[u] + r_uv * subtree_cap[v] * cap_ff_to_ps_factor;
                dfs_el(v);
            }
        };
        dfs_el(root_idx);

        // m2（ps^2）
        vector<double> m2_ps2(topo.nodes.size(), 0.0);
        if (fs.moment) {
            vector<double> subtree_cap_mul_m1(topo.nodes.size(), 0.0);
            function<void(int)> dfs_cap_m1 = [&](int u) {
                double sum = node_total_cap[u] * elmore[u];
                for (int v : children[u]) {
                    dfs_cap_m1(v);
                    sum += subtree_cap_mul_m1[v];
                }
                subtree_cap_mul_m1[u] = sum;
            };
            dfs_cap_m1(root_idx);

            function<void(int)> dfs_m2 = [&](int u) {
                for (int v : children[u]) {
                    double r_uv = EdgeR(adj, u, v);
                    m2_ps2[v] = m2_ps2[u] + 2.0 * r_uv * subtree_cap_mul_m1[v] * cap_ff_to_ps_factor;
                    dfs_m2(v);
                }
            };
            dfs_m2(root_idx);
        }

        // 对每个 input 输出一行
        for (const auto &inp : inputs) {
            const string &inp_name = get<0>(inp);
            const auto &info = get<1>(inp);

            int sink_idx = -1;
            try {
                sink_idx = MapNodeNameToIndex(inp_name, out_name, inputs, topo.base_for_special, topo.name_to_idx);
            } catch (...) {
                sink_idx = -1;
            }
            if (sink_idx < 0 || sink_idx >= (int)adj.size()) continue;
            if (dist[sink_idx] >= INF / 2) continue;

            // root→sink 路径
            vector<int> path_nodes;
            for (int cur = sink_idx; cur != -1; cur = parent[cur]) path_nodes.push_back(cur);
            reverse(path_nodes.begin(), path_nodes.end());
            if (path_nodes.size() < 2) continue;

            int num_stages = (int)path_nodes.size() - 1;
            double branch_R = dist[sink_idx];
            double rho = (max_R_dist_all > 0.0) ? (branch_R / max_R_dist_all) : 0.0;

            double elmore_ps = elmore[sink_idx];

            // 主路径后缀和（path-only 下游 C）
            int L = (int)path_nodes.size() - 1;
            vector<double> suf_path(path_nodes.size(), 0.0);
            suf_path[L] = node_total_cap[path_nodes[L]];
            for (int i = L - 1; i >= 0; --i) {
                suf_path[i] = node_total_cap[path_nodes[i]] + suf_path[i + 1];
            }

            double R_stage[10] = {0};
            double C_stage[10] = {0};
            double SD_stage[10] = {0};

            double path_only_elmore = 0.0;
            int use_stages = min(num_stages, 10);
            for (int k = 0; k < num_stages; ++k) {
                int u = path_nodes[k];
                int v = path_nodes[k + 1];
                double r_uv = EdgeR(adj, u, v);
                double Cdown_total = subtree_cap[v];
                double Cdown_path = suf_path[k + 1];

                path_only_elmore += r_uv * Cdown_path * cap_ff_to_ps_factor;

                if (k < use_stages) {
                    R_stage[k] = r_uv;
                    C_stage[k] = Cdown_total;
                    SD_stage[k] = r_uv * Cdown_total * cap_ff_to_ps_factor;
                }
            }
            double subtree_elmore = elmore_ps - path_only_elmore;

            // 基础三列
            csv << net.name << "," << out_real_name << "," << info.name;

            // base 34
            if (fs.base) {
                csv << "," << num_stages << "," << rho << "," << elmore_ps << "," << subtree_elmore;
                for (int i = 0; i < 10; ++i) csv << "," << R_stage[i];
                for (int i = 0; i < 10; ++i) csv << "," << C_stage[i];
                for (int i = 0; i < 10; ++i) csv << "," << SD_stage[i];
            }

            // delay 相关（注意：训练时不要把 std/err 当输入特征）
            if (fs.delay) {
                double std_ps = info.delay * 1000.0;
                double err_ps = elmore_ps - std_ps;
                double rel_err = (std_ps != 0.0) ? (err_ps / std_ps) : 0.0;

                const double DELAY_THRESHOLD_PS = 50.0;
                bool metric_is_rel = (std_ps >= DELAY_THRESHOLD_PS);
                string metric_type = metric_is_rel ? "REL" : "ABS";
                double metric_err = metric_is_rel ? std::abs(rel_err) : std::abs(err_ps);

                csv << "," << std_ps << "," << err_ps << "," << rel_err << "," << metric_type << "," << metric_err;
            }

            if (fs.path) {
                int input_total_deg = (int)adj[sink_idx].size();
                csv << "," << branch_R << "," << num_stages << "," << input_total_deg;
            }

            if (fs.moment) {
                double second_moment_ps2 = (sink_idx >= 0 && sink_idx < (int)m2_ps2.size()) ? m2_ps2[sink_idx] : 0.0;
                double d2m_delay_ps = DelayFromMoments(elmore_ps, second_moment_ps2);
                csv << "," << second_moment_ps2 << "," << d2m_delay_ps;
            }

            if (fs.subtree) {
                double extra_R_after_input = (sink_idx >= 0 && sink_idx < (int)extra_R_sub.size()) ? extra_R_sub[sink_idx] : 0.0;
                double extra_C_after_input = (sink_idx >= 0 && sink_idx < (int)extra_C_sub.size()) ? extra_C_sub[sink_idx] : 0.0;
                csv << "," << extra_R_after_input << "," << extra_C_after_input;
            }

            if (fs.geom) {
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

            if (fs.net) {
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
