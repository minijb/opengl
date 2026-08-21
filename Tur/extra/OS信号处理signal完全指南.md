# Extra · OS 信号处理 signal 完全指南

> 目标：另一个"Signal"——**操作系统发给进程的软件中断**。用户按 Ctrl+C、程序除零、内存越界，
> 内核都是用一个整数编号打断你的程序。这篇讲清楚：信号从哪来、`std::signal` 怎么接、
> **信号处理函数里为什么几乎什么都不能做**、以及游戏引擎最关心的一件事——Ctrl+C 优雅退出主循环。
> 练习全部附参考答案；纯 C++ 部分的代码均在本仓库环境（Windows 11 + MSVC 19.50 / C++17）下编译运行通过。
> 信号槽（库级别的观察者模式）请看姊妹篇 [Signal/Slot 信号槽完全指南](Signal-Slot信号槽完全指南.md)。

---

## 1. 信号是什么：内核 → 进程的异步中断

进程平时在跑自己的代码，内核随时可能"从天而降"一个信号：

```mermaid
flowchart TD
    A[用户按 Ctrl+C] --> K[内核]
    B[kill -TERM pid] --> K
    C[除零 / 野指针] --> K
    K --> P{目标进程 打断当前执行流}
    P -->|默认行为| D[终止 / 忽略 / 停止 / 转储核心]
    P -->|注册了 handler| H[调用你的 handler(sig)]
```

三个关键认知：

1. **异步**：信号可能落在主线程执行到任何两条指令之间（同步信号除外，见 §2 的表）。
2. **处理方式三选一**：默认（`SIG_DFL`，通常是终止进程）、忽略（`SIG_IGN`）、自定义 handler。
3. **C++ 标准只给了最小接口** `<csignal>`；POSIX 的 `sigaction`（§7）才是工程正解；Windows 是另一套现实（§8）。

---

## 2. 六个标准信号 + POSIX 常见扩展

`<csignal>` 保证存在的是这六个：

| 信号 | 触发场景 | 默认行为 | 性质 |
|---|---|---|---|
| `SIGABRT` | `abort()`（如 `assert` 失败、`std::terminate` 的常见路径） | 异常终止 | 同步 |
| `SIGFPE` | 除零、整数溢出等算术错误 | 终止 | **同步** |
| `SIGILL` | 执行非法指令 | 终止 | 同步 |
| `SIGINT` | 终端按 Ctrl+C | 终止 | **异步** |
| `SIGSEGV` | 野指针/越界写 | 终止 + 转储 | **同步** |
| `SIGTERM` | `kill` 默认发送的"请你退出" | 终止 | 异步 |

POSIX 系统上还会遇到（Windows CRT 没有）：

| 信号 | 含义 | 能否捕获 |
|---|---|---|
| `SIGHUP` | 终端断开/守护进程重载 | ✅ |
| `SIGKILL` (9) | 立即强杀 | ❌ **不可捕获不可忽略** |
| `SIGSTOP`/`SIGCONT` | 暂停/继续 | SIGSTOP 不可捕获 |
| `SIGPIPE` | 写已关闭的管道/套接字 | ✅（网络程序常忽略它） |
| `SIGUSR1`/`SIGUSR2` | 留给用户的自定义信号 | ✅ |

> 💡 「同步信号」= 你自己的代码执行的错误（SIGFPE/SIGSEGV），handler 跑在你出错的那个线程；
> 「异步信号」= 外部发来的（SIGINT/SIGTERM），handler 可能跑在进程任意线程。

---

## 3. `std::signal` 最小用法

```cpp
#include <csignal>
#include <cstdio>

static volatile std::sig_atomic_t g_stop = 0;

static void onInterrupt(int sig) {
    (void)sig;
    g_stop = 1;                 // handler 里只做最安全的事：置标志
}

int main() {
    // 注册；第二个参数也可以是 SIG_DFL（恢复默认）或 SIG_IGN（忽略）
    if (std::signal(SIGINT, onInterrupt) == SIG_ERR) {
        std::perror("signal");  // 注册失败返回 SIG_ERR
        return 1;
    }

    std::raise(SIGINT);         // 给自己发信号（测试用；同步调用 handler）

    int guard = 0;
    while (!g_stop)             // 主循环轮询标志
        if (++guard > 100) break;
    std::printf("exited, g_stop=%d\n", (int)g_stop);
    return 0;
}
```

