#define __DEVELOP__                 // develop version, embed path parameters into code

#define CFG_FEATURE_MODE  34
// 2. 拟合模式: 1 (Ratio: kx), 2 (Delta: x+b), 3 (Linear: kx+b)
#define CFG_FIT_MODE      2
// 3. 分桶模式: 1 (True: 分开推理), 0 (False: 统一推理)
#define CFG_SPLIT_MODE    1

#include <getopt.h>
#include <iostream>
#include <unordered_map>
#include <parser-spef.hpp>
#include <list>
#include <string>
#include <cstring>
#include "Read_train/Read.h"
#include <fstream>
#include <thread>
#include <future>
#include <stdexcept>
#include <queue>
#include <functional>
#include <sstream> 
#include <omp.h>   
#include <cmath>   
#include <algorithm> 
#include <tuple>  
#include <chrono> 
#include <filesystem>
#include "Tree/inc/build_tree.h"
#include "Tree/inc/calc_delay.h"
#include "Tree/inc/analysis.h"
#include "Tree/inc/io.h"


#ifdef __DEVELOP__
bool default_logging_enabled = false;
bool default_analyze_enabled = false;
std::string default_file_path = "D:\\Documents\\Coding\\Projects\\Delay_calc\\Delay_calc\\Data";
int default_spef_num = 0;
std::string default_feature_path = "D:\\Documents\\Coding\\Projects\\Delay_calc\\Delay_calc\\features";
#endif


using namespace std;
namespace fs = std::filesystem;

extern unordered_map<string, vector<Input_info>> netlist_info;


string NormalizeName(string name) {
    for (char &c : name) {
        if (c == '[' || c == ']') c = '_';
    }
    return name;
}

