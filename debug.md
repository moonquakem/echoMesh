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