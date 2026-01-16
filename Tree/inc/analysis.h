#pragma once

#include <unordered_map>
#include <vector>
#include <string>
#include <tuple>
#include <parser-spef.hpp>
#include "Read_train/Read.h"
#include "Tree/inc/build_tree.h"

// 输出 connections 长度统计（英文）
void print_connections_length_stats(const spef::Spef &p);

// net中包含了connection，cap，res等信息，检查 net.ress 的 pin ID 是否连续
void check_ress_pin_consecutive(const spef::Net &net,
                                const std::string &out_name,
                                const std::vector<std::tuple<std::string, Input_info>> &Input);

// 单路径调试接口（可选调用）
void DebugElmoreSinglePath(
    const spef::Net &net,
    const std::string &out_name,
    const std::vector<std::tuple<std::string, Input_info>> &Input,
    Topology &topo,
    double pin_load_unit_factor,
    double cap_ff_to_ps_factor);


// 将 RC 拓扑导出为 Gephi 可读的两个 CSV：节点表 + 边表
// - nodes_csv_path: 节点表路径
// - edges_csv_path: 边表路径
void ExportNetToGephiCsv(
    const spef::Net &net,
    const std::string &out_name,
    const std::vector<std::tuple<std::string, Input_info>> &Input,
    const Topology &topo,
    const std::string &nodes_csv_path,
    const std::string &edges_csv_path);

stringstream& write2csv(stringstream &ss, const std::vector<std::pair<std::string, double>> &res, const Topology &topo,
    const std::vector<std::tuple<std::string, Input_info>> &Input, const spef::Net &net, int precision=4);