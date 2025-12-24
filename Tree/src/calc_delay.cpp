#include "Tree/inc/calc_delay.h"
#include <iostream>
#include <functional>
#include <cmath>
#include "Tree/resc/linear_true_34_0.h"
#include "Tree/resc/delta_false_34_0.h"

# define ln2 (double)(0.693147181)

using namespace std;

namespace ELMORE{
// 递归计算m1
double recursive_calc_m1(int cur_nd_idx, int src_nd_idx,
    const double &downstream_cap, Topology &topo, 
    const std::vector<std::tuple<std::string, Input_info>> &Input,
    double cap_ff_to_ps_factor, double pin_load_unit_factor) {

    Node_Info &cur_nd = topo.nodes[cur_nd_idx];
    if (src_nd_idx != -1) cur_nd.backward_flag--;

    // output pin cap is not used in elmore calc
    if (cur_nd_idx == topo.base_for_special) {
        return 0.0;
    }

    if (cur_nd.backward_flag > 1) {
        cur_nd.subtree_cap += downstream_cap;
        for (auto &[leaf_nd_idx, visited_flag]: topo.leaf_node_idx){
            if (visited_flag) continue;
            visited_flag = 1;
            recursive_calc_m1(
                leaf_nd_idx, -1, 
                0.0, topo, Input, 
                cap_ff_to_ps_factor, pin_load_unit_factor
            );
        }
        // elmore delay and parent_idx will be filled in recursive_calc from another leaf node
        return cur_nd.elmore_delay;
    }
    else{
        cur_nd.subtree_cap += downstream_cap+cur_nd.ground_cap; // 加上自身接地电容, 仅在只有一个未访问邻居时(父节点)加上自身ground_cap
        if (cur_nd_idx > topo.base_for_special) {
            cur_nd.subtree_cap += get<1>(Input[cur_nd_idx - topo.base_for_special - 1]).pin_cap * pin_load_unit_factor;
        }
        for (const auto &[nbr_idx, res] : cur_nd.neighbors) {
            if (topo.nodes[nbr_idx].backward_flag == 0) continue;   //不重复访问

            cur_nd.parent_idx = nbr_idx;
            cur_nd.backward_flag--;
            double delay = recursive_calc_m1(
                nbr_idx, cur_nd_idx, 
                cur_nd.subtree_cap, topo, Input, 
                cap_ff_to_ps_factor, pin_load_unit_factor
            );
            cur_nd.elmore_delay = delay + res * cur_nd.subtree_cap * cap_ff_to_ps_factor; // 转 ps
        }
        return cur_nd.elmore_delay;
    }
}

void forward_propagate_voltage(int src_idx, int cur_idx, Topology &topo, double pin_m1,
    const std::vector<std::tuple<std::string, Input_info>> &Input,
    double cap_ff_to_ps_factor, double pin_load_unit_factor) {

    Node_Info &cur_nd = topo.nodes[cur_idx];
    Node_Info &src_nd = topo.nodes[src_idx];
    // cur_nd.voltage = src_nd.voltage - (cur_nd.elmore_delay - src_nd.elmore_delay) * 0.5 / pin_m1;

    cur_nd.voltage = 1 - exp(-pin_m1 *ln2 / (cur_nd.elmore_delay+1e-6));
    for (auto &[nbr_idx, res]: cur_nd.neighbors){
        if (nbr_idx == src_idx) continue;
        forward_propagate_voltage(cur_idx, nbr_idx, topo, pin_m1, Input, cap_ff_to_ps_factor, pin_load_unit_factor);
    }
}

void backward_propagate_voltage(int src_idx, int cur_idx, Topology &topo, double pin_m1,
    const std::vector<std::tuple<std::string, Input_info>> &Input,
    double cap_ff_to_ps_factor, double pin_load_unit_factor) {
    Node_Info &cur_nd = topo.nodes[cur_idx];
    Node_Info &src_nd = topo.nodes[src_idx];
    double ress = 0.0;
    for (auto &[nbr_idx, res]: cur_nd.neighbors){
        if (nbr_idx != src_idx) continue;
        ress = res;
    }
    // cur_nd.voltage = src_nd.voltage + ress * src_nd.current;
    cur_nd.voltage = 1 - exp(-pin_m1 *ln2 / (cur_nd.elmore_delay+1e-6));
    cur_nd.dv_dt = src_nd.dv_dt - ress * src_nd.current / (pin_m1);
    double cap_load = 0.0;
    if (cur_idx > topo.base_for_special) {
        cap_load = get<1>(Input[cur_idx - topo.base_for_special -1]).pin_cap * pin_load_unit_factor + topo.nodes[cur_idx].ground_cap;
    } else {
        cap_load = topo.nodes[cur_idx].ground_cap;
    }
    cur_nd.current = cur_nd.dv_dt * cap_load + src_nd.current;

    if (cur_nd.neighbors.size() > 2 ) {
        for (auto &[nbr_idx, res]: cur_nd.neighbors){
            if (nbr_idx == src_idx) continue;
            if (nbr_idx == cur_nd.parent_idx) continue;
            forward_propagate_voltage(cur_idx, nbr_idx, topo, pin_m1, Input, cap_ff_to_ps_factor, pin_load_unit_factor);
        }
    }
    if (cur_nd.parent_idx == -1) return;
    else backward_propagate_voltage(cur_idx, cur_nd.parent_idx, topo, pin_m1, Input, cap_ff_to_ps_factor, pin_load_unit_factor);
}

void propagate_voltage(int pin_nd_idx, Topology& topo, 
    const std::vector<std::tuple<std::string, Input_info>> &Input,
    double cap_ff_to_ps_factor, double pin_load_unit_factor) {

    topo.nodes[pin_nd_idx].voltage = 0.5;
    topo.nodes[pin_nd_idx].dv_dt = 1/(2*topo.nodes[pin_nd_idx].elmore_delay);
    double cap_load = get<1>(Input[pin_nd_idx - topo.base_for_special -1]).pin_cap * pin_load_unit_factor + topo.nodes[pin_nd_idx].ground_cap;
    topo.nodes[pin_nd_idx].current = topo.nodes[pin_nd_idx].dv_dt * cap_load;

    backward_propagate_voltage(pin_nd_idx, topo.nodes[pin_nd_idx].parent_idx, 
        topo, topo.nodes[pin_nd_idx].elmore_delay, Input, cap_ff_to_ps_factor, pin_load_unit_factor);
}

double recursive_calc_m1_revised(int cur_nd_idx, int src_nd_idx,
    const double &eff_downstream_cap, Topology &topo, 
    const std::vector<std::tuple<std::string, Input_info>> &Input,
    double cap_ff_to_ps_factor, double pin_load_unit_factor) {

    Node_Info &cur_nd = topo.nodes[cur_nd_idx];
    if (src_nd_idx != -1) cur_nd.backward_flag--;

    // output pin cap is not used in elmore calc
    if (cur_nd_idx == topo.base_for_special) {
        return 0.0;
    }

    if (cur_nd.backward_flag > 1) {
        cur_nd.eff_subtree_cap += eff_downstream_cap;
        for (auto &[leaf_nd_idx, visited_flag]: topo.leaf_node_idx){
            if (visited_flag) continue;
            visited_flag = 1;
            recursive_calc_m1_revised(
                leaf_nd_idx, -1, 
                0.0, topo, Input, 
                cap_ff_to_ps_factor, pin_load_unit_factor
            );
        }
        // elmore delay and parent_idx will be filled in recursive_calc from another leaf node
        return cur_nd.elmore_delay_new;
    }
    else{
        cur_nd.eff_subtree_cap += eff_downstream_cap + cur_nd.voltage * cur_nd.ground_cap; // 加上自身接地电容, 仅在只有一个未访问邻居时(父节点)加上自身ground_cap
        if (cur_nd_idx > topo.base_for_special) {
            cur_nd.eff_subtree_cap += get<1>(Input[cur_nd_idx - topo.base_for_special - 1]).pin_cap * pin_load_unit_factor * cur_nd.voltage;
        }
        for (const auto &[nbr_idx, res] : cur_nd.neighbors) {
            if (topo.nodes[nbr_idx].backward_flag == 0) continue;   //不重复访问

            cur_nd.parent_idx = nbr_idx;
            cur_nd.backward_flag--;
            double delay = recursive_calc_m1_revised(
                nbr_idx, cur_nd_idx, 
                cur_nd.eff_subtree_cap, topo, Input, 
                cap_ff_to_ps_factor, pin_load_unit_factor
            );
            cur_nd.elmore_delay_new = delay + res * cur_nd.eff_subtree_cap * cap_ff_to_ps_factor; // 转 ps
        }
        return cur_nd.elmore_delay_new;
    }
}
}

