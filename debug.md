# EchoMesh 项目调试日志

本文档记录了在开发 EchoMesh 项目过程中遇到的主要 Bug 及其修复过程，旨在帮助开发者理解问题根源、学习调试思路。

---

## Bug 3: `EventLoop::abortNotInLoopThread()` 线程断言失败

### 问题现象

在集成了 `AudioEngine` 模块后，服务器启动时或客户端连接时，程序会因一个线程断言而崩溃。

报错信息如下：
```
EventLoop::abortNotInLoopThread() - EventLoop 0x7ffdbd693390 was created in threadId_ = 134775441638464, current thread id = 134775192938176
```
这个错误表明，一个 `EventLoop` 对象的方法被一个与其创建线程不同的线程调用了，这违反了 `EventLoop` 的“one loop per thread”设计原则。

### 分析与思考过程

这个问题在集成音频引擎后，由于引入了更复杂的线程模型，经历了多次尝试和修复。

#### 第一次尝试：主线程阻塞

最初的 `main` 函数逻辑是：
```cpp
// main.cpp
EventLoop loop;
TcpServer server(&loop, 8888, 4);
server.start();
AudioEngine audio_engine("127.0.0.1", 12345);
audio_engine.start();
loop.loop(); // 主线程在此阻塞
audio_engine.stop(); // 这行代码永远不会被执行
```
*   **问题**: `loop.loop()` 会阻塞主线程，导致 `audio_engine.stop()` 永远无法被调用，程序无法正常关闭。

#### 第二次尝试：在 `main` 函数中引入 `sleep`

为了让程序能够定时关闭，修改了 `main` 函数：
```cpp
// main.cpp
EventLoop loop;
TcpServer server(&loop, 8888, 4);
server.start();
AudioEngine audio_engine("127.0.0.1", 12345);
audio_engine.start();

std::this_thread::sleep_for(std::chrono::seconds(60)); // 主线程休眠

audio_engine.stop();
// loop.unloop(); // 尝试停止loop，但EventLoop没有unloop方法
```
*   **问题**: `loop.loop()` 没有被调用，所以 `TcpServer` 虽然被 `start()`，但其底层的 `EventLoop` 没有运转，导致服务器无法接受任何TCP连接。客户端连接时会报 “Connection refused”。

#### 第三次尝试：将 `EventLoop` 放入独立线程

为了解决 `loop.loop()` 阻塞主线程的问题，尝试将其放入一个单独的线程：
```cpp
// main.cpp
EventLoop loop;
TcpServer server(&loop, 8888, 4); // 在主线程创建
server.start(); // 在主线程启动

std::thread loop_thread([&]() {
    loop.loop(); // 在子线程运行
});

// ... sleep and stop logic ...
```
*   **问题**: `TcpServer` 的构造函数和 `start()` 方法都在主线程中被调用，但其关联的 `EventLoop` 却在 `loop_thread` 中运行。这违反了 `TcpServer` 的设计——`TcpServer` 的所有操作都必须在其关联的 `EventLoop` 所在的线程中执行。这直接导致了 `EventLoop::abortNotInLoopThread()` 断言失败。

#### 第四次尝试：将 `TcpServer` 的创建和启动也移入 `loop_thread`

```cpp
// main.cpp
EventLoop loop;
std::thread loop_thread([&]() {
    TcpServer server(&loop, 8888, 4); // 在子线程创建
    server.start(); // 在子线程启动
    loop.loop();
});

// ... sleep and stop logic ...
```
*   **问题**: 理论上这个方向是正确的，因为它保证了 `TcpServer` 和 `EventLoop` 在同一个线程中。但在实际运行时，仍然可能出现同样的断言失败。经过深入分析，发现 `registerBusinessLogicHandlers()` 这个全局函数的调用位置也很关键，它内部可能会间接与 `EventLoop` 交互。更深层次的原因是，`TcpServer` 和 `EventLoop` 的生命周期管理变得非常复杂，很容易在程序关闭时出现竞态条件。

### 最终解决方法

回顾整个系统的需求：
1.  `TcpServer` 需要在一个 `EventLoop` 中稳定运行，处理客户端的信令交互。
2.  `AudioEngine` 需要独立运行，进行音频的采集、编码和发送。
3.  主程序需要有一个明确的、可控的生命周期，例如运行一段时间后自动关闭所有服务。

最终的解决方案是将主线程完全交给 `EventLoop`，同时创建一个“控制线程”来管理程序的生命周期。

