#pragma once

// ============================================================================
// Texture2D —— GLObject<CRTP> 家族的纹理子类(Magnum GL::Texture2D 教学简化版)
//
// 对应调研报告 §7 P0: "Texture(构造即完成全部参数)"。
// 取证(2025-11, Magnum master 分支,逐行核对):
//   Magnum/src/Magnum/GL/Texture.h
//     * Texture<dimensions> 模板:1D/2D/3D 用模板参数表达,
//       Implementation::textureTarget<2>() 特化返回 GL_TEXTURE_2D;
//     * 采样参数是流式 setter:每个返回 Texture&,可链式
//       setWrapping(...).setMinificationFilter(...).setMagnificationFilter(...);
//     * 拷贝 delete / 移动 default / NoCreate / wrap() —— 与 GLObject 完全同构。
//   Magnum/src/Magnum/GL/AbstractTexture.h
//     * bind(Int textureUnit):"绑定"的真正主语是【纹理单元】,不是纹理;
//     * 版本分层:有 4.5 DSA 用 glBindTextureUnit,有 4.4 multi_bind 用
//       glBindTextures;否则(本课程 GL 3.3)经典路径
//       glActiveTexture + glBindTexture —— 本类走的就是这条路。
//
// 本文件相对 Magnum 的刻意简化:
//   * 只做 2D(LearnOpenGL 4.1/4.2 的范围),CubeMap/3D/数组留给课程进度;
//   * 采样参数用裸 GLenum 而非 Magnum 的 SamplerWrapping 枚举 —— 保持与
//     教程原文一一对应(项目一贯取舍,同 gl_buffer.hpp);
//   * GL 3.3 无 DSA(glTextureParameteri 是 4.5 的):所有参数设置/上传
//     都走"先 bind 自己"的经典路径,理由同 VertexArray::attribute()。
//
// ── 纹理与前几个类的本质区别(心智模型)─────────────────────────────────
// Buffer/VertexArray/Program:一个对象 = 一份自己的显存/状态表,bind 只是
// "选中它"。Texture 多一层"单元调度":
//   glActiveTexture(GL_TEXTURE0 + unit)  ① 选中单元(全局状态,至少 16 个,
//                                          glGetIntegerv(GL_MAX_COMBINED_...) 可查)
//   glBindTexture(GL_TEXTURE_2D, id)     ② 把纹理挂到该单元
//   shader 里 sampler uniform 的值 = 【单元编号】,不是纹理 id!
//   → 标准三行: tex.bind(0); program.setInt("ourTexture", 0);
// ============================================================================

#include "gl_object.hpp"

#include <glad/glad.h>
#include <stb_image.h>

#include <cassert>
#include <cstdio>
#include <string>

namespace tutorial::gl {

class Texture2D final : public GLObject<Texture2D> {
public:
    // ---- 采样参数包("构造即完成全部参数"的"全部参数")---------------------
    // 默认值 = LearnOpenGL 4.1 / 旧 texture_common.hpp 的默认采样配置。
    // flipVertically 只对 fromFile 生效(stbi 的加载期翻转);
    // 内存构造路径的像素排布由调用者自己负责。
    struct Options {
        bool   flipVertically = true;  // 图片自上而下存行,GL 的 UV 原点在左下 → 翻
        GLenum wrapS = GL_REPEAT;      // U 越出 [0,1] 的行为
        GLenum wrapT = GL_REPEAT;      // V 同
        GLenum minFilter = GL_LINEAR_MIPMAP_LINEAR;  // 缩小:三线性(消耗 mip 链)
        GLenum magFilter = GL_LINEAR;  // 放大:mipmap 对放大无意义,只能 NEAREST/LINEAR
    };

    // 继承基类构造:Texture2D t;(无效占位)、Texture2D t{NoCreate}
    using GLObject<Texture2D>::GLObject;

