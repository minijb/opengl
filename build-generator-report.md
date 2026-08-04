# 构建系统生成器对比报告：Premake5 / CMake / GENie / xmake

> 日期：2026-08-05
> 范围：四款「用脚本描述工程、生成 IDE/构建文件」的工具的定位、优劣势与选型建议。
> 事实核对：Premake5 版本与 GENie 用户名单均来自官方仓库/文档，标注见文末。

## 1. 一句话定位

| 工具 | 类型 | 脚本语言 | 许可 | 维护状态 |
|---|---|---|---|---|
| Premake5 | 纯生成器（只生成工程文件） | Lua | BSD-3 | 活跃，5.0.0-beta7（2025-06） |
| CMake | 生成器 + 构建编排（构建交给 Ninja/Make/…） | 自家 DSL | BSD-3 | 成熟稳定，4.x（2025 起） |
| GENie | 纯生成器 | Lua | BSD-3 | 维护中（随 bgfx 作者），rev1196 |
| xmake | 生成器 + 自带构建引擎 + 包管理 | Lua 风格 | Apache-2.0 | 活跃 |

共同点：都不直接手写 `.sln`/Makefile，而是维护一份工程描述，按目标平台生成产物。

## 2. 逐个分析

### Premake5

简介：从 Premake4 重写，游戏行业事实标准之一。单个 `premake5.exe` 即可运行，脚本是纯 Lua，写起来轻快。

优势：
- 语法简单直接，可编程性（条件、循环、函数）天然继承 Lua。
- 生成目标覆盖广：VS2005–VS2022、Xcode、CodeLite、gmake/gmake2，Ninja 等走第三方模块。
- 单文件二进制、零依赖安装，克隆仓库即可用（Hazel 的 `Scripts/Setup.bat` 就是这个流程）。
- 游戏/图形圈用例扎实：Hazel 引擎、Dear ImGui 官方示例均用 premake5。

劣势：
- 5.0 长期处于 beta（现为 beta7），API/行为偶有变动。
- 无官方包管理：拉第三方库要靠子模块/vendor 目录或社区模块（如 Ninja 生成器本身也是第三方模块）。
- 无测试、安装、打包的配套工具链，纯「出文件」。
- 不参与实际构建：生成完还得靠 msbuild/make/ninja 干活。

### CMake

简介：Kitware 出品，现代 C/C++ 项目事实标准，生态最大。

优势：
- 生态碾压：`find_package`、`FetchContent`、vcpkg/Conan 深度集成，第三方库几乎都带 CMake 支持。
- 生成器最全：VS、Xcode、Ninja、Make、Eclipse 等一网打尽。
- 自带 CTest（测试）与 CPack（打包），覆盖完整交付链路。
- 大项目验证充分：LLVM、OpenCV、Qt 等。

劣势：
- 语法是自家 DSL，历史包袱重：作用域/引用传递/缓存变量语义难懂，写复杂逻辑很痛苦。
- 学习曲线陡，「能跑」和「会写」之间差距大。
- 复杂工程脚本冗长（Hazel 作者公开吐槽过这一点，这也是它选 premake 的原因之一）。

### GENie

简介：Premake 4.4 beta5 的 fork（作者 bkaradzic，bgfx 作者），明确声明不再与 premake 保持兼容。游戏/主机平台向。

优势：
- 与 premake 同源，Lua 语法几乎一致，premake 用户迁移成本低。
- 平台支持深：历史上支持 PS4/Switch 工程生成，VS 支持到 2026。
- 附带 JSON 编译数据库、Ninja（实验）输出，配合 clangd 等工具链好用。
- 与 bgfx 生态配套（zidar 构建脚本集）。

劣势：
- 社区小、文档少，遇到问题基本靠读源码。
- 与 premake4 脚本不兼容，生态分裂；不随 premake5 演进。
- 无包管理、无构建能力、无测试/打包。
- 用户面窄：bgfx、MAME、SoLoud、Psybrus、Crackshell（《Heroes of Hammerwatch》）等，商用大项目少。

### xmake

简介：tboox 开发，Lua 风格语法但全新实现。和前两者本质区别：它**自带构建引擎**——默认直接构建，生成 VS/Xcode 工程只是附加功能；还内置包管理 xrepo。

优势：
- 语法现代简洁，比 premake 更易读（如 `add_requires`/`target()` 声明式写法）。
- 一条命令完成「拉依赖 → 配置 → 构建」，原生支持 Ninja，速度快。
- 内置包管理 xrepo，`add_requires("glfw")` 即可，不用手管 vendor。
- 多语言支持（C/C++/Rust/Go/…），跨平台。
- 国内社区活跃，文档中文友好。

劣势：
- 生态年轻：第三方库的官方 CMake 支持是标配，对 xmake 的支持则要碰运气。
- 团队/大型项目案例少，踩坑时能搜到的资料有限（且多为中文）。
- 生成 VS 工程的效果不如 premake/CMake 打磨得久（日常路径是直接用 xmake 构建）。

## 3. 对比总表

| 维度 | Premake5 | CMake | GENie | xmake |
|---|---|---|---|---|
| 定位 | 纯生成器 | 生成器+编排 | 纯生成器 | 生成+自建+包管理 |
| 脚本语言 | Lua | 自家 DSL | Lua（premake4 系） | Lua 风格 |
| 最新版本 | 5.0.0-beta7（2025-06） | 4.x 稳定 | rev1196 | 持续迭代 |
| 生成目标 | VS05-22/Xcode/CL/gmake/Ninja* | VS/Xcode/Ninja/Make/…（最全） | VS10-26/Xcode/Make/Ninja* | VS/Xcode/Ninja/Make/… |
| 包管理 | 无（社区模块） | 无内置（find_package/FetchContent/vcpkg/Conan） | 无 | 内置 xrepo |
| 测试/打包 | 无 | CTest + CPack | 无 | 内置测试运行，打包有限 |
| 控制台/主机平台 | 无专门支持 | 无专门支持 | 历史上支持 PS4/Switch | 无专门支持 |
| 典型用户 | Hazel、Dear ImGui | LLVM、OpenCV、Qt | bgfx、MAME、SoLoud | 个人/中小项目、国内社区 |
| 学习曲线 | 低 | 中–高 | 低（premake 系） | 低–中 |

\* Ninja 为实验性/第三方模块支持。

## 4. 选型建议

- **团队/商业/大项目，要生态兜底** → CMake。资料、工具链集成、招聘都最省心。
- **游戏引擎或图形向，团队熟 Lua，想要清爽的工程描述** → Premake5（Hazel 路线）。前提是接受自己管理依赖。
- **已经泡在 bgfx 生态或老 premake4 脚本里** → GENie，迁移最平滑；否则不推荐新项目入坑。
- **个人/小项目，想省掉包管理、加快构建** → xmake，体验现代；代价是生态资料少。
- **本仓库（OpenGL + GLFW/glad/glm，已有 CMakeLists.txt）** → 维持 CMake 即可：glm/glfw 都有官方 CMake 支持，`FetchContent` 或 vcpkg 一条路走通。若想尝鲜，xmake 是唯一值得实际试试的替代品（自带 xrepo，依赖一条命令解决）。

## 5. 参考资料

- Premake5 版本与发布：https://github.com/premake/premake-core/releases
- GENie README（用户名单、生成目标、变更史）：https://github.com/bkaradzic/GENie
- Hazel Getting Started（Setup.bat + premake 流程）：https://docs.hazelengine.com/Welcome/GettingStarted.html
- xmake：https://github.com/xmake-io/xmake