```cpp
// main.cpp
int main() {
    try {
        EventLoop loop; // 在主线程创建 EventLoop
        TcpServer server(&loop, 8888, 4); // 在主线程创建 TcpServer

        registerBusinessLogicHandlers();
        server.start();

        // 创建一个控制线程
        std::thread control_thread([&]() {
            AudioEngine audio_engine("127.0.0.1", 12345);
            audio_engine.start();

            std::cout << "Server and AudioEngine running for 60 seconds..." << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(60));
            
            // 停止服务
            audio_engine.stop();
            loop.quit(); // 安全地请求 EventLoop 退出
        });

        loop.loop(); // 阻塞主线程，运行 EventLoop
        control_thread.join(); // 等待控制线程结束

    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
```

**这个方案的优点**:
*   **线程职责清晰**: 主线程专门负责运行 `EventLoop`，完全符合 `TcpServer` 的“one loop per thread”模型。
*   **生命周期可控**: `control_thread` 成为了程序的“大脑”，它决定了 `AudioEngine` 何时启动/停止，以及整个程序何时退出。
*   **优雅关闭**: `loop.quit()` 是一个线程安全的方法，它会唤醒 `EventLoop` 并使其从 `loop()` 函数中正常返回，从而实现优雅停机。`control_thread.join()` 确保主线程会等待控制线程完成后再退出。

通过这个最终方案，服务器成功地同时运行了TCP服务和UDP音频引擎，并且能够在预定时间后干净地关闭，彻底解决了线程断言问题。
---

## Bug 1: 客户端断开连接时服务器断言失败

### 问题现象

在修复了服务器无法启动的 `std::bad_function_call` 问题之后，服务器能够正常运行。Python 测试客户端可以成功连接、登录、创建/加入房间并发送消息。然而，当客户端正常关闭连接或连接超时后，服务器立即因断言失败而崩溃。

报错信息如下：
```
echomesh_server: /home/moon/桌面/code/echoMesh/src/EpollPoller.cpp:72: void EpollPoller::removeChannel(Channel*): Assertion `index == 1' failed.
```

### 分析与思考过程

1.  **定位问题**: 错误信息非常明确，问题出在 `EpollPoller.cpp` 的 `removeChannel` 函数中，一个 `assert(index == 1)` 的断言失败了。这说明在调用此函数时，传入的 `channel` 对象的 `index` 成员变量不等于 `1`。

2.  **理解 `Channel::index` 的作用**: 通过分析 `EpollPoller` 的代码，我们知道 `index` 变量是用来追踪一个 `Channel` 在 `epoll` 中的状态的：
    *   `-1`: 表示该 `Channel` 不在 `epoll` 的监听集合中。
    *   `1`: 表示该 `Channel` 已经被 `EPOLL_CTL_ADD` 添加到 `epoll` 的监听集合中。

3.  **追踪调用链**: 为了弄清楚 `removeChannel` 是在什么情况下被调用的，以及为什么 `index` 的值不为 `1`，我们从客户端断开连接的源头开始追踪：
    *   客户端断开连接，服务器的 `TcpConnection::handleRead` 读取到0字节，调用 `handleClose()`。
    *   在 `handleClose()` 中，会调用 `channel_->disableAll()` 来禁止该 Channel 上的所有事件。
    *   `disableAll()` 会将 Channel 的监听事件 `events_` 设为 `kNoneEvent`，然后调用 `update()`。
    *   `update()` 会触发 `EpollPoller::updateChannel()`。
    *   在 `updateChannel()` 中，代码检查到 `channel->isNoneEvent()` 为 `true`，于是执行了 `update(EPOLL_CTL_DEL, channel)`，**并将 `channel->set_index(-1)`**。这一步是关键！这意味着，仅仅是“禁用”一个 Channel，就已经将其从 `epoll` 内核事件表中移除了，并更新了 `index` 状态。
    *   `handleClose()` 继续执行，最终会触发连接销毁的逻辑 `connectDestroyed()`。
    *   `connectDestroyed()` 调用 `channel_->remove()`。
    *   `channel_->remove()` 调用 `EpollPoller::removeChannel()`。

4.  **发现根本原因**: 当程序执行到 `EpollPoller::removeChannel()` 时，传入的 `channel` 对象的 `index` 已经是 `-1` 了（在第3步中被 `updateChannel` 修改）。因此，`assert(index == 1)` 断言自然就失败了。

    **结论**: Bug 的根源在于对 Channel 的状态管理混乱。一个 Channel 在其生命周期结束时，被事实상地从 `epoll` 中移除了两次：一次是在 `updateChannel` 中（当事件被禁用时），另一次是在 `removeChannel` 中（当连接被销毁时）。而 `removeChannel` 函数的实现过于理想化，没有预料到 `channel` 可能已经被移除了。

### 解决方法

为了解决这个问题，需要让 `removeChannel` 函数变得更加健壮，能够正确处理一个已经被部分清理过的 `Channel` 对象。

1.  修改 `EpollPoller::removeChannel` 的实现，使其能够应对 `index` 为 `1` (仍在 epoll 中) 或 `-1` (已从 epoll 中移除) 的两种情况。
2.  只有当 `index` 为 `1` 时，才需要执行 `update(EPOLL_CTL_DEL, channel)` 来通知内核移除文件描述符。
3.  无论 `index` 是多少，都需要将 `channel` 从 `EpollPoller` 的内部 `channels_` 哈希表中移除，并最终确保其 `index` 被设为 `-1`。

最终的修复代码如下 (`EpollPoller.cpp`)：

```cpp
void EpollPoller::removeChannel(Channel* channel) {
    ownerLoop_->assertInLoopThread();
    int fd = channel->fd();
    assert(channels_.find(fd) != channels_.end());
    assert(channels_[fd] == channel);
    assert(channel->isNoneEvent());
    int index = channel->index();
    // 关键修复：断言 index 可以是 1 或 -1
    assert(index == 1 || index == -1);
    size_t n = channels_.erase(fd);
    assert(n == 1);

    // 关键修复：只有当 channel 还在 epoll 监听集合中时，才执行 DEL
    if (index == 1) {
        update(EPOLL_CTL_DEL, channel);
    }
    channel->set_index(-1);
}
```

在应用此修复的过程中，还解决了两个连带的编译错误：
*   在 `EpollPoller.cpp` 中添加了 `#include "EventLoop.h"`，因为新代码调用了 `ownerLoop_` 的方法，需要 `EventLoop` 的完整定义。
*   在 `TcpConnection.h` 中添加了 `peerAddress()` 的公有 `getter` 方法，使得回调函数可以访问连接的对端地址。

