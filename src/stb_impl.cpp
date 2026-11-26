// stb_image 的【唯一】实现编译单元。
//
// stb 系列头文件是"声明与实现一体"的设计:
//   直接 include      → 只拿到函数声明;
//   先 define 宏再 include → 生成函数体(stb_image.h 约 7000 行实现)。
// 必须在恰好一个 .cpp 里做这件事 —— 两个 TU 都定义 = 链接器重复符号(ODR)。
// include/texture_common.hpp、include/gl_texture.hpp 只负责拿声明。
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
