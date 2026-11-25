#include "tools.hpp"

#include <fstream>

// 读整个文件到字符串; 打不开/读失败返回空 optional
std::optional<std::string> ReadFile(const char* fileName)
{
    std::ifstream f(fileName, std::ios::binary | std::ios::ate);
    if (!f)
        return std::nullopt;               // 失败: 空值, 调用方用 if 检查
    const auto size = static_cast<size_t>(f.tellg());
    std::string content(size, '\0');
    f.seekg(0);
    f.read(content.data(), static_cast<std::streamsize>(size));
    return content;                        // 成功: 整个文件内容
}