namespace D2M {

// 根据一阶、二阶矩 (m1, m2) 计算等效二阶 RC 模型的 50% 延时
// 单位约定：m1 为 ps，m2 为 ps^2，返回值为 ps
double DelayFromMoments(double m1, double m2) {
    if (m1 <= 0.0) return 0.0;
    // 单极点近似的 50% 延时，用作初始值/回退
    const double single_pole_delay = m1;

    // D2M 二极点模型：
    // h(t) = (e^{-t/τ1} - e^{-t/τ2}) / (τ1 - τ2)
    // m1 = τ1 + τ2
    // m2 = 2(τ1^2 + τ1 τ2 + τ2^2)
    const double s1 = m1;
    const double s2 = 0.5 * m2;

    const double p = s1 * s1 - s2;               // τ1 τ2
    const double disc = s1 * s1 - 4.0 * p;       // 判别式 = 2*m2 - 3*m1^2

    if (p <= 0.0 || disc <= 0.0) {
        return single_pole_delay;
    }

    const double sqrt_disc = std::sqrt(disc);
    double tau1 = 0.5 * (s1 + sqrt_disc);
    double tau2 = 0.5 * (s1 - sqrt_disc);

    if (tau1 <= 0.0 || tau2 <= 0.0 || std::abs(tau1 - tau2) < 1e-12) {
        return single_pole_delay;
    }

    const double target = 0.5;   // 50% Vdd

    auto v = [&](double t) {
        double e1 = std::exp(-t / tau1);
        double e2 = std::exp(-t / tau2);
        return 1.0 - (tau1 * e1 - tau2 * e2) / (tau1 - tau2);
    };

    auto dv = [&](double t) {
        double e1 = std::exp(-t / tau1);
        double e2 = std::exp(-t / tau2);
        return (e1 - e2) / (tau1 - tau2);
    };

    // Newton 迭代，初值用单极点 50% 延时
    double t = single_pole_delay;
    for (int iter = 0; iter < 20; ++iter) {
        double f = v(t) - target;
        double df = dv(t);
        if (std::abs(df) < 1e-15) break;
        double t_new = t - f / df;
        if (!(t_new > 0.0) || !std::isfinite(t_new)) break;
        if (std::abs(t_new - t) < 1e-6) {
            return t_new/ln2;
        }
        t = t_new;
    }

    // 如果 Newton 不收敛，退回到二分法
    double lo = 0.0;
    double hi = 5.0 * single_pole_delay;
    if (hi <= 0.0) hi = 5.0 * m1;

    double vlo = v(lo) - target;
    double vhi = v(hi) - target;
    if (vlo * vhi > 0.0) {
        // 没有明显的符号变化，退回单极点近似
        return single_pole_delay;
    }

    for (int i = 0; i < 60; ++i) {
        double mid = 0.5 * (lo + hi);
        double vm = v(mid) - target;
        if (std::abs(vm) < 1e-6) {
            return mid / ln2;
        }
        if (vlo * vm < 0.0) {
            hi = mid;
            vhi = vm;
        } else {
            lo = mid;
            vlo = vm;
        }
    }

    return 0.5 * (lo + hi)/ln2;
}


double recursive_calc_m2(int cur_nd_idx, int src_nd_idx,
    const double &downstream_cap_mul_m1, Topology &topo, 
    const std::vector<std::tuple<std::string, Input_info>> &Input,
    double cap_ff_to_ps_factor, double pin_load_unit_factor) {

    // develop without parent_idx, could be updated later
    Node_Info &cur_nd = topo.nodes[cur_nd_idx];
    if (src_nd_idx != -1) cur_nd.backward_flag--;

    if (cur_nd_idx == topo.base_for_special) {
        return 0.0;
    }

    if (cur_nd.backward_flag > 1) {
        cur_nd.subtree_cap_mul_m1 += downstream_cap_mul_m1;
        for (auto &[leaf_nd_idx, visited_flag]: topo.leaf_node_idx){
            if (visited_flag) continue;
            visited_flag = 1;
            recursive_calc_m2(
                leaf_nd_idx, -1, 
                0.0, topo, Input, 
                cap_ff_to_ps_factor, pin_load_unit_factor
            );
        }
        return cur_nd.m2;
    }
    else{
        cur_nd.subtree_cap_mul_m1 += downstream_cap_mul_m1 + cur_nd.ground_cap*cur_nd.elmore_delay; 
        if (cur_nd_idx > topo.base_for_special) {
            cur_nd.subtree_cap_mul_m1 += get<1>(Input[cur_nd_idx - topo.base_for_special - 1]).pin_cap * pin_load_unit_factor * cur_nd.elmore_delay;
        }
        for (const auto &[nbr_idx, res] : cur_nd.neighbors) {
            if (topo.nodes[nbr_idx].backward_flag == 0) continue;   //不重复访问

            cur_nd.backward_flag--;
            double pre_m2 = recursive_calc_m2(
                nbr_idx, cur_nd_idx, 
                cur_nd.subtree_cap_mul_m1, topo, Input, 
                cap_ff_to_ps_factor, pin_load_unit_factor
            );
            cur_nd.m2 = pre_m2 + 2 * res * cur_nd.subtree_cap_mul_m1 * cap_ff_to_ps_factor; 
        }
        return cur_nd.m2;
    }
}

}

