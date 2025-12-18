#include "Tree/inc/io.h"
#include <iostream>

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

    string delay_file = base_path + "/delay_data/Group" + to_string(spef_num) + ".txt";
    if (Read_delay_file(delay_file))
    {
        cerr << "读取 delay 文件失败: " << delay_file << endl;
        return 2;
    }
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
