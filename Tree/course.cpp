#include <getopt.h>
#include <iostream>
#include <unordered_map>
#include <parser-spef.hpp>
#include <list>
#include <string>
#include "Read_train/Read.h"
#include <fstream>
#include <thread>
#include <stdexcept>
#include <queue>
#include <functional>
using namespace std;

#define __DEBUG__

extern unordered_map<string, vector<Input_info>> netlist_info;

// Step 1: Build circuit topology tree (data holder for later Elmore)
struct Node_Info {
    double cap = 0.0;            // 累积电容（稍后从 caps 填充）
    double resistor = 0.0;       // 节点关联的电阻（若需要，可累积或留作占位）
    double ground_cap = 0.0;     // 对地电容（稍后计算）
    vector<int> neighbors;       // 相邻节点索引
    int backward_flag = 0;       // 与 neighbors.size() 保持一致
};

struct Topology {
    vector<Node_Info> nodes;                // 节点信息
    int base_for_special = 0;               // output/input 的基准偏移
    unordered_map<string,int> name_to_idx;  // 节点名到索引映射
};

// 将 SPEF 节点名映射为索引：
// - 非 output/input：取 ':' 之后的 pin 并转为数字
// - output_name 与各 input_name：分配到 [base, base + Input.size()) 的小范围
inline int MapNodeNameToIndex(const string &name,
                              const string &out_name,
                              const vector<tuple<string, Input_info>> &Input,
                              int base_for_special,
                              unordered_map<string,int> &cache)
{
    // 先查缓存
    auto it = cache.find(name);
    if (it != cache.end()) return it->second;

    // output
    if (name == out_name) {
        int idx = base_for_special; // 输出节点占用 base 位置
        cache[name] = idx;
        return idx;
    }
    // inputs
    for (size_t i = 0; i < Input.size(); ++i) {
        if (name == get<0>(Input[i])) {
            int idx = base_for_special + 1 + static_cast<int>(i); // 输入节点依次分配
            cache[name] = idx;
            return idx;
        }
    }

    // 其他普通节点：提取 ':' 之后的 pin 并转为数字
    int colon = static_cast<int>(name.rfind(':'));
    if (colon <= 0 || colon + 1 >= static_cast<int>(name.size())) {
        cerr << "[Mapping Error] Invalid node format: " << name << endl;
        throw runtime_error("Invalid node format (missing colon or pin)");
    }
    const string pin_part = name.substr(colon + 1);
    // 要求 pin 必须是纯数字
    for (char ch : pin_part) {
        if (!isdigit(static_cast<unsigned char>(ch))) {
            cerr << "[Mapping Error] Non-numeric pin encountered: " << pin_part << " in node " << name << endl;
            throw runtime_error("Non-numeric pin in node name");
        }
    }
    int idx = stoi(pin_part);
    cache[name] = idx;
    return idx;
}