    // ---- 三件套之一:从内存像素构造(核心构造)---------------------------
    // pixels: 逐行紧密排布,大小 = width*height*每像素通道数 字节;
    // pixelFormat: GL_RED / GL_RG / GL_RGB / GL_RGBA(驱动按它解释内存)。
    // 契约:构造完成即可被采样(参数已设、mip 链已按需生成、已解绑)。
    explicit Texture2D(const unsigned char* pixels, int width, int height,
                       GLenum pixelFormat, const Options& options = {})
        : width_{width}, height_{height}
    {
        assert(pixels != nullptr && "Texture2D 需要像素数据");
        assert(width > 0 && height > 0);

        glGenTextures(1, &id_);  // 领名字(需要当前线程已有 GL 上下文)
        bind(0);                 // 经典路径:先挂上再配置(见 bind 的注释)

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, options.wrapS);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, options.wrapT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, options.minFilter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, options.magFilter);

        // internalFormat 直接传基格式(GL_RGB 等),精度让驱动选;
        // 想要 sRGB 伽马自动纠正,显式传 GL_SRGB8 / GL_SRGB8_ALPHA8(课程 5.9)
        glTexImage2D(GL_TEXTURE_2D, 0, pixelFormat, width, height, 0,
                     pixelFormat, GL_UNSIGNED_BYTE, pixels);

