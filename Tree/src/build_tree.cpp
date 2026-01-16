#include "Tree/inc/build_tree.h"
#include <iostream>
#include <stdexcept>
#include <cctype>
#include <algorithm>

using namespace std;

// param:   name: node name from SPEF
//          out_name: output pin name
//          Input: vector of input pins and their info, to get their input node name
//          base_for_special: base index for special nodes (output and inputs)
//          cache: cache for name to index mapping
//              cache is like this:
//                  output pin: out_name -> base_for_special
//                  input pins: Input[i].name -> base_for_special + 1 + i
//                  internal nodes: "*net_name:pin_number" -> pin_number (int) [option]
//  
// return:  index in Topology.nodes
int MapNodeNameToIndex(const string &name,
                       const string &out_name,
                       const vector<tuple<string, Input_info>> &Input,
                       int base_for_special,
                       unordered_map<string,int> &cache)
{
    // for output pin and input pins
    auto it = cache.find(name);
    if (it != cache.end()) return it->second;

    if (name == out_name) {
        int idx = base_for_special;
        cache[name] = idx;
        return idx;
    }
    for (int i = 0; i < Input.size(); ++i) {
        if (name == get<0>(Input[i])) {
            int idx = base_for_special + 1 + static_cast<int>(i);
            cache[name] = idx;
            return idx;
        }
    }

    // for internal nodes, regular name format *net_name:pin_number
    int colon = static_cast<int>(name.rfind(':'));
    if (colon <= 0 || colon + 1 >= static_cast<int>(name.size())) {
        cerr << "[Mapping Error] Invalid node format: " << name << endl;
        throw runtime_error("Invalid node format (missing colon or pin)");
    }
    const string pin_part = name.substr(colon + 1);
    for (char ch : pin_part) {
        if (!isdigit(static_cast<unsigned char>(ch))) {
            cerr << "[Mapping Error] Non-numeric pin encountered: " << pin_part << " in node " << name << endl;
            throw runtime_error("Non-numeric pin in node name");
        }
    }
    int idx = stoi(pin_part);
    // cache[name] = idx;  // if it will be faster without this, TODO: test it   RES: build topo & calc faster but fill cap slower
    return idx;
}

Topology BuildTopologyFromRess(const spef::Net &net,
                               const string &out_name,
                               const vector<tuple<string, Input_info>> &Input)
{   
    Topology topo = Topology();
    topo.base_for_special = static_cast<int>(net.ress.size() + 1);  // pin number start from 0, num_nodes = num_edges+1, last possible num node index = net.ress.size()
    topo.name_to_idx.reserve(topo.base_for_special + 1 + static_cast<int>(Input.size()));   // remain to be tested if we need to cache all the nodes
    topo.nodes.assign(topo.base_for_special + 1 + static_cast<int>(Input.size()), Node_Info());
    topo.leaf_node_idx.reserve(static_cast<int>(Input.size()) + 1);
    for (const auto &res : net.ress) {
        const string &a = get<0>(res);
        const string &b = get<1>(res);
        double r = get<2>(res);
        topo.total_resistor += r;
        int u = MapNodeNameToIndex(a, out_name, Input, topo.base_for_special, topo.name_to_idx);
        int v = MapNodeNameToIndex(b, out_name, Input, topo.base_for_special, topo.name_to_idx);

        // 邻接关系中存储 (neighbor index, resistor)
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
                     const string &out_name,
                     const vector<tuple<string, Input_info>> &Input)
{   
    for (const auto &cap : net.caps) {
        const string &n1 = get<0>(cap);     // name of node
        const string &n2 = get<1>(cap);     // empty!
        double cval = get<2>(cap);          // value of capacitance
        
        int idx1 = MapNodeNameToIndex(n1, out_name, Input, topo.base_for_special, topo.name_to_idx);
        topo.nodes[idx1].ground_cap += cval;
        topo.total_capacitance += cval;
    }
}
