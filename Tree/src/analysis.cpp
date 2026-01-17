#include "Tree/inc/analysis.h"
#include "Tree/inc/build_tree.h" // 确保包含新定义的头文件
#include <iostream>
#include <unordered_set>
#include <algorithm>
#include <queue>
#include <functional>
#include <fstream>
#include <string_view> // Added

using namespace std;

// (lenth_ratio and print_connections_length_stats 保持不变，省略以节省空间...)
double lenth_ratio(const Topology &topo, int pin_idx) {
    int cur_nd_idx = pin_idx;
    int pin_len = 0;
    int max_len = 0;
    while(true){
        if (topo.nodes[cur_nd_idx].parent_idx == -1) break;
        pin_len += 1;
        cur_nd_idx = topo.nodes[cur_nd_idx].parent_idx;
    }
    for (auto& [leaf_idx, visited]: topo.leaf_node_idx){
        int temp = 0;
        cur_nd_idx = leaf_idx;
        while(true){
            if(topo.nodes[cur_nd_idx].parent_idx == -1) break;
            temp +=1;
            cur_nd_idx = topo.nodes[cur_nd_idx].parent_idx;
        }
        max_len = max(max_len, temp);
    }
    return (double)pin_len / (double)max_len;
}

void print_connections_length_stats(const spef::Spef &p)
{
    // ... (内容不变)
    unordered_map<size_t, size_t> length_counts;
    size_t total_nets = p.nets.size();
    for (const auto &net : p.nets) { size_t len = net.connections.size(); ++length_counts[len]; }
    // ... (打印代码略)
}

void check_ress_pin_consecutive(const spef::Net &net,
                                const std::string &out_name,
                                const std::vector<std::tuple<std::string, Input_info>> &Input)
{
    vector<string> res_endpoints;
    res_endpoints.reserve(net.ress.size() * 2);
    for (const auto &res : net.ress)
    {
        // string_view -> string 显式转换
        res_endpoints.emplace_back(get<0>(res));
        res_endpoints.emplace_back(get<1>(res));
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

    // ... (去重和排序逻辑不变) ...
    {
        unordered_set<string> seen;
        vector<string> unique_remaining;
        unique_remaining.reserve(remaining.size());
        for (const auto &pin : remaining)
        {
            if (seen.insert(pin).second) unique_remaining.push_back(pin);
        }
        remaining.swap(unique_remaining);
    }

    vector<int> ids;
    ids.reserve(remaining.size());
    for (const auto &s : remaining)
    {
        int index = static_cast<int>(s.rfind(':'));
        if (index <= 1) continue;
        try { ids.push_back(stoi(s.substr(index + 1))); } catch (...) {}
    }
    // ... (打印逻辑略)
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
            // 使用 string_view 接收
            std::string_view a = get<0>(res);
            std::string_view b = get<1>(res);
            double r = get<2>(res);
            // MapNodeNameToIndex 现在接受 string_view，所以这里可以直接传 a, b
            int u = MapNodeNameToIndex(a, out_name, Input, topo.base_for_special, topo.name_to_idx);
            int v = MapNodeNameToIndex(b, out_name, Input, topo.base_for_special, topo.name_to_idx);
            adj[u].push_back({v,r});
            adj[v].push_back({u,r});
        }
    } catch (const std::exception &e) {
        cerr << "[DebugElmore] Build adjacency error: " << e.what() << endl;
        return;
    }

    // ... (后续逻辑基本不变，只要不涉及 MapNodeNameToIndex 的调用) ...
    
    int root_idx, sink_idx;
    try {
        root_idx = MapNodeNameToIndex(out_name, out_name, Input, topo.base_for_special, topo.name_to_idx);
        sink_idx = MapNodeNameToIndex(sink_name, out_name, Input, topo.base_for_special, topo.name_to_idx);
    } catch (...) { return; }

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
    
    // ... (Path reconstruction, DFS, and Output logic remains identical) ...
    if (parent[sink_idx] == -1) return;

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
        node_total_cap[i] = topo.nodes[i].ground_cap + load;
    }
    vector<double> subtree_cap(topo.nodes.size(),0.0);
    for (int i=(int)order.size()-1;i>=0;--i){
        int u=order[i]; double sum=node_total_cap[u];
        for (auto &pr: adj[u]) { int v=pr.first; if(parent[v]==u) sum += subtree_cap[v]; }
        subtree_cap[u]=sum;
    }
    
    // Output
    for (int idx : path) {
        double load = sink_load_cap.count(idx)? sink_load_cap[idx]:0.0;
        cout << idx << " | " << (idx_to_name.count(idx)?idx_to_name[idx]:"<unnamed>")
             << " | " << topo.nodes[idx].ground_cap
             << " | " << load << " | " << subtree_cap[idx] << endl;
    }
}