        if (usesMipmap(options.minFilter)) {
            glGenerateMipmap(GL_TEXTURE_2D);  // 见下方"纹理完整性"注释
        }
        unbind(0);  // 状态卫生(与 Mesh 构造收工 unbind VAO 同一习惯)
    }

    // ---- 三件套之二:删除(基类 reset()/析构会调用)-----------------------
    static void destroy(GLuint id) { glDeleteTextures(1, &id); }

    // ---- 三件套之三:业务方法 ---------------------------------------------

    // 绑定到纹理单元(GL 3.3 经典双层:先选单元,再把纹理挂上去)。
    // const 的理由与 Buffer::bind 相同:改的是全局状态机,不是本对象成员。
    // 留意副作用:glActiveTexture 改的是"哪个单元活跃"这个全局状态,
    //   Magnum 的经典路径会先存后恢复 GL_ACTIVE_TEXTURE(礼貌做法,此处从简);
    //   代价仅仅是之后的裸 glTexImage 系列写到别的单元,本封装内部自洽。
    void bind(GLuint unit) const {
        glActiveTexture(GL_TEXTURE0 + unit);
        glBindTexture(GL_TEXTURE_2D, id_);
    }

    // 解绑:往该单元挂"空"纹理。注意同样要先选单元 —— 忘了 glActiveTexture
    // 就 glBindTexture(0) 解绑的是别的单元上的东西(经典静默 bug)。
    static void unbind(GLuint unit) {
        glActiveTexture(GL_TEXTURE0 + unit);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    // ---- 流式采样参数 setter(Magnum 风格:返回自身引用,可链式)----------
    // "构造即完成全部参数"是默认路径;这组 setter 面向之后想改的场景
    // (如运行时把 REPEAT 换 CLAMP_TO_EDGE、做纹理动画)。每个 setter 开头
    // bind 自己:glTexParameteri 作用在"当前绑定的 GL_TEXTURE_2D"上,
    // 不先 bind 就可能改到别的纹理 —— 和 VertexArray::attribute() 同一个坑。
    //
    // ⚠ 完整性责任边界:把 minFilter 改成 mipmap 系列时,必须确保 mip 链
    //   存在(构造时已生成,或再调一次 generateMipmap()),否则黑屏
    //   (原因见下方"纹理完整性")。setter 无法可靠代劳 —— 它不知道
    //   base level 是否已有数据,这是"防御到哪一层"的真实取舍案例。
    Texture2D& setWrapping(GLenum s, GLenum t) {
        bind(0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, s);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, t);
        return *this;
    }
    Texture2D& setMinFilter(GLenum filter) {
        bind(0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
        return *this;
    }
    Texture2D& setMagFilter(GLenum filter) {
        bind(0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
        return *this;
    }

    // 生成 mip 链:必须在 base level 数据上传【之后】调用
    // (GL 逐级 box-filter 下采样,没有基础数据就是空转)。
    void generateMipmap() {
        bind(0);
        glGenerateMipmap(GL_TEXTURE_2D);
    }

    // ---- 文件加载工厂(与 Program::fromFiles 对称)------------------------
    // 失败:打日志、返回无效对象(对应旧 loadTexture() 返回 0 的模式)。
    // 通道数自动映射 1→RED 2→RG 3→RGB 4→RGBA:
    //   旧代码写死 GL_RGB 会把带 alpha 的 png 的透明通道整个丢掉!
    [[nodiscard]] static Texture2D fromFile(const std::string& path,
                                            const Options& options = {}) {
        // stbi 的翻转开关是【全局粘性状态】,不是每次调用的参数;
        // 在加载前设置即可(stbi 每次加载都会读它)
        stbi_set_flip_vertically_on_load(options.flipVertically ? 1 : 0);

        int width = 0, height = 0, channels = 0;
        unsigned char* pixels = stbi_load(path.c_str(), &width, &height, &channels, 0);
        if (!pixels) {
            std::fprintf(stderr, "Texture load failed: %s\n  stbi: %s\n",
                         path.c_str(), stbi_failure_reason());
            return Texture2D{NoCreate};
        }

        Texture2D tex{pixels, width, height, channelsToFormat(channels), options};
        stbi_image_free(pixels);  // 数据已上传显存,CPU 侧立刻释放
        return tex;
    }

    // ---- 观察器 -----------------------------------------------------------
    [[nodiscard]] int width()  const { return width_; }
    [[nodiscard]] int height() const { return height_; }

// ─── 纹理完整性(texture completeness)────────────────────────────────────
// 纹理的头号"黑屏不报错"坑。GL 在采样前检查纹理是否可用:
//   * MIN filter 是 mipmap 系列,但 mip 链不存在 → 不完整;
//   * (GL 3.0 之前)非 2 次幂尺寸 + REPEAT/Mipmap → 不完整。
// 采样不完整纹理 = 返回 (0,0,0,1) 灰黑,glGetError 也不报!
// 典型事故:忘了 glGenerateMipmap,而默认 MIN filter 恰好是 mipmap 系列。
// 本类的结构性防御:构造函数里"minFilter 用了 mipmap → 自动生成"。

private:
    int width_  = 0;
    int height_ = 0;

    static bool usesMipmap(GLenum minFilter) {
        return minFilter == GL_NEAREST_MIPMAP_NEAREST
            || minFilter == GL_LINEAR_MIPMAP_NEAREST
            || minFilter == GL_NEAREST_MIPMAP_LINEAR
            || minFilter == GL_LINEAR_MIPMAP_LINEAR;
    }

    static GLenum channelsToFormat(int channels) {
        switch (channels) {
        case 1:  return GL_RED;   // 灰度(高度图/遮罩)
        case 2:  return GL_RG;    // 灰度+alpha(罕见)
        case 4:  return GL_RGBA;  // png 常见,含透明
        case 3:
        default: return GL_RGB;
        }
    }
};

} // namespace tutorial::gl

// ─── 用法示例(对应 LearnOpenGL 4.2 的两纹理混合)─────────────────────────
// namespace t = tutorial::gl;
// const auto face  = t::Texture2D::fromFile(TEXTURE("container.jpg"));
// const auto face2 = t::Texture2D::fromFile(TEXTURE("awesomeface.png"),
//                                           {.flipVertically = true}); // C++20 指定初始化
// if (!face || !face2) { /* 日志已打,提前退出 */ }
//
// 渲染循环里:
// face.bind(0);   face2.bind(1);      // 挂到 0/1 号纹理单元
// program.setInt("texture1", 0);      // 告诉 sampler 去哪个单元取(数字=单元号)
// program.setInt("texture2", 1);
// mesh.draw();
//
// 纪律(调研报告 §6.3):GL 对象留在 context 存活范围内 —— main() 里的
// 局部对象天然满足,析构自动 glDeleteTextures。
