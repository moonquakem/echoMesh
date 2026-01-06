import socket
import struct
from proto import message_pb2 # Assuming proto folder is in the same directory as this script

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
        print("Error: 'proto/message_pb2.py' not found. Please ensure it's generated in the 'proto' subdirectory.")
    except Exception as e:
        print(f"An error occurred: {e}")