std::vector<std::pair<std::string,double>> ComputeElmoreDelays(
    const spef::Net &net,
    const std::string &out_name,
    const std::vector<std::tuple<std::string, Input_info>> &Input,
    Topology &topo,
    double pin_load_unit_factor,
    double cap_ff_to_ps_factor)
{
    // Elmore 延时计算主函数：返回每个输入端口名及其 Elmore 延时
    vector<pair<string,double>> result;

    // calc m1
    for (auto& [leaf_nd_idx, visited_flag]: topo.leaf_node_idx){
        if (visited_flag) continue;
        visited_flag = 1;
        ELMORE::recursive_calc_m1(
            leaf_nd_idx, -1, 0.0, topo, Input, 
            cap_ff_to_ps_factor, pin_load_unit_factor
        );
    }

    // 将所有输入 pin 对应节点的 Elmore 延时取出并返回
    for (const auto &inp : Input) {
        const string &inp_name = get<0>(inp);
        int idx = MapNodeNameToIndex(inp_name, out_name, Input, topo.base_for_special, topo.name_to_idx);
        result.push_back({inp_name, topo.nodes[idx].elmore_delay});
    }

    return result;
}

std::vector<std::tuple<std::string,double,double>> ML_fix(
    const std::string &out_name,
    const std::vector<std::tuple<std::string, Input_info>> &Input,
    Topology &topo,
    double pin_load_unit_factor,
    double cap_ff_to_ps_factor,
    const int &feature_mode,
    bool split_mode,
    FitMode fit_mode)
{
    // ML 修正延时计算主函数：返回每个输入端口名及其 Elmore 延时 和 ML 修正后延时
    vector<tuple<string,double,double>> result;
    int root_idx = topo.base_for_special;
    double* features = new double[feature_mode];   // 定义特征数组

    for (const auto &inp : Input) {
        const std::string &inp_name = std::get<0>(inp);
        int pin_idx = MapNodeNameToIndex(inp_name, out_name, Input, topo.base_for_special, topo.name_to_idx);
        
        // --- 6.1 路径回溯 (通用) ---
        std::vector<int> path_nodes;
        int curr_idx = pin_idx;
        while (curr_idx != -1) { 
            path_nodes.push_back(curr_idx);
            if (curr_idx == root_idx) break; 
            curr_idx = topo.nodes[curr_idx].parent_idx;
        }
        std::reverse(path_nodes.begin(), path_nodes.end()); 
        int path_len = (int)path_nodes.size();

        // --- 6.2 动态特征提取 (根据 CFG_FEATURE_MODE) ---
        int f = 0;

        double path_total_res = 0.0;
        double subtree_elmore_acc = 0.0;
        
        // 只有 34 特征模式才需要详细的向量提取循环
        double vec_res[10] = {0}; 
        double vec_cap[10] = {0}; 
        double vec_stage[10] = {0};
        int vec_idx = 0;
        

        // 遍历路径计算标量 (rho, subtree) 和 向量
        for (int i = 1; i < path_len; ++i) {
            int u = path_nodes[i-1]; 
            int v = path_nodes[i];
            double r = 0.0;
            for(auto& e : topo.nodes[u].neighbors) { if(e.first == v) { r = e.second; break; }}
            
            path_total_res += r;
            
            // 计算 Subtree Elmore
            int next_node = (i + 1 < path_len) ? path_nodes[i+1] : -1;
            double c_down = topo.nodes[v].subtree_cap;
            double c_next = (next_node != -1) ? topo.nodes[next_node].subtree_cap : 0.0;
            double c_side = c_down - c_next;
            subtree_elmore_acc += r * c_side * cap_ff_to_ps_factor;

            // 向量特征提取 (仅 34 模式)
            if(feature_mode == 34) {
                double stage_d = r * c_down * cap_ff_to_ps_factor;
                if (vec_idx < 10) {
                    vec_res[vec_idx] = r; vec_cap[vec_idx] = c_down; vec_stage[vec_idx] = stage_d; vec_idx++;
                }
            }
        }

        // --- 6.3 填充特征数组 ---
        // 1. 标量特征 (4个)
        features[f++] = (double)(path_len - 1); // num_stages
        double rho = (topo.total_resistor > 1e-9) ? (path_total_res / topo.total_resistor) : 0.0;
        features[f++] = rho;                    // rho
        features[f++] = topo.nodes[pin_idx].elmore_delay;            // Elmore_ps
        features[f++] = subtree_elmore_acc;     // subtree_Elmore_ps

        // 2. 向量特征 (30个) - 仅 34 模式
        if(feature_mode == 34) {
            // 若路径长度不足 10 段，剩余部分补 0
            for(int k=0; k<10; ++k) features[f++] = vec_res[k];
            for(int k=0; k<10; ++k) features[f++] = vec_cap[k];
            for(int k=0; k<10; ++k) features[f++] = vec_stage[k];
        }

        // --- 6.4 推理与结果计算 (根据 CFG_SPLIT_MODE 和 CFG_FIT_MODE) ---
        double raw_elmore = topo.nodes[pin_idx].elmore_delay;
        double ml_pred = 0.0;

        // Step A: 获取模型预测值
        if (split_mode){
            // 分桶模式: 检查 Elmore 阈值 (50ps)
            if (raw_elmore < 50.0) {
                ml_pred = score_short(features);
            } else {
                ml_pred = score_long(features);
            }
        }
        else{
            // 统一模式
            ml_pred = score(features);
        }

        // Step B: 计算最终延时
        double final_delay = 0.0;
        switch(fit_mode) {
            case MODE_RATIO:
                // Ratio 模式: 限制范围并乘回去
                if (ml_pred < 0.1) ml_pred = 0.1;
                if (ml_pred > 10.0) ml_pred = 10.0;
                final_delay = raw_elmore * ml_pred;
                break;

            case MODE_DELTA:
                // Delta 模式: 直接相加
                final_delay = raw_elmore + ml_pred;
                if (final_delay < 0.0) final_delay = 0.0; // 物理约束
                break;

            case MODE_LINEAR:
                // Linear 模式: 直接替换
                final_delay = ml_pred;
                if (final_delay < 0.0) final_delay = 0.0;
                break;

            default:
                cerr << "Error: Unknown FitMode!" << endl;
                exit(1);
        }

        result.push_back(std::make_tuple(inp_name, final_delay, raw_elmore));
    }

    delete[] features;
    return result;
}