void ExportNetToGephiCsv(
    const spef::Net &net,
    const std::string &out_name,
    const std::vector<std::tuple<std::string, Input_info>> &Input,
    const Topology &topo,
    const std::string &nodes_csv_path,
    const std::string &edges_csv_path)
{
    // ... (构造 map 逻辑不变)
    std::unordered_map<int, std::string> idx_to_name;
    for (const auto &kv : topo.name_to_idx) idx_to_name[kv.second] = kv.first;

    // ... (输出节点逻辑不变) ...
    // ... (写入 nodes.csv) ...

    // 4. 构建带权无向邻接表
    {
        int out_idx = -1;
        auto it_out = topo.name_to_idx.find(out_name);
        if (it_out != topo.name_to_idx.end()) out_idx = it_out->second;
        else return;

        std::vector<std::vector<std::pair<int, double>>> adj(topo.nodes.size());
        for (const auto &res : net.ress) {
            // string_view -> string (for map lookup compatibility)
            std::string a(get<0>(res));
            std::string b(get<1>(res));
            double r = get<2>(res);

            auto ita = topo.name_to_idx.find(a);
            auto itb = topo.name_to_idx.find(b);
            if (ita != topo.name_to_idx.end() && itb != topo.name_to_idx.end()) {
                adj[ita->second].push_back({itb->second, r});
                adj[itb->second].push_back({ita->second, r});
            }
        }
        // ... (写入 edges.csv, DFS逻辑不变) ...
        std::ofstream ofs(edges_csv_path);
        if (ofs.is_open()) {
            ofs << "source,target,res_ohm\n";
            std::vector<char> visited(topo.nodes.size(), 0);
            std::function<void(int)> dfs = [&](int u) {
                visited[u] = 1;
                for (const auto &pr : adj[u]) {
                    int v = pr.first; double r = pr.second;
                    if (!visited[v]) {
                        ofs << u << "," << v << "," << r << "\n";
                        dfs(v);
                    }
                }
            };
            dfs(out_idx);
        }
    }
}

// write2csv 保持不变
stringstream& write2csv(stringstream &ss, const std::vector<std::pair<std::string, double>> &res, const Topology &topo,
    const std::vector<std::tuple<std::string, Input_info>> &Input, const spef::Net &net, int precision)
{
    unordered_map<string, double> res_map;
    for (auto &t : res) {
        res_map[t.first] = t.second;
    }
    ss.setf(std::ios::fixed);
    ss.precision(precision);
    for (const auto &inp : Input) {
        const string &inp_name = get<0>(inp);
        const auto &info = get<1>(inp);
        double std_ps = info.delay * 1000.0; 
        
        auto it = res_map.find(inp_name);
        if (it != res_map.end()) {
            double calc_ps = it->second;
            ss << inp_name << "," << std_ps << "," << calc_ps << "," 
               << (std_ps-calc_ps) << "," << (std_ps!=0?(std_ps-calc_ps)/std_ps:0.0) << ","
               << lenth_ratio(topo, topo.name_to_idx.at(inp_name)) << "," << Input.size() << "\n";
        }
    }
    ss.unsetf(std::ios::fixed);
    return ss;
}