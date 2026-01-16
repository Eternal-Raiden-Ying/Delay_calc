#pragma once

/* 
    Eternal::  l1262: add 'inline' 
*/

// 定义_MEASURE_宏以启用详细的性能统计
// 编译时添加 -D_MEASURE_ 或在此处取消注释来启用
// #define _MEASURE_

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstring>
#include <experimental/filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#ifdef _WIN32
    #include <windows.h>
#else
    #include <sys/mman.h>
    #include <fcntl.h>
    #include <sys/stat.h>
    #include <unistd.h>
#endif
#include "pegtl/pegtl.hpp"
#include "comment_removal.hpp"
#include "parallel_parse.hpp"

namespace spef
{

    // ------------------------------------------------------------------------------------------------
    // Parser-SPEF stores the data to the folloing data structures.
    // ------------------------------------------------------------------------------------------------

    // Visit https://en.wikipedia.org/wiki/Standard_Parasitic_Exchange_Format
    // first to understand what are essential field in a SPEF.

    // ConnectionType:
    //   EXTERNAL: connection to a external port (*P)
    //   INTERNAL: connection to a cell instance (*I)
    enum class ConnectionType
    {
        INTERNAL,
        EXTERNAL
    };

    // ConnectionDirection:
    //   the direction of pin, either in, out or bidirectional.
    enum class ConnectionDirection
    {
        INPUT,
        OUTPUT,
        INOUT
    };

    // Port: the port in *PORTS section
    struct Port
    {
        Port() = default;
        Port(const std::string &s) : name(s) {}
        std::string name;
        ConnectionDirection direction; // I, O, B
    };

    // Connection: the *CONN section in *D_NET
    struct Connection
    {

        std::string name;
        ConnectionType type;
        ConnectionDirection direction;

        std::optional<std::pair<float, float>> coordinate;
        std::optional<float> load;
        std::string driving_cell;

        Connection() = default;

        void scale_capacitance(float);
    };

    // Net: the data in a *D_NET section
    //   - Capacitor can be ground (one node) or coupled (two nodes)
    struct Net
    {
        std::string name;
        float lcap;
        std::vector<Connection> connections;
        std::vector<std::tuple<std::string, std::string, float>> caps;
        std::vector<std::tuple<std::string, std::string, float>> ress;

        Net() = default;
        Net(const std::string &s, const float f) : name{s}, lcap{f} {}

        void scale_capacitance(float);
        void scale_resistance(float);
    };

    // Spef: the data in a SPEF.
    // There are four parts: header, name map, ports, nets.
    struct Spef
    {

        struct Error
        {
            std::string line;
            size_t line_number;
            size_t byte_in_line;
        };

        std::string standard;
        std::string design_name;
        std::string date;
        std::string vendor;
        std::string program;
        std::string version;
        std::string design_flow;
        std::string divider;
        std::string delimiter;
        std::string bus_delimiter;
        std::string time_unit;
        std::string capacitance_unit;
        std::string resistance_unit;
        std::string inductance_unit;

        std::unordered_map<size_t, std::string> name_map;
        std::vector<Port> ports;
        std::vector<Net> nets;

        std::optional<Error> error;

        std::string dump() const;
        std::string dump_compact() const;

        void dump(std::ostream &) const;
        void dump_compact(std::ostream &) const;
        void clear();
        void expand_name();
        void expand_name(Net &);
        void expand_name(Port &);
        void scale_capacitance(float);
        void scale_resistance(float);

        bool read(const std::experimental::filesystem::path &);

        template <typename T>
        friend struct Action;

    private:
        Net *_current_net{nullptr};
        std::vector<std::string_view> _tokens;
    };

    // ------------------------------------------------------------------------------------------------
    // DO NOT CHANGE ANYTHING BELOW UNLESS YOU KNOW WHAT YOU ARE DOING!
    // (development use only)
    // ------------------------------------------------------------------------------------------------

    namespace double_
    {
        using namespace tao::TAO_PEGTL_NAMESPACE; // NOLINT

        struct plus_minus : opt<one<'+', '-'>>
        {
        };
        struct dot : one<'.'>
        {
        };

        struct inf : seq<istring<'i', 'n', 'f'>, opt<istring<'i', 'n', 'i', 't', 'y'>>>
        {
        };

        struct nan : seq<istring<'n', 'a', 'n'>, opt<one<'('>, plus<alnum>, one<')'>>>
        {
        };

        template <typename D>
        struct number : if_then_else<dot, plus<D>, seq<plus<D>, opt<dot, star<D>>>>
        {
        };

        struct e : one<'e', 'E'>
        {
        };
        struct p : one<'p', 'P'>
        {
        };
        struct exponent : seq<plus_minus, plus<digit>>
        {
        };

        struct decimal : seq<number<digit>, opt<e, exponent>>
        {
        };
        struct hexadecimal : seq<one<'0'>, one<'x', 'X'>, number<xdigit>, opt<p, exponent>>
        {
        };

        struct rule : seq<plus_minus, sor<hexadecimal, decimal, inf, nan>>
        {
        };
    }; // namespace double_

    // Function: split_on_space
    inline void split_on_space(const char *beg, const char *end, std::vector<std::string_view> &tokens)
    {

        // Parse the token.
        const char *token{nullptr};
        size_t len{0};

        tokens.clear();

        for (const char *itr = beg; itr != end && *itr != 0; ++itr)
        {
            if (std::isspace(*itr))
            {
                if (len > 0)
                { // Add the current token.
                    tokens.push_back({token, len});
                    token = nullptr;
                    len = 0;
                }
            }
            else
            {
                if (len == 0)
                {
                    token = itr;
                }
                ++len;
            }
        }

        if (len > 0)
        {
            tokens.push_back({token, len});
        }
    }

    inline std::ostream &operator<<(std::ostream &os, const ConnectionType &c)
    {
        switch (c)
        {
        case ConnectionType::INTERNAL:
            os << "*I";
            break;
        case ConnectionType::EXTERNAL:
            os << "*P";
            break;
        }
        return os;
    }

    inline std::ostream &operator<<(std::ostream &os, const ConnectionDirection &c)
    {
        switch (c)
        {
        case ConnectionDirection::INPUT:
            os << 'I';
            break;
        case ConnectionDirection::OUTPUT:
            os << 'O';
            break;
        case ConnectionDirection::INOUT:
            os << 'B';
            break;
        }
        return os;
    }

