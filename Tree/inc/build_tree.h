#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <tuple>
#include <parser-spef.hpp>
#include "Read_train/Read.h"

// Node / topology definitions moved from course.cpp

struct Node_Info {
    double ground_cap = 0.0;    // 该节点到地的电容
    std::vector<std::pair<int,double>> neighbors; // 相邻节点及对应电阻值 (neighbor index, resistor)
    
    int backward_flag = 0;
    int parent_idx = -1;

    double subtree_cap = 0.0;   // 缓存该节点子树的总电容（自身 + 所有子孙节点），在 Elmore 计算时填充
    double elmore_delay = 0.0;   // 缓存该节点的 Elmore 延时，在 Elmore 计算时填充      D2M的m1
    double subtree_cap_mul_m1 = 0.0;
    double m2 = 0.0;

    double voltage = 0.0;
    double eff_subtree_cap = 0.0; 
    double elmore_delay_new = 0.0;

    Node_Info() {
        neighbors.reserve(2);
    }
};

struct Topology {
    std::vector<Node_Info> nodes;
    int base_for_special = 0;
    double total_resistor = 0.0;
    double total_capacitance = 0.0;
    std::unordered_map<std::string,int> name_to_idx;        // only for input and output pins
    std::vector<std::pair<int, int>> leaf_node_idx;         // idx, visited_flag
};

// Build RC tree from SPEF net


// node name to index mapping
int MapNodeNameToIndex(const std::string &name,
                       const std::string &out_name,
                       const std::vector<std::tuple<std::string, Input_info>> &Input,
                       int base_for_special,
                       std::unordered_map<std::string,int> &cache);

Topology BuildTopologyFromRess(const spef::Net &net,
                               const std::string &out_name,
                               const std::vector<std::tuple<std::string, Input_info>> &Input);

void FillCapsFromNet(const spef::Net &net,
                     Topology &topo,
                     const std::string &out_name,
                     const std::vector<std::tuple<std::string, Input_info>> &Input);
