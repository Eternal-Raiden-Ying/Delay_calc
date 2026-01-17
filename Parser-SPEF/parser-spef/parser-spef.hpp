#pragma once

/* Eternal:: Hand-Written Fast Parser + Zero-Copy
   - Replaced PEGTL with direct pointer scanning for robustness and speed.
   - Uses mmap + string_view for minimal memory allocation.
*/

#include <algorithm>
#include <charconv> // std::from_chars
#include <cmath>
#include <cstring>
#include <experimental/filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <vector>
#include <memory>

#ifdef _WIN32
    #include <windows.h>
#else
    #include <sys/mman.h>
    #include <fcntl.h>
    #include <sys/stat.h>
    #include <unistd.h>
#endif

// We don't need parallel_parse anymore for this linear scanner approach
// But we keep the file structure compatible

namespace spef
{

// ------------------------------------------------------------------------------------------------
// Helper: Fast Float Parsing
// ------------------------------------------------------------------------------------------------
inline float fast_stof(const char* p, const char* end)
{
    float result = 0.0f;
    // Skip leading whitespace if any (though usually pre-skipped)
    while (p < end && (*p == ' ' || *p == '\t')) ++p;
    if (p >= end) return 0.0f;
    
    auto [ptr, ec] = std::from_chars(p, end, result);
    if (ec == std::errc()) return result;
    // Fallback if from_chars fails (rare)
    return std::strtof(p, nullptr);
}

// ------------------------------------------------------------------------------------------------
// RAII Memory Mapped File Wrapper
// ------------------------------------------------------------------------------------------------
class MmappedFile {
public:
    std::string_view view;
    void* mapped = nullptr;
#ifdef _WIN32
    HANDLE hFile = INVALID_HANDLE_VALUE;
    HANDLE hMapping = NULL;
#else
    int fd = -1;
    size_t size = 0;
#endif

    MmappedFile() = default;

    MmappedFile(const std::experimental::filesystem::path &p) {
#ifdef _WIN32
        hFile = CreateFileA(p.string().c_str(), GENERIC_READ, 
                           FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile == INVALID_HANDLE_VALUE) return;

        LARGE_INTEGER fileSize;
        if (!GetFileSizeEx(hFile, &fileSize)) { CloseHandle(hFile); hFile = INVALID_HANDLE_VALUE; return; }
        if (fileSize.QuadPart == 0) { CloseHandle(hFile); hFile = INVALID_HANDLE_VALUE; return; }

        hMapping = CreateFileMapping(hFile, NULL, PAGE_READONLY, 0, 0, NULL);
        if (hMapping == NULL) { CloseHandle(hFile); hFile = INVALID_HANDLE_VALUE; return; }

        mapped = MapViewOfFile(hMapping, FILE_MAP_READ, 0, 0, 0);
        if (mapped) {
            view = std::string_view(static_cast<char*>(mapped), static_cast<size_t>(fileSize.QuadPart));
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
        if (!mapped) return;
#ifdef _WIN32
        UnmapViewOfFile(mapped);
        if (hMapping) CloseHandle(hMapping);
        if (hFile != INVALID_HANDLE_VALUE) CloseHandle(hFile);
#else
        if (mapped != MAP_FAILED) munmap(mapped, size);
#endif
    }

    bool valid() const { return mapped != nullptr; }
};

// ------------------------------------------------------------------------------------------------
// Data Structures (Using string_view for Zero-Copy)
// ------------------------------------------------------------------------------------------------

enum class ConnectionType { INTERNAL, EXTERNAL };
enum class ConnectionDirection { INPUT, OUTPUT, INOUT };

struct Port
{
    Port() = default;
    Port(std::string_view s) : name(s) {}
    std::string_view name;
    ConnectionDirection direction;
};

struct Connection
{
    std::string_view name;
    ConnectionType type;
    ConnectionDirection direction;
    std::optional<std::pair<float, float>> coordinate;
    std::optional<float> load;
    std::string_view driving_cell;
};

struct Net
{
    std::string_view name;
    float lcap = 0.0f;
    std::vector<Connection> connections;
    // tuple<node1, node2, value>
    std::vector<std::tuple<std::string_view, std::string_view, float>> caps;
    std::vector<std::tuple<std::string_view, std::string_view, float>> ress;

