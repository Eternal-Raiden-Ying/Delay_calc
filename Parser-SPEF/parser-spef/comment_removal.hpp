#pragma once

#include <string>
#include <vector>
#include <omp.h>
#include <iostream>

namespace spef {
namespace detail {

// 串行版本注释移除（原有逻辑）
inline void remove_comments_serial(std::string& buffer)
{
    for (size_t i = 0; i < buffer.size(); i++)
    {
        // 处理 // 注释
        if (buffer[i] == '/' && i + 1 < buffer.size() && buffer[i + 1] == '/')
        {
            buffer[i] = buffer[i + 1] = ' ';
            for (i = i + 2; i < buffer.size(); ++i)
            {
                if (buffer[i] == '\n' || buffer[i] == '\r')
                {
                    break;
                }
                buffer[i] = ' ';
            }
        }

        // 处理 *N 行（SPEF特殊格式）
        if (buffer[i] == '*' && i + 1 < buffer.size() && buffer[i + 1] == 'N' && 
            i + 2 < buffer.size() && buffer[i + 2] == ' ')
        {
            buffer[i] = buffer[i + 1] = ' ';
            for (i = i + 2; i < buffer.size(); ++i)
            {
                if (buffer[i] == '\n' || buffer[i] == '\r')
                {
                    break;
                }
                buffer[i] = ' ';
            }
        }
    }
}

// 并行版本注释移除（分块处理）
inline void remove_comments_parallel(std::string& buffer, int num_threads)
{
    if (num_threads <= 1 || buffer.size() < 1024*1024) {
        // 小文件或单线程，直接使用串行版本
        remove_comments_serial(buffer);
        return;
    }

    const size_t buffer_size = buffer.size();
    const size_t chunk_size = buffer_size / num_threads;
    
    #pragma omp parallel num_threads(num_threads)
    {
        const int tid = omp_get_thread_num();
        
        // 计算chunk的起始和结束位置
        size_t start = tid * chunk_size;
        size_t end = (tid == num_threads - 1) ? buffer_size : (tid + 1) * chunk_size;
        
        // ✨ 关键：向前查找行首，避免从注释中间开始处理
        // 这样可以安全处理跨chunk边界的注释
        if (tid > 0 && start > 0) {
            // 回退到上一个换行符之后
            while (start > 0 && buffer[start - 1] != '\n' && buffer[start - 1] != '\r') {
                --start;
            }
        }
        
        // 扫描并移除注释
        for (size_t i = start; i < end; ++i)
        {
            // 处理 // 注释
            if (buffer[i] == '/' && i + 1 < buffer_size && buffer[i + 1] == '/')
            {
                buffer[i] = buffer[i + 1] = ' ';
                i += 2;
                
                // 清除到行尾（允许超出chunk边界）
                while (i < buffer_size && buffer[i] != '\n' && buffer[i] != '\r')
                {
                    buffer[i] = ' ';
                    ++i;
                }
                
                // 回退一个位置，让外层for继续
                if (i < buffer_size) --i;
            }
            // 处理 *N 行
            else if (buffer[i] == '*' && i + 1 < buffer_size && buffer[i + 1] == 'N' && 
                     i + 2 < buffer_size && buffer[i + 2] == ' ')
            {
                buffer[i] = buffer[i + 1] = ' ';
                i += 2;
                
                // 清除到行尾
                while (i < buffer_size && buffer[i] != '\n' && buffer[i] != '\r')
                {
                    buffer[i] = ' ';
                    ++i;
                }
                
                if (i < buffer_size) --i;
            }
        }
    }
}

// 统一接口
inline void remove_comments(std::string& buffer, bool enable_parallel = true, int num_threads = 0)
{
    if (!enable_parallel) {
        remove_comments_serial(buffer);
        return;
    }
    
    // 自动检测线程数
    if (num_threads <= 0) {
        num_threads = omp_get_max_threads();
    }
    
    // 小文件使用串行（避免线程开销）
    if (buffer.size() < 512 * 1024) {  // 512KB阈值
        std::cout << "[INFO] File size small, using serial comment removal\n";
        remove_comments_serial(buffer);
        return;
    }
    
    try {
        remove_comments_parallel(buffer, num_threads);
    } catch (...) {
        // 并行失败时回退到串行（安全保障）
        std::cerr << "[WARNING] Parallel comment removal failed, falling back to serial\n";
        remove_comments_serial(buffer);
    }
}

} // namespace detail
} // namespace spef
