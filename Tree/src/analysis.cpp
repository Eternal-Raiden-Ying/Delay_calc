#include "Tree/inc/analysis.h"
#include <iostream>
#include <unordered_set>
#include <algorithm>
#include <queue>
#include <functional>
#include <fstream>   // 导出到 CSV

using namespace std;

void print_connections_length_stats(const spef::Spef &p)
{
    unordered_map<size_t, size_t> length_counts;
    size_t total_nets = p.nets.size();
    for (const auto &net : p.nets)
    {
        size_t len = net.connections.size();
        ++length_counts[len];
    }

    cout << "\n[Stats] Distribution of net.connections length (percentage)" << endl;
    cout.setf(std::ios::fixed);
    cout.precision(2);
    for (const auto &kv : length_counts)
    {
        double pct = total_nets ? (100.0 * kv.second / static_cast<double>(total_nets)) : 0.0;
        cout << "length=" << kv.first << ", count=" << kv.second << ", ratio=" << pct << "%" << endl;
    }
    cout.unsetf(std::ios::fixed);
}

void check_ress_pin_consecutive(const spef::Net &net,
                                const std::string &out_name,
                                const std::vector<std::tuple<std::string, Input_info>> &Input)
{
    vector<string> res_endpoints;
    res_endpoints.reserve(net.ress.size() * 2);
    for (const auto &res : net.ress)
    {
        const string &a = get<0>(res);
        const string &b = get<1>(res);
        res_endpoints.push_back(a);
        res_endpoints.push_back(b);
    }

    unordered_set<string> filter_names;
    filter_names.insert(out_name);
    for (const auto &inp : Input)
    {
        filter_names.insert(get<0>(inp));
    }

    vector<string> remaining;
    remaining.reserve(res_endpoints.size());
    for (const auto &s : res_endpoints)
    {
        if (filter_names.find(s) == filter_names.end())
        {
            remaining.push_back(s);
        }
    }

    {
        unordered_set<string> seen;
        vector<string> unique_remaining;
        unique_remaining.reserve(remaining.size());
        for (const auto &pin : remaining)
        {
            if (seen.insert(pin).second)
            {
                unique_remaining.push_back(pin);
            }
        }
        remaining.swap(unique_remaining);
    }

    vector<int> ids;
    ids.reserve(remaining.size());
    for (const auto &s : remaining)
    {
        int index = static_cast<int>(s.rfind(':'));
        if (index <= 1) continue;
        try
        {
            int id = stoi(s.substr(index + 1, -1));
            ids.push_back(id);
        }
        catch (...)
        {
        }
    }

    sort(ids.begin(), ids.end());
    ids.erase(unique(ids.begin(), ids.end()), ids.end());

    cout << "Res endpoints remaining IDs: ";
    if (ids.empty())
    {
        cout << "<empty>" << endl;
    }
    else
    {
        for (size_t i = 0; i < ids.size(); ++i)
        {
            if (i) cout << ", ";
            cout << ids[i];
        }
        cout << endl;
    }

    bool consecutive = true;
    for (size_t i = 1; i < ids.size(); ++i)
    {
        if (ids[i] != ids[i - 1] + 1)
        {
            consecutive = false;
            break;
        }
    }
    if (!ids.empty())
    {
        cout << "Res IDs consecutive: " << (consecutive ? "true" : "false") << endl;
    }
}