// 构造 Node_Info 数组并建立邻接关系（双向索引）
inline Topology BuildTopologyFromRess(const spef::Net &net,
                                      const string &out_name,
                                      const vector<tuple<string, Input_info>> &Input)
{
    Topology topo;
    topo.base_for_special = static_cast<int>(net.ress.size() * 2); // 预留一个安全偏移

    // 收集所有出现过的节点名，建立映射
    vector<pair<int,int>> edges; // (u,v) 仅用于邻接关系，电阻值稍后可另存
    vector<double> edge_res;     // 对应电阻值
    edges.reserve(net.ress.size());
    edge_res.reserve(net.ress.size());

    for (const auto &res : net.ress) {
        const string &a = get<0>(res);
        const string &b = get<1>(res);
        double r = get<2>(res);
        int u = MapNodeNameToIndex(a, out_name, Input, topo.base_for_special, topo.name_to_idx);
        int v = MapNodeNameToIndex(b, out_name, Input, topo.base_for_special, topo.name_to_idx);
        edges.emplace_back(u, v);
        edge_res.emplace_back(r);
    }

    // 计算最大索引以分配 Node_Info 容器
    int max_idx = topo.base_for_special + 1 + static_cast<int>(Input.size());
    for (const auto &kv : topo.name_to_idx) {
        if (kv.second > max_idx) max_idx = kv.second;
    }
    topo.nodes.assign(static_cast<size_t>(max_idx + 1), Node_Info{});
    for (auto &n : topo.nodes) n.neighbors.reserve(2);

    // 建立双向邻接关系，并简单累计电阻到两个端点（后续可改为边权存储）
    for (size_t i = 0; i < edges.size(); ++i) {
        int u = edges[i].first;
        int v = edges[i].second;
        double r = edge_res[i];
        topo.nodes[u].neighbors.push_back(v);
        topo.nodes[v].neighbors.push_back(u);
        topo.nodes[u].resistor += r;
        topo.nodes[v].resistor += r;
    }
    for (auto &n : topo.nodes) {
        n.backward_flag = static_cast<int>(n.neighbors.size());
    }
    return topo;
}

// Step 2: 根据 caps 信息填充节点电容及对地等效电容（耦合电容按一半分配）
inline void FillCapsFromNet(const spef::Net &net,
                            Topology &topo,
                            const string &out_name,
                            const vector<tuple<string, Input_info>> &Input)
{
    for (const auto &cap : net.caps) {
        const string &n1 = get<0>(cap);
        const string &n2 = get<1>(cap); // 若为空字符串则为对地电容
        double cval = get<2>(cap);
        try {
            int idx1 = MapNodeNameToIndex(n1, out_name, Input, topo.base_for_special, topo.name_to_idx);
            if (n2.empty()) {
                if (idx1 >= static_cast<int>(topo.nodes.size())) topo.nodes.resize(idx1 + 1);
                // 对地电容仅累加到 ground_cap，避免与 cap 重复
                topo.nodes[idx1].ground_cap += cval;
            } else {
                int idx2 = MapNodeNameToIndex(n2, out_name, Input, topo.base_for_special, topo.name_to_idx);
                int need = std::max(idx1, idx2) + 1;
                if (need > static_cast<int>(topo.nodes.size())) topo.nodes.resize(need);
                // 耦合电容按一半分配到两端的对地等效（ground_cap）
                double half = cval * 0.5;
                topo.nodes[idx1].ground_cap += half;
                topo.nodes[idx2].ground_cap += half;
            }
        } catch (const std::exception &e) {
            cerr << "[Cap Fill Error] " << e.what() << " (cap nodes: " << n1 << (n2.empty()?"":" , ") << n2 << ")" << endl;
        }
    }
}

