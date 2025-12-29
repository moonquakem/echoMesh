# EchoMesh - 高性能 C++20 实时语音通讯系统 (服务端网络骨架)

## 项目简介
EchoMesh 旨在开发一个支持高并发文字聊天与低延迟实时语音的 Linux 服务端及客户端。此阶段主要完成了服务端的网络底层骨架搭建。

## 已完成工作 (服务端网络骨架)

### 技术栈
*   **语言**: C++20 (已利用智能指针, `std::jthread`, `std::span` 等特性)
*   **网络**: Linux Epoll (边缘触发 ET 模式)
*   **构建系统**: CMake

### 核心模块 (当前阶段)
1.  **Network 模块 (Reactor 模式)**:
    *   基于 Epoll 实现非阻塞 I/O。
    *   实现了 Reactor 模式的 `EventLoop`、`EpollPoller` 和 `Channel` 类。
    *   包含一个简单的 `ThreadPool` 来处理已就绪的任务。
    *   封装了一个 `Buffer` 类来管理读写数据。
    *   实现了 `Acceptor` 类用于处理新连接的接受。
    *   实现了 `TcpServer` 和 `TcpConnection` 类来管理服务器的生命周期和客户端连接。

### 代码规范
*   代码符合现代 C++ 规范，广泛使用智能指针 (`std::unique_ptr`, `std::shared_ptr`)。
*   严格禁止手动 `delete`，以避免内存泄漏和悬空指针问题。

## 如何构建和运行

### 1. 构建项目
在项目根目录下执行以下命令：
```bash
cmake -S . -B build
cmake --build build
```
这将在 `build/` 目录下生成可执行文件 `echomesh_server`。

### 2. 运行服务器
在项目根目录下执行：
```bash
./build/echomesh_server
```
服务器将在 `localhost:8888` 监听传入连接。

### 3. 测试服务器
您可以使用 `netcat` (或 `nc`) 工具来测试服务器的回显功能。
打开一个新的终端，执行：
```bash
nc localhost 8888
```
连接成功后，您可以输入任何文本并按回车。服务器会将您发送的消息回显回来。

## 下一步计划
在网络底层骨架稳定后，我们将逐步集成以下模块：
*   **Protocol 模块**: 引入 Google Protobuf 进行消息序列化。
*   **Voice 模块**: 封装 Opus 音频编解码逻辑，集成 PortAudio/ALSA 实现音频 I/O。
*   **Audio Engine (Jitter Buffer)**: 实现应用层抖动缓冲区。
*   **Router/SFU 模块**: 服务端语音包的快速路由转发。
