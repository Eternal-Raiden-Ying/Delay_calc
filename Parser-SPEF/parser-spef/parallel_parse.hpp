#pragma once

#include <string>
#include <vector>
#include <omp.h>
#include <algorithm>

namespace spef {
namespace parallel {

// Net在buffer中的位置信息
struct NetLocation {
    size_t start_pos;        // *D_NET的位置
    size_t end_pos;          // 包含*END后的换行
};

// 预扫描：找到所有*D_NET和对应*END的位置
// 这个函数会被串行调用一次
inline std::vector<NetLocation> scan_net_locations(const std::string& buffer)
{
    std::vector<NetLocation> locations;
    locations.reserve(10000);  // 预估~10k nets
    
    size_t pos = 0;
    const size_t buf_size = buffer.size();
    
    while (pos < buf_size)
    {
        // 查找下一个 *D_NET（必须在行首或前面是空白）
        pos = buffer.find("*D_NET", pos);
        if (pos == std::string::npos) break;
        
        // 验证是否在行首（前面是\n或者是文件开头）
        bool at_line_start = (pos == 0) || (buffer[pos-1] == '\n');
        if (!at_line_start) {
            pos += 6;  // 跳过这个*D_NET
            continue;
        }
        
        size_t start_pos = pos;
        
        // 查找对应的 *END
        pos = buffer.find("\n*END", pos);
        if (pos == std::string::npos) {
            // 到达文件末尾
            locations.push_back({start_pos, buf_size});
            break;
        }
        
        // 跳到*END行的末尾
        pos += 5;  // \n*END
        while (pos < buf_size && buffer[pos] != '\n') {
            pos++;
        }
        if (pos < buf_size) pos++;  // 跳过\n
        
        locations.push_back({start_pos, pos});
    }
    
    return locations;
}

} // namespace parallel
} // namespace spef