    inline std::ostream &operator<<(std::ostream &os, const Port &p)
    {
        os << p.name << ' ';
        switch (p.direction)
        {
        case ConnectionDirection::INPUT:
            os << 'I';
            break;
        case ConnectionDirection::OUTPUT:
            os << 'O';
            break;
        case ConnectionDirection::INOUT:
            os << 'B';
            break;
        }
        return os;
    }

    inline bool operator==(const Connection &lhs, const Connection &rhs)
    {

        auto is_same_float = [](float a, float b)
        {
            if (::fabs(a - b) > 1e-3)
            {
                return false;
            }
            return true;
        };

        if (lhs.name != rhs.name or lhs.type != rhs.type or lhs.direction != rhs.direction or lhs.driving_cell != rhs.driving_cell)
        {
            return false;
        }

        if (lhs.load.has_value() and rhs.load.has_value())
        {
            if (not is_same_float(*lhs.load, *rhs.load))
            {
                return false;
            }
        }
        else
        {
            if (lhs.load != rhs.load)
            {
                return false;
            }
        }

        if (lhs.coordinate.has_value() and rhs.coordinate.has_value())
        {
            auto &x = std::get<0>(*lhs.coordinate);
            auto &y = std::get<1>(*lhs.coordinate);
            auto &rx = std::get<0>(*rhs.coordinate);
            auto &ry = std::get<1>(*rhs.coordinate);
            if (not is_same_float(x, rx) or not is_same_float(y, ry))
            {
                return false;
            }
        }
        else
        {
            if (lhs.coordinate != rhs.coordinate)
            {
                return false;
            }
        }
        return true;
    }

    inline bool operator!=(const Connection &lhs, const Connection &rhs)
    {
        return not(lhs == rhs);
    }

    inline std::ostream &operator<<(std::ostream &os, const Connection &c)
    {
        os << c.type << ' ' << c.name << ' ' << c.direction;
        if (c.coordinate.has_value())
        {
            os << " *C " << std::get<0>(*c.coordinate) << ' ' << std::get<1>(*c.coordinate);
        }
        if (c.load.has_value())
        {
            os << " *L " << *c.load;
        }
        if (not c.driving_cell.empty())
        {
            os << " *D " << c.driving_cell;
        }
        return os;
    }

    inline bool operator==(const Net &lhs, const Net &rhs)
    {
        if (lhs.name != rhs.name or ::fabs(lhs.lcap - rhs.lcap) > 1e-3)
            return false;
        if (lhs.connections != rhs.connections)
            return false;
        if (lhs.caps.size() != rhs.caps.size() or lhs.ress.size() != rhs.ress.size())
        {
            return false;
        }

        auto is_same_tuple = [](const std::tuple<std::string, std::string, float> &l, const std::tuple<std::string, std::string, float> &r)
        {
            const auto &[l1, l2, l3] = l;
            const auto &[r1, r2, r3] = r;
            if (l1 != r1 or l2 != r2)
            {
                return false;
            }
            if (::fabs(l3 - r3) > 1e-3)
            {
                return false;
            }
            return true;
        };

        for (size_t i = 0; i < lhs.caps.size(); i++)
        {
            if (not is_same_tuple(lhs.caps[i], rhs.caps[i]))
            {
                return false;
            }
        }

        for (size_t i = 0; i < lhs.ress.size(); i++)
        {
            if (not is_same_tuple(lhs.ress[i], rhs.ress[i]))
            {
                return false;
            }
        }

        return true;
    }

    inline bool operator!=(const Net &lhs, const Net &rhs)
    {
        return not(lhs == rhs);
    }

    inline std::ostream &operator<<(std::ostream &os, const Net &n)
    {
        os << "*D_NET " << n.name << ' ' << n.lcap << '\n';
        if (not n.connections.empty())
        {
            os << "*CONN\n";
        }
        for (const auto &c : n.connections)
        {
            os << c << '\n';
        }
        if (not n.caps.empty())
        {
            os << "*CAP\n";
        }
        for (size_t i = 0; i < n.caps.size(); ++i)
        {
            os << i + 1 << ' ' << std::get<0>(n.caps[i]);
            if (not std::get<1>(n.caps[i]).empty())
            {
                os << ' ' << std::get<1>(n.caps[i]);
            }
            os << ' ' << std::get<2>(n.caps[i]) << '\n';
        }
        if (not n.ress.empty())
        {
            os << "*RES\n";
        }
        for (size_t i = 0; i < n.ress.size(); ++i)
        {
            os << i + 1 << ' ' << std::get<0>(n.ress[i]) << ' ' << std::get<1>(n.ress[i]) << ' ' << std::get<2>(n.ress[i]) << '\n';
        }
        os << "*END\n";
        return os;
    }

    inline void Connection::scale_capacitance(float scale)
    {
        if (load.has_value())
        {
            load = (*load) * scale;
        }
    }

    inline void Net::scale_capacitance(float scale)
    {
        lcap *= scale;
        for (auto &c : connections)
        {
            c.scale_capacitance(scale);
        }

        for (auto &cap : caps)
        {
            std::get<2>(cap) *= scale;
        }
    }

    inline void Net::scale_resistance(float scale)
    {
        for (auto &res : ress)
        {
            std::get<2>(res) *= scale;
        }
    }

    // --------------------------------------------------------
    // Begin Spef definition
    // --------------------------------------------------------

    inline void Spef::scale_capacitance(float scale)
    {
        for (auto &n : nets)
        {
            n.scale_capacitance(scale);
        }
    }

    inline void Spef::scale_resistance(float scale)
    {
        for (auto &n : nets)
        {
            n.scale_resistance(scale);
        }
    }

    inline void Spef::clear()
    {

        standard.clear();
        design_name.clear();
        date.clear();
        vendor.clear();
        program.clear();
        version.clear();
        design_flow.clear();
        divider.clear();
        delimiter.clear();
        bus_delimiter.clear();

        time_unit.clear();
        capacitance_unit.clear();
        resistance_unit.clear();
        inductance_unit.clear();

        name_map.clear();
        ports.clear();
        nets.clear();

        error.reset();

        _current_net = nullptr;
        _tokens.clear();
    }

    // Procedure: dump
    // dump the spef data structrue to a SPEF
    inline std::string Spef::dump() const
    {
        std::ostringstream os;
        dump(os);
        return os.str();
    }

