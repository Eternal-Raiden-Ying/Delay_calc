#define __DEVELOP__                 // develop version, embed path parameters into code

#include <getopt.h>
#include <iostream>
#include <unordered_map>
#include <parser-spef.hpp>
#include <list>
#include <string>
#include "Read_train/Read.h"
#include <fstream>
#include <thread>
#include <stdexcept>
#include <queue>
#include <functional>
#include "Tree/inc/build_tree.h"
#include "Tree/inc/calc_delay.h"
#include "Tree/inc/analysis.h"
#include "Tree/inc/io.h"            // <-- new include
using namespace std;

extern unordered_map<string, vector<Input_info>> netlist_info;

int main(int argc, char **argv)
{
    // Redirect all std::cout outputs to test.log for diagnosis
    std::ofstream log_file("test.log", std::ios::out | std::ios::app);
    if (log_file.is_open()) {
        std::cout.rdbuf(log_file.rdbuf());
    } else {
        std::cerr << "Failed to open test.log for writing" << std::endl;
    }

    int c;
    int spef_num;
    char *file_path = nullptr;
    int option_index = 0;
    static struct option long_options[] = {
        {"file_path", required_argument, 0, 'f'}, 
        {"spef_num", required_argument, 0, 'm'}
    };

    // set default argument during development
    #ifdef __DEVELOP__
    string default_file_path = "D:\\Documents\\Coding\\Projects\\Delay_calc\\Delay_calc\\Data";
    int default_spef_num = 0;
    file_path = (char *)malloc((strlen(default_file_path.c_str()) + 1) * sizeof(char));
    strcpy(file_path, default_file_path.c_str());
    spef_num = default_spef_num;
    #endif

    while ((c = getopt_long(argc, argv, "s", long_options, &option_index)))
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
        default:
            printf("?? getopt returned character code 0%o ??\n", c);
        }
    }

    // 统一封装后的 IO 调用
    std::string base_path = std::string(file_path);

    if (load_netlist_and_delay(base_path, spef_num) != 0)
        exit(1);

    spef::Spef parser;
    if (load_spef(base_path, spef_num, parser) != 0)
        exit(1);

    string target_net_name = "*127412";
    /* 一个SPEF文件含有多个net，针对每一个net做处理 */
    for (auto &net : parser.nets)
    {
        if (net.name != target_net_name) continue;

        /* 一个net只有一个output，有多个input */
        string out_name;      // spef文件中output的名字，是一个ID，例如 *1681713:Q，用于建立net的数据结构
        string OUT_REAL_NAME; // 最后用来写入文件真实的名字 CNU17/R4_reg_18_
        /* 一条path包含多个从output->input的路径，也就是一个net包含多条path */
        /* Input变量第一个string元素是一个input pin的 ID */
        /* Input_info记录了这个input的real name和引脚电容、以及以这个input结束的path的延时值 */
        vector<tuple<string, Input_info>> Input;

        vector<Input_info> paths; // 暂时用来记录查询到的input信息
        bool flag = 0;
        // cout << "net.name: " << net.name << endl;
        for (auto &connection : net.connections)
        {
            if (connection.direction == spef::ConnectionDirection::OUTPUT)
            {
                out_name = connection.name;
                int index = connection.name.rfind(':');                 // 找到‘:’的位置
                int ID = stoi(connection.name.substr(1, index - 1));    // 提取出ID号 name格式参考 *1681713:Q
                OUT_REAL_NAME = parser.name_map[ID] + '/' + connection.name.substr(index + 1, -1);      // 拼接出真实名字, name_map： ID->real name(string)

                if (netlist_info.find(OUT_REAL_NAME) == netlist_info.end())
                {
                    // 如果该net的out不在net_info中，那就跳过这个net，继续下一个net
                    // continue loop;
                    flag = 1;
                    // cout << "This net is useless" << net.name << endl;
                    // cout << "index: " << index << endl;
                    // cout << "ID: " << ID << endl;
                    // cout << "OUT_REAL_NAME: " << OUT_REAL_NAME << endl;
                }
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
                string Input_Name = parser.name_map[ID] + '/' + connection.name.substr(index + 1, -1);

                for (auto &p : paths)
                {
                    if (Input_Name == p.name)
                    {
                        Input.push_back(make_pair(connection.name, p));
                    }
                }
            }
        }

        /********************************************************************************************/
        /********************************************************************************************/
        /********************************************************************************************/
        // Input中包含了这个net所有的path的延时、input pin的id以及real name，还有引脚电容
        
        // Step 1: 搭建电路拓扑的树（基于 ress）
        Topology topo = BuildTopologyFromRess(net, out_name, Input);

        // Step 2: 填充电容（基于 caps 和 input pin load）
        FillCapsFromNet(net, topo, out_name, Input);


        // Step 3: 计算 Elmore 延时（单位转换因子暂设 1）
        // 单位设定：caps 为 fF、res 为 ohm，输出 ps。e3
        // ohm * fF -> seconds: 1e-15；转成 ps 乘以 1e12 => 综合因子 1e-3。
        double pin_load_unit_factor = 1e3;    // 若 Input.pin_cap 单位与 caps 不同，可在此调整为把其换算到 fF
        double cap_ff_to_ps_factor = 1e-3;    // R(ohm)*C(fF) 转 ps 的系数
        auto elmore = ComputeElmoreDelays(net, out_name, Input, topo, pin_load_unit_factor, cap_ff_to_ps_factor);

        // Compare with standard delays in Input_info (ns -> ps)
        // Build a quick lookup from input name to computed ps
        unordered_map<string,double> elmore_map;
        for (auto &pr : elmore) elmore_map[pr.first] = pr.second;

        // cout << "[Compare] name | standard(ps) | computed(ps) | rel_error" << endl;
        cout.setf(std::ios::fixed);
        cout.precision(6);
        for (const auto &inp : Input) {
            const string &inp_name = get<0>(inp);
            const auto &info = get<1>(inp);
            double std_ps = info.delay * 1000.0; // ns -> ps
            auto it = elmore_map.find(inp_name);
            if (it != elmore_map.end()) {
                double calc_ps = it->second;
                double rel_err = (std_ps != 0.0) ? ((calc_ps - std_ps) / std_ps) : 0.0;
                cout << inp_name << " | " << std_ps << " | " << calc_ps << " | " << rel_err << endl;
            } else {
                cout << inp_name << " | " << std_ps << " | " << "N/A" << " | " << "N/A" << endl;
            }
        }
        cout.unsetf(std::ios::fixed);
        ExportNetToGephiCsv(net, out_name, Input, topo, "rc_nodes.csv", "rc_edges.csv");

        // 调试：单路径详细信息（仅在只有一个 input 时调用）
        // if (Input.size() == 1) {
        //     DebugElmoreSinglePath(net, out_name, Input, topo, pin_load_unit_factor, cap_ff_to_ps_factor);
        // }


    }

    /********************************************************************************************/
    /********************************************************************************************/
    /********************************************************************************************/

    /* 释放内存 */
    if (file_path)
    {
        free(file_path);
        file_path = nullptr;
    }
    return 0;
}
