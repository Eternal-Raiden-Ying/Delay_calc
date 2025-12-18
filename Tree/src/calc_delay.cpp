#include "Tree/inc/calc_delay.h"
#include <iostream>
#include <functional>

using namespace std;

std::vector<std::pair<std::string,double>> ComputeElmoreDelays(
    const spef::Net &net,
    const std::string &out_name,
    const std::vector<std::tuple<std::string, Input_info>> &Input,
    Topology &topo,
    double pin_load_unit_factor,
    double cap_ff_to_ps_factor)
{
    vector<pair<string,double>> result;
    if (net.ress.empty()) return result;

    vector<vector<pair<int,double>>> adj(topo.nodes.size());
    try {
        for (const auto &res : net.ress) {
            const string &a = get<0>(res);
            const string &b = get<1>(res);
            double r = get<2>(res);
            int u = MapNodeNameToIndex(a, out_name, Input, topo.base_for_special, topo.name_to_idx);
            int v = MapNodeNameToIndex(b, out_name, Input, topo.base_for_special, topo.name_to_idx);
            if (u >= (int)adj.size() || v >= (int)adj.size()) continue;
            adj[u].push_back({v,r});
            adj[v].push_back({u,r});
        }
    } catch (const std::exception &e) {
        cerr << "[Elmore Build Adj Error] " << e.what() << endl;
        return result;
    }

    int root_idx;
    try {
        root_idx = MapNodeNameToIndex(out_name, out_name, Input, topo.base_for_special, topo.name_to_idx);
    } catch (const std::exception &e) {
        cerr << "[Elmore Root Error] " << e.what() << endl;
        return result;
    }

    unordered_map<int,double> sink_load_cap;
    for (const auto &inp : Input) {
        const string &inp_name = get<0>(inp);
        try {
            int idx = MapNodeNameToIndex(inp_name, out_name, Input, topo.base_for_special, topo.name_to_idx);
            sink_load_cap[idx] = get<1>(inp).pin_cap * pin_load_unit_factor;
        } catch (const std::exception &e) {
            cerr << "[Elmore Sink Map Error] " << e.what() << " name=" << inp_name << endl;
        }
    }

    vector<int> parent(topo.nodes.size(), -1);
    vector<int> order; order.reserve(topo.nodes.size());
    vector<char> visited(topo.nodes.size(), 0);
    function<void(int)> dfs = [&](int u){
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

    vector<double> node_total_cap(topo.nodes.size(), 0.0);
    for (size_t i = 0; i < topo.nodes.size(); ++i) {
        double load = 0.0;
        auto it = sink_load_cap.find((int)i);
        if (it != sink_load_cap.end()) load = it->second;
        node_total_cap[i] = topo.nodes[i].cap_downstream + topo.nodes[i].ground_cap + load;
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

    vector<double> elmore(topo.nodes.size(), 0.0);
    for (int u : order) {
        for (auto [v,r] : adj[u]) {
            if (parent[v] == u) {
                elmore[v] = elmore[u] + r * subtree_cap[v] * cap_ff_to_ps_factor;
            }
        }
    }

    for (const auto &inp : Input) {
        const string &inp_name = get<0>(inp);
        try {
            int idx = MapNodeNameToIndex(inp_name, out_name, Input, topo.base_for_special, topo.name_to_idx);
            result.push_back({inp_name, elmore[idx]});
        } catch (...) {}
    }
    return result;
}