要点：

- handler 签名固定：`void handler(int)`，标准期望 **C 链接**（`extern "C"`）；实践中各编译器也接受普通 C++ 函数。
- 返回值是**上一次的 handler**，失败是 `SIG_ERR`——递归库可以利用它做"临时替换、用完还原"。
- `std::raise(sig)`：给自己发信号。**同步**执行（handler 返回后 raise 才返回），跟真实的异步 Ctrl+C 不同，测试时要分清。

---

## 4. 实测坑：MSVC 递送信号后会把 handler 重置为默认

在本仓库环境（MSVC 19.50，Windows 11）实测到的真实案例，值得单独一节：

```cpp
static volatile std::sig_atomic_t g_pressCount = 0;

static void onInterruptTwoStrike(int) {        // ❌ 有坑的写法
    ++g_pressCount;
}

std::signal(SIGINT, onInterruptTwoStrike);
std::raise(SIGINT);   // handler 执行，count=1 ✅
std::raise(SIGINT);   // 💥 进程直接被默认行为终止（控制台退出码 3）
```

原因：**MSVC 的 CRT（以及 SysV 风格的 `signal()` 实现）在递送信号前会把 handler 重置回默认**。第二次信号到来时注册已经失效。POSIX 引入 `sigaction` 很大程度上就是为了消灭这种实现差异（§7）。

修复：handler 第一件事把自己注册回去——这也是严格 C/C++ 规则下 handler 里**唯一合法的 `signal` 调用**（只能重注册"当前正在处理的这个信号"）：

```cpp
static void onInterruptTwoStrike(int sig) {    // ✅ 自重新注册
    std::signal(sig, onInterruptTwoStrike);
    ++g_pressCount;
}
```

实测输出（修复后）：

```text
[2] ok: press1=1 press2=1 (exit only after 2nd)
```

> ⚠️ 可移植性铁律：**任何依赖"handler 注册一次终身有效"的代码都不可移植**。要么 handler 自注册，要么用 POSIX `sigaction`（`SA_RESETHAND` 不设即保持）。

---

## 5. 异步信号安全：handler 里几乎什么都不能做