void DebugElmoreSinglePath(
    const spef::Net &net,
    const std::string &out_name,
    const std::vector<std::tuple<std::string, Input_info>> &Input,
    Topology &topo,
    double pin_load_unit_factor,
    double cap_ff_to_ps_factor)
{
    cout << "[DebugElmore] Begin single-path RC details" << endl;
    if (Input.empty()) {
        cout << "[DebugElmore] No input pins, abort." << endl;
        return;
    }
    const string &sink_name = get<0>(Input[0]);

    vector<vector<pair<int,double>>> adj(topo.nodes.size());
    try {
        for (const auto &res : net.ress) {
            const string &a = get<0>(res);
            const string &b = get<1>(res);
            double r = get<2>(res);
            int u = MapNodeNameToIndex(a, out_name, Input, topo.base_for_special, topo.name_to_idx);
            int v = MapNodeNameToIndex(b, out_name, Input, topo.base_for_special, topo.name_to_idx);
            adj[u].push_back({v,r});
            adj[v].push_back({u,r});
        }
    } catch (const std::exception &e) {
        cerr << "[DebugElmore] Build adjacency error: " << e.what() << endl;
        return;
    }

    int root_idx, sink_idx;
    try {
        root_idx = MapNodeNameToIndex(out_name, out_name, Input, topo.base_for_special, topo.name_to_idx);
        sink_idx = MapNodeNameToIndex(sink_name, out_name, Input, topo.base_for_special, topo.name_to_idx);
    } catch (const std::exception &e) {
        cerr << "[DebugElmore] Map root/sink error: " << e.what() << endl;
        return;
    }

    unordered_map<int,string> idx_to_name;
    for (auto &kv : topo.name_to_idx) idx_to_name[kv.second] = kv.first;

    unordered_map<int,double> sink_load_cap;
    for (auto &inp : Input) {
        try {
            int idx = MapNodeNameToIndex(get<0>(inp), out_name, Input, topo.base_for_special, topo.name_to_idx);
            sink_load_cap[idx] = get<1>(inp).pin_cap * pin_load_unit_factor;
        } catch (...) {}
    }

    vector<int> parent(topo.nodes.size(), -1);
    queue<int> q; q.push(root_idx); parent[root_idx] = root_idx;
    while (!q.empty() && parent[sink_idx] == -1) {
        int u = q.front(); q.pop();
        for (auto &pr : adj[u]) {
            int v = pr.first;
            if (parent[v] == -1) { parent[v] = u; q.push(v); }
        }
    }
    if (parent[sink_idx] == -1) {
        cout << "[DebugElmore] Sink not reachable from root." << endl;
        return;
    }

    vector<int> path;
    for (int cur = sink_idx; cur != root_idx; cur = parent[cur]) path.push_back(cur);
    path.push_back(root_idx);
    reverse(path.begin(), path.end());

    vector<char> visited(topo.nodes.size(), 0);
    vector<int> order;
    function<void(int)> dfs = [&](int u){
        visited[u]=1; order.push_back(u);
        for (auto &pr: adj[u]) {
            int v=pr.first; if(!visited[v]){ parent[v]=u; dfs(v);} }
    };
    parent.assign(topo.nodes.size(), -1);
    dfs(root_idx);

    vector<double> node_total_cap(topo.nodes.size(),0.0);
    for (size_t i=0;i<topo.nodes.size();++i){
        double load = 0.0; auto it=sink_load_cap.find((int)i); if(it!=sink_load_cap.end()) load=it->second;
        node_total_cap[i] = topo.nodes[i].cap_downstream + topo.nodes[i].ground_cap + load;
    }
    vector<double> subtree_cap(topo.nodes.size(),0.0);
    for (int i=(int)order.size()-1;i>=0;--i){
        int u=order[i]; double sum=node_total_cap[u];
        for (auto &pr: adj[u]) { int v=pr.first; if(parent[v]==u) sum += subtree_cap[v]; }
        subtree_cap[u]=sum;
    }

    cout << "[DebugElmore] Node list (index | name | cap_fF | ground_fF | load_fF | subtree_fF)" << endl;
    for (int idx : path) {
        double load = sink_load_cap.count(idx)? sink_load_cap[idx]:0.0;
        cout << idx << " | " << (idx_to_name.count(idx)?idx_to_name[idx]:"<unnamed>")
             << " | " << topo.nodes[idx].cap_downstream
             << " | " << topo.nodes[idx].ground_cap
             << " | " << load
             << " | " << subtree_cap[idx]
             << endl;
    }

    cout << "[DebugElmore] Edge contributions (u->v: R_ohm * C_downstream_fF -> ps)" << endl;
    double cumulative = 0.0;
    for (size_t i=0;i+1<path.size();++i){
        int u=path[i]; int v=path[i+1];
        double r_edge = 0.0;
        for (auto &pr: adj[u]) if(pr.first==v){ r_edge = pr.second; break; }
        double contrib_ps = r_edge * subtree_cap[v] * cap_ff_to_ps_factor;
        cumulative += contrib_ps;
        cout << (idx_to_name.count(u)?idx_to_name[u]:to_string(u)) << " -> "
             << (idx_to_name.count(v)?idx_to_name[v]:to_string(v))
             << ": R=" << r_edge << " ohm, C_downstream=" << subtree_cap[v] << " fF, contrib=" << contrib_ps
             << " ps, cum=" << cumulative << " ps" << endl;
    }
    cout << "[DebugElmore] Total delay to sink (" << sink_name << ") = " << cumulative << " ps" << endl;
    cout << "[DebugElmore] End" << endl;
}

