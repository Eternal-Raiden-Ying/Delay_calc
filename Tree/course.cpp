// #define __DEVELOP__                 // develop version, embed path parameters into code

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
#include <atomic>
#include "Tree/inc/build_tree.h"
#include "Tree/inc/calc_delay.h"
#include "Tree/inc/analysis.h"
#include "Tree/inc/io.h"

// 必须包含 string_view
#include <string_view>

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
    bool use_ml = false; 
    bool logging_enabled = false;
    bool analyze_enabled = false;
    int option_index = 0;
    static struct option long_options[] = {
        {"file_path",       required_argument, 0, 'f'},
        {"spef_num",        required_argument, 0, 'm'},
        {"feature_path",    required_argument, 0, 't'},
        {"use_ml",          no_argument,       0, 'u'}, 
        {"no_ml",           no_argument,       0, 'U'}, 
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
    // 放大输出缓冲，减少频繁写盘
    std::vector<char> fout_buf(128 << 10); // 128KB
    fout.rdbuf()->pubsetbuf(fout_buf.data(), static_cast<std::streamsize>(fout_buf.size()));

    std::ofstream log_file;
    std::ostream *log = &std::cout;
    if (logging_enabled) {
        string fit_str = "unknown";
        int config_fit_mode = CFG_FIT_MODE;
        switch(config_fit_mode) {
            case MODE_RATIO: fit_str = "ratio"; break;
            case MODE_DELTA: fit_str = "delta"; break;
            case MODE_LINEAR: fit_str = "linear"; break;
            default: fit_str = "unknown"; break;
        }

        string split_str = (CFG_SPLIT_MODE == 1) ? "true" : "false";
        string feat_str = to_string(CFG_FEATURE_MODE);
        string group_str = to_string(spef_num);

        string log_name = fit_str + "_" + split_str + "_" + feat_str + "_" + group_str + ".log";
        std::cout << "Logging enabled, write to " << log_name << std::endl;

        log_file.open(log_name, std::ios::out | std::ios::trunc);
        std::vector<char> log_buf_file(512 << 10); // 512KB
        log_file.rdbuf()->pubsetbuf(log_buf_file.data(), static_cast<std::streamsize>(log_buf_file.size()));
        
        if (log_file.is_open()) {
            log = &log_file;
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
        std::vector<char> analyze_buf_file(512 << 10); // 512KB
        analyze_file.rdbuf()->pubsetbuf(analyze_buf_file.data(), static_cast<std::streamsize>(analyze_buf_file.size()));
        
        if (analyze_file.is_open()) {
            analyze_log = &analyze_file;
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

    spef::Spef parser;
    
    // 异步启动 SPEF 读取
    auto spef_future = std::async(std::launch::async, [&file_path, &spef_num, &parser]() {
        return load_spef(file_path, spef_num, parser);
    });
    
    // 主线程读取 netlist
    int netlist_status = load_netlist_and_delay(file_path, spef_num);
    
    if (netlist_status != 0) {
        std::cerr << "[ERROR] Failed to load netlist and delay files" << std::endl;
        exit(1);
    }
    
    // 等待 SPEF 读取完成
    int spef_status = spef_future.get();
    
    if (spef_status != 0) {
        std::cerr << "[ERROR] Failed to load SPEF file" << std::endl;
        exit(1);
    }

    auto t_load_end = steady_clock::now();
    auto load_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t_load_end - t_load_start).count();
    std::cout << "[TIME] load phase: " << load_ms << " ms" << std::endl;
    // === load 部分计时结束 ===

    // === compute 部分计时开始 ===
    auto t_compute_start = steady_clock::now();
    int omp_max_threads = omp_get_max_threads();

    // 线程本地缓冲
    std::vector<std::string> out_buf(omp_max_threads);
    std::vector<std::string> analyze_buf(omp_max_threads);
    std::vector<std::string> log_buf(omp_max_threads);
    
    // 使用索引循环，方便引用和错误报告
    #pragma omp parallel for schedule(dynamic)
    for (size_t i = 0; i < parser.nets.size(); ++i)
    {
        auto &net = parser.nets[i];

        // === 鲁棒性隔离：单个 Net 的失败不应导致进程崩溃 ===
        try {
            string out_name;            
            string OUT_REAL_NAME;       
            vector<tuple<string, Input_info>> Input;
            vector<Input_info> paths;   
            bool flag = 0;              
            stringstream ss, sout, s_analyze;

            // --- 第一遍扫描：处理 OUTPUT 引脚 ---
            for (auto &connection : net.connections)
            {
                if (connection.direction == spef::ConnectionDirection::OUTPUT)
                {
                    out_name = std::string(connection.name);
                    
                    // [Robust Check] 检查 rfind 是否找到了冒号
                    size_t colon_pos = connection.name.rfind(':');
                    if (colon_pos == std::string_view::npos || colon_pos < 2) {
                        // 格式异常，跳过此连接或整个net
                        flag = 1; break; 
                    }
                    
                    int ID = 0;
                    try {
                        // 显式转为 string 后调用 stoi
                        ID = std::stoi(std::string(connection.name.substr(1, colon_pos - 1)));
                    } catch (...) {
                        // ID 解析失败（非数字），标记跳过
                        flag = 1; break;
                    }
                    
                    // 字符串拼接：先转 string
                    OUT_REAL_NAME = NormalizeName(
                        std::string(parser.name_map[ID]) + 
                        '/' + 
                        std::string(connection.name.substr(colon_pos + 1))
                    );      

                    if (netlist_info.find(OUT_REAL_NAME) == netlist_info.end())
                        flag = 1;
                    else
                        paths = netlist_info.find(OUT_REAL_NAME)->second;
                }
            }
            
            if(flag) continue; // 跳过无效 net
            
            // --- 第二遍扫描：处理 INPUT 引脚 ---
            for (auto &connection : net.connections)
            {
                if (connection.direction == spef::ConnectionDirection::INPUT)
                {
                    // [Robust Check]
                    size_t colon_pos = connection.name.rfind(':');
                    if (colon_pos == std::string_view::npos || colon_pos < 2) continue;

                    int ID = 0;
                    try {
                        ID = std::stoi(std::string(connection.name.substr(1, colon_pos - 1)));
                    } catch (...) { continue; }
                    
                    string Input_Name = NormalizeName(
                        std::string(parser.name_map[ID]) + 
                        '/' + 
                        std::string(connection.name.substr(colon_pos + 1))
                    );
                    
                    for (auto &p : paths)
                    {
                        if (Input_Name == p.name)
                        {
                            // 显式转 string 存入 tuple
                            Input.push_back(make_pair(std::string(connection.name), p));
                        }
                    }
                }
            }
            
            // 构建拓扑与计算延时
            Topology topo = BuildTopologyFromRess(net, out_name, Input);
            FillCapsFromNet(net, topo, out_name, Input);

            double pin_load_unit_factor = 1e3;    
            double cap_ff_to_ps_factor = 1e-3;    
            auto base_res = ComputeElmoreDelays_advanced(net, out_name, Input, topo, pin_load_unit_factor, cap_ff_to_ps_factor);

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

            // 安全写入线程本地缓冲
            const int tid = omp_get_thread_num();
            out_buf[tid] += sout.str();
            if (analyze_enabled) analyze_buf[tid] += s_analyze.str();
            if (logging_enabled) log_buf[tid] += ss.str();

        } 
        catch (const std::exception& e) {
            // 捕获已知异常，记录但不中断
            #pragma omp critical 
            {
                if (logging_enabled) {
                    (*log) << "[WARN] Skipped Net due to error: " << net.name 
                           << " | " << e.what() << endl;
                } else {
                    std::cerr << "[WARN] Skipped Net: " << net.name 
                              << " | " << e.what() << endl;
                }
            }
        }
        catch (...) {
            // 捕获未知异常
            #pragma omp critical 
            {
                std::cerr << "[CRITICAL] Unknown error processing Net: " << net.name << endl;
            }
        }
    }

    auto t_compute_end = steady_clock::now();

    // 串行 flush 文件
    for (int i = 0; i < omp_max_threads; ++i) {
        if (!out_buf[i].empty()) fout << out_buf[i];
        if (analyze_enabled && !analyze_buf[i].empty()) (*analyze_log) << analyze_buf[i];
        if (logging_enabled && !log_buf[i].empty()) (*log) << log_buf[i];
    }
    
    auto compute_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t_compute_end - t_compute_start).count();
    std::cout << "[TIME] compute phase: " << compute_ms << " ms" << std::endl;

    if (file_path) {
        free(file_path);
        file_path = nullptr;
    }
    return 0;
}