    // Procedure: dump
    inline void Spef::dump(std::ostream &os) const
    {
        os << "*SPEF " << standard << '\n'
           << "*DESIGN " << design_name << '\n'
           << "*DATE " << date << '\n'
           << "*VENDOR " << vendor << '\n'
           << "*PROGRAM " << program << '\n'
           << "*VERSION " << version << '\n'
           << "*DESIGN_FLOW " << design_flow << '\n'
           << "*DIVIDER " << divider << '\n'
           << "*DELIMITER " << delimiter << '\n'
           << "*BUS_DELIMITER " << bus_delimiter << '\n'
           << "*T_UNIT " << time_unit << '\n'
           << "*C_UNIT " << capacitance_unit << '\n'
           << "*R_UNIT " << resistance_unit << '\n'
           << "*L_UNIT " << inductance_unit << '\n';
        os << '\n';

        if (not name_map.empty())
        {
            os << "*NAME_MAP\n";
        }
        for (const auto &[k, v] : name_map)
        {
            os << '*' << k << ' ' << v << '\n';
        }
        os << '\n';
        if (not ports.empty())
        {
            os << "*PORTS\n";
        }
        for (const auto &p : ports)
        {
            os << p << '\n';
        }
        os << '\n';
        for (const auto &net : nets)
        {
            os << net << '\n';
        }
    }

    // Function: dump_compact
    inline std::string Spef::dump_compact() const
    {
        std::ostringstream os;
        dump_compact(os);
        return os.str();
    }

    // Procedure: dump_compact
    inline void Spef::dump_compact(std::ostream &os) const
    {

        if (not name_map.empty())
        {
            dump(os);
            return;
        }

        os << "*SPEF " << standard << '\n'
           << "*DESIGN " << design_name << '\n'
           << "*DATE " << date << '\n'
           << "*VENDOR " << vendor << '\n'
           << "*PROGRAM " << program << '\n'
           << "*VERSION " << version << '\n'
           << "*DESIGN_FLOW " << design_flow << '\n'
           << "*DIVIDER " << divider << '\n'
           << "*DELIMITER " << delimiter << '\n'
           << "*BUS_DELIMITER " << bus_delimiter << '\n'
           << "*T_UNIT " << time_unit << '\n'
           << "*C_UNIT " << capacitance_unit << '\n'
           << "*R_UNIT " << resistance_unit << '\n'
           << "*L_UNIT " << inductance_unit << '\n';
        os << '\n';

        std::vector<Port> port_copy = ports;
        std::vector<Net> net_copy = nets;
        std::unordered_map<std::string, size_t> nm;
        auto replace_name = [&](std::string &str)
        {
            if (str.empty())
                return;
            if (auto pos = str.find(delimiter); pos != std::string::npos)
            {
                auto prefix = str.substr(0, pos);
                nm.try_emplace(prefix, nm.size() + 1);
                str = '*' + std::to_string(nm.at(prefix)) + str.substr(pos, str.size() - pos);
            }
            else
            {
                nm.try_emplace(str, nm.size() + 1);
                str = '*' + std::to_string(nm.at(str));
            }
        };

        // Construct namp map
        for (auto &p : port_copy)
        {
            replace_name(p.name);
        }

        for (auto &net : net_copy)
        {
            replace_name(net.name);

            for (auto &c : net.connections)
            {
                replace_name(c.name);
                replace_name(c.driving_cell);
            }

            for (auto &t : net.caps)
            {
                replace_name(std::get<0>(t));
                replace_name(std::get<1>(t));
            }

            for (auto &r : net.ress)
            {
                replace_name(std::get<0>(r));
                replace_name(std::get<1>(r));
            }
        }

        if (not nm.empty())
        {
            os << "*NAME_MAP\n";
        }
        for (const auto &[k, v] : nm)
        {
            os << '*' << v << ' ' << k << '\n';
        }
        os << '\n';

        if (not port_copy.empty())
        {
            os << "*PORTS\n";
        }
        for (const auto &p : port_copy)
        {
            os << p << '\n';
        }
        os << '\n';
        for (const auto &net : net_copy)
        {
            os << net << '\n';
        }
    }

    // Operator: <<
    inline std::ostream &operator<<(std::ostream &os, const Spef::Error &err)
    {
        os << "error at line " << err.line_number << ":\n";
        os << "  " << err.line << '\n';
        os << std::setw(err.byte_in_line + 3) << '^' << '\n';
        return os;
    }

    // ------------------------------------------------------------------------------------------------
    // Begin of PEG rules
    // ------------------------------------------------------------------------------------------------

    namespace pegtl = tao::TAO_PEGTL_NAMESPACE;

    using RuleToken = pegtl::until<pegtl::at<pegtl::sor<pegtl::space, pegtl::one<'*'>, pegtl::eof>>>;
    using RuleDontCare = pegtl::star<pegtl::space>;
    using RuleSpace = pegtl::plus<pegtl::space>;

    // The double_::rule does not check successive characters after digits. For example:
    // 1.243abc still satisfies the double_::rule. This RuleDouble enforce the successive
    // characters should be either a space or *
    struct RuleDouble : pegtl::seq<double_::rule, pegtl::at<pegtl::sor<pegtl::space, pegtl::one<'*'>>>>
    {
    };

    template <typename T>
    struct Action : pegtl::nothing<T>
    {
    };

    struct RuleQuote : pegtl::string<'"'>
    {
    };

    struct RuleQuotedString : pegtl::if_must<RuleQuote, pegtl::until<RuleQuote>>
    {
    };

    struct RuleHeaderValue : pegtl::plus<pegtl::seq<RuleQuotedString, pegtl::star<RuleSpace, RuleQuotedString>>>
    {
    };

    struct Divider : pegtl::any
    {
    };

    template <>
    struct Action<Divider>
    {
        template <typename Input>
        static bool apply(const Input &in, Spef &d)
        {
            if (in.size() != 1)
            {
                return false;
            }
            d.divider = in.string();
            return true;
        };
    };

    struct Delimiter : pegtl::any
    {
    };

    template <>
    struct Action<Delimiter>
    {
        template <typename Input>
        static bool apply(const Input &in, Spef &d)
        {
            if (in.size() != 1)
            {
                return false;
            }
            d.delimiter = in.string();
            return true;
        };
    };

    struct BusDelimiter : pegtl::must<pegtl::any, pegtl::star<pegtl::space>, pegtl::any>
    {
    };

    template <>
    struct Action<BusDelimiter>
    {
        template <typename Input>
        static void apply(const Input &in, Spef &d)
        {
            d.bus_delimiter = in.string();
            // Remove space in middle
            d.bus_delimiter.erase(std::remove_if(d.bus_delimiter.begin(), d.bus_delimiter.end(), [](auto c)
                                                 { return std::isspace(c); }),
                                  d.bus_delimiter.end());
        };
    };