这是 OS 信号最反直觉、也最重要的规则。标准（[cppreference: std::signal](https://en.cppreference.com/cpp/utility/program/signal)）对异步信号的 handler 规定：

**能做的**：

- 读写 `volatile std::sig_atomic_t` 对象；
- 读写 **lock-free** 的 `std::atomic`（C++11 起，需 `is_always_lock_free` / `is_lock_free()` 为真）；
- 调用极少数"信号安全"函数：`std::signal`（重注册自己）、`std::abort`、`std::_Exit`、`std::quick_exit`、`std::memcpy`/`std::memmove` 等。

**不能做的（做了 = UB）**：

- ❌ `printf` / `std::cout`——标准库 I/O；
- ❌ `malloc` / `new`——动态分配；
- ❌ 加锁（`mutex.lock()`）；
- ❌ 抛异常、`dynamic_cast`；
- ❌ 触碰非原子的普通全局/静态变量（写入后其值"不确定"）。

### 5.1 为什么？两个典型死法

**死法一：重入死锁。** 主线程正好在 `malloc` 内部、持有堆锁时被 SIGINT 打断，handler 里又调 `malloc`（比如 `printf` 内部缓冲分配）→ 永远等不来的锁 → 整个进程卡死。

**死法二：数据撕裂。** handler 写的 `bool`、主循环读的 `bool`，编译器没义务知道 handler 存在（它就是个普通函数调用点之外闯进来的执行流），可能被优化进寄存器、永远读不到新值。`volatile sig_atomic_t` 就是标准为此开的"保证通道"。

### 5.2 黄金模式：handler 只立 flag，主循环轮询

```cpp
static volatile std::sig_atomic_t g_stop = 0;

static void onInterrupt(int) { g_stop = 1; }   // 全部工作就这一行

// 主循环（引擎的 while (!window.shouldClose())）
while (!g_stop /* && !window.shouldClose() */) {
    // 一帧的全部工作
}
// 循环退出后，在正常上下文里慢慢清理：存档、关音频、释放 GL 资源、glfwTerminate
```

所有"响应 Ctrl+C 要做的事"都搬到循环退出后的**正常代码**里做，handler 只负责喊停。

### 5.3 lock-free atomic：C++11 的现代写法

```cpp
#include <atomic>
static std::atomic<int> g_atomicStop{0};
static_assert(std::atomic<int>::is_always_lock_free);   // C++17：编译期确认

static void onInterrupt(int) {
    g_atomicStop.store(1, std::memory_order_relaxed);   // 合法且正确
}
```

比 `sig_atomic_t` 强的地方：类型系统里就是原子量，可以和普通多线程代码混用（如别的线程也可能置位）。前提是 `is_lock_free()` 为真——内部带锁的 atomic 在 handler 里同样非法。

---

## 6. 同步信号（SIGSEGV/SIGFPE）的特殊规则

- handler 返回后如果回到出错指令 → **再次触发，通常无限循环**。标准直接规定：处理这类"计算异常"信号的 handler **返回即 UB**。
- 所以捕获 SIGSEGV 的 handler 要么 `_Exit`，要么（POSIX）`siglongjmp` 逃出去。内存池 / 沙箱 / 自动崩溃收集器才会碰它。
- 日常建议：**别捕获 SIGSEGV/SIGFPE**，让它们转储核心，用调试器/崩溃报告查根因。Windows 上它们和 SEH 异常的关系见 §8。

---

## 7. POSIX 正解：sigaction

`signal()` 的语义各平台实现不一（§4 的重置问题就是一例）。POSIX 的 `sigaction` 把每件事都说死了，Linux/macOS 上工程代码一律用它（**MSVC 没有，Windows 上跳过本节**）：

```cpp
#include <signal.h>   // POSIX

static void onTerm(int, siginfo_t* info, void*) {
    // siginfo 拿得到发送者 pid、出错地址等细节
}

struct sigaction sa{};
sa.sa_sigaction = onTerm;
sa.sa_flags = SA_RESTART | SA_SIGINFO;  // SA_RESTART：自动重启被打断的系统调用
sigemptyset(&sa.sa_mask);               // handler 执行期间额外屏蔽的信号集
sigaction(SIGTERM, &sa, nullptr);
```

相对 `signal()` 的关键优势：

| 能力 | `signal()` | `sigaction` |
|---|---|---|
| 递送后 handler 保持 | 看实现（MSVC：重置） | 默认保持（不设 `SA_RESETHAND`） |
| 被打断的系统调用 | 可能返回 `EINTR` | `SA_RESTART` 自动重启 |
| 拿到信号细节 | 只有编号 | `siginfo_t`（发送者、地址） |
| handler 期间屏蔽谁 | 不确定 | `sa_mask` 明确控制 |

> 💡 引擎关心的实例：没有 `SA_RESTART` 时，Ctrl+C 恰好打断 `sleep`/`recv`，主循环会以为调用失败——一个"偶尔按 Ctrl+C 才复现"的诡异 bug 来源。

---

## 8. Windows 现实

你的开发机是 Windows，要知道 CRT 的 `signal` 只是"仿真"：

| 信号 | Windows 上的真实情况 |
|---|---|
| `SIGINT` | 仅控制台程序 + 用户按 Ctrl+C / Ctrl+Break 时由 CRT 触发；服务/GUI 程序基本收不到 |
| `SIGTERM` | **没有任何外部来源**，只有自己 `raise` 才有（本篇实测 §练习 5） |
| `SIGSEGV`/`SIGFPE`/`SIGILL` | 由硬件异常经 SEH 转换而来；MSVC 还提供 `_set_se_translator` 把它变成 C++ 异常 |
| `SIGKILL`/`SIGHUP`/`SIGUSR1`… | 不存在。强杀进程靠 `TerminateProcess`，同样**不可拦截** |

Windows 原生的 Ctrl+C 通知是 **console control handler**：

```cpp
#include <windows.h>

static BOOL WINAPI consoleHandler(DWORD type) {   // CTRL_C_EVENT / CTRL_CLOSE_EVENT...
    if (type == CTRL_C_EVENT) { g_stop = 1; return TRUE; }
    return FALSE;                                  // 不认识的交回系统
}
SetConsoleCtrlHandler(consoleHandler, TRUE);
```

取舍：`signal(SIGINT, ...)` 跨平台、写起来短；`SetConsoleCtrlHandler` 能收到**关窗/注销**等更多事件，但只 Windows。跨平台引擎的常见做法：POSIX 用 `sigaction`，Windows 用 `SetConsoleCtrlHandler`，两者都只做"置 flag"这一件事。

---

## 9. 应用到引擎：Ctrl+C 优雅退出 GLFW 主循环

需求：控制台启动的引擎，按 Ctrl+C 不当场死，而是像点窗口 ✕ 一样走完 `glfwSetWindowShouldClose` → 正常清理。核心逻辑（实测通过）：

```cpp
// src/main.cpp（节选）
#include <csignal>

static volatile std::sig_atomic_t g_interrupted = 0;

static void onInterrupt(int sig) {
    std::signal(sig, onInterrupt);   // MSVC 重置语义下必须自注册（§4）
    g_interrupted = 1;
}

int main() {
    std::signal(SIGINT,  onInterrupt);
    std::signal(SIGTERM, onInterrupt);

    GLFWManager window;
    window.init();

    while (!g_interrupted && !window.shouldClose()) {   // ← 两个退出条件并列
        window.pollEvents();
        /* render(dt); */
    }
    // 走到这里说明是被 Ctrl+C 或 ✕ 打断的，正常上下文从容清理：
    // 存档 flush、音频淡出、ImGui shutdown、window.shutdown()、glfwTerminate
    return 0;
}
```

被其他线程触发的场景同样成立（实测：工作线程 `raise` → 主循环 3 帧内干净退出）：

```text
[4] ok: engine loop ran 3 frames then exited cleanly
```

> ⚠️ 顺序问题：信号 flag 只是"请求退出"，**不要在 handler 里直接调 `glfwSetWindowShouldClose`**——GLFW 多数函数不是信号安全的，而且 GLFW 约定 `glfwSetWindowShouldClose` 应在主线程调用。flag → 主循环下一帧自然退出即可。

---

## 10. 陷阱清单

1. **handler 里 `printf`/`new`/加锁** → UB 或死锁（§5.1）。连"只是打条日志"都不行——写进环形缓冲、循环外再输出。
2. **忘了 `volatile sig_atomic_t`** → 主循环可能永远读不到 flag（优化器把它缓存进寄存器）。
3. **依赖"注册一次终身有效"** → MSVC/SysV 递送后重置（§4 实测退出码 3）。
4. **SIGSEGV/SIGFPE 的 handler `return`** → UB/死循环（§6）。
5. **多线程程序以为信号会给主线程** → 异步信号递送给**任意**线程；多线程下要么屏蔽 + 专用信号线程（POSIX `sigwait`），要么只靠"置全局 flag"这种线程无关的方式。
6. **测试全靠 `raise()`** → `raise` 是同步的，测不出真实 Ctrl+C 的"打断在任何位置"行为；最终要真按一次 Ctrl+C 验收。
7. **以为 Windows 上有 `SIGTERM`/`SIGKILL`** → 没有（§8），强杀是 `TerminateProcess`，不可拦截。

---

## 🔧 练习

1. **两次才退出**：实现"第一次 Ctrl+C 打印『再按一次退出』，第二次才真正退出循环"。用 `raise` 模拟两次按键验证。
2. **代码评审**：下面这段代码有 4 处信号相关错误，找出并说明每处后果与修法：
   ```cpp
   static bool stop = false;                        // (1)
   static FILE* logFile = nullptr;
   static void handler(int) {
       fprintf(logFile, "signal!\n");               // (2)
       stop = true;
       char* msg = new char[64];                     // (3)
       sprintf(msg, "sig %d", 0); delete[] msg;
   }
   static void crashHandler(int) {                   // 用于捕获 SIGSEGV
       stop = true;                                  // (4)
   }
   ```
3. **优雅退出引擎**：把 §9 的方案落到你的项目：`main.cpp` 注册 `SIGINT`，主循环条件加 `!g_interrupted`，退出后打印"clean exit"。先在无窗口的纯循环版验证逻辑，再接 GLFW。
4. **概念辨析**（文字题）：
   - a) `raise(SIGINT)` 和用户真按 Ctrl+C 有什么本质区别？
   - b) `SIGINT`、`SIGTERM`、`SIGKILL` 都能"弄死"进程，为什么服务端脚本通常监听 `SIGTERM` 而不是 `SIGINT`？
   - c) 为什么"忽略 `SIGPIPE`"几乎是网络程序的标配？
