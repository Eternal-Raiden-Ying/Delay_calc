#include "Tree/inc/calc_delay.h"
#include <iostream>
#include <functional>
#include <cmath>
#include "Tree/resc/delta_true_34_combined.h"

# define ln2 (double)(0.693147181)
# define ln10 (double)(2.302585093)

using namespace std;

namespace ELMORE{

double node_all_res(int nd_idx, Topology &topo){
    double total_res = 0.0;
    Node_Info &cur_nd = topo.nodes[nd_idx];
    for (const auto &[nbr_idx, res] : cur_nd.neighbors) {
        if (topo.nodes[nbr_idx].ground_cap == 0 && topo.nodes[nbr_idx].neighbors.size() == 1) continue;
        total_res += res;
    }
    return total_res;
}

double lenth_ratio(const Topology &topo, int pin_idx) {
    int cur_nd_idx = pin_idx;
    int pin_len = 0;
    int max_len = 0;
    while(true){
        if (topo.nodes[cur_nd_idx].parent_idx == -1) break;
        pin_len += 1;
        cur_nd_idx = topo.nodes[cur_nd_idx].parent_idx;
    }

    for (auto& [leaf_idx, visited]: topo.leaf_node_idx){
        int temp = 0;
        cur_nd_idx = leaf_idx;
        while(true){
            if(topo.nodes[cur_nd_idx].parent_idx == -1) break;
            temp +=1;
            cur_nd_idx = topo.nodes[cur_nd_idx].parent_idx;
        }
        max_len = max(max_len, temp);
    }
    return (double)pin_len / (double)max_len;
}

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
        cur_nd.subtree_cap += downstream_cap;
        if (cur_nd_idx > topo.base_for_special) {
            cur_nd.subtree_cap += get<1>(Input[cur_nd_idx - topo.base_for_special - 1]).pin_cap * pin_load_unit_factor;
        }
        for (const auto &[nbr_idx, res] : cur_nd.neighbors) {
            if (topo.nodes[nbr_idx].backward_flag == 0) continue;   //不重复访问

            cur_nd.parent_idx = nbr_idx;
            // cur_nd.subtree_cap += cur_nd.ground_cap;

            // cur_nd.subtree_cap += (1 / double(topo.nodes[cur_nd.parent_idx].neighbors.size())*topo.nodes[cur_nd.parent_idx].ground_cap 
            //                         + 1 / double(cur_nd.neighbors.size()) * cur_nd.ground_cap);
            
            double cur_nd_res = node_all_res(cur_nd_idx, topo);
            double parent_nd_res = node_all_res(cur_nd.parent_idx, topo);
            cur_nd.subtree_cap += (
                (res+1e-6)/(cur_nd_res+1e-6)*cur_nd.ground_cap 
                + (res+1e-6)/(parent_nd_res+1e-6)*topo.nodes[cur_nd.parent_idx].ground_cap
            );
            
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

    // if (cur_nd.elmore_delay < pin_m1){
    //     cur_nd.voltage = 1.0;
    // }
    // else{
    //     cur_nd.voltage = 1 - exp(-pin_m1 / cur_nd.elmore_delay * 2 * ln10);
    // }
    cur_nd.voltage = 1 - exp(-pin_m1 / cur_nd.elmore_delay * 2 * ln10);


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

    // if (cur_nd.elmore_delay < pin_m1){
    //     cur_nd.voltage = 1.0;
    // }
    // else{
    //     cur_nd.voltage = 1 - exp(-pin_m1 / cur_nd.elmore_delay * 2 * ln10);
    // }
    cur_nd.voltage = 1 - exp(-pin_m1 / cur_nd.elmore_delay * 2 * ln10);

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
    
    Node_Info &cur_nd = topo.nodes[pin_nd_idx];
    topo.nodes[pin_nd_idx].voltage = 0.99;
    
    if (cur_nd.neighbors.size() > 1 ) {
        for (auto &[nbr_idx, res]: cur_nd.neighbors){
            if (nbr_idx == cur_nd.parent_idx) continue;
            forward_propagate_voltage(pin_nd_idx, nbr_idx, topo, cur_nd.elmore_delay, Input, cap_ff_to_ps_factor, pin_load_unit_factor);
        }
    }
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
        cur_nd.eff_subtree_cap += eff_downstream_cap; // 加上自身接地电容, 仅在只有一个未访问邻居时(父节点)加上自身ground_cap
        if (cur_nd_idx > topo.base_for_special) {
            cur_nd.eff_subtree_cap += get<1>(Input[cur_nd_idx - topo.base_for_special - 1]).pin_cap * pin_load_unit_factor * cur_nd.voltage;
        }
        for (const auto &[nbr_idx, res] : cur_nd.neighbors) {
            if (topo.nodes[nbr_idx].backward_flag == 0) continue;   //不重复访问

            cur_nd.parent_idx = nbr_idx;

            // cur_nd.eff_subtree_cap += cur_nd.ground_cap * cur_nd.voltage;
            double cur_nd_res = node_all_res(cur_nd_idx, topo);
            double parent_nd_res = node_all_res(cur_nd.parent_idx, topo);
            cur_nd.eff_subtree_cap += (
                (res+1e-6)/(cur_nd_res+1e-6)*cur_nd.ground_cap * cur_nd.voltage 
                + (res+1e-6)/(parent_nd_res+1e-6)*topo.nodes[cur_nd.parent_idx].ground_cap*cur_nd.voltage
            );
            
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
    FitMode fit_mode,
    const std::vector<std::pair<std::string,double>> &elmore_adv_ps,
    std::vector<MLFeatureDumpRow>* dump_rows)
{
    std::vector<std::tuple<std::string,double,double>> result;
    const int root_idx = topo.base_for_special;

    double* features = new double[feature_mode];

    const int N = (int)topo.nodes.size();
    if (root_idx < 0 || root_idx >= N) {
        delete[] features;
        return result;
    }

    // ---------- build adv elmore map ----------
    std::unordered_map<std::string,double> adv_map;
    adv_map.reserve(elmore_adv_ps.size() * 2);
    for (const auto &p : elmore_adv_ps) adv_map[p.first] = p.second;

    // ============================================================
    // PURE 电容体系：
    //   node_total_cap[u] = ground_cap[u] + sum(pin_loads at u)
    //   subtree_cap_pure[u] = node_total_cap[u] + sum(subtree_cap_pure[child])
    // ============================================================
    std::vector<double> node_total_cap(N, 0.0);
    for (int i = 0; i < N; ++i) node_total_cap[i] = topo.nodes[i].ground_cap;

    // 加入所有 sink pin 的 pin_cap（全网同时存在，应该一次性加进去）
    for (const auto &inp : Input) {
        const std::string &inp_name = std::get<0>(inp);
        const Input_info  &info     = std::get<1>(inp);
        int pin_idx = -1;
        try {
            pin_idx = MapNodeNameToIndex(inp_name, out_name, Input, topo.base_for_special, topo.name_to_idx);
        } catch (...) {
            continue;
        }
        if (pin_idx < 0 || pin_idx >= N) continue;
        node_total_cap[pin_idx] += info.pin_cap * pin_load_unit_factor;
    }

    // children list from parent_idx
    std::vector<std::vector<int>> children(N);
    for (int v = 0; v < N; ++v) {
        int p = topo.nodes[v].parent_idx;
        if (p >= 0 && p < N) children[p].push_back(v);
    }

    // postorder from root (only reachable nodes)
    std::vector<int> order;
    order.reserve(N);
    std::vector<int> st;
    st.push_back(root_idx);
    while (!st.empty()) {
        int u = st.back(); st.pop_back();
        order.push_back(u);
        for (int ch : children[u]) st.push_back(ch);
    }
    std::reverse(order.begin(), order.end());

    std::vector<double> subtree_cap_pure(N, 0.0);
    for (int u : order) {
        double s = node_total_cap[u];
        for (int ch : children[u]) s += subtree_cap_pure[ch];
        subtree_cap_pure[u] = s;
    }

    if (dump_rows) dump_rows->reserve(dump_rows->size() + Input.size());

    // ============================================================
    // per-input: build features (all PURE), raw_elmore from adv_map,
    // subtree_elmore = elmore - path_only (path_only uses PURE suffix caps)
    // 10对向量特征也用 PURE subtree_cap_pure[v]
    // ============================================================
    for (const auto &inp : Input) {
        const std::string &inp_name = std::get<0>(inp);
        int pin_idx = MapNodeNameToIndex(inp_name, out_name, Input, topo.base_for_special, topo.name_to_idx);

        // --- path backtrace ---
        std::vector<int> path_nodes;
        int curr = pin_idx;
        while (curr != -1) {
            path_nodes.push_back(curr);
            if (curr == root_idx) break;
            curr = topo.nodes[curr].parent_idx;
        }
        std::reverse(path_nodes.begin(), path_nodes.end());
        const int path_len = (int)path_nodes.size();
        if (path_len < 2) continue;

        // --- PURE path-only suffix caps ---
        // raw elmore (advanced) from ComputeElmoreDelays_advanced output
        double elmore_adv_ps = 0.0;
        auto it = adv_map.find(inp_name);
        if (it != adv_map.end()) elmore_adv_ps = it->second;
        else elmore_adv_ps = topo.nodes[pin_idx].elmore_delay; // fallback

        // --- PURE path-only suffix caps (主路径后缀和，只含主路径节点自己的 node_total_cap) ---
        std::vector<double> suf_path(path_len, 0.0);
        suf_path[path_len - 1] = node_total_cap[path_nodes[path_len - 1]];
        for (int i = path_len - 2; i >= 0; --i) {
            suf_path[i] = node_total_cap[path_nodes[i]] + suf_path[i + 1];
        }

        double path_total_res    = 0.0;
        double path_only_elmore  = 0.0;   // PURE: only main-path caps
        double pure_total_elmore = 0.0;   // PURE: total elmore using subtree_cap_pure (含侧枝)

        double vec_res[10]   = {0};
        double vec_cap[10]   = {0};
        double vec_stage[10] = {0};
        int vec_idx = 0;

        for (int i = 1; i < path_len; ++i) {
            int u = path_nodes[i - 1];
            int v = path_nodes[i];

            double r = 0.0;
            for (auto &e : topo.nodes[u].neighbors) {
                if (e.first == v) { r = e.second; break; }
            }

            path_total_res += r;

            // PURE path-only elmore: 用主路径后缀电容（不含侧枝）
            path_only_elmore += r * suf_path[i] * cap_ff_to_ps_factor;

            // PURE total elmore: 用 pure 子树电容 subtree_cap_pure[v]（含侧枝）
            const double c_down_pure = subtree_cap_pure[v];
            pure_total_elmore += r * c_down_pure * cap_ff_to_ps_factor;

            // 10对向量特征也用 PURE
            if (feature_mode == 34 && vec_idx < 10) {
                vec_res[vec_idx]   = r;
                vec_cap[vec_idx]   = c_down_pure;
                vec_stage[vec_idx] = r * c_down_pure * cap_ff_to_ps_factor;
                vec_idx++;
            }
        }

        // ★ subtree-elmore 改成纯电容体系：pure_total - pure_path_only
        double subtree_elmore_ps = pure_total_elmore - path_only_elmore;


        // --- fill feature array ---
        for (int i = 0; i < feature_mode; ++i) features[i] = 0.0;
        int f = 0;
        features[f++] = (double)(path_len - 1); // num_stages
        double rho = (topo.total_resistor > 1e-9) ? (path_total_res / topo.total_resistor) : 0.0;
        features[f++] = rho;
        features[f++] = elmore_adv_ps;
        features[f++] = subtree_elmore_ps;

        if (feature_mode == 34) {
            for (int k = 0; k < 10; ++k) features[f++] = vec_res[k];
            for (int k = 0; k < 10; ++k) features[f++] = vec_cap[k];
            for (int k = 0; k < 10; ++k) features[f++] = vec_stage[k];
        }

        // --- inference uses raw_elmore = elmore_ps (advanced) ---
        double raw_elmore = elmore_adv_ps;
        double ml_pred = 0.0;
        std::string bucket = "all";

        // split mode supported only
        if (raw_elmore < 50.0) {
            bucket = "short";
            ml_pred = score_short(features);
        } else {
            bucket = "long";
            ml_pred = score_long(features);
        }

        double final_delay = 0.0;
        switch (fit_mode) {
            case MODE_RATIO:
                if (ml_pred < 0.1)  ml_pred = 0.1;
                if (ml_pred > 10.0) ml_pred = 10.0;
                final_delay = raw_elmore * ml_pred;
                break;
            case MODE_DELTA:
                final_delay = raw_elmore + ml_pred;
                if (final_delay < 0.0) final_delay = 0.0;
                break;
            case MODE_LINEAR:
                final_delay = ml_pred;
                if (final_delay < 0.0) final_delay = 0.0;
                break;
            default:
                std::cerr << "Error: Unknown FitMode!" << std::endl;
                std::exit(1);
        }

        if (dump_rows) {
            MLFeatureDumpRow row;
            row.inp_name = inp_name;
            row.feats.assign(features, features + feature_mode);
            row.ml_pred = ml_pred;
            row.final_delay_ps = final_delay;
            row.bucket = bucket;
            dump_rows->push_back(std::move(row));
        }

        result.push_back(std::make_tuple(inp_name, final_delay, raw_elmore));
    }

    delete[] features;
    return result;
}

std::vector<std::pair<std::string,double>> ComputeElmoreDelays_advanced(
    const spef::Net &net,
    const std::string &out_name,
    const std::vector<std::tuple<std::string, Input_info>> &Input,
    Topology &topo,
    double pin_load_unit_factor,
    double cap_ff_to_ps_factor)
{
    // Elmore 延时计算主函数：返回每个输入端口名及其 Elmore 延时
    vector<pair<string,double>> result;

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
        // 重置 backward_flag 和leaf_node_idx的visited_flag以便计算new m1
        for (auto &node : topo.nodes) {
            node.backward_flag = static_cast<int>(node.neighbors.size());
            node.voltage = 0.0;
            node.eff_subtree_cap = 0.0;
            node.elmore_delay_new = 0.0;
        }
        for (auto &pair : topo.leaf_node_idx) {
            pair.second = 0;
        }
        ELMORE::propagate_voltage(idx, topo, Input, cap_ff_to_ps_factor, pin_load_unit_factor);
        ELMORE::recursive_calc_m1_revised(
            idx, -1, 0.0, topo, Input, 
            cap_ff_to_ps_factor, pin_load_unit_factor
        );

        result.push_back({inp_name, topo.nodes[idx].elmore_delay_new});
    }
    return result;
}

// std::vector<std::pair<std::string,double>> ComputeElmoreDelays_dev(
//     const spef::Net &net,
//     const std::string &out_name,
//     const std::vector<std::tuple<std::string, Input_info>> &Input,
//     Topology &topo,
//     double pin_load_unit_factor,
//     double cap_ff_to_ps_factor)
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
//         double delay_ps = D2M::DelayFromMoments(m1, m2);
//         result.push_back({inp_name, delay_ps}); 
//         // result.push_back({inp_name, m1*m1/sqrt(m2)/ln2});
//     }

//     return result;
// }