    //  Header Section --------------------------------------------------------------------------------

    // Procedure: RemoveHeaderKey removes the the key in header and returns the value
    //            e.g.  *SPEF "IEEE 1994" will return "IEEE 1994" (quoted)
    //            e.g.  *DESIGN "simple"  will return "simple"    (quoted)
    template <typename Input>
    inline std::string RemoveHeaderKey(const Input &in, size_t offset)
    {
        auto beg = in.begin() + offset;
        while (std::isspace(*beg))
        {
            ++beg;
            ++offset;
        }
        return in.string().erase(0, offset);
    }

    struct RuleStandard : pegtl::seq<TAO_PEGTL_STRING("*SPEF"), pegtl::opt<RuleSpace, RuleHeaderValue>>
    {
    };

    template <>
    struct Action<RuleStandard>
    {
        template <typename Input>
        static void apply(const Input &in, Spef &d)
        {
            d.standard = RemoveHeaderKey(in, sizeof("*SPEF"));
        };
    };

    struct RuleDesign : pegtl::seq<TAO_PEGTL_STRING("*DESIGN"), pegtl::opt<RuleSpace, RuleHeaderValue>>
    {
    };

    template <>
    struct Action<RuleDesign>
    {
        template <typename Input>
        static void apply(const Input &in, Spef &d)
        {
            d.design_name = RemoveHeaderKey(in, sizeof("*DESIGN"));
        };
    };

    struct RuleDate : pegtl::seq<TAO_PEGTL_STRING("*DATE"), pegtl::opt<RuleSpace, RuleHeaderValue>>
    {
    };

    template <>
    struct Action<RuleDate>
    {
        template <typename Input>
        static void apply(const Input &in, Spef &d)
        {
            d.date = RemoveHeaderKey(in, sizeof("*DATE"));
        };
    };

    struct RuleVendor : pegtl::seq<TAO_PEGTL_STRING("*VENDOR"), pegtl::opt<RuleSpace, RuleHeaderValue>>
    {
    };

    template <>
    struct Action<RuleVendor>
    {
        template <typename Input>
        static void apply(const Input &in, Spef &d)
        {
            d.vendor = RemoveHeaderKey(in, sizeof("VENDOR"));
        };
    };

    struct RuleProgram : pegtl::seq<TAO_PEGTL_STRING("*PROGRAM"), pegtl::opt<RuleSpace, RuleHeaderValue>>
    {
    };

    template <>
    struct Action<RuleProgram>
    {
        template <typename Input>
        static void apply(const Input &in, Spef &d)
        {
            d.program = RemoveHeaderKey(in, sizeof("*PROGRAM"));
        };
    };

    struct RuleVersion : pegtl::seq<TAO_PEGTL_STRING("*VERSION"), pegtl::opt<RuleSpace, RuleHeaderValue>>
    {
    };

    template <>
    struct Action<RuleVersion>
    {
        template <typename Input>
        static void apply(const Input &in, Spef &d)
        {
            d.version = RemoveHeaderKey(in, sizeof("*VERSION"));
        };
    };

    struct RuleDesignFlow : pegtl::seq<TAO_PEGTL_STRING("*DESIGN_FLOW"), pegtl::opt<RuleSpace, RuleHeaderValue>>
    {
    };

    template <>
    struct Action<RuleDesignFlow>
    {
        template <typename Input>
        static void apply(const Input &in, Spef &d)
        {
            d.design_flow = RemoveHeaderKey(in, sizeof("*DESIGN_FLOW"));
        };
    };

    struct RuleDivider : pegtl::seq<TAO_PEGTL_STRING("*DIVIDER"), pegtl::opt<RuleSpace, Divider>>
    {
    };

    struct RuleDelimiter : pegtl::seq<TAO_PEGTL_STRING("*DELIMITER"), pegtl::opt<RuleSpace, Delimiter>>
    {
    };

    struct RuleBusDelimiter : pegtl::seq<TAO_PEGTL_STRING("*BUS_DELIMITER"), pegtl::opt<RuleSpace, BusDelimiter>>
    {
    };

    struct RuleUnit : pegtl::seq<TAO_PEGTL_STRING("*"), pegtl::one<'T', 'C', 'R', 'L'>, TAO_PEGTL_STRING("_UNIT"), pegtl::must<RuleSpace, double_::rule, RuleSpace, RuleToken>>
    {
    };

    template <>
    struct Action<RuleUnit>
    {
        template <typename Input>
        static bool apply(const Input &in, Spef &d)
        {
            switch (in.peek_char(1))
            {
            case 'T':
                d.time_unit = RemoveHeaderKey(in, sizeof("*T_UNIT"));
                break;
            case 'C':
                d.capacitance_unit = RemoveHeaderKey(in, sizeof("*C_UNIT"));
                break;
            case 'R':
                d.resistance_unit = RemoveHeaderKey(in, sizeof("*R_UNIT"));
                break;
            case 'L':
                d.inductance_unit = RemoveHeaderKey(in, sizeof("*L_UNIT"));
                break;
            default:
                break;
            }
            return true;
        }
    };

    //  Name Map Section -------------------------------------------------------------------------------
    struct RuleNameMapBeg : pegtl::seq<TAO_PEGTL_STRING("*NAME_MAP"), RuleDontCare>
    {
    };

    template <>
    struct Action<RuleNameMapBeg>
    {
        template <typename Input>
        static void apply(const Input &in, Spef &d) {}
    };

    struct RuleNameMap : pegtl::seq<pegtl::not_at<TAO_PEGTL_STRING("*PORTS")>, pegtl::not_at<TAO_PEGTL_STRING("*D_NET")>, TAO_PEGTL_STRING("*"), pegtl::must<RuleToken, RuleSpace, RuleToken>>
    {
    };

    template <>
    struct Action<RuleNameMap>
    {
        template <typename Input>
        static void apply(const Input &in, Spef &d)
        {
            // Skip the '*'
            split_on_space(in.begin(), in.end(), d._tokens);
            size_t key = ::strtoul(&d._tokens[0][1], nullptr, 10);
            d.name_map.try_emplace(key, std::string{d._tokens[1]});
        }
    };

    //  Port Section ----------------------------------------------------------------------------------

    struct RulePortBeg : pegtl::seq<TAO_PEGTL_STRING("*PORTS"), RuleDontCare>
    {
    };

