#pragma once

#include <string_view> // Change string to string_view
#include <vector>
#include <omp.h>
#include <algorithm>

namespace spef {
namespace parallel {

struct NetLocation {
    size_t start_pos;
    size_t end_pos;
};

// Optimization: Use string_view to avoid copying the buffer
inline std::vector<NetLocation> scan_net_locations(std::string_view buffer)
{
    std::vector<NetLocation> locations;
    locations.reserve(20000); 
    
    size_t pos = 0;
    const size_t buf_size = buffer.size();
    
    // Quick scan using find
    while (pos < buf_size)
    {
        // string_view::find is efficient
        pos = buffer.find("*D_NET", pos);
        if (pos == std::string_view::npos) break;
        
        // Check for start of line
        bool at_line_start = (pos == 0) || (buffer[pos-1] == '\n');
        if (!at_line_start) {
            pos += 6;
            continue;
        }
        
        size_t start_pos = pos;
        
        // Find end
        pos = buffer.find("\n*END", pos);
        if (pos == std::string_view::npos) {
            locations.push_back({start_pos, buf_size});
            break;
        }
        
        pos += 5; // Skip \n*END
        // Simple scan for next newline
        while (pos < buf_size && buffer[pos] != '\n') {
            pos++;
        }
        if (pos < buf_size) pos++;
        
        locations.push_back({start_pos, pos});
    }
    
    return locations;
}

} // namespace parallel
} // namespace spef