经过这一系列修复，服务器不再因为客户端断开而崩溃，整个通信链路完全打通。

---

## Bug 2: 服务器启动时 `std::bad_function_call` 崩溃

### 问题现象

项目启动时最先遇到的问题，`echomesh_server` 在执行 `./echomesh_server` 命令后立即崩溃。

报错信息如下：
```
terminate called after throwing an instance of 'std::bad_function_call'
  what():  bad_function_call
```

这个错误表明程序尝试调用一个空的 `std::function` 对象，即该函数对象在被调用时没有绑定任何实际的函数。

### 分析与思考过程

1.  **理解 `std::bad_function_call`**: 这是 C++ 标准库中的一个异常，当尝试执行一个没有有效目标（未初始化或已reset）的 `std::function` 对象时抛出。

2.  **定位触发时机**: 服务器在启动时立即崩溃，这表明问题发生在一个初始化的回调函数或启动逻辑中，而不是在处理客户端请求的阶段。

3.  **排查 `std::function` 相关的代码**:
    *   在服务器代码中，`std::function` 主要用于回调函数，例如 `TcpServer`、`TcpConnection` 和 `Channel` 中的各种回调。
    *   首先排查了 `ThreadPool` 和 `EventLoopThread` 的启动过程，因为线程启动时可能会触发回调。但检查发现，当前代码中的 `ThreadPool` 并没有使用回调来初始化子线程的 `EventLoop`，我的初步判断有误（这可能是早期版本或我记忆中的代码逻辑）。

4.  **重新审查 `TcpServer` 的初始化**: `main.cpp` 在启动时创建 `TcpServer` 对象：`TcpServer server(&loop, 8888, 4);`
    *   检查 `TcpServer.h`，发现 `TcpServer` 内部定义了 `ConnectionCallback connectionCallback_` 和 `MessageCallback messageCallback_` 两个 `std::function` 类型的成员变量。
    *   检查 `TcpServer.cpp` 的构造函数，发现这两个成员变量**没有被初始化**。它们被默认构造，此时是空的 `std::function` 对象。
    *   检查 `main.cpp`，发现也没有调用 `server.setConnectionCallback()` 或 `server.setMessageCallback()` 来设置这些回调。