    template <>
    struct Action<RulePortBeg>
    {
        template <typename Input>
        static void apply(const Input &in, Spef &d) {}
    };

    struct RulePort : pegtl::seq<pegtl::not_at<TAO_PEGTL_STRING("*D_NET")>, TAO_PEGTL_STRING("*"),
                                 pegtl::must<RuleToken, RuleSpace, pegtl::must<pegtl::one<'I', 'O', 'B'>>,
                                             pegtl::star<pegtl::sor<pegtl::seq<RuleSpace, pegtl::seq<TAO_PEGTL_STRING("*C"), RuleSpace, double_::rule, RuleSpace, double_::rule>>,
                                                                    pegtl::seq<RuleSpace, pegtl::seq<TAO_PEGTL_STRING("*L"), RuleSpace, double_::rule>>,
                                                                    pegtl::seq<RuleSpace, pegtl::seq<TAO_PEGTL_STRING("*S"), RuleSpace, double_::rule, RuleSpace, double_::rule>>>>>>
    {
    };

    template <>
    struct Action<RulePort>
    {
        template <typename Input>
        static bool apply(const Input &in, Spef &d)
        {
            split_on_space(in.begin(), in.end(), d._tokens);

            d.ports.emplace_back(std::string{d._tokens[0]});

            // Set up port direction
            switch (d._tokens[1][0])
            {
            case 'O':
                d.ports.back().direction = ConnectionDirection::OUTPUT;
                break;
            case 'I':
                d.ports.back().direction = ConnectionDirection::INPUT;
                break;
            case 'B':
                d.ports.back().direction = ConnectionDirection::INOUT;
                break;
            default:
                return false;
                break;
            }

            // TODO:
            // right now we ignore the values after port direction (future work)

            //// Set up type
            // if(d._tokens.size() > 2){
            //  p.type = d._tokens[2][1];
            //}

            //// Insert values
            // for(size_t i=3; i<d._tokens.size(); i++){
            //  p.values.emplace_back(std::strtof(d._tokens[i].data(), nullptr));
            //}
            return true;
        }
    };

    //  Net Section -----------------------------------------------------------------------------------

    // RuleVar represents a token to not stop at '*' as name mapping will use * in token.
    using RuleVar = pegtl::until<pegtl::at<pegtl::sor<pegtl::space, pegtl::eof>>>;

    struct RuleConnBeg : pegtl::seq<TAO_PEGTL_STRING("*CONN")>
    {
    };

    template <>
    struct Action<RuleConnBeg>
    {
        template <typename Input>
        static void apply(const Input &in, Spef &d) {}
    };

    struct RuleConn : pegtl::seq<pegtl::sor<TAO_PEGTL_STRING("*P"), TAO_PEGTL_STRING("*I")>, RuleSpace, RuleVar, RuleSpace, pegtl::must<pegtl::one<'I', 'O', 'B'>>,

                                 pegtl::star<pegtl::sor<pegtl::seq<RuleSpace, pegtl::seq<TAO_PEGTL_STRING("*C"), RuleSpace, double_::rule, RuleSpace, double_::rule>>,

                                                        pegtl::seq<RuleSpace, pegtl::seq<TAO_PEGTL_STRING("*L"), RuleSpace, double_::rule>>,

                                                        pegtl::seq<RuleSpace, pegtl::seq<TAO_PEGTL_STRING("*D"), RuleSpace, RuleToken>>>>>
    {
    };

    template <>
    struct Action<RuleConn>
    {
        template <typename Input>
        static void apply(const Input &in, Spef &d)
        {
            auto &c = d._current_net->connections.emplace_back();

            split_on_space(in.begin(), in.end(), d._tokens);

            c.type = d._tokens[0][1] == 'P' ? ConnectionType::EXTERNAL : ConnectionType::INTERNAL;
            c.name = d._tokens[1];
            switch (d._tokens[2][0])
            {
            case 'I':
                c.direction = ConnectionDirection::INPUT;
                break;
            case 'O':
                c.direction = ConnectionDirection::OUTPUT;
                break;
            default:
                c.direction = ConnectionDirection::INOUT;
                break;
            }

            for (size_t i = 3; i < d._tokens.size(); i++)
            {
                if (d._tokens[i].compare("*C") == 0)
                {
                    c.coordinate = std::make_pair(std::strtof(d._tokens[i + 1].data(), nullptr), std::strtof(d._tokens[i + 2].data(), nullptr));
                    i += 2;
                }
                else if (d._tokens[i].compare("*L") == 0)
                {
                    c.load = std::strtof(d._tokens[i + 1].data(), nullptr);
                    i += 1;
                }
                else if (d._tokens[i].compare("*D") == 0)
                {
                    c.driving_cell = d._tokens[i + 1];
                    i += 1;
                }
                else
                {
                    throw pegtl::parse_error("Unrecognized token in CONN section", in);
                }
            }
        }
    };

    struct RuleCapBeg : pegtl::seq<TAO_PEGTL_STRING("*CAP")>
    {
    };

    template <>
    struct Action<RuleCapBeg>
    {
        template <typename Input>
        static void apply(const Input &in, Spef &d) {}
    };

    struct RuleCapGround : pegtl::seq<pegtl::plus<pegtl::digit>, RuleSpace, RuleVar, RuleSpace, RuleDouble>
    {
    };

    template <>
    struct Action<RuleCapGround>
    {
        template <typename Input>
        static void apply(const Input &in, Spef &d)
        {
            split_on_space(in.begin(), in.end(), d._tokens);
            // Ignore the first numbering token
            d._current_net->caps.emplace_back(std::forward_as_tuple(d._tokens[1], "", std::strtof(d._tokens[2].data(), nullptr)));
        }
    };

    struct RuleCapCouple : pegtl::seq<pegtl::plus<pegtl::digit>, RuleSpace, RuleVar, RuleSpace, RuleVar, RuleSpace, RuleDouble>
    {
    };

    template <>
    struct Action<RuleCapCouple>
    {
        template <typename Input>
        static void apply(const Input &in, Spef &d)
        {
            split_on_space(in.begin(), in.end(), d._tokens);
            d._current_net->caps.emplace_back(std::forward_as_tuple(d._tokens[1], d._tokens[2], std::strtof(d._tokens[3].data(), nullptr)));
        }
    };

    struct RuleResBeg : pegtl::seq<TAO_PEGTL_STRING("*RES")>
    {
    };