    Net() = default;
    Net(std::string_view s, float f) : name(s), lcap(f) {}
    
    // Helper to scale values if needed
    void scale_capacitance(float scale) {
        lcap *= scale;
        for (auto &c : connections) if (c.load) *c.load *= scale;
        for (auto &t : caps) std::get<2>(t) *= scale;
    }
};

struct Spef
{
    struct Error
    {
        std::string line;
        size_t line_number;
        size_t byte_in_line;
        
        friend std::ostream& operator<<(std::ostream& os, const Error& e) {
            os << "Error at line " << e.line_number << ": " << e.line;
            return os;
        }
    };

    // Header info (std::string because they are small and parsed once)
    std::string standard, design_name, date, vendor, program, version, design_flow;
    std::string divider, delimiter, bus_delimiter;
    std::string time_unit, capacitance_unit, resistance_unit, inductance_unit;

    std::unordered_map<size_t, std::string_view> name_map;
    std::vector<Port> ports;
    std::vector<Net> nets;

    std::optional<Error> error;
    std::shared_ptr<MmappedFile> _file_storage;

    void clear();
    bool read(const std::experimental::filesystem::path& filepath);

    // Helpers for parsing
private:
    static inline const char* skip_spaces(const char* p, const char* end) {
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) ++p;
        return p;
    }
    
    static inline const char* skip_line(const char* p, const char* end) {
        while (p < end && *p != '\n' && *p != '\r') ++p;
        while (p < end && (*p == '\n' || *p == '\r')) ++p;
        return p;
    }

    static inline const char* read_token_view(const char* p, const char* end, std::string_view& token) {
        p = skip_spaces(p, end);
        const char* start = p;
        while (p < end && *p > 32) ++p; // > 32 means not space/tab/newline/control
        if (p > start) token = std::string_view(start, p - start);
        else token = std::string_view();
        return p;
    }
    