5.  **追踪回调的使用**:
    *   当有新连接到来时，`TcpServer::newConnection` 函数会被调用。
    *   在该函数中，新创建的 `TcpConnection` 对象会通过 `conn->setConnectionCallback(connectionCallback_)` 和 `conn->setMessageCallback(messageCallback_)` 来设置其自身的连接回调和消息回调。
    *   由于 `TcpServer` 自身的 `connectionCallback_` 和 `messageCallback_` 是空的，所以这些空的 `std::function` 对象被传递给了 `TcpConnection`。
    *   `TcpServer::newConnection` 最后会安排 `conn->connectEstablished()` 在 IO 线程中执行。
    *   在 `TcpConnection::connectEstablished()` 函数中，会调用 `connectionCallback_(shared_from_this());`。

6.  **确定崩溃原因**: 由于 `connectionCallback_` 是一个空的 `std::function` 对象，当 `TcpConnection::connectEstablished()` 尝试调用它时，就会抛出 `std::bad_function_call` 异常，导致服务器崩溃。

7.  **关于“立即崩溃”的误解**: 虽然 `std::bad_function_call` 发生在新连接建立时，但由于客户端 (`client.py`) 连接速度非常快，以及服务器在接受连接后会立即处理连接建立逻辑，使得服务器看起来像是在启动时就立即崩溃了。当我尝试在后台启动服务器后运行客户端时，由于服务器立即崩溃，导致客户端收到了 `Connection refused` 错误，这进一步加剧了误解。

### 解决方法

为了解决这个问题，需要确保所有 `std::function` 对象在使用前都已被正确初始化。

1.  **为 `TcpServer` 的回调成员提供默认实现**:
    *   在 `TcpServer.cpp` 中定义 `defaultConnectionCallback` 和 `defaultMessageCallback` 两个默认的回调函数。
    *   在 `TcpServer` 的构造函数中，将这些默认回调赋值给 `connectionCallback_` 和 `messageCallback_` 成员变量。这样即使 `main.cpp` 没有设置，它们也不会是空的。

2.  **确保 `setMessageCallback` 被调用**:
    *   在 `TcpServer::newConnection` 函数中，除了设置 `connectionCallback_`，还需补上 `conn->setMessageCallback(messageCallback_);`。这确保 `TcpConnection` 能接收到消息回调。

3.  **优化 `TcpConnection::handleRead` 的设计**:
    *   最初，`TcpConnection::handleRead` 内部包含了详细的消息解析逻辑。在设置 `defaultMessageCallback` 时，为了快速修复，我将这部分逻辑复制到了 `defaultMessageCallback` 中，造成了冗余。
    *   **最终修正**: 将 `TcpConnection::handleRead` 简化，使其不再进行消息解析，而是直接调用 `messageCallback_` 并传递 `inputBuffer_`。这样，所有的消息解析逻辑都统一由 `defaultMessageCallback`（或用户自定义的消息回调）来处理，使得代码结构更清晰，更符合回调的设计意图。

**代码修改要点**：
*   **`src/TcpServer.cpp`**:
    *   添加 `defaultConnectionCallback` 和 `defaultMessageCallback` 的定义。
    *   在 `TcpServer` 构造函数初始化列表中添加 `connectionCallback_(defaultConnectionCallback)` 和 `messageCallback_(defaultMessageCallback)`。
    *   在 `newConnection` 函数中，添加 `conn->setMessageCallback(messageCallback_);`。
*   **`src/TcpConnection.cpp`**:
    *   修改 `handleRead` 函数，移除内部的消息解析循环，改为直接调用 `messageCallback_(shared_from_this(), &inputBuffer_);`。
*   **`include/TcpConnection.h`**:
    *   为了使 `defaultConnectionCallback` 能够访问连接的对端 IP，添加了公有方法 `const sockaddr_in& peerAddress() const { return peerAddr_; }`。
*   **`src/EpollPoller.cpp`**:
    *   为了解决编译 `TcpServer.cpp` 后 `defaultConnectionCallback` 中的 `ownerLoop_->assertInLoopThread()` 错误，添加 `#include "EventLoop.h"`。

通过这些修复，服务器能够稳定启动并正确处理客户端的连接与消息交互，彻底解决了 `std::bad_function_call` 崩溃。