    template <>
    struct Action<RuleResBeg>
    {
        template <typename Input>
        static void apply(const Input &in, Spef &d) {}
    };

    struct RuleRes : pegtl::seq<pegtl::plus<pegtl::digit>, RuleSpace, RuleVar, RuleSpace, RuleVar, RuleSpace, double_::rule>
    {
    };

    template <>
    struct Action<RuleRes>
    {
        template <typename Input>
        static void apply(const Input &in, Spef &d)
        {
            split_on_space(in.begin(), in.end(), d._tokens);
            d._current_net->ress.emplace_back(std::forward_as_tuple(d._tokens[1], d._tokens[2], std::strtof(d._tokens[3].data(), nullptr)));
        }
    };

    struct RuleNetBeg : pegtl::seq<TAO_PEGTL_STRING("*D_NET"), pegtl::must<RuleSpace, RuleVar, RuleSpace, double_::rule>>
    {
    };

    template <>
    struct Action<RuleNetBeg>
    {
        template <typename Input>
        static void apply(const Input &in, Spef &d)
        {
            split_on_space(in.begin(), in.end(), d._tokens);
            d._current_net = &(d.nets.emplace_back());
            d._current_net->name = d._tokens[1];
            d._current_net->lcap = std::strtof(d._tokens[2].data(), nullptr);
        }
    };

    struct RuleNetEnd : pegtl::seq<TAO_PEGTL_STRING("*END")>
    {
    };

    template <>
    struct Action<RuleNetEnd>
    {
        template <typename Input>
        static void apply(const Input &in, Spef &d) {}
    };

    struct RuleInputEnd : pegtl::star<pegtl::any>
    {
    };

    template <>
    struct Action<RuleInputEnd>
    {
        template <typename Input>
        static void apply(const Input &in, Spef &d)
        {
            if (in.size() != 0)
            {
                throw pegtl::parse_error("Unrecognized token", in);
            }
        }
    };

    // Spef Top Rule ----------------------------------------------------------------------------------

    struct RuleSpef
        : pegtl::must<pegtl::star<pegtl::space>, // strip leading space
                      pegtl::rep_max<10, pegtl::sor<pegtl::seq<RuleStandard, RuleDontCare>, pegtl::seq<RuleDate, RuleDontCare>, pegtl::seq<RuleVendor, RuleDontCare>, pegtl::seq<RuleProgram, RuleDontCare>,
                                                    pegtl::seq<RuleVersion, RuleDontCare>, pegtl::seq<RuleDesignFlow, RuleDontCare>, pegtl::seq<RuleDesign, RuleDontCare>,
                                                    pegtl::seq<RuleDivider, RuleDontCare>, pegtl::seq<RuleDelimiter, RuleDontCare>, pegtl::seq<RuleBusDelimiter, RuleDontCare>>>,

                      pegtl::rep_max<4, pegtl::seq<RuleUnit, RuleDontCare>>,

                      pegtl::opt<RuleNameMapBeg, pegtl::star<pegtl::seq<RuleNameMap, RuleDontCare>>>,

                      pegtl::opt<RulePortBeg, pegtl::star<pegtl::seq<RulePort, RuleDontCare>>>,

                      pegtl::star<pegtl::if_must<RuleNetBeg, RuleDontCare, pegtl::opt<pegtl::seq<RuleConnBeg, RuleDontCare>, pegtl::star<pegtl::seq<RuleConn, RuleDontCare>>>,
                                                 pegtl::opt<pegtl::seq<RuleCapBeg, RuleDontCare>, pegtl::star<pegtl::seq<pegtl::sor<RuleCapGround, RuleCapCouple>, RuleSpace>>>,
                                                 pegtl::opt<pegtl::seq<RuleResBeg, RuleDontCare>, pegtl::star<pegtl::seq<RuleRes, RuleSpace>>>, RuleNetEnd, RuleDontCare>>,
                      pegtl::star<pegtl::space>, // strip trailing spaces
                      RuleInputEnd               // can't have anything more
                      >
    {
    };

    // RuleHeaderOnly: 只解析header和name_map，不解析nets
    struct RuleHeaderOnly
        : pegtl::must<pegtl::star<pegtl::space>,
                      pegtl::rep_max<10, pegtl::sor<pegtl::seq<RuleStandard, RuleDontCare>, pegtl::seq<RuleDate, RuleDontCare>, pegtl::seq<RuleVendor, RuleDontCare>, pegtl::seq<RuleProgram, RuleDontCare>,
                                                    pegtl::seq<RuleVersion, RuleDontCare>, pegtl::seq<RuleDesignFlow, RuleDontCare>, pegtl::seq<RuleDesign, RuleDontCare>,
                                                    pegtl::seq<RuleDivider, RuleDontCare>, pegtl::seq<RuleDelimiter, RuleDontCare>, pegtl::seq<RuleBusDelimiter, RuleDontCare>>>,
                      pegtl::rep_max<4, pegtl::seq<RuleUnit, RuleDontCare>>,
                      pegtl::opt<RuleNameMapBeg, pegtl::star<pegtl::seq<RuleNameMap, RuleDontCare>>>,
                      pegtl::opt<RulePortBeg, pegtl::star<pegtl::seq<RulePort, RuleDontCare>>>,
                      pegtl::star<pegtl::any>>  // 跳过剩余部分
    {
    };

    // RuleSingleNet: 解析单个net
    struct RuleSingleNet
        : pegtl::must<pegtl::star<pegtl::space>,
                      RuleNetBeg, RuleDontCare,
                      pegtl::opt<pegtl::seq<RuleConnBeg, RuleDontCare>, pegtl::star<pegtl::seq<RuleConn, RuleDontCare>>>,
                      pegtl::opt<pegtl::seq<RuleCapBeg, RuleDontCare>, pegtl::star<pegtl::seq<pegtl::sor<RuleCapGround, RuleCapCouple>, RuleSpace>>>,
                      pegtl::opt<pegtl::seq<RuleResBeg, RuleDontCare>, pegtl::star<pegtl::seq<RuleRes, RuleSpace>>>,
                      RuleNetEnd, pegtl::star<pegtl::space>>
    {
    };

    // Error control ----------------------------------------------------------------------------------

    template <typename Rule>
    struct Control : tao::pegtl::normal<Rule>
    {
        static const std::string error_message;

        template <typename Input, typename... States>
        static void raise(const Input &in, States &&...)
        {
            throw tao::pegtl::parse_error(error_message, in);
        }
    };

