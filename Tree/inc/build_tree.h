#ifndef BUILD_TREE_H
#define BUILD_TREE_H

#include <string>
#include <string_view> // 必须包含
#include <vector>
#include <tuple>
#include <unordered_map>
#include "parser-spef.hpp" // 包含 spef::Net 定义
#include "Read_train/Read.h" // 包含 Input_info 定义

// 定义节点信息的结构体
struct Node_Info {
    int id;
    std::vector<std::pair<int, double>> neighbors; // (neighbor_index, resistance)
    double ground_cap = 0.0;
    double subtree_cap = 0.0;
    double elmore_delay = 0.0;
    
    // === 补充缺失的成员变量 (Fix for calc_delay.cpp) ===
    double eff_subtree_cap = 0.0;      // 有效子树电容
    double elmore_delay_new = 0.0;     // 计算后的新Elmore延时
    double subtree_cap_mul_m1 = 0.0;   // 用于二阶矩计算: C * m1
    double m2 = 0.0;                   // 二阶矩
    // ===================================================

    int parent_idx = -1;
    int backward_flag = 0; // 用于拓扑分析
    double voltage = 0.0;  // 可选，用于ML特征
    
    Node_Info() = default;
};

// 拓扑结构体
struct Topology {
    std::vector<Node_Info> nodes;
    std::unordered_map<std::string, int> name_to_idx; // 名字到索引的映射
    std::vector<std::pair<int, int>> leaf_node_idx;   // 叶子节点索引
    int base_for_special = 0; // 输出引脚和输入引脚的基础索引偏移
    double total_capacitance = 0.0;
    double total_resistor = 0.0;
    
    Topology() = default;
};

// 核心函数声明
// 注意：参数类型已修改为 std::string_view 以匹配 parser 的 Zero-Copy 优化
int MapNodeNameToIndex(std::string_view name,
                       std::string_view out_name,
                       const std::vector<std::tuple<std::string, Input_info>> &Input,
                       int base_for_special,
                       std::unordered_map<std::string, int> &cache);

Topology BuildTopologyFromRess(const spef::Net &net,
                               std::string_view out_name,
                               const std::vector<std::tuple<std::string, Input_info>> &Input);

void FillCapsFromNet(const spef::Net &net,
                     Topology &topo,
                     std::string_view out_name,
                     const std::vector<std::tuple<std::string, Input_info>> &Input);

#endif // BUILD_TREE_H