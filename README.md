# EchoMesh - 高性能 C++20 实时语音通讯系统 (服务端网络骨架 + 协议与逻辑层)

## 项目简介
EchoMesh 旨在开发一个支持高并发文字聊天与低延迟实时语音的 Linux 服务端及客户端。此阶段已完成服务端的网络底层骨架搭建以及基础的协议和业务逻辑层实现。

## 已完成工作 (服务端网络骨架 & 协议与逻辑层)

### 技术栈
*   **语言**: C++20 (已利用智能指针, `std::jthread`, `std::span` 等特性)
*   **网络**: Linux Epoll (边缘触发 ET 模式)
*   **消息序列化**: Google Protobuf
*   **构建系统**: CMake

### 核心模块 (当前阶段)
1.  **Network 模块 (Reactor 模式)**:
    *   基于 Epoll 实现非阻塞 I/O。
    *   实现了 Reactor 模式的 `EventLoop`、`EpollPoller` 和 `Channel` 类。
    *   利用 `EventLoopThreadPool` 管理多个 I/O 线程，每个线程运行独立的 `EventLoop`。
    *   封装了一个 `Buffer` 类来管理读写数据，支持消息长度前缀的解析。
    *   实现了 `Acceptor` 类用于处理新连接的接受。
    *   实现了 `TcpServer` 和 `TcpConnection` 类来管理服务器的生命周期和客户端连接。
2.  **Protocol 模块**:
    *   定义了基于 Google Protobuf 的消息格式 (`message.proto`)。
    *   `EchoMsg` 作为统一消息信封，包含 `LoginRequest`, `LoginResponse`, `ChatMsg`, `RoomAction`, `VoiceAnnounce` 等消息类型。
    *   定义了 `MsgType` 枚举和 `StatusCode` 错误码。
3.  **Logic 模块**:
    *   `MsgDispatcher`：一个线程安全的单例消息分发器，用于注册和分发不同 `MsgType` 的消息处理器。
    *   `UserManager`：管理在线用户及其连接。
    *   `RoomManager`：管理聊天室，支持房间创建、加入、退出，并实现了房间内消息广播功能。
    *   业务逻辑层与网络层通过回调接口对接，并使用 `std::shared_ptr` 管理连接对象，确保连接安全。

### 代码规范
*   代码符合现代 C++ 规范，广泛使用智能指针 (`std::unique_ptr`, `std::shared_ptr`)。
*   严格禁止手动 `delete`，以避免内存泄漏和悬空指针问题。
*   C++20 特性（如 `std::jthread`）的应用。

## 如何构建和运行

### 1. 构建项目
在项目根目录下执行以下命令：
```bash
# 确保安装了 CMake 和 Protobuf 编译器 (protoc)
# 在 Debian/Ubuntu 上: sudo apt-get install cmake libprotobuf-dev protobuf-compiler

mkdir -p build
cd build
cmake ..
make
```
这将在 `build/` 目录下生成可执行文件 `echomesh_server`。

### 2. 运行服务器
在 `build/` 目录下执行：
```bash
./echomesh_server
```
服务器将在 `localhost:8888` 监听传入连接。

### 3. 测试服务器 (需要一个客户端)
由于引入了 Protobuf 协议，您不能再直接使用 `netcat` 进行简单文本测试。您需要一个能够序列化和发送 Protobuf 消息的客户端。

#### 示例 Python 客户端 (用于发送登录请求):

您可以在另一个终端中运行以下 Python 脚本来测试登录功能。首先，确保安装了 `protobuf` 库：`pip install protobuf`。