    template <typename T>
    inline const std::string Control<T>::error_message = "Fail to match the Spef rule: " + tao::pegtl::internal::demangle<T>();

    // API for parsing --------------------------------------------------------------------------------

    // RAII类管理mmap内存生命周期
    class MmappedFile {
    public:
        std::string_view view;
    #ifdef _WIN32
        HANDLE hFile, hMapping;
        void* mapped;
    #else
        int fd;
        void* mapped;
        size_t size;
    #endif

        MmappedFile(const std::experimental::filesystem::path &p) : mapped(nullptr) {
    #ifdef _WIN32
            hFile = CreateFileA(p.string().c_str(), GENERIC_READ, 
                               FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
            if (hFile == INVALID_HANDLE_VALUE) return;

            DWORD dwFileSize = GetFileSize(hFile, NULL);
            if (dwFileSize == INVALID_FILE_SIZE) { CloseHandle(hFile); return; }

            hMapping = CreateFileMapping(hFile, NULL, PAGE_READONLY, 0, 0, NULL);
            if (hMapping == NULL) { CloseHandle(hFile); return; }

            mapped = MapViewOfFile(hMapping, FILE_MAP_READ, 0, 0, 0);
            if (mapped) {
                view = std::string_view(static_cast<char*>(mapped), dwFileSize);
            }
    #else
            fd = open(p.c_str(), O_RDONLY);
            if (fd == -1) return;
            
            struct stat st;
            fstat(fd, &st);
            size = st.st_size;
            
            mapped = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
            close(fd);
            
            if (mapped != MAP_FAILED) {
                view = std::string_view(static_cast<char*>(mapped), size);
            } else {
                mapped = nullptr;
            }
    #endif
        }

        ~MmappedFile() {
    #ifdef _WIN32
            if (mapped) UnmapViewOfFile(mapped);
            if (hMapping) CloseHandle(hMapping);
            if (hFile && hFile != INVALID_HANDLE_VALUE) CloseHandle(hFile);
    #else
            if (mapped && mapped != MAP_FAILED) munmap(mapped, size);
    #endif
        }

        bool valid() const { return mapped != nullptr; }
    };

    // Procedure:: file_to_memory读取文件到string（使用mmap优化）
    inline std::string file_to_memory(const std::experimental::filesystem::path &p)
    {
        if (not std::experimental::filesystem::exists(p))
        {
            return "";
        }

        MmappedFile mf(p);
        if (!mf.valid()) {
            // 降级到普通文件读取
            std::ifstream ifs(p);
            ifs.seekg(0, std::ios::end);
            std::string buffer;
            buffer.resize(ifs.tellg());
            ifs.seekg(0);
            ifs.read(&buffer[0], buffer.size());
            ifs.close();
            return buffer;
        }
        return std::string(mf.view);
    }