std::vector<std::pair<std::string,double>> ComputeElmoreDelays_dev(
    const spef::Net &net,
    const std::string &out_name,
    const std::vector<std::tuple<std::string, Input_info>> &Input,
    Topology &topo,
    double pin_load_unit_factor,
    double cap_ff_to_ps_factor)
{
    // Elmore 延时计算主函数：返回每个输入端口名及其 Elmore 延时
    vector<pair<string,double>> result;

    // 将所有输入 pin 对应节点的 Elmore 延时取出并返回
    for (const auto &inp : Input) {
        const string &inp_name = get<0>(inp);
        int idx = MapNodeNameToIndex(inp_name, out_name, Input, topo.base_for_special, topo.name_to_idx);
        // 重置 backward_flag 和leaf_node_idx的visited_flag以便计算new m1
        for (auto &node : topo.nodes) {
            node.backward_flag = static_cast<int>(node.neighbors.size());
        }
        for (auto &pair : topo.leaf_node_idx) {
            pair.second = 0;
        }
        ELMORE::propagate_voltage(idx, topo, Input, cap_ff_to_ps_factor, pin_load_unit_factor);
        ELMORE::recursive_calc_m1_revised(
            idx, -1, 0.0, topo, Input, 
            cap_ff_to_ps_factor, pin_load_unit_factor
        );
        result.push_back({inp_name, topo.nodes[idx].elmore_delay_new / ln2});
    }

    return result;
}
// {
//         // Elmore 延时计算主函数：返回每个输入端口名及其 Elmore 延时
//     vector<pair<string,double>> result;