// Step 3: 计算 Elmore 延时
// 说明：
// - 使用 net.ress 重建带电阻的邻接（因为 Node_Info 仅累计总电阻，不含单边电阻）
// - subtree_cap 计算包含节点自身的 (cap + ground_cap + sink_load_cap*unit_factor)
// - Elmore(delay, sink) = 路径上每条边的 R_edge * C_downstream(该边指向 sink 的子树)
// - 负载电容来自 Input_info.pin_cap（假定字段名符合语义），通过 unit_factor 转换（当前设 1）
inline vector<pair<string,double>> ComputeElmoreDelays(const spef::Net &net,
                                                       const string &out_name,
                                                       const vector<tuple<string, Input_info>> &Input,
                                                       Topology &topo,
                                                       double pin_load_unit_factor,
                                                       double cap_ff_to_ps_factor)
{
    vector<pair<string,double>> result; // (input_name, delay)
    if (net.ress.empty()) return result;

    // 构建带电阻邻接
    vector<vector<pair<int,double>>> adj(topo.nodes.size());
    try {
        for (const auto &res : net.ress) {
            const string &a = get<0>(res);
            const string &b = get<1>(res);
            double r = get<2>(res);
            int u = MapNodeNameToIndex(a, out_name, Input, topo.base_for_special, topo.name_to_idx);
            int v = MapNodeNameToIndex(b, out_name, Input, topo.base_for_special, topo.name_to_idx);
            if (u >= (int)adj.size() || v >= (int)adj.size()) continue; // 安全检查
            adj[u].push_back({v,r});
            adj[v].push_back({u,r});
        }
    } catch (const std::exception &e) {
        cerr << "[Elmore Build Adj Error] " << e.what() << endl;
        return result;
    }

    // 根节点索引
    int root_idx;
    try {
        root_idx = MapNodeNameToIndex(out_name, out_name, Input, topo.base_for_special, topo.name_to_idx);
    } catch (const std::exception &e) {
        cerr << "[Elmore Root Error] " << e.what() << endl;
        return result;
    }

    // 标记 sink（输入 pin）索引以及对应负载电容
    unordered_map<int,double> sink_load_cap; // idx -> load_cap (in fF after unit_factor)
    for (const auto &inp : Input) {
        const string &inp_name = get<0>(inp);
        try {
            int idx = MapNodeNameToIndex(inp_name, out_name, Input, topo.base_for_special, topo.name_to_idx);
            sink_load_cap[idx] = get<1>(inp).pin_cap * pin_load_unit_factor; // pin_cap 按单位系数转换到与 caps 相同单位（fF）
        } catch (const std::exception &e) {
            cerr << "[Elmore Sink Map Error] " << e.what() << " name=" << inp_name << endl;
        }
    }

    // DFS 建树（假设为 RC 树），记录 parent 与遍历顺序
    vector<int> parent(topo.nodes.size(), -1);
    vector<int> order; order.reserve(topo.nodes.size());
    vector<char> visited(topo.nodes.size(), 0);
    std::function<void(int)> dfs = [&](int u){
        visited[u] = 1;
        order.push_back(u);
        for (auto [v,r] : adj[u]) {
            if (!visited[v]) {
                parent[v] = u;
                dfs(v);
            }
        }
    };
    dfs(root_idx);

    // 计算子树电容：逆序累加
    vector<double> node_total_cap(topo.nodes.size(), 0.0);
    for (size_t i = 0; i < topo.nodes.size(); ++i) {
        double load = 0.0;
        auto it = sink_load_cap.find((int)i);
        if (it != sink_load_cap.end()) load = it->second;
        node_total_cap[i] = topo.nodes[i].cap + topo.nodes[i].ground_cap + load;
    }

    vector<double> subtree_cap(topo.nodes.size(), 0.0);
    for (int i = (int)order.size() - 1; i >= 0; --i) {
        int u = order[i];
        double sum = node_total_cap[u];
        for (auto [v,r] : adj[u]) {
            if (parent[v] == u) {
                sum += subtree_cap[v];
            }
        }
        subtree_cap[u] = sum;
    }

    // 计算到每个节点的延时（再挑选 sinks）
    vector<double> elmore(topo.nodes.size(), 0.0);
    // 对每条树边 (u->v) 贡献 R(u,v)*subtree_cap[v]
    // 在 DFS 序中再次遍历树边即可
    for (int u : order) {
        for (auto [v,r] : adj[u]) {
            if (parent[v] == u) {
                elmore[v] = elmore[u] + r * subtree_cap[v] * cap_ff_to_ps_factor; // 将 ohm*fF 转换为 ps
            }
        }
    }

    // 汇总 sinks
    for (const auto &inp : Input) {
        const string &inp_name = get<0>(inp);
        try {
            int idx = MapNodeNameToIndex(inp_name, out_name, Input, topo.base_for_special, topo.name_to_idx);
            result.push_back({inp_name, elmore[idx]});
        } catch (...) { /* 已在上方报错 */ }
    }
    return result;
}

