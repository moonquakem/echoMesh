# EchoMesh - 高性能 C++20 实时语音通讯系统

## 项目简介
EchoMesh 旨在开发一个支持高并发文字聊天与低延迟实时语音的 Linux 服务端及客户端。此项目目前包含了服务端的核心功能，包括基于Epoll的TCP网络服务、业务逻辑处理以及一个完整的多线程音频引擎，实现了从音频采集、Opus编码、UDP发送到网络抖动缓冲的完整实时语音链路。

## 技术栈
*   **语言**: C++20
*   **网络**: Linux Epoll (TCP/IP for signaling, UDP for media), Reactor模式
*   **消息序列化**: Google Protobuf
*   **音频**: Opus Codec, PortAudio
*   **构建系统**: CMake

## 核心模块

### 1. 服务端网络骨架 & 协议与逻辑层 (TCP)
*   **Network 模块 (Reactor 模式)**:
    *   基于 Epoll 实现非阻塞 I/O。
    *   实现了 Reactor 模式的 `EventLoop`、`EpollPoller` 和 `Channel` 类。
    *   利用 `EventLoopThreadPool` 管理多个 I/O 线程，每个线程运行独立的 `EventLoop`。
    *   封装了一个 `Buffer` 类来管理读写数据，支持消息长度前缀的解析。
    *   实现了 `Acceptor` 类用于处理新连接的接受。
    *   实现了 `TcpServer` 和 `TcpConnection` 类来管理服务器的生命周期和客户端连接。
*   **Protocol 模块**:
    *   定义了基于 Google Protobuf 的消息格式 (`message.proto`)。
    *   `EchoMsg` 作为统一消息信封，包含 `LoginRequest`, `LoginResponse`, `ChatMsg`, `RoomAction` 等消息类型。
*   **Logic 模块**:
    *   `MsgDispatcher`：一个线程安全的单例消息分发器，用于注册和分发不同 `MsgType` 的消息处理器。
    *   `UserManager`：管理在线用户及其连接。
    *   `RoomManager`：管理聊天室，支持房间创建、加入、退出，并实现了房间内消息广播功能。

### 2. 音频引擎 (Audio Engine - UDP)
*   **Opus 封装 (OpusWrapper)**:
    *   集成了 `libopus` 库，提供高性能的音频压缩和解压。
    *   配置为48kHz采样率和VOIP应用场景 (`OPUS_APPLICATION_VOIP`)，在保证音质的同时，显著降低了带宽需求。
*   **UDP 传输与语音包 (UdpSender & VoicePacket)**:
    *   定义了 `VoicePacket` 结构体，包含自定义头部（序列号, 时间戳, 用户ID），用于网络传输。
    *   `UdpSender` 类负责将编码后的Opus包通过UDP协议发送出去。
*   **抖动缓冲 (JitterBuffer)**:
    *   实现了一个基于 `std::priority_queue` 的抖动缓冲区，用于解决UDP网络传输中的包乱序和抖动问题。
    *   通过对数据包按序列号进行重排序，保证音频的平滑播放。
    *   包含一个清理机制，当缓冲区超过预设大小时，会自动丢弃旧的数据包。
*   **多线程音频处理**:
    *   `AudioEngine` 类管理着一个多线程的音频处理流水线。
    *   **采集线程**: 使用 `PortAudio` 库从麦克风采集PCM音频数据。
    *   **发送线程**: 从一个线程安全的环形缓冲区 (`RingBuffer`) 中获取PCM数据，使用 `OpusWrapper` 进行编码，然后通过 `UdpSender` 发送。
    *   这种设计将音频I/O和网络发送分离，降低了延迟，并提高了系统的响应性。

## 如何构建和运行

### 1. 构建项目
在项目根目录下执行以下命令：
```bash
# 确保安装了 CMake, Protobuf, Opus, 和 PortAudio
# 在 Debian/Ubuntu 上: sudo apt-get install cmake libprotobuf-dev protobuf-compiler libopus-dev portaudio19-dev

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
服务器将在 `localhost:8888` (TCP) 监听传入连接，并开始通过UDP在 `localhost:12345` 发送音频数据。

### 3. 测试服务器
您可以使用提供的Python测试客户端 (`test_client/client.py`) 来与服务器进行交互。该客户端会尝试登录，加入聊天室，并同时在一个单独的线程中监听UDP端口以接收音频包。

#### 示例 Python 客户端 (`test_client/client.py`):
确保安装了 `protobuf` 库: `pip install protobuf`。

```python
import socket
import struct
import threading
from proto import message_pb2 # Assuming proto folder is in the same directory as this script

def udp_server_thread():
    UDP_IP = "127.0.0.1"
    UDP_PORT = 12345

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((UDP_IP, UDP_PORT))

    print(f"UDP server listening on {UDP_IP}:{UDP_PORT}")

    while True:
        data, addr = sock.recvfrom(1024) # buffer size is 1024 bytes
        if not data:
            break
        
        # Deserialize VoicePacket
        sequence = struct.unpack("!I", data[0:4])[0]
        timestamp = struct.unpack("!I", data[4:8])[0]
        userId = struct.unpack("!I", data[8:12])[0]
        
        print(f"Received voice packet: seq={sequence}, ts={timestamp}, user={userId}, size={len(data)} bytes")


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

    udp_thread = threading.Thread(target=udp_server_thread, daemon=True)
    udp_thread.start()

    try:
        # Step 1: Login
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            s.connect((HOST, PORT))
            print(f"Connected to {HOST}:{PORT}")

            # Send Login Request
            login_packet = create_login_request("test_user", "password123")
            s.sendall(.py
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

                # Step 3: Join the room
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
        print("Error: 'proto/message_pb2.py' not found. Please ensure it's generated in the 'proto' subdirectory.")
    except Exception as e:
        print(f"An error occurred: {e}")
```
**注意：**
1.  您需要将 `proto/message.proto` 文件复制到 Python 客户端脚本所在的目录下的 `proto` 文件夹中。
2.  然后，在 `proto` 文件夹中运行 `protoc --python_out=. message.proto` 命令来生成 `message_pb2.py` 文件。
3.  确保服务器 `echomesh_server` 正在运行。

## 后续步骤
*   **客户端实现**: 开发一个完整的客户端，能够接收UDP音频包，通过`JitterBuffer`和`Opus`解码，并使用`PortAudio`播放出来，从而完成端到端的实时语音通话。
*   **媒体转发 (SFU)**: 在服务端实现媒体转发逻辑，允许多个用户在同一个房间内进行语音通话。
*   **优化与测试**: 对系统进行性能分析和压力测试，进一步优化延迟和资源占用。