```python
import socket
import struct
from proto import message_pb2 # 假设 proto 文件夹与客户端脚本在同一目录下

def create_login_request(username, password):
    login_req = message_pb2.LoginRequest()
    login_req.username = username
    login_req.password = password

    echo_msg = message_pb2.EchoMsg()
    echo_msg.type = message_pb2.MT_LOGIN_REQUEST
    echo_msg.login_request.CopyFrom(login_req)
    
    serialized_msg = echo_msg.SerializeToString()
    # Prepend message length (4 bytes, network byte order)
    length = len(serialized_msg)
    return struct.pack("!I", length) + serialized_msg

def create_room_action_request(action_type, room_id, user_id):
    room_action = message_pb2.RoomAction()
    room_action.action_type = action_type
    room_action.room_id = room_id
    room_action.user_id = user_id

    echo_msg = message_pb2.EchoMsg()
    echo_msg.type = message_pb2.MT_ROOM_ACTION
    echo_msg.room_action.CopyFrom(room_action)
    
    serialized_msg = echo_msg.SerializeToString()
    length = len(serialized_msg)
    return struct.pack("!I", length) + serialized_msg

def create_chat_message(user_id, room_id, content):
    chat_msg = message_pb2.ChatMsg()
    chat_msg.user_id = user_id
    chat_msg.room_id = room_id
    chat_msg.content = content

    echo_msg = message_pb2.EchoMsg()
    echo_msg.type = message_pb2.MT_CHAT_MSG
    echo_msg.chat_msg.CopyFrom(chat_msg)
    
    serialized_msg = echo_msg.SerializeToString()
    length = len(serialized_msg)
    return struct.pack("!I", length) + serialized_msg

def receive_message(sock):
    # Read 4-byte length prefix
    len_bytes = sock.recv(4)
    if not len_bytes:
        return None
    length = struct.unpack("!I", len_bytes)[0]
    
    # Read the actual protobuf message
    data = b''
    while len(data) < length:
        packet = sock.recv(length - len(data))
        if not packet:
            return None
        data += packet
    
    echo_msg = message_pb2.EchoMsg()
    echo_msg.ParseFromString(data)
    return echo_msg

if __name__ == "__main__":
    HOST = 'localhost'
    PORT = 8888

    # Ensure message_pb2.py is generated in a 'proto' directory relative to this script
    # To generate: protoc --python_out=. message.proto

    try:
        # Step 1: Login
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            s.connect((HOST, PORT))
            print(f"Connected to {HOST}:{PORT}")

            # Send Login Request
            login_packet = create_login_request("test_user", "password123")
            s.sendall(login_packet)
            print("Sent login request.")

            # Receive Login Response
            login_response = receive_message(s)
            if login_response and login_response.type == message_pb2.MT_LOGIN_RESPONSE:
                print(f"Login Response: Status={login_response.login_response.status_code}, UserID={login_response.login_response.user_id}, Message='{login_response.login_response.message}'")
                user_id = login_response.login_response.user_id
            else:
                print("Failed to receive login response or invalid response.")
                user_id = 0

            if user_id:
                # Step 2: Create a room
                create_room_packet = create_room_action_request(message_pb2.RA_CREATE, "roomA", user_id)
                s.sendall(create_room_packet)
                print(f"Sent create room (roomA) request for user {user_id}.")
                # Server doesn't send a response for room actions in current implementation,
                # you'd ideally add one.

                # Step 3: Join the room (if not already joined by create)
                join_room_packet = create_room_action_request(message_pb2.RA_JOIN, "roomA", user_id)
                s.sendall(join_room_packet)
                print(f"Sent join room (roomA) request for user {user_id}.")

                # Step 4: Send a chat message
                chat_packet = create_chat_message(user_id, "roomA", "Hello everyone in roomA!")
                s.sendall(chat_packet)
                print(f"Sent chat message from user {user_id} in roomA.")

                # Keep receiving for a short while to see broadcast messages
                print("Listening for incoming messages (e.g., chat broadcasts)...")
                s.settimeout(5) # Set a timeout for receiving
                try:
                    while True:
                        msg = receive_message(s)
                        if msg:
                            if msg.type == message_pb2.MT_CHAT_MSG:
                                print(f"Received Chat Message: UserID={msg.chat_msg.user_id}, RoomID='{msg.chat_msg.room_id}', Content='{msg.chat_msg.content}'")
                            else:
                                print(f"Received other message type: {msg.type}")
                        else:
                            print("No more messages or connection closed by server.")
                            break
                except socket.timeout:
                    print("Socket receive timed out.")


            s.close()
            print("Connection closed.")

    except ConnectionRefusedError:
        print(f"Connection refused. Is the server running on {HOST}:{PORT}?")
    except FileNotFoundError:
        print("Error: 'proto/message_pb2.py' not found. Please generate it using: protoc --python_out=. proto/message.proto")
    except Exception as e:
        print(f"An error occurred: {e}")

```
**注意：**
1.  您需要将 `proto/message.proto` 文件复制到 Python 客户端脚本所在的目录下的 `proto` 文件夹中。
2.  然后，在 `proto` 文件夹中运行 `protoc --python_out=. message.proto` 命令来生成 `message_pb2.py` 文件。
3.  确保服务器 `echomesh_server` 正在运行。

## 下一步计划
在协议与逻辑层稳定后，我们将逐步集成以下模块：
*   **Voice 模块**: 封装 Opus 音频编解码逻辑，集成 PortAudio/ALSA 实现音频 I/O。
*   **Audio Engine (Jitter Buffer)**: 实现应用层抖动缓冲区。
*   **Router/SFU 模块**: 服务端语音包的快速路由转发。