// 调试函数：针对当前只有一个 output 和一个 input 的电路，逐级输出电阻与电容、Elmore贡献
inline void DebugElmoreSinglePath(const spef::Net &net,
                                  const string &out_name,
                                  const vector<tuple<string, Input_info>> &Input,
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
    // 构建带电阻邻接
    vector<vector<pair<int,double>>> adj(topo.nodes.size());
    unordered_map<int,double> edge_res_accum; // key (u<<32)|v if needed (not essential here)
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

    // 反向映射（仅用于打印）
    unordered_map<int,string> idx_to_name;
    for (auto &kv : topo.name_to_idx) idx_to_name[kv.second] = kv.first;

    // 负载电容映射
    unordered_map<int,double> sink_load_cap;
    for (auto &inp : Input) {
        try {
            int idx = MapNodeNameToIndex(get<0>(inp), out_name, Input, topo.base_for_special, topo.name_to_idx);
            sink_load_cap[idx] = get<1>(inp).pin_cap * pin_load_unit_factor; // fF
        } catch (...) {}
    }

    // BFS 找到 root->sink 路径
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
    // 重建路径
    vector<int> path;
    for (int cur = sink_idx; cur != root_idx; cur = parent[cur]) path.push_back(cur);
    path.push_back(root_idx);
    reverse(path.begin(), path.end());

    // 子树电容计算（使用全树 DFS）
    vector<char> visited(topo.nodes.size(), 0);
    vector<int> order;
    function<void(int)> dfs = [&](int u){
        visited[u]=1; order.push_back(u);
        for (auto &pr: adj[u]) {
            int v=pr.first; if(!visited[v]){ parent[v]=u; dfs(v);} }
    };
    // 重置 parent 以便子树计算
    parent.assign(topo.nodes.size(), -1); dfs(root_idx);

    vector<double> node_total_cap(topo.nodes.size(),0.0);
    for (size_t i=0;i<topo.nodes.size();++i){
        double load = 0.0; auto it=sink_load_cap.find((int)i); if(it!=sink_load_cap.end()) load=it->second;
        node_total_cap[i] = topo.nodes[i].cap + topo.nodes[i].ground_cap + load; // fF
    }
    vector<double> subtree_cap(topo.nodes.size(),0.0);
    for (int i=(int)order.size()-1;i>=0;--i){
        int u=order[i]; double sum=node_total_cap[u];
        for (auto &pr: adj[u]) { int v=pr.first; if(parent[v]==u) sum += subtree_cap[v]; }
        subtree_cap[u]=sum; // fF
    }

    // 打印节点信息
    cout << "[DebugElmore] Node list (index | name | cap_fF | ground_fF | load_fF | subtree_fF)" << endl;
    for (int idx : path) {
        double load = sink_load_cap.count(idx)? sink_load_cap[idx]:0.0;
        cout << idx << " | " << (idx_to_name.count(idx)?idx_to_name[idx]:"<unnamed>")
             << " | " << topo.nodes[idx].cap
             << " | " << topo.nodes[idx].ground_cap
             << " | " << load
             << " | " << subtree_cap[idx]
             << endl;
    }

    // 计算并打印每条边的 Elmore 贡献
    cout << "[DebugElmore] Edge contributions (u->v: R_ohm * C_downstream_fF -> ps)" << endl;
    double cumulative = 0.0;
    for (size_t i=0;i+1<path.size();++i){
        int u=path[i]; int v=path[i+1];
        double r_edge = 0.0;
        for (auto &pr: adj[u]) if(pr.first==v){ r_edge = pr.second; break; }
        double contrib_ps = r_edge * subtree_cap[v] * cap_ff_to_ps_factor; // ps
        cumulative += contrib_ps;
        cout << (idx_to_name.count(u)?idx_to_name[u]:to_string(u)) << " -> "
             << (idx_to_name.count(v)?idx_to_name[v]:to_string(v))
             << ": R=" << r_edge << " ohm, C_downstream=" << subtree_cap[v] << " fF, contrib=" << contrib_ps << " ps, cum=" << cumulative << " ps" << endl;
    }
    cout << "[DebugElmore] Total delay to sink (" << sink_name << ") = " << cumulative << " ps" << endl;
    cout << "[DebugElmore] End" << endl;
}

