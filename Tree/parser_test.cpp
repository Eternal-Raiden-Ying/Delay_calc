#include <getopt.h>
#include <iostream>
#include <unordered_map>
#include <parser-spef.hpp>
#include <list>
#include <string>
#include <fstream>

// 输入是spef路径，输出是对应所有net的信息结构体
using namespace std;

int main(int argc, char **argv)
{
    /* SPEF文件路径（注意选择benchmark中的文件） */
    string spef_file = "E:\\courses\\2025.9\\EDA\\Delay_calc\\Delay_calc\\benchmarks\\Group0.spef";

    /* 读取SPEF文件 */
    spef::Spef parser; // 文件解析结果会保存在parser变量里面
    cout << "读取spef文件:" << spef_file << endl;
    if (not parser.read(spef_file))
    {
        cerr << "读取spef文件失败:" << *parser.error << endl;
        exit(1);
    }
/************************************************************************************************/
    /* 针对SPEF文件解析出的每个net做处理 */
    for (auto &net : parser.nets)
    {
        // net中包含了connection，cap，res等信息，下面给出读取示例
        cout<<"res:"<<endl;
        /* ress是一个vector，里面存放的是这个net的所有res,也就是SPEF文件中*RES部分中的一行 */
        for (auto &res : net.ress) 
        {
            /* res的类型为 tuple<string,string,float> */
            std::cout<<get<0>(res)<<" "<<get<1>(res)<<" "<<get<2>(res)<<std::endl; // get<>是tuple的用法
        }
        cout<<"cap:"<<endl;
        /* caps是各节点电容，是SPEF文件中 *CAP的部分 */
        for (auto &cap : net.caps)
        {
            /* cap的类型为 tuple<string,string,float> */
            std::cout<<get<0>(cap)<<" "<<get<2>(cap)<<std::endl; // 注意这里是get<2>(cap)，<1>是空
        }
    }
/************************************************************************************************/
    return 0;
}