    static inline const char* read_token_view_raw(const char* p, const char* end, const char*& out_start, size_t& out_len) {
        p = skip_spaces(p, end);
        out_start = p;
        while (p < end && *p > 32) ++p;
        out_len = p - out_start;
        return p;
    }
};

inline void Spef::clear() {
    standard.clear(); design_name.clear(); date.clear(); vendor.clear();
    program.clear(); version.clear(); design_flow.clear(); divider.clear();
    delimiter.clear(); bus_delimiter.clear(); time_unit.clear();
    capacitance_unit.clear(); resistance_unit.clear(); inductance_unit.clear();
    name_map.clear(); ports.clear(); nets.clear(); error.reset();
    _file_storage.reset();
}

// ------------------------------------------------------------------------------------------------
// The Core Parser Logic (Hand-Written Scanner)
// ------------------------------------------------------------------------------------------------
inline bool Spef::read(const std::experimental::filesystem::path& filepath)
{
    clear();
    
    // Map file
    _file_storage = std::make_shared<MmappedFile>(filepath);
    if (!_file_storage->valid()) {
        error = Error{"Cannot open file or empty file", 0, 0};
        return false;
    }
    
    std::string_view buffer = _file_storage->view;
    const char* p = buffer.data();
    const char* end = buffer.data() + buffer.size();
    
    nets.reserve(100000); // Pre-allocate to reduce resize overhead
    name_map.reserve(200000);
    
    Net* current_net = nullptr;
    enum class Section { NONE, HEADER, NAME_MAP, PORTS, NET_CONN, NET_CAP, NET_RES };
    Section section = Section::HEADER;
    
    std::string_view token;
    const char* tok_start;
    size_t tok_len;
    
    while (p < end)
    {
        p = skip_spaces(p, end);
        if (p >= end) break;
        
        // Comment skipping
        if (p + 1 < end && p[0] == '/' && p[1] == '/') { 
            p = skip_line(p, end); 
            continue; 
        }
        
        if (*p == '*')
        {
            const char* kw_start = p++;
            // Read keyword until space
            while (p < end && *p > 32) ++p;
            std::string_view keyword(kw_start, p - kw_start);
            
            if (keyword == "*D_NET") {
                // Format: *D_NET net_name lcap
                p = read_token_view(p, end, token); // net_name
                std::string_view net_name = token;
                
                p = read_token_view_raw(p, end, tok_start, tok_len); // lcap
                float lcap = fast_stof(tok_start, tok_start + tok_len);
                
                nets.emplace_back(net_name, lcap);
                current_net = &nets.back();
                
                // Heuristic reservation
                current_net->caps.reserve(16);
                current_net->ress.reserve(16);
                current_net->connections.reserve(8);
                
                section = Section::NONE; // Wait for *CONN / *CAP / *RES
            }
            else if (keyword == "*CONN") { section = Section::NET_CONN; }
            else if (keyword == "*CAP") { section = Section::NET_CAP; }
            else if (keyword == "*RES") { section = Section::NET_RES; }
            else if (keyword == "*END") { current_net = nullptr; section = Section::NONE; }
            
            else if (keyword == "*NAME_MAP") { section = Section::NAME_MAP; }
            else if (keyword == "*PORTS") { section = Section::PORTS; }
            
            else if (keyword == "*P" || keyword == "*I") {
                // Connection line: *P node_name Direction ...
                if (current_net && section == Section::NET_CONN) {
                    auto& conn = current_net->connections.emplace_back();
                    conn.type = (keyword == "*P") ? ConnectionType::EXTERNAL : ConnectionType::INTERNAL;
                    
                    p = read_token_view(p, end, conn.name); // node_name
                    p = read_token_view(p, end, token);     // Direction (I/O/B)
                    
                    if (!token.empty()) {
                        char c = token[0];
                        if (c == 'I') conn.direction = ConnectionDirection::INPUT;
                        else if (c == 'O') conn.direction = ConnectionDirection::OUTPUT;
                        else if (c == 'B') conn.direction = ConnectionDirection::INOUT;
                    }
                    
                    // Parse optional attributes: *C coord, *L load, *D driving_cell
                    // Just scan the rest of the line for now as per simple parser logic
                    // Or implement robust attribute parsing if needed. 
                    // Your provided simple parser skipped the line, let's try to grab *C or *L if present.
                    // For speed and simplicity based on your example, we skip rest of line unless strict parsing needed.
                    // BUT, original PEGTL logic parsed *C and *L. Let's add basic support:
                    
                    while (p < end && *p != '\n' && *p != '\r') {
                        // Peek next token without advancing line
                        const char* next_tok = p;
                        while(next_tok < end && (*next_tok == ' ' || *next_tok == '\t')) next_tok++;
                        if (next_tok >= end || *next_tok == '\n' || *next_tok == '\r') break;
                        
                        if (*next_tok == '*') {
                            if (next_tok[1] == 'C') { // *C x y
                                p = next_tok + 2; 
                                p = read_token_view_raw(p, end, tok_start, tok_len); float x = fast_stof(tok_start, tok_start+tok_len);
                                p = read_token_view_raw(p, end, tok_start, tok_len); float y = fast_stof(tok_start, tok_start+tok_len);
                                conn.coordinate = {x, y};
                            }
                            else if (next_tok[1] == 'L') { // *L load
                                p = next_tok + 2;
                                p = read_token_view_raw(p, end, tok_start, tok_len);
                                conn.load = fast_stof(tok_start, tok_start+tok_len);
                            }
                            else if (next_tok[1] == 'D') { // *D cell
                                p = next_tok + 2;
                                p = read_token_view(p, end, token);
                                conn.driving_cell = token;
                            }
                            else {
                                // Unknown attribute, skip token
                                p = next_tok;
                                while(p < end && *p > 32) p++;
                            }
                        } else {
                            // Not an attribute key, skip
                             while(p < end && *p > 32) p++;
                        }
                        // Advance to next token space
                        while(p < end && (*p == ' ' || *p == '\t')) p++;
                    }
                    p = skip_line(p, end);
                }
                else {
                    p = skip_line(p, end); 
                }
            }
            // Header parsing
            else if (keyword == "*SPEF") { p = read_token_view(p, end, token); /*standard*/ }
            else if (keyword == "*DESIGN") { p = read_token_view(p, end, token); /*design*/ }
            // ... (Other headers can be parsed similarly if needed)
            
            else if (section == Section::NAME_MAP) {
                // *key name
                // keyword is the key (e.g. *123)
                if (keyword.size() > 1 && std::isdigit(keyword[1])) {
                    size_t id = 0;
                    std::from_chars(keyword.data() + 1, keyword.data() + keyword.size(), id);
                    p = read_token_view(p, end, token); // name
                    name_map[id] = token;
                }
            }
            else if (section == Section::PORTS) {
                // *port_name Direction
                // keyword is the port name (e.g. *clk)
                Port port;
                if (keyword.size() > 1) port.name = keyword.substr(1); // remove *
                else port.name = keyword; // fallback
                
                p = read_token_view(p, end, token);
                if (!token.empty()) {
                    char c = token[0];
                    if (c == 'I') port.direction = ConnectionDirection::INPUT;
                    else if (c == 'O') port.direction = ConnectionDirection::OUTPUT;
                    else if (c == 'B') port.direction = ConnectionDirection::INOUT;
                }
                p = skip_line(p, end);
                ports.push_back(std::move(port));
            }
            else {
                // Unknown keyword, skip line
                p = skip_line(p, end);
            }
        }
        else if (std::isdigit(*p))
        {
            // Numeric line: 1 node1 val  OR  1 node1 node2 val
            if (current_net) {
                // Skip the index number (e.g. "1" in "1 *2:A ...")
                while (p < end && std::isdigit(*p)) ++p;
                
                p = read_token_view(p, end, token); // First node (node1)
                std::string_view node1 = token;
                
                p = read_token_view_raw(p, end, tok_start, tok_len); // Next token (node2 OR val)
                
                // Check if next token is a number (start with digit, -, +, .)
                bool is_number = (tok_len > 0 && (std::isdigit(*tok_start) || *tok_start == '-' || *tok_start == '+' || *tok_start == '.'));
                
                if (section == Section::NET_CAP) {
                    if (is_number) {
                        // Ground cap: index node1 val
                        float cap_val = fast_stof(tok_start, tok_start + tok_len);
                        current_net->caps.emplace_back(node1, std::string_view{}, cap_val);
                    } else {
                        // Coupled cap: index node1 node2 val
                        std::string_view node2(tok_start, tok_len);
                        
                        p = read_token_view_raw(p, end, tok_start, tok_len); // val
                        float cap_val = fast_stof(tok_start, tok_start + tok_len);
                        current_net->caps.emplace_back(node1, node2, cap_val);
                    }
                }
                else if (section == Section::NET_RES) {
                    // Resistor: index node1 node2 val
                    std::string_view node2(tok_start, tok_len);
                    
                    p = read_token_view_raw(p, end, tok_start, tok_len); // val
                    float res_val = fast_stof(tok_start, tok_start + tok_len);
                    current_net->ress.emplace_back(node1, node2, res_val);
                }
            } else {
                p = skip_line(p, end);
            }
        }
        else {
            // Not starting with * or digit, skip line
            p = skip_line(p, end);
        }
    }
    
    return true;
}

} // namespace spef