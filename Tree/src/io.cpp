#include "Tree/inc/io.h"
#include <iostream>
#include <fstream>
#include <iomanip>
#include <tuple>
#include <unordered_map>
#include <vector>

using namespace std;

// netlist_info 定义在 Read_train/Read.cpp 中，这里只需 extern 声明即可
extern unordered_map<string, vector<Input_info>> netlist_info;

int load_netlist_and_delay(const std::string &base_path, int spef_num)
{
    string netlist_file = base_path + "/netlist_info.txt";
    if (Read_netlist_file(netlist_file))
    {
        cerr << "读取 netlist_info 失败: " << netlist_file << endl;
        return 1;
    }

    // 本来不需要读取，避免未给出而意外报错
    // string delay_file = base_path + "/delay_data/Group" + to_string(spef_num) + ".txt";
    // if (Read_delay_file(delay_file))
    // {
    //     cerr << "读取 delay 文件失败: " << delay_file << endl;
    //     return 2;
    // }
    return 0;
}

int load_spef(const std::string &base_path, int spef_num, spef::Spef &parser)
{
    string spef_file = base_path + "/SPEF/Group" + to_string(spef_num) + ".spef";
    if (!parser.read(spef_file))
    {
        cerr << "读取 spef 文件失败: " << spef_file
             << " error: " << (*parser.error) << endl;
        return 1;
    }
    return 0;
}

stringstream& write2log(stringstream &ss, const std::vector<std::pair<std::string, double>> &res, 
    const std::vector<std::tuple<std::string, Input_info>> &Input, const spef::Net &net, int precision)
{
    unordered_map<string, double> res_map;
    for (auto &t : res) {
        res_map[t.first] = t.second;
    }

    // 输出日志
    ss << "[Compare Net: " << net.name << "]" << endl;
    ss << "Name | Golden(ps) | Calc(ps) | Error | Type" << endl;
    ss.setf(std::ios::fixed);
    ss.precision(precision);

    for (const auto &inp : Input) {
        const string &inp_name = get<0>(inp);
        const auto &info = get<1>(inp);
        double std_ps = info.delay * 1000.0; 
        
        auto it = res_map.find(inp_name);
        if (it != res_map.end()) {
            double calc_ps = it->second;
            double final_error = 0.0;
            string err_type = "";

            if (std_ps < 50.0) {
                final_error = std::abs(calc_ps - std_ps);
                err_type = "Abs(ps)";
            } else {
                if (std_ps != 0.0) final_error = std::abs((calc_ps - std_ps) / std_ps);
                err_type = "Rel(%)";
            }
            
            ss << inp_name << " | " 
                << std_ps << " | " 
                << calc_ps << " | " 
                << final_error << " | " 
                << err_type << endl;
        } else {
            ss << inp_name << " | " << std_ps << " | " << "N/A" << " | N/A | N/A | Missing" << endl;
        }
    }
    ss.unsetf(std::ios::fixed);
    return ss;
}

stringstream& write2log(stringstream &ss, const std::vector<std::tuple<std::string, double, double>> &res, 
    const std::vector<std::tuple<std::string, Input_info>> &Input, const spef::Net &net, int precision)
{
    unordered_map<string, pair<double,double>> res_map;
    for (auto &t : res) {
        res_map[get<0>(t)] = {get<1>(t), get<2>(t)};
    }

    // 输出日志
    ss << "[Compare Net: " << net.name << "]" << endl;
    ss << "Name | Golden(ps) | Calc_Advanced(ps) | Calc_Base(ps) | Error | Type" << endl;
    ss.setf(std::ios::fixed);
    ss.precision(precision);

    for (const auto &inp : Input) {
        const string &inp_name = get<0>(inp);
        const auto &info = get<1>(inp);
        double std_ps = info.delay * 1000.0; 
        
        auto it = res_map.find(inp_name);
        if (it != res_map.end()) {
            double calc_ps = it->second.first;  // 修正后 (ML Final)
            double calc_before = it->second.second; // 修正前 (Raw Elmore)
            
            double final_error = 0.0;
            string err_type = "";

            if (std_ps < 50.0) {
                final_error = std::abs(calc_ps - std_ps);
                err_type = "Abs(ps)";
            } else {
                if (std_ps != 0.0) final_error = std::abs((calc_ps - std_ps) / std_ps);
                err_type = "Rel(%)";
            }
            
            ss << inp_name << " | " 
                << std_ps << " | " 
                << calc_ps << " | " 
                << calc_before << " | " 
                << final_error << " | " 
                << err_type << endl;
        } else {
            ss << inp_name << " | " << std_ps << " | " << "N/A" << " | N/A | N/A | Missing" << endl;
        }
    }
    ss.unsetf(std::ios::fixed);
    return ss;
}

stringstream& write_delay(stringstream &ss, const std::vector<std::pair<std::string, double>> &res, 
    const std::vector<std::tuple<std::string, Input_info>> &Input, const string &out_real_name, int precision){
    
    ss.setf(std::ios::fixed);
    ss.precision(precision);
    for (auto& inp: Input){
        const string &inp_name = get<0>(inp);
        const auto &info = get<1>(inp);
        double calc_ps = 0.0;
        auto it = find_if(res.begin(), res.end(), [&](const pair<string,double> &p){
            return p.first == inp_name;
        });
        if (it != res.end()){
            calc_ps = it->second;
        }
        ss << out_real_name << " " << info.name << " " <<  calc_ps << endl;
    }
    ss.unsetf(std::ios::fixed);
    return ss;
}

stringstream& write_delay(stringstream &ss, const std::vector<std::tuple<std::string, double, double>> &res, 
    const std::vector<std::tuple<std::string, Input_info>> &Input, const string &out_real_name, int precision){

    ss.setf(std::ios::fixed);
    ss.precision(precision);
    for (auto& inp: Input){
        const string &inp_name = get<0>(inp);
        const auto &info = get<1>(inp);
        double calc_ps = 0.0;
        auto it = find_if(res.begin(), res.end(), [&](const tuple<string,double, double> &p){
            return get<0>(p) == inp_name;
        });
        if (it != res.end()){
            calc_ps = get<1>(*it);
        }
        ss << out_real_name << " " << info.name << " " <<  calc_ps << endl;
    }
    ss.unsetf(std::ios::fixed);
    return ss;
}