// 输出 connections 长度统计（英文），封装为函数调用
auto print_connections_length_stats = [](const spef::Spef &p)
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
};

// net中包含了connection，cap，res等信息
// 对 net.ress 执行所需的 4 步处理
auto check_ress_pin_consecutive(const spef::Net &net, const string &out_name, const vector<tuple<string, Input_info>> &Input)
{
    // 1) 提取各个 res 的元素<0>和元素<1>（都是字符串）
    vector<string> res_endpoints;
    res_endpoints.reserve(net.ress.size() * 2);
    for (const auto &res : net.ress)
    {
        const string &a = get<0>(res);
        const string &b = get<1>(res);
        res_endpoints.push_back(a);
        res_endpoints.push_back(b);
    }

    // 2) 滤掉 out_name 和 Input 中的 connection.name（Input 各元素的 <0>）
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

    // 对 remaining 进行去重（保持首次出现的顺序）
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

    // 3) 对剩下的字符串，提取 ID，参考上面的做法：'*ID:Pin'，取 1..index-1
    vector<int> ids;
    ids.reserve(remaining.size());
    for (const auto &s : remaining)
    {
        int index = static_cast<int>(s.rfind(':'));
        if (index <= 1) continue; // 无法形成有效 '*<ID>:' 格式
        try
        {
            int id = stoi(s.substr(index+1, - 1));
            ids.push_back(id);
        }
        catch (...)
        {
            // 跳过无法解析的项
        }
    }

    // 去重并排序
    sort(ids.begin(), ids.end());
    ids.erase(unique(ids.begin(), ids.end()), ids.end());

    // 4) 输出数字，并检测是否连续整数
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

int main(int argc, char **argv)
{
    // Redirect all std::cout outputs to test.log for diagnosis
    std::ofstream log_file("test.log", std::ios::out | std::ios::trunc);
    if (log_file.is_open()) {
        std::cout.rdbuf(log_file.rdbuf());
    } else {
        std::cerr << "Failed to open test.log for writing" << std::endl;
    }
    /* 以下部分代码用于指令的参数读入，请选择性使用 */
    int c;
    int spef_num;
    char *file_path = nullptr;
    // char *feature_path = nullptr;                // 已经与助教确认，feature_path参数目前不需要使用
    int option_index = 0;

    static struct option long_options[] = {
        {"file_path", required_argument, 0, 'f'}, 
        //{"feature_path", required_argument, 0, 'e'}, 
        {"spef_num", required_argument, 0, 'm'}
    };

    //命令行参数默认值
    // windows环境记得把路径的 ‘/’ 改成 ‘\\’
    #ifdef __DEBUG__
    string default_file_path = "D:\\Documents\\Coding\\Projects\\Delay_calc\\Delay_calc\\simple";
    int default_spef_num = 0;
    file_path = (char *)malloc((strlen(default_file_path.c_str()) + 1) * sizeof(char));
    strcpy(file_path, default_file_path.c_str());
    spef_num = default_spef_num;
    #endif

    // 解析命令行参数
    while ((c = getopt_long(argc, argv, "s", long_options, &option_index)))
    {
        if (c == -1)
            break;
        switch (c)
        {
        case 'f':
            file_path = (char *)malloc((strlen(optarg) + 1) * sizeof(char));
            strcpy(file_path, optarg);
            // cout << spef_file << endl;
            break;
        // case 'e':
        //     feature_path = (char *)malloc((strlen(optarg) + 1) * sizeof(char));
        //     strcpy(feature_path, optarg);
        //     // cout << spef_file << endl;
        //     break;
        case 'm':
            spef_num = atoi(optarg);
            break;
        default:
            printf("?? getopt returned character code 0%o ??\n", c);
        }
    }


    /* 进行netlist_info.txt和 delay_data文件夹中Groupx.txt文件的解析*/
    /* Read_netlist_file函数将读取到的信息存在外部变量 netlist_info 中，是一个Hash结构 */
    // Read netlist file 会读取电路拓扑结构和负载电容，Read delay file 会读取延时信息，信息都存储在 netlist_info 变量中
    string netlist_file = string(file_path) + "/netlist_info.txt";
    if (Read_netlist_file(netlist_file))
        exit(1);
    /* 读取delay文件，注意spef0和1是两种corner下的同一批电路，这意味着同一个net会有两种delay，处理不同spef采用不同的delay */
    string delay_file = string(file_path) + "/delay_data/Group" + to_string(spef_num) + ".txt";
    if (Read_delay_file(delay_file))
    {
        cerr << "读取delay文件失败" << endl;
        exit(1);
    }

    // auto elapsed_t1 = chrono::steady_clock::now();
    string spef_file = string(file_path) + "/SPEF/Group" + to_string(spef_num) + ".spef";
    /// 读取SPEF文件
    spef::Spef parser;
    if (not parser.read(spef_file))
    {
        cerr << "读取spef文件失败:" << *parser.error << endl;
        exit(1);
    }

    // 输出 connections 长度统计
    print_connections_length_stats(parser);
    
    /* 一个SPEF文件含有多个net，针对每一个net做处理 */
    for (auto &net : parser.nets)
    {
        /* 一个net只有一个output，有多个input */
        string out_name;      // spef文件中output的名字，是一个ID，例如 *1681713:Q，用于建立net的数据结构
        string OUT_REAL_NAME; // 最后用来写入文件真实的名字 CNU17/R4_reg_18_
        /* 一条path包含多个从output->input的路径，也就是一个net包含多条path */
        /* Input变量第一个string元素是一个input pin的 ID */
        /* Input_info记录了这个input的real name和引脚电容、以及以这个input结束的path的延时值 */
        vector<tuple<string, Input_info>> Input;

        vector<Input_info> paths; // 暂时用来记录查询到的input信息
        bool flag = 0;
        // cout << "net.name: " << net.name << endl;
        for (auto &connection : net.connections)
        {
            if (connection.direction == spef::ConnectionDirection::OUTPUT)
            {
                out_name = connection.name;
                int index = connection.name.rfind(':');                 // 找到‘:’的位置
                int ID = stoi(connection.name.substr(1, index - 1));    // 提取出ID号 name格式参考 *1681713:Q
                OUT_REAL_NAME = parser.name_map[ID] + '/' + connection.name.substr(index + 1, -1);      // 拼接出真实名字, name_map： ID->real name(string)

                if (netlist_info.find(OUT_REAL_NAME) == netlist_info.end())
                {
                    // 如果该net的out不在net_info中，那就跳过这个net，继续下一个net
                    // continue loop;
                    flag = 1;
                    #ifndef __DEBUG__
                    cout << "This net is useless" << net.name << endl;
                    cout << "index: " << index << endl;
                    cout << "ID: " << ID << endl;
                    cout << "OUT_REAL_NAME: " << OUT_REAL_NAME << endl;
                    #endif
                }
                else
                    paths = netlist_info.find(OUT_REAL_NAME)->second;
            }
        }
        for (auto &connection : net.connections)
        {
            if (connection.direction == spef::ConnectionDirection::INPUT)
            {
                // 将connection.name翻译为真实名，然后在info中寻找它的pincap
                int index = connection.name.rfind(':');
                int ID = stoi(connection.name.substr(1, index - 1));
                string Input_Name = parser.name_map[ID] + '/' + connection.name.substr(index + 1, -1);

                for (auto &p : paths)
                {
                    if (Input_Name == p.name)
                    {
                        Input.push_back(make_pair(connection.name, p));
                    }
                }
            }
        }
        if (flag)
            continue;

        /********************************************************************************************/
        /********************************************************************************************/
        /********************************************************************************************/
        // Input中包含了这个net所有的path的延时、input pin的id以及real name，还有引脚电容
        
        for (auto i : Input)
        {
            std::cout << get<0>(i) << " " << get<1>(i).name << " " << get<1>(i).delay << " " << get<1>(i).pin_cap << endl;
        }
        // Step 1: 搭建电路拓扑的树（基于 ress）
        Topology topo = BuildTopologyFromRess(net, out_name, Input);
        cout << "[Topology] nodes=" << topo.nodes.size() << ", edges=" << net.ress.size() << endl;
        FillCapsFromNet(net, topo, out_name, Input);
        // 预览前几个节点的电容
        int preview = 5;
        cout << "[Caps Preview] index:cap:ground_cap -> ";
        for (int i = 0; i < preview && i < static_cast<int>(topo.nodes.size()); ++i) {
            cout << i << ":" << topo.nodes[i].cap << ":" << topo.nodes[i].ground_cap << "  ";
        }
        cout << endl;

        // Step 3: 计算 Elmore 延时（单位转换因子暂设 1）
        // 单位设定：caps 为 fF、res 为 ohm，输出 ps。e3
        // ohm * fF -> seconds: 1e-15；转成 ps 乘以 1e12 => 综合因子 1e-3。
        double pin_load_unit_factor = 1e3;    // 若 Input.pin_cap 单位与 caps 不同，可在此调整为把其换算到 fF
        double cap_ff_to_ps_factor = 1e-3;    // R(ohm)*C(fF) 转 ps 的系数
        auto elmore = ComputeElmoreDelays(net, out_name, Input, topo, pin_load_unit_factor, cap_ff_to_ps_factor);
        cout << "[Elmore] delays to input pins (ps):" << endl;
        for (auto &pr : elmore) {
            cout << pr.first << " => " << pr.second << endl;
        }

        // Compare with standard delays in Input_info (ns -> ps)
        // Build a quick lookup from input name to computed ps
        unordered_map<string,double> elmore_map;
        for (auto &pr : elmore) elmore_map[pr.first] = pr.second;

        cout << "[Compare] name | standard(ps) | computed(ps) | rel_error" << endl;
        cout.setf(std::ios::fixed);
        cout.precision(6);
        for (const auto &inp : Input) {
            const string &inp_name = get<0>(inp);
            const auto &info = get<1>(inp);
            double std_ps = info.delay * 1000.0; // ns -> ps
            auto it = elmore_map.find(inp_name);
            if (it != elmore_map.end()) {
                double calc_ps = it->second;
                double rel_err = (std_ps != 0.0) ? ((calc_ps - std_ps) / std_ps) : 0.0;
                cout << inp_name << " | " << std_ps << " | " << calc_ps << " | " << rel_err << endl;
            } else {
                cout << inp_name << " | " << std_ps << " | " << "N/A" << " | " << "N/A" << endl;
            }
        }
        cout.unsetf(std::ios::fixed);

        // 调试：单路径详细信息（仅在只有一个 input 时调用）
        if (Input.size() == 1) {
            DebugElmoreSinglePath(net, out_name, Input, topo, pin_load_unit_factor, cap_ff_to_ps_factor);
        }


        for (auto &cap : net.caps)
        {
        }
    }

    /********************************************************************************************/
    /********************************************************************************************/
    /********************************************************************************************/

    /* 释放内存 */
    if (file_path)
    {
        free(file_path);
        file_path = nullptr;
    }
    // if (feature_path)
    // {
    //     free(feature_path);
    //     feature_path = nullptr;
    // }
    return 0;
}
