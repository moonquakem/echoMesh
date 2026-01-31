import socket
import struct
import threading
import argparse
import pyaudio
import opuslib
import time
from proto import message_pb2

# --- Audio Constants ---
SAMPLE_RATE = 48000
CHANNELS = 1
FRAMES_PER_BUFFER = 960  # For 20ms audio frames
FORMAT = pyaudio.paInt16
OPUS_BITRATE = 64000

# --- Server Addresses ---
TCP_HOST = 'localhost'
TCP_PORT = 8888
UDP_HOST = 'localhost'
UDP_PORT = 9999

def audio_receiver_thread(sock, stop_event):
    """
    Listens for incoming UDP packets from the server, decodes the Opus audio,
    and plays it back.
    """
    p = pyaudio.PyAudio()
    stream = p.open(format=FORMAT,
                    channels=CHANNELS,
                    rate=SAMPLE_RATE,
                    output=True,
                    frames_per_buffer=FRAMES_PER_BUFFER)
    
    decoder = opuslib.Decoder(SAMPLE_RATE, CHANNELS)
    print("Audio receiver thread started. Waiting for audio from server...")

    while not stop_event.is_set():
        try:
            data, addr = sock.recvfrom(2048)
            if data:
                # Unpack header
                sequence, timestamp, userId = struct.unpack("!III", data[:12])
                opus_data = data[12:]
                
                # Decode and play
                pcm_data = decoder.decode(opus_data, FRAMES_PER_BUFFER)
                stream.write(pcm_data)
        except socket.timeout:
            continue
        except Exception as e:
            # print(f"Receiver error: {e}") # Can be noisy
            pass
            
    print("Audio receiver stopping.")
    stream.stop_stream()
    stream.close()
    p.terminate()

def audio_sender_thread(sock, user_id, server_address, stop_event):
    """
    Captures audio from the microphone, encodes it with Opus, and sends it
    to the server.
    """
    p = pyaudio.PyAudio()
    stream = p.open(format=FORMAT,
                    channels=CHANNELS,
                    rate=SAMPLE_RATE,
                    input=True,
                    frames_per_buffer=FRAMES_PER_BUFFER)

    encoder = opuslib.Encoder(SAMPLE_RATE, CHANNELS, opuslib.APPLICATION_VOIP)
    encoder.bitrate = OPUS_BITRATE
    
    sequence = 0
    print("Audio sender thread started. Capturing audio...")

    while not stop_event.is_set():
        try:
            pcm_data = stream.read(FRAMES_PER_BUFFER)
            opus_data = encoder.encode(pcm_data, FRAMES_PER_BUFFER)
            
            # Pack header and send
            timestamp = int(time.time())
            header = struct.pack("!III", sequence, timestamp, user_id)
            packet = header + opus_data
            
            sock.sendto(packet, server_address)
            sequence += 1
        except Exception as e:
            print(f"Sender error: {e}")
            break
    
    print("Audio sender stopping.")
    stream.stop_stream()
    stream.close()
    p.terminate()


def create_login_request(username):
    # (omitted for brevity - same as before)
    login_req = message_pb2.LoginRequest()
    login_req.username = username
    login_req.password = "password123"
    echo_msg = message_pb2.EchoMsg()
    echo_msg.type = message_pb2.MT_LOGIN_REQUEST
    echo_msg.login_request.CopyFrom(login_req)
    serialized_msg = echo_msg.SerializeToString()
    return struct.pack("!I", len(serialized_msg)) + serialized_msg

def create_room_action_request(action_type, room_id, user_id):
    # (omitted for brevity - same as before)
    room_action = message_pb2.RoomAction()
    room_action.action_type = action_type
    room_action.room_id = room_id
    room_action.user_id = user_id
    echo_msg = message_pb2.EchoMsg()
    echo_msg.type = message_pb2.MT_ROOM_ACTION
    echo_msg.room_action.CopyFrom(room_action)
    serialized_msg = echo_msg.SerializeToString()
    return struct.pack("!I", len(serialized_msg)) + serialized_msg

def receive_message(sock):
    # (omitted for brevity - same as before)
    len_bytes = sock.recv(4)
    if not len_bytes: return None
    length = struct.unpack("!I", len_bytes)[0]
    data = b''
    while len(data) < length:
        packet = sock.recv(length - len(data))
        if not packet: return None
        data += packet
    echo_msg = message_pb2.EchoMsg()
    echo_msg.ParseFromString(data)
    return echo_msg


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="EchoMesh Client")
    parser.add_argument("username", help="Your username")
    parser.add_argument("room_id", help="The room ID to join")
    args = parser.parse_args()

    user_id = 0
    stop_event = threading.Event()
    
    # Create UDP socket for audio
    udp_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    udp_socket.settimeout(1.0)
    
    receiver = None
    sender = None

    try:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as tcp_socket:
            tcp_socket.connect((TCP_HOST, TCP_PORT))
            print(f"Connected to TCP server at {TCP_HOST}:{TCP_PORT}")

            # Step 1: Login
            tcp_socket.sendall(create_login_request(args.username))
            login_response = receive_message(tcp_socket)
            if login_response and login_response.type == message_pb2.MT_LOGIN_RESPONSE and login_response.login_response.status_code == 0:
                user_id = login_response.login_response.user_id
                print(f"Login successful. UserID: {user_id}")
            else:
                raise Exception("Login failed.")

            # Step 2: Join Room
            tcp_socket.sendall(create_room_action_request(message_pb2.RA_JOIN, args.room_id, user_id))
            print(f"Sent join room request for room '{args.room_id}'.")
            
            # Step 3: Start audio threads
            server_udp_address = (UDP_HOST, UDP_PORT)
            
            receiver = threading.Thread(target=audio_receiver_thread, args=(udp_socket, stop_event))
            sender = threading.Thread(target=audio_sender_thread, args=(udp_socket, user_id, server_udp_address, stop_event))
            
            receiver.start()
            sender.start()

            print("\nVoice chat running. Press Ctrl+C to exit.")
            while True:
                time.sleep(1)

    except ConnectionRefusedError:
        print(f"Connection refused. Is the server running on {TCP_HOST}:{TCP_PORT}?")
    except KeyboardInterrupt:
        print("\nExiting client.")
    except Exception as e:
        print(f"An error occurred: {e}")
    finally:
        stop_event.set()
        if receiver: receiver.join()
        if sender: sender.join()
        udp_socket.close()
        print("Client shut down.")