5. **`volatile bool` vs `volatile sig_atomic_t` vs `std::atomic<bool>`**：作为信号 flag，三者哪些是标准保证安全的？`std::atomic<bool>` 什么时候反而不合法？

---

## 📝 参考答案

### 1. 两次才退出

```cpp
#include <csignal>
#include <cstdio>

static volatile std::sig_atomic_t g_pressCount = 0;

static void onInterruptTwoStrike(int sig) {
    std::signal(sig, onInterruptTwoStrike);   // 自注册：应对 MSVC 重置语义
    ++g_pressCount;
}

int main() {
    std::signal(SIGINT, onInterruptTwoStrike);

    std::raise(SIGINT);                       // 模拟第一次 Ctrl+C
    std::raise(SIGINT);                       // 模拟第二次 Ctrl+C

    int guard = 0;
    for (;;) {
        if (g_pressCount == 1)
            std::printf("再按一次退出\n");      // 常规上下文里打印，handler 里不打印
        if (g_pressCount >= 2) break;
        if (++guard > 1'000'000) return 2;    // 防御性退出
    }
    std::printf("clean exit\n");
    return 0;
}
```

细节：打印放在轮询里而不是 handler 里（§5）；`sig_atomic_t` 的 `++` 在主流实现上原子（且自注册后 handler 只跑置数这一件事）。本仓库环境实测两次 `raise` 后 `g_pressCount == 2`、正常退出。若把自注册那行删掉，第二次 `raise` 在 MSVC 下直接终止进程（退出码 3，§4 实测）。

