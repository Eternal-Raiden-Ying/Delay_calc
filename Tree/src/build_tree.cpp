#include "Tree/inc/build_tree.h"
#include <iostream>
#include <stdexcept>
#include <cctype>
#include <algorithm>
#include <string_view>

using namespace std;

// param:   name: node name from SPEF (string_view)
//          out_name: output pin name (string_view)
//          Input: vector of input pins
// return:  index in Topology.nodes
int MapNodeNameToIndex(std::string_view name,
                       std::string_view out_name,
                       const vector<tuple<string, Input_info>> &Input,
                       int base_for_special,
                       unordered_map<string,int> &cache)
{
    // C++17 map::find 需要 string，显式构造
    string name_str(name); 
    auto it = cache.find(name_str);
    if (it != cache.end()) return it->second;

    if (name == out_name) {
        int idx = base_for_special;
        cache[name_str] = idx;
        return idx;
    }
    for (int i = 0; i < Input.size(); ++i) {
        // tuple<string> vs string_view 比较是合法的
        if (name == get<0>(Input[i])) {
            int idx = base_for_special + 1 + static_cast<int>(i);
            cache[name_str] = idx;
            return idx;
        }
    }

    // 处理内部节点，格式 *net_name:pin_number
    // 使用 string_view 查找冒号，避免拷贝
    size_t colon_pos = name.rfind(':');
    if (colon_pos == string_view::npos || colon_pos + 1 >= name.size()) {
        cerr << "[Mapping Error] Invalid node format: " << name << endl;
        throw runtime_error("Invalid node format (missing colon or pin)");
    }
    
    // 提取数字部分
    string_view pin_part = name.substr(colon_pos + 1);
    for (char ch : pin_part) {
        if (!isdigit(static_cast<unsigned char>(ch))) {
            cerr << "[Mapping Error] Non-numeric pin encountered: " << pin_part << " in node " << name << endl;
            throw runtime_error("Non-numeric pin in node name");
        }
    }
    
    // stoi 需要 string
    int idx = stoi(string(pin_part));
    // cache[name_str] = idx; // Optional optimization
    return idx;
}

Topology BuildTopologyFromRess(const spef::Net &net,
                               std::string_view out_name,
                               const vector<tuple<string, Input_info>> &Input)
{   
    Topology topo = Topology();
    topo.base_for_special = static_cast<int>(net.ress.size() + 1);
    topo.name_to_idx.reserve(topo.base_for_special + 1 + static_cast<int>(Input.size()));
    topo.nodes.assign(topo.base_for_special + 1 + static_cast<int>(Input.size()), Node_Info());
    topo.leaf_node_idx.reserve(static_cast<int>(Input.size()) + 1);
    
    for (const auto &res : net.ress) {
        std::string_view a = get<0>(res);
        std::string_view b = get<1>(res);
        double r = get<2>(res);
        
        topo.total_resistor += r;
        int u = MapNodeNameToIndex(a, out_name, Input, topo.base_for_special, topo.name_to_idx);
        int v = MapNodeNameToIndex(b, out_name, Input, topo.base_for_special, topo.name_to_idx);

        topo.nodes[u].neighbors.push_back(pair<int,double>(v, r));
        topo.nodes[v].neighbors.push_back(pair<int,double>(u, r));
    }
    
    for (size_t i=0; i < topo.nodes.size(); ++i) {
        Node_Info &n = topo.nodes[i];
        n.backward_flag = static_cast<int>(n.neighbors.size());
        if (n.backward_flag == 1 && static_cast<int>(i) != topo.base_for_special) {
            topo.leaf_node_idx.push_back(pair<int,int>(static_cast<int>(i), 0));
        }
    }

    return topo;
}

void FillCapsFromNet(const spef::Net &net,
                     Topology &topo,
                     std::string_view out_name,
                     const vector<tuple<string, Input_info>> &Input)
{   
    for (const auto &cap : net.caps) {
        std::string_view n1 = get<0>(cap);
        // n2 (get<1>) is usually ignored for ground caps
        double cval = get<2>(cap);
        
        int idx1 = MapNodeNameToIndex(n1, out_name, Input, topo.base_for_special, topo.name_to_idx);
        topo.nodes[idx1].ground_cap += cval;
        topo.total_capacitance += cval;
    }
}