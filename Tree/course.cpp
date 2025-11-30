#include <getopt.h>
#include <iostream>
#include <unordered_map>
#include <parser-spef.hpp>
#include <list>
#include <string>
#include "Read_train/Read.h"
#include <fstream>
#include <thread>
using namespace std;

#define __DEBUG__

extern unordered_map<string, vector<Input_info>> netlist_info;

int main(int argc, char **argv)
{
    /* 以下部分代码用于指令的参数读入，请选择性使用 */
    int c;
    int spef_num;
    char *file_path = nullptr;
    // char *feature_path = nullptr;                // 已经与助教确认，feature_path参数目前不需要使用
    int option_index = 0;

    static struct option long_options[] = {
        {"file_path", required_argument, 0, 'f'}, 
        //{"feature_path", required_argument, 0, 'e'}, 
        {"spef_num", required_argument, 0, 'm'}
    };

    //命令行参数默认值
    #ifdef __DEBUG__
    string default_file_path = "D:\\Documents\\Coding\\Projects\\Delay_calc\\Delay_calc\\simple";
    int default_spef_num = 0;
    file_path = (char *)malloc((strlen(default_file_path.c_str()) + 1) * sizeof(char));
    strcpy(file_path, default_file_path.c_str());
    spef_num = default_spef_num;
    #endif

    // 解析命令行参数
    while ((c = getopt_long(argc, argv, "s", long_options, &option_index)))
    {
        if (c == -1)
            break;
        switch (c)
        {
        case 'f':
            file_path = (char *)malloc((strlen(optarg) + 1) * sizeof(char));
            strcpy(file_path, optarg);
            // cout << spef_file << endl;
            break;
        // case 'e':
        //     feature_path = (char *)malloc((strlen(optarg) + 1) * sizeof(char));
        //     strcpy(feature_path, optarg);
        //     // cout << spef_file << endl;
        //     break;
        case 'm':
            spef_num = atoi(optarg);
            break;
        default:
            printf("?? getopt returned character code 0%o ??\n", c);
        }
    }

    // windows环境记得把路径的 ‘/’ 改成 ‘\\’

    /* 进行netlist_info.txt和 delay_data文件夹中Groupx.txt文件的解析*/
    /* Read_netlist_file函数将读取到的信息存在外部变量 netlist_info 中，是一个Hash结构 */
    string netlist_file = string(file_path) + "/netlist_info.txt";
    if (Read_netlist_file(netlist_file))
        exit(1);
    /* 读取delay文件，注意spef0和1是两种corner下的同一批电路，这意味着同一个net会有两种delay，处理不同spef采用不同的delay */
    string delay_file = string(file_path) + "/delay_data/Group" + to_string(spef_num) + ".txt";
    if (Read_delay_file(delay_file))
    {
        cerr << "读取delay文件失败" << endl;
        exit(1);
    }
    // auto elapsed_t1 = chrono::steady_clock::now();
    string spef_file = string(file_path) + "/SPEF/Group" + to_string(spef_num) + ".spef";
    /// 读取SPEF文件
    spef::Spef parser;
    if (not parser.read(spef_file))
    {
        cerr << "读取spef文件失败:" << *parser.error << endl;
        exit(1);
    }

    /* 一个SPEF文件含有多个net，针对每一个net做处理 */
    for (auto &net : parser.nets)
    {
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
                    #ifndef __DEBUG__
                    cout << "This net is useless" << net.name << endl;
                    cout << "index: " << index << endl;
                    cout << "ID: " << ID << endl;
                    cout << "OUT_REAL_NAME: " << OUT_REAL_NAME << endl;
                    #endif
                }
                else
                    paths = netlist_info.find(OUT_REAL_NAME)->second;
            }
        }
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
        if (flag)
            continue;

        /********************************************************************************************/
        /********************************************************************************************/
        /********************************************************************************************/
        // Input中包含了这个net所有的path的延时、input pin的id以及real name，还有引脚电容
        for (auto i : Input)
        {
            std::cout << get<0>(i) << " " << get<1>(i).name << " " << get<1>(i).delay << " " << get<1>(i).pin_cap << endl;
        }

        // net中包含了connection，cap，res等信息，下面给出示例
        for (auto &res : net.ress)
        {
        }
        for (auto &cap : net.caps)
        {
        }
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
    // if (feature_path)
    // {
    //     free(feature_path);
    //     feature_path = nullptr;
    // }
    return 0;
}