### 2. 代码评审

1. **(1) `static bool stop`** —— 非 `volatile sig_atomic_t` 的普通 bool。handler 写、主循环读，UB：主循环可能永远读不到 `true`（缓存进寄存器）。修：`static volatile std::sig_atomic_t stop;`。
2. **(2) `fprintf`** —— handler 里调标准库 I/O，直接违反异步信号安全规则：日志缓冲内部可能有锁/分配，若信号恰好打断另一个 `fprintf` → 死锁或堆损坏。修：只置 flag，把"写日志"挪到主循环退出后；确实要在信号上下文留痕的场合用预分配的 `lock-free` 环形缓冲。
3. **(3) `new`** —— 动态分配同样非法：打断 `malloc` 再 `malloc` = 死锁候选。和 (2) 一样挪出 handler。
4. **(4) `crashHandler` 处理 SIGSEGV 却会 `return`** —— 返回后跳回出错指令，再次 SIGSEGV，无限循环；标准规定此类同步信号的 handler 返回即 UB。修：要么不捕获（推荐，让它转储），要么 handler 里 `_Exit(1)` /（POSIX）`siglongjmp`。

### 3. 优雅退出引擎

纯逻辑版（实测通过，可直接编译运行）：

```cpp
#include <chrono>
#include <csignal>
#include <cstdio>
#include <thread>

static volatile std::sig_atomic_t g_interrupted = 0;
static void onInterrupt(int sig) {
    std::signal(sig, onInterrupt);
    g_interrupted = 1;
}

int main() {
    std::signal(SIGINT, onInterrupt);
    std::signal(SIGTERM, onInterrupt);

    int frames = 0;
    while (!g_interrupted) {
        ++frames;                                        // 一帧的工作
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    std::printf("clean exit after %d frames\n", frames); // 从容清理的位置
    return 0;
}
```