//     // calc m1
//     for (auto& [leaf_nd_idx, visited_flag]: topo.leaf_node_idx){
//         if (visited_flag) continue;
//         visited_flag = 1;
//         ELMORE::recursive_calc_m1(
//             leaf_nd_idx, -1, 0.0, topo, Input, 
//             cap_ff_to_ps_factor, pin_load_unit_factor
//         );
//     }

//     // 重置 backward_flag 和leaf_node_idx的visited_flag以便计算 m2
//     for (auto &node : topo.nodes) {
//         node.backward_flag = static_cast<int>(node.neighbors.size());
//     }
//     for (auto &pair : topo.leaf_node_idx) {
//         pair.second = 0;
//     }

//     // calc m2
//     for (auto& [leaf_nd_idx, visited_flag]: topo.leaf_node_idx){
//         if (visited_flag) continue;
//         visited_flag = 1;
//         D2M::recursive_calc_m2(
//             leaf_nd_idx, -1, 0.0, topo, Input, 
//             cap_ff_to_ps_factor, pin_load_unit_factor
//         );
//     }

//     // 将所有输入 pin 对应节点的 Elmore 延时取出并返回
//     for (const auto &inp : Input) {
//         const string &inp_name = get<0>(inp);
//         int idx = MapNodeNameToIndex(inp_name, out_name, Input, topo.base_for_special, topo.name_to_idx);
//         Node_Info &node = topo.nodes[idx];
//         double m1 = node.elmore_delay;
//         double m2 = node.m2;
//         // double delay_ps = D2M::DelayFromMoments(m1, m2);
//         // result.push_back({inp_name, delay_ps}); 
//         result.push_back({inp_name, m1*m1/sqrt(m2)});
//     }

//     return result;
// }
