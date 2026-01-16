#ifndef READ_H
#define READ_H

#include <fstream>
#include <iostream>
#include <string>
#include "include/tao/pegtl.hpp"
#include <unordered_map>
#include <vector>

using namespace std;
namespace pegtl = tao::pegtl;

// 前向声明
inline string file_to_memory_fallback(string p);

struct Input_info
{
    string name;
    float pin_cap;
    float delay;
};

using RuleDontCare = pegtl::star<pegtl::space>;
using RuleSpace = pegtl::plus<pegtl::space>;
using RuleVar = pegtl::until<pegtl::at<pegtl::sor<pegtl::space, pegtl::eof>>>;

struct Net_name : RuleVar
{
};
struct Net_name_beg : pegtl::seq<TAO_PEGTL_STRING("Net name:"), RuleDontCare, Net_name, pegtl::eol>
{
};

struct Output : RuleVar
{
};
struct Output_beg : pegtl::seq<RuleDontCare, TAO_PEGTL_STRING("Output"), RuleDontCare, pegtl::one<':'>, RuleSpace, Output, RuleSpace, RuleVar, pegtl::eol>
{
};

struct Input : RuleVar
{
};
struct Input_beg : pegtl::seq<RuleDontCare, TAO_PEGTL_STRING("Input"), RuleDontCare, pegtl::one<':'>, RuleSpace, Input, RuleSpace, RuleVar, pegtl::eol>
{
};

struct Pin_Cap : RuleVar
{
};
struct Pin_Cap_beg : pegtl::seq<RuleDontCare, TAO_PEGTL_STRING("Pin Cap."), RuleDontCare, pegtl::one<':'>, RuleDontCare, Pin_Cap, pegtl::eol>
{
};

struct info_txt : pegtl::seq<Net_name_beg, Output_beg, pegtl::plus<pegtl::seq<Input_beg, Pin_Cap_beg>>>
{
};
struct netlist : pegtl::star<pegtl::seq<info_txt, RuleDontCare>>
{
};

struct delay_output : RuleVar
{
};
struct delay_input : RuleVar
{
};
struct delay_value : RuleVar
{
};
struct delay_beg : pegtl::seq<delay_output, RuleDontCare, delay_input, RuleDontCare, delay_value, pegtl::eol>
{
};
struct delay : pegtl::star<delay_beg>
{
};

bool Read_netlist_file(string netlist_file);
bool Read_delay_file(string delay_file);

#endif