接 GLFW 时只改两处：循环条件 `while (!g_interrupted && !window.shouldClose())`，清理区补 `window.shutdown()`。注意点见 §9 的 ⚠️：handler 里不碰任何 GLFW 函数。

### 4. 概念辨析

- **a)** `raise` 是**同步**信号：在调用线程当前执行点直接调 handler，返回后继续下一行，完全可预期；真实 Ctrl+C 是**异步**的：内核在任意两条指令之间打断任意线程，handler 结束位置也不可控。所以 `raise` 只能测 handler 逻辑本身，测不了竞态。
- **b)** `SIGINT` 是"终端中断"，语义绑定在交互式终端上（服务进程通常没有终端）；`SIGTERM` 是 `kill` 的默认信号，"请你体面退出"的标准通知，也是进程管理器（systemd/k8s）停止服务时先发的那个；服务监听 `SIGTERM` = 响应一切正常运维停止流程。`SIGKILL` 是最后手段，不给任何清理机会。
- **c)** TCP 对端关闭后继续 `write`，内核会发 `SIGPIPE`，默认行为是**杀进程**——一个聊天服务器可能因为一个客户端掉线就整站退出。所以网络程序标配 `signal(SIGPIPE, SIG_IGN)`（或 `MSG_NOSIGNAL`），把错误变成 `write` 的返回值 `-1/EPIPE`，走正常错误处理路径。

### 5. flag 类型三选一

| 写法 | 标准保证安全？ | 说明 |
|---|---|---|
| `static bool` | ❌ | handler 与主循环的数据竞争，值还可能"不确定" |
| `static volatile std::sig_atomic_t` | ✅ | 标准钦点的信号通道（§5.2），但只在"信号↔同线程"场景有意义 |
| `static std::atomic<bool>` | ✅ **当且仅当 lock-free** | `is_lock_free()` 为真才允许在 handler 里用；带锁的 atomic 内部有互斥，等同在 handler 里加锁 = UB |

```cpp
std::atomic<bool> flag{false};
if (flag.is_lock_free()) {  // 或 C++17 的 std::atomic<bool>::is_always_lock_free
    /* 这个平台上 handler 里用它是合法的 */
}
```

选型：纯信号 flag 用 `volatile sig_atomic_t` 最省心；flag 同时也被其他线程读写（多线程引擎）时，用**确认 lock-free** 的 `std::atomic`，一个类型同时满足两个世界。

---

## 参考资料

- [cppreference — std::signal](https://en.cppreference.com/cpp/utility/program/signal)（异步信号安全的完整规则清单，本篇 §5 的依据）
- [cppreference — signal handler 与 C++ 并发模型](https://en.cppreference.com/cpp/atomic/atomic_signal_fence)
- [POSIX sigaction](https://pubs.opengroup.org/onlinepubs/9799919799/functions/sigaction.html)
- [POSIX async-signal-safe functions 列表](https://pubs.opengroup.org/onlinepubs/9799919799/functions/V2_chap02.html#tag_16_04)
- [M. Kerrisk, The Linux Programming Interface, ch.20–22](https://man7.org/tlpi/)（信号的系统级圣经）
- [GLFW 入门指南（主线程约定）](https://www.glfw.org/docs/latest/intro_guide.html)

---

> Extra 系列到此：`std::function` → `std::bind/lambda/ref` → 事件系统全景 → Signal/Slot → OS 信号。
> 回到 [教程主页](../README.md)，或继续 [进阶 07 性能与多线程](../advanced/07-性能与多线程.md)（多线程下的信号与原子量还会再见面）。
