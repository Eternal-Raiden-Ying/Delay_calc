#pragma once

#include <vector>
#include <string>
#include <tuple>
#include <unordered_map>
#include <parser-spef.hpp>
#include "Read_train/Read.h"
#include "Tree/inc/build_tree.h"

// Elmore 延时计算接口
std::vector<std::pair<std::string,double>> ComputeElmoreDelays(
    const spef::Net &net,
    const std::string &out_name,
    const std::vector<std::tuple<std::string, Input_info>> &Input,
    Topology &topo,
    double pin_load_unit_factor,
    double cap_ff_to_ps_factor);

enum FitMode {
    MODE_RATIO = 1,
    MODE_DELTA = 2,
    MODE_LINEAR = 3
};

// Optional: capture the exact ML feature vector used during inference.
// Intended for feature export / debugging (e.g., path_features).
struct MLFeatureDumpRow {
    std::string inp_name;
    std::vector<double> feats;   // length = feature_mode (typically 34)
    double ml_pred = 0.0;
    double final_delay_ps = 0.0;
    std::string bucket;          // "short" / "long" / "all"
};

std::vector<std::tuple<std::string,double,double>> ML_fix(
    const std::string &out_name,
    const std::vector<std::tuple<std::string, Input_info>> &Input,
    Topology &topo,
    double pin_load_unit_factor,
    double cap_ff_to_ps_factor,
    const int &feature_mode,
    bool split_mode,
    FitMode fit_mode,
    const std::vector<std::pair<std::string,double>> &elmore_adv_ps,
    std::vector<MLFeatureDumpRow>* dump_rows = nullptr);


std::vector<std::pair<std::string,double>> ComputeElmoreDelays_advanced(
    const spef::Net &net,
    const std::string &out_name,
    const std::vector<std::tuple<std::string, Input_info>> &Input,
    Topology &topo,
    double pin_load_unit_factor,
    double cap_ff_to_ps_factor);