// 将 RC 拓扑导出为 Gephi 可读的两个 CSV：节点表 + 边表（从 output_pin 出发 DFS，导出有向边）
// - nodes_csv_path: 节点表路径
// - edges_csv_path: 边表路径
void ExportNetToGephiCsv(
    const spef::Net &net,
    const std::string &out_name,
    const std::vector<std::tuple<std::string, Input_info>> &Input,
    const Topology &topo,
    const std::string &nodes_csv_path,
    const std::string &edges_csv_path)
{
    // 1. 构造 idx -> name 映射
    std::unordered_map<int, std::string> idx_to_name;
    idx_to_name.reserve(topo.name_to_idx.size());
    for (const auto &kv : topo.name_to_idx) {
        idx_to_name[kv.second] = kv.first;
    }

    // 2. 输出 / 输入 节点的索引集合
    int out_idx = -1;
    auto it_out = topo.name_to_idx.find(out_name);
    if (it_out != topo.name_to_idx.end()) {
        out_idx = it_out->second;
    }

    std::unordered_set<int> input_indices;
    input_indices.reserve(Input.size());
    for (const auto &inp : Input) {
        const std::string &spef_name = std::get<0>(inp); // connection.name
        auto it = topo.name_to_idx.find(spef_name);
        if (it != topo.name_to_idx.end()) {
            input_indices.insert(it->second);
        }
    }

    // 3. 写 nodes.csv（保持原来的无向度信息等）
    {
        std::ofstream ofs(nodes_csv_path);
        if (!ofs.is_open()) {
            std::cerr << "[GephiExport] Failed to open nodes csv: " << nodes_csv_path << std::endl;
        } else {
            ofs << "id,label,is_output,is_input,degree,ground_cap,resistor\n";
            for (size_t i = 0; i < topo.nodes.size(); ++i) {
                const Node_Info &n = topo.nodes[i];
                std::string label;
                auto it_name = idx_to_name.find(static_cast<int>(i));
                if (it_name != idx_to_name.end()) {
                    label = it_name->second;
                } else {
                    label = std::to_string(i);
                }

                bool is_out = (static_cast<int>(i) == out_idx);
                bool is_in = (input_indices.find(static_cast<int>(i)) != input_indices.end());
                size_t degree = n.neighbors.size();

                ofs << i << ", "
                    << "\"" << label << "\"" << ", "
                    << (is_out ? 1 : 0) << ", "
                    << (is_in ? 1 : 0) << ", "
                    << degree << ", "
                    << n.ground_cap << ", "
                    << n.resistor
                    << "\n";
            }
            std::cout << "[GephiExport] Nodes written to " << nodes_csv_path << std::endl;
        }
    }

    // 4. 构建带权无向邻接表（基于 net.ress），然后从 out_idx 做 DFS，导出有向树边
    {
        if (out_idx == -1) {
            std::cerr << "[GephiExport] Cannot find out_name in topology: " << out_name
                      << " , skip edges export." << std::endl;
            return;
        }

        // 4.1 邻接表：adj[u] = { (v, R_uv), ... }
        std::vector<std::vector<std::pair<int, double>>> adj(topo.nodes.size());
        for (const auto &res : net.ress) {
            const std::string &a = std::get<0>(res);
            const std::string &b = std::get<1>(res);
            double r = std::get<2>(res);

            auto ita = topo.name_to_idx.find(a);
            auto itb = topo.name_to_idx.find(b);
            if (ita == topo.name_to_idx.end() || itb == topo.name_to_idx.end()) {
                continue; // 理论上不会发生
            }
            int u = ita->second;
            int v = itb->second;

            adj[u].push_back({v, r});
            adj[v].push_back({u, r});
        }

        std::ofstream ofs(edges_csv_path);
        if (!ofs.is_open()) {
            std::cerr << "[GephiExport] Failed to open edges csv: " << edges_csv_path << std::endl;
        } else {
            ofs << "source,target,res_ohm\n";

            // 4.2 从 output_pin 开始 DFS，导出有向边 u -> v
            std::vector<char> visited(topo.nodes.size(), 0);
            std::function<void(int)> dfs = [&](int u) {
                visited[u] = 1;
                for (const auto &pr : adj[u]) {
                    int v = pr.first;
                    double r = pr.second;
                    if (!visited[v]) {
                        // 在树方向上：u -> v
                        ofs << u << "," << v << "," << r << "\n";
                        dfs(v);
                    }
                }
            };

            dfs(out_idx);
            std::cout << "[GephiExport] Directed edges (from output_pin) written to "
                      << edges_csv_path << std::endl;
        }
    }
}