    // Function: read
    inline bool Spef::read(const std::experimental::filesystem::path &p)
    {
        auto buffer{file_to_memory(p)};
        if (buffer.empty())
        {
            return false;
        }
        
        // Remove comments
#ifdef _MEASURE_
        auto t_comment_start = std::chrono::high_resolution_clock::now();
#endif
        detail::remove_comments(buffer, true, 0);
#ifdef _MEASURE_
        auto t_comment_end = std::chrono::high_resolution_clock::now();
        auto ms_comment = std::chrono::duration_cast<std::chrono::milliseconds>(t_comment_end - t_comment_start).count();
        std::cout << "[TIME] Remove comments: " << ms_comment << " ms" << std::endl;
#endif

        // 查找第一个*D_NET的位置
#ifdef _MEASURE_
        auto t_find_start = std::chrono::high_resolution_clock::now();
#endif
        size_t dnet_pos = buffer.find("\n*D_NET");
        if (dnet_pos == std::string::npos) {
            dnet_pos = buffer.find("*D_NET");  // 可能在文件开头
        }
#ifdef _MEASURE_
        auto t_find_end = std::chrono::high_resolution_clock::now();
        auto ms_find = std::chrono::duration_cast<std::chrono::microseconds>(t_find_end - t_find_start).count();
        std::cout << "[TIME] Find *D_NET position: " << ms_find << " us" << std::endl;
#endif

        // 如果没有nets，使用原始串行解析
        if (dnet_pos == std::string::npos) {
            tao::pegtl::memory_input<pegtl::tracking_mode::LAZY> in(buffer, "");
            try {
                tao::pegtl::parse<spef::RuleSpef, spef::Action, spef::Control>(in, *this);
                return true;
            }
            catch (const tao::pegtl::parse_error &e) {
                const auto &p = e.positions.front();
                error = Error{in.line_as_string(p), p.line, p.byte_in_line};
                return false;
            }
        }

        // ====== 并行chunk解析策略 ======
#ifdef _MEASURE_
        std::cout << "[INFO] Using parallel chunk parsing for nets (5 threads)" << std::endl;
        auto t_start = std::chrono::high_resolution_clock::now();
#endif

        // Step1和Step2并行执行：解析header & 扫描net位置
        std::vector<parallel::NetLocation> net_locations;
        size_t dnet_pos_found = std::string::npos;
        
        #pragma omp parallel sections
        {
            // 步骤1: 解析header和name_map（到*D_NET之前）
            #pragma omp section
            {
#ifdef _MEASURE_
                auto t1 = std::chrono::high_resolution_clock::now();
#endif
                
                // 查找*D_NET位置用于header分割
                size_t find_pos = buffer.find("\n*D_NET");
                if (find_pos == std::string::npos) {
                    find_pos = buffer.find("*D_NET");
                }
                
                if (find_pos != std::string::npos) {
                    std::string header_part = buffer.substr(0, find_pos);
                    header_part += "\n";
                    tao::pegtl::memory_input<pegtl::tracking_mode::LAZY> header_in(header_part, "");
                    
                    try {
                        tao::pegtl::parse<spef::RuleHeaderOnly, spef::Action, spef::Control>(header_in, *this);
                    }
                    catch (const tao::pegtl::parse_error &e) {
                        const auto &p = e.positions.front();
                        error = Error{header_in.line_as_string(p), p.line, p.byte_in_line};
                    }
                    
                    #pragma omp critical
                    dnet_pos_found = find_pos;
                }
                
#ifdef _MEASURE_
                auto t2 = std::chrono::high_resolution_clock::now();
                auto ms1 = std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count();
                std::cout << "[TIME] Step1 (parse header): " << ms1 << " ms" << std::endl;
#endif
            }
            
            // 步骤2: 扫描所有net的位置
            #pragma omp section
            {
#ifdef _MEASURE_
                auto t2_start = std::chrono::high_resolution_clock::now();
#endif
                net_locations = parallel::scan_net_locations(buffer);
#ifdef _MEASURE_
                auto t2_end = std::chrono::high_resolution_clock::now();
                auto ms2 = std::chrono::duration_cast<std::chrono::milliseconds>(t2_end - t2_start).count();
                std::cout << "[TIME] Step2 (scan locations): " << ms2 << " ms, found " << net_locations.size() << " nets" << std::endl;
#endif
            }
        }
        
        if (net_locations.empty()) {
            return true;
        }

        // 步骤3: 分chunk并行解析（用4个线程）
        const int num_threads = 4;
        const size_t nets_per_chunk = (net_locations.size() + num_threads - 1) / num_threads;
        
        // 预分配所有nets空间
        nets.resize(net_locations.size());
        
        std::atomic<size_t> parse_errors{0};
        
#ifdef _MEASURE_
        // Step3详细计时：substr、parse、move三个部分
        std::atomic<long long> total_substr_us{0};
        std::atomic<long long> total_parse_us{0};
        std::atomic<long long> total_move_us{0};
#endif
        
        #pragma omp parallel num_threads(num_threads)
        {
            int tid = omp_get_thread_num();
            size_t start_net = tid * nets_per_chunk;
            size_t end_net = std::min(start_net + nets_per_chunk, net_locations.size());
            
            if (start_net < net_locations.size()) {
                // 为每个线程创建临时Spef对象（复用解析多个nets）
                Spef temp_spef;
                temp_spef.name_map = this->name_map;  // 共享name_map（只读）
                
                // 解析这个chunk的所有nets
                for (size_t i = start_net; i < end_net; ++i) {
                    const auto& loc = net_locations[i];
                    
#ifdef _MEASURE_
                    auto t_substr_start = std::chrono::high_resolution_clock::now();
#endif
                    // 提取net字符串
                    std::string net_str = buffer.substr(loc.start_pos, loc.end_pos - loc.start_pos);
#ifdef _MEASURE_
                    auto t_substr_end = std::chrono::high_resolution_clock::now();
                    auto us_substr = std::chrono::duration_cast<std::chrono::microseconds>(t_substr_end - t_substr_start).count();
                    total_substr_us += us_substr;
#endif
                    
                    tao::pegtl::memory_input<pegtl::tracking_mode::LAZY> net_in(net_str, "");
                    
                    try {
                        temp_spef.nets.clear();
                        
#ifdef _MEASURE_
                        auto t_parse_start = std::chrono::high_resolution_clock::now();
#endif
                        tao::pegtl::parse<spef::RuleSingleNet, spef::Action, spef::Control>(net_in, temp_spef);
#ifdef _MEASURE_
                        auto t_parse_end = std::chrono::high_resolution_clock::now();
                        auto us_parse = std::chrono::duration_cast<std::chrono::microseconds>(t_parse_end - t_parse_start).count();
                        total_parse_us += us_parse;
#endif
                        
                        if (!temp_spef.nets.empty()) {
#ifdef _MEASURE_
                            auto t_move_start = std::chrono::high_resolution_clock::now();
#endif
                            nets[i] = std::move(temp_spef.nets[0]);
#ifdef _MEASURE_
                            auto t_move_end = std::chrono::high_resolution_clock::now();
                            auto us_move = std::chrono::duration_cast<std::chrono::microseconds>(t_move_end - t_move_start).count();
                            total_move_us += us_move;
#endif
                        }
                    }
                    catch (const std::exception& e) {
                        parse_errors++;
                    }
                }
            }
        }
        
#ifdef _MEASURE_
        auto t4 = std::chrono::high_resolution_clock::now();
        auto ms3 = std::chrono::duration_cast<std::chrono::milliseconds>(t4 - t_start).count();
        std::cout << "[TIME] Step3 (parallel parse with 5 threads): " << ms3 << " ms" << std::endl;
        std::cout << "[TIME]   - substr total: " << total_substr_us.load() / 1000.0 << " ms" << std::endl;
        std::cout << "[TIME]   - parse total: " << total_parse_us.load() / 1000.0 << " ms" << std::endl;
        std::cout << "[TIME]   - move total: " << total_move_us.load() / 1000.0 << " ms" << std::endl;

        if (parse_errors > 0) {
            std::cout << "[WARN] " << parse_errors << " nets failed to parse" << std::endl;
        }
#endif

        return true;
    }

    // Procedure: replace the keys in str by the values in the mapping
    inline void expand_string(std::string &str, const std::unordered_map<size_t, std::string> &mapping)
    {
        if (str.empty() or mapping.empty())
            return;
        size_t beg{str.size()};
        size_t end{0};
        size_t last;
        size_t key;
        char *endptr{nullptr};
        while (beg > 0)
        {
            last = beg;
            --beg;
            if (beg = str.find_last_of('*', beg); beg != std::string::npos)
            {
                end = beg + 1;
                while (end < last and std::isdigit(str[end]))
                {
                    ++end;
                }
                endptr = (&str.data()[end]);
                key = ::strtoul(&str.data()[beg + 1], &(endptr), 10);
                if (mapping.find(key) != mapping.end())
                {
                    str.replace(beg, end - beg, mapping.at(key));
                }
            }
            else
            {
                break;
            }
        }
    }

    // Procedure: expand all mappings in the SPEF file
    inline void Spef::expand_name()
    {

        if (name_map.empty())
        {
            return;
        }

        for (auto &p : ports)
        {
            expand_name(p);
        }

        for (auto &n : nets)
        {
            expand_name(n);
        }

        name_map.clear();
    }

    // Procedure: expand the mapping in port name
    inline void Spef::expand_name(Port &port)
    {
        expand_string(port.name, name_map);
    }

    // Procedure: expand the mapping in a net, including the net name, pin names in each section
    inline void Spef::expand_name(Net &net)
    {

        expand_string(net.name, name_map);
        for (auto &c : net.connections)
        {
            expand_string(c.name, name_map);
            expand_string(c.driving_cell, name_map);
        }

        for (auto &t : net.caps)
        {
            expand_string(std::get<0>(t), name_map);
            expand_string(std::get<1>(t), name_map);
        }

        for (auto &r : net.ress)
        {
            expand_string(std::get<0>(r), name_map);
            expand_string(std::get<1>(r), name_map);
        }
    }

}; // namespace spef
