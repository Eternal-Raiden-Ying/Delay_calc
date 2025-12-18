#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <tuple>
#include <parser-spef.hpp>
#include "Read_train/Read.h"

// Node / topology definitions moved from course.cpp

struct Node_Info {
    double cap_downstream = 0.0;
    double resistor = 0.0;
    double ground_cap = 0.0;
    std::vector<int> neighbors;
    int backward_flag = 0;

    Node_Info() {
        neighbors.reserve(2);
    }
};

struct Topology {
    std::vector<Node_Info> nodes;
    int base_for_special = 0;
    std::unordered_map<std::string,int> name_to_idx;        // only for input and output pins
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
