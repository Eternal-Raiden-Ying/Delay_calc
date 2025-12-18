#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <parser-spef.hpp>
#include "Read_train/Read.h"

// 外部全局：从 Read_train/Read.cpp 暴露
extern std::unordered_map<std::string, std::vector<Input_info>> netlist_info;

// 封装读取 netlist_info 和 delay 文件
// base_path: 例如 "D:/.../simple"
// spef_num:  GroupX 里的 X
// 返回非 0 表示失败
int load_netlist_and_delay(const std::string &base_path, int spef_num);

// 读取 SPEF 到 parser 中，返回非 0 表示失败
int load_spef(const std::string &base_path, int spef_num, spef::Spef &parser);
