# EchoMesh - C++20 实时语音聊天室

## 项目简介
EchoMesh 是一个功能完善的多人实时语音聊天室应用。它采用客户端-服务器架构，服务端使用 C++20 实现，作为一个高效的音频转发单元 (SFU)；客户端使用 Python 实现，能够采集、编码、发送、接收、解码并播放实时音频流。

用户可以登录、加入不同的房间，并在房间内与其他人进行语音通话。

## 技术栈与架构
*   **服务端 (C++)**:
    *   **语言**: C++20
    *   **网络**: 基于 Linux Epoll 的 Reactor 模式，实现高并发网络处理。
        *   **TCP (端口 8888)**: 用于信令传输，如登录、加入/离开房间等。
        *   **UDP (端口 9999)**: 用于实时音频数据的接收和转发。
    *   **架构**: 服务端作为 SFU (Selective Forwarding Unit)，接收来自一个客户端的音频流，并将其转发给同一房间中的所有其他客户端。
    *   **消息**: 使用 Google Protobuf 定义和序列化信令。
    *   **构建**: 使用 CMake。

*   **客户端 (Python)**:
    *   **音频**: 使用 `PyAudio` 库进行音频的录制和播放，使用 `opuslib` 进行高质量的 Opus 编解码。
    *   **多线程**: 同时运行音频发送（录音->编码->发送）和接收（接收->解码->播放）线程。
    *   **模式**: 支持“说话者”和“收听者”两种模式，以解决在同一台机器上测试时的音频设备独占问题。
    *   **设备诊断**: 提供工具来列出和选择音频设备，解决不同硬件环境下的兼容性问题。

## 如何构建与运行

### 1. 依赖安装
在开始之前，请确保您已安装所有必要的依赖。

**服务端 (Debian/Ubuntu):**
```bash
sudo apt-get update
sudo apt-get install build-essential cmake libprotobuf-dev protobuf-compiler libopus-dev portaudio19-dev
```

**客户端 (Python):**
```bash
# 确保已安装 Python 3 和 venv
sudo apt-get install python3 python3.12-venv
```

### 2. 构建服务端
```bash
mkdir -p build
cd build
cmake ..
make
```
编译成功后，会在 `build` 目录下生成可执行文件 `echomesh_server`。

### 3. 配置并运行客户端
我们将在单机上通过运行两个客户端（一个说话，一个收听）来模拟语音通话。

**A. 准备 Python 环境**
首先，进入客户端目录并设置好虚拟环境。
```bash
cd test_client

# 创建虚拟环境
python3 -m venv venv

# 安装所有 Python 依赖
venv/bin/pip install protobuf pyaudio opuslib
```

**B. (仅首次需要) 查找麦克风设备**
为了避免录音错误，我们需要找到您麦克风对应的设备号。
```bash
# 运行设备诊断命令
venv/bin/python3 client.py --list-devices
```
在输出的列表中，找到代表您麦克风的设备（通常名字里包含 `Mic` 或 `ADC`，并且 `Max Input Channels` 大于0），记下它的 `Device Index`。

**C. 开始测试！**
现在，打开三个终端，并都进入 `echoMesh` 项目的根目录。

*   **终端 1: 启动服务端**
    ```bash
    ./build/echomesh_server
    ```

*   **终端 2: 启动说话者 (Client A)**
    将下面的 `<YOUR_DEVICE_INDEX>` 替换为您在上一步中找到的麦克风设备号 (例如, `0`)。
    ```bash
    cd test_client
    venv/bin/python3 client.py user_A room_1 --mode speaker --input-device <YOUR_DEVICE_INDEX>
    ```
    您应该会看到它成功登录并开始打印 `S`，表示正在发送语音。

*   **终端 3: 启动收听者 (Client B)**
    ```bash
    cd test_client
    venv/bin/python3 client.py user_B room_1 --mode listener
    ```
    当 **Client A** 的麦克风有声音输入时，这个客户端应该会开始打印 `R`，并且您的扬声器会播放出声音。

### 4. (可选) 两台电脑测试
如果您想在两台不同的电脑（A和B）上测试：
1.  在电脑A上启动 `echomesh_server`。
2.  使用 `ip addr show` 找到电脑A的局域网IP地址 (例如 `192.168.1.10`)。
3.  在电脑B上，修改 `test_client/client.py` 文件顶部的 `TCP_HOST` 和 `UDP_HOST`，将其值从 `'localhost'` 改为电脑A的IP地址。
4.  在电脑B上运行客户端。
5.  **注意**: 确保电脑A的防火墙允许 TCP 端口 `8888` 和 UDP 端口 `9999` 的入站连接。