int main(int argc, char **argv)
{   
    int c;
    int spef_num;
    char *file_path = nullptr;
    char *feature_path = nullptr;
    bool use_ml = false; // default: output advanced Elmore only; enable with --use_ml
    bool logging_enabled = false;
    bool analyze_enabled = false;
    int option_index = 0;
    static struct option long_options[] = {
        {"file_path",       required_argument, 0, 'f'},
        {"spef_num",        required_argument, 0, 'm'},
        {"feature_path",    required_argument, 0, 't'},
        {"use_ml",          no_argument,       0, 'u'},  // enable ML, output ML-fixed delay
        {"no_ml",           no_argument,       0, 'U'},  // disable ML, output advanced Elmore
        {0, 0, 0, 0}
    };

    // set default argument during development
    #ifdef __DEVELOP__
    file_path = (char *)malloc((strlen(default_file_path.c_str()) + 1) * sizeof(char));
    strcpy(file_path, default_file_path.c_str());
    spef_num = default_spef_num;
    feature_path = (char *)malloc((strlen(default_feature_path.c_str()) + 1) * sizeof(char));
    strcpy(feature_path, default_feature_path.c_str());
    logging_enabled = default_logging_enabled;
    analyze_enabled = default_analyze_enabled;
    #endif

    while ((c = getopt_long(argc, argv, "f:m:t:uU", long_options, &option_index)))
    {
        if (c == -1)
            break;
        switch (c)
        {
        case 'f':
            file_path = (char *)malloc((strlen(optarg) + 1) * sizeof(char));
            strcpy(file_path, optarg);
            break;
        case 'm':
            spef_num = atoi(optarg);
            break;
        case 't':
            feature_path = (char *)malloc((strlen(optarg) + 1) * sizeof(char));
            strcpy(feature_path, optarg);
            break;
        case 'u':
            use_ml = true;
            break;
        case 'U':
            use_ml = false;
            break;
        default:
            printf("?? getopt returned character code 0%o ??\n", c);
        }
    }

    if (!feature_path) {
        std::cerr << "feature_path is not set, please use -t or define default_feature_path" << std::endl;
        return 1;
    }

    fs::path feature_dir(feature_path);
    std::error_code ec;
    if (!fs::exists(feature_dir)) {
        if (!fs::create_directories(feature_dir, ec)) {
            std::cerr << "Failed to create feature directory: "
                      << feature_dir << " : " << ec.message() << std::endl;
            return 1;
        }
    }

    string output_filename = string(feature_path) + "\\delay" + to_string(spef_num) + ".txt";
    std::ofstream fout(output_filename, std::ios::out | std::ios::trunc);
    if (!fout.is_open()) {
        std::cerr << "Failed to open output file: " << output_filename << std::endl;
        return 1;
    }

    std::ofstream log_file;
    std::ostream *log = &std::cout;
    if (logging_enabled) {
        // =========================================================================
        // 动态生成日志文件名
        // 格式: fitmode_splitmode_featuremode_groupid.log
        // 例如: ratio_true_34_0.log
        // =========================================================================
        string fit_str = "unknown";
        int config_fit_mode = CFG_FIT_MODE;
        switch(config_fit_mode) {
            case MODE_RATIO:
                fit_str = "ratio";
                break;
            case MODE_DELTA:
                fit_str = "delta";
                break;
            case MODE_LINEAR:
                fit_str = "linear";
                break;
            default:
                fit_str = "unknown";
                break;
        }

        string split_str = (CFG_SPLIT_MODE == 1) ? "true" : "false";
        string feat_str = to_string(CFG_FEATURE_MODE);
        string group_str = to_string(spef_num); // 使用解析后的 spef_num

        string log_name = fit_str + "_" + split_str + "_" + feat_str + "_" + group_str + ".log";
        std::cout << "Logging enabled, write to " << log_name << std::endl;

        log_file.open(log_name, std::ios::out | std::ios::trunc);
        
        if (log_file.is_open()) {
            log = &log_file;
            // 在日志开头打印配置信息，方便核对
            (*log) << "[Config] Log File: " << log_name << endl;
            (*log) << "[Config] Feature: " << feat_str << ", Split: " << split_str << ", Fit: " << fit_str << endl;
        } else {
            std::cerr << "无法打开日志文件: " << log_name << std::endl;
            if (file_path) free(file_path);
            return 1;
        }
    }
    
    std::ofstream analyze_file;
    std::ostream *analyze_log = &std::cout;
    if (analyze_enabled) {
        string analyze_file_name = "analyze_data_group_" + to_string(spef_num) + ".csv";
        std::cout << "Analyze enabled, write to " << analyze_file_name << std::endl;

        analyze_file.open(analyze_file_name, std::ios::out | std::ios::trunc);
        
        if (analyze_file.is_open()) {
            analyze_log = &analyze_file;
            // 在日志开头打印配置信息，方便核对
            (*analyze_log) << "Name,Golden(ps),Calc(ps),AbsError,RelError,LengthRatio,input_pin_num\n";
        } else {
            std::cerr << "无法打开日志文件: " << analyze_file_name << std::endl;
            if (file_path) free(file_path);
            return 1;
        }
    }

    // === load 部分计时开始 ===
    using steady_clock = std::chrono::steady_clock;
    auto t_load_start = steady_clock::now();

    // =========================================================================
    // 优化：并行读取 netlist_info 和 SPEF 文件
    // 性能提升：20-40%（两个 IO 操作可以并行进行）
    // =========================================================================
    spef::Spef parser;
    std::future<int> netlist_result;
    std::future<int> spef_result;
    
    // 并行启动两个异步任务
    netlist_result = std::async(std::launch::async, [&file_path, &spef_num]() {
        return load_netlist_and_delay(file_path, spef_num);
    });
    
    spef_result = std::async(std::launch::async, [&file_path, &spef_num, &parser]() {
        return load_spef(file_path, spef_num, parser);
    });
    
    // 等待两个任务完成并检查错误
    int netlist_status = netlist_result.get();
    int spef_status = spef_result.get();
    
    if (netlist_status != 0) {
        std::cerr << "[ERROR] Failed to load netlist and delay files" << std::endl;
        exit(1);
    }
    
    if (spef_status != 0) {
        std::cerr << "[ERROR] Failed to load SPEF file" << std::endl;
        exit(1);
    }

    auto t_load_end = steady_clock::now();
    auto load_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t_load_end - t_load_start).count();
    std::cout << "[TIME] load phase: " << load_ms << " ms" << std::endl;
    // === load 部分计时结束 ===

    // === for 循环整体计算部分计时开始 ===
    auto t_compute_start = steady_clock::now();
    string target_net_name = "*1540529";
    /* 一个SPEF文件含有多个net，针对每一个net做处理 */
    #pragma omp parallel for schedule(dynamic)
    for (auto &net : parser.nets)
    {
        /* 一个net只有一个output，有多个input */
        string out_name;            // spef文件中output的名字，是一个ID，例如 *1681713:Q，用于建立net的数据结构
        string OUT_REAL_NAME;       // 最后用来写入文件真实的名字 CNU17/R4_reg_18_
        /* 一条path包含多个从output->input的路径，也就是一个net包含多条path */
        /* Input变量第一个string元素是一个input pin的 ID */
        /* Input_info记录了这个input的real name和引脚电容、以及以这个input结束的path的延时值 */
        vector<tuple<string, Input_info>> Input;
        vector<Input_info> paths;   // 暂时用来记录查询到的input信息
        bool flag = 0;              //跳过无效net
        stringstream ss, sout, s_analyze;

        for (auto &connection : net.connections)
        {
            if (connection.direction == spef::ConnectionDirection::OUTPUT)
            {
                out_name = connection.name;
                int index = connection.name.rfind(':');                 // 找到‘:’的位置
                int ID = stoi(connection.name.substr(1, index - 1));    // 提取出ID号 name格式参考 *1681713:Q
                OUT_REAL_NAME = NormalizeName(parser.name_map[ID] + '/' + connection.name.substr(index + 1, -1));      // 拼接出真实名字, name_map： ID->real name(string)

                if (netlist_info.find(OUT_REAL_NAME) == netlist_info.end())
                    flag = 1;
                else
                    paths = netlist_info.find(OUT_REAL_NAME)->second;
            }
        }
        
        if(flag) continue; // 跳过这个net，继续下一个net
        
        for (auto &connection : net.connections)
        {
            if (connection.direction == spef::ConnectionDirection::INPUT)
            {
                // 将connection.name翻译为真实名，然后在info中寻找它的pincap
                int index = connection.name.rfind(':');
                int ID = stoi(connection.name.substr(1, index - 1));
                string Input_Name = NormalizeName(parser.name_map[ID] + '/' + connection.name.substr(index + 1, -1));
                
                // Input中包含了这个net所有的path的延时、input pin的id以及real name，还有引脚电容
                for (auto &p : paths)
                {
                    if (Input_Name == p.name)
                    {
                        Input.push_back(make_pair(connection.name, p));
                    }
                }
            }
        }
        
        Topology topo = BuildTopologyFromRess(net, out_name, Input);

        FillCapsFromNet(net, topo, out_name, Input);

        // 单位设定：caps 为 fF、res 为 ohm，输出 ps。e3
        // ohm * fF -> seconds: 1e-15；转成 ps 乘以 1e12 => 综合因子 1e-3。
        double pin_load_unit_factor = 1e3;    // 若 Input.pin_cap 单位与 caps 不同，可在此调整为把其换算到 fF
        double cap_ff_to_ps_factor = 1e-3;    // R(ohm)*C(fF) 转 ps 的系数
        // auto base_res = ComputeElmoreDelays(net, out_name, Input, topo, pin_load_unit_factor, cap_ff_to_ps_factor);
        auto base_res = ComputeElmoreDelays_advanced(net, out_name, Input, topo, pin_load_unit_factor, cap_ff_to_ps_factor);

        // Interface: choose whether to apply ML correction.
        // - use_ml == false: output advanced Elmore (base_res)
        // - use_ml == true : output ML-fixed delay (ml_res)
        if (use_ml) {
            auto ml_res = ML_fix(
                out_name, Input, topo,
                pin_load_unit_factor, cap_ff_to_ps_factor,
                CFG_FEATURE_MODE,
                CFG_SPLIT_MODE,
                static_cast<FitMode>(CFG_FIT_MODE),
                base_res
            );

            if (logging_enabled) {
                write2log(ss, ml_res, Input, net, 4);
            }
            if (analyze_enabled) {
                // write2csv expects vector<pair<string,double>>
                std::vector<std::pair<std::string,double>> ml_pairs;
                ml_pairs.reserve(ml_res.size());
                for (const auto &t : ml_res) {
                    ml_pairs.emplace_back(std::get<0>(t), std::get<1>(t));
                }
                write2csv(s_analyze, ml_pairs, topo, Input, net, 4);
            }
            write_delay(sout, ml_res, Input, OUT_REAL_NAME, 4);

        } else {
            if (logging_enabled) {
                write2log(ss, base_res, Input, net, 4);
            }
            if (analyze_enabled) {
                write2csv(s_analyze, base_res, topo, Input, net, 4);
            }
            write_delay(sout, base_res, Input, OUT_REAL_NAME, 4);
        }

        #pragma omp critical 
        {
            fout << sout.str();
            if (analyze_enabled) (*analyze_log) << s_analyze.str();
            if (logging_enabled) (*log) << ss.str();
        }

        // 导出后可以使用Gephi可视化RC树结构
        // if(net.name == target_net_name){
        //     ExportNetToGephiCsv(net, out_name, Input, topo, "rc_nodes.csv", "rc_edges.csv");
        // }

    }

    auto t_compute_end = steady_clock::now();
    auto compute_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t_compute_end - t_compute_start).count();
    std::cout << "[TIME] compute phase (for nets loop): " << compute_ms << " ms" << std::endl;

    /* 释放内存 */
    if (file_path)
    {
        free(file_path);
        file_path = nullptr;
    }
    return 0;
}
