import socket
import struct
import threading
import argparse
import pyaudio
import opuslib
import time
import sys
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

def list_audio_devices():
    """Prints all available audio devices and their information."""
    print("Listing available audio devices:")
    p = pyaudio.PyAudio()
    info = p.get_host_api_info_by_index(0)
    numdevices = info.get('deviceCount')
    for i in range(0, numdevices):
        device_info = p.get_device_info_by_host_api_device_index(0, i)
        print(f"Device Index: {i}")
        print(f"  Name: {device_info.get('name')}")
        print(f"  Max Input Channels: {device_info.get('maxInputChannels')}")
        print(f"  Max Output Channels: {device_info.get('maxOutputChannels')}")
        print("-" * 20)
    p.terminate()


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
    received_packet_count = 0

    while not stop_event.is_set():
        try:
            data, addr = sock.recvfrom(2048)
            if data:
                received_packet_count += 1
                if received_packet_count % 100 == 0:
                    print("R", end="", flush=True)
                # Unpack header
                sequence, timestamp, userId = struct.unpack("!III", data[:12])
                opus_data = data[12:]
                
                # Decode and play
                pcm_data = decoder.decode(opus_data, FRAMES_PER_BUFFER)
                stream.write(pcm_data)
        except socket.timeout:
            continue
        except Exception:
            # print(f"Receiver error: {e}") # Can be noisy
            pass
            
    print("\nAudio receiver stopping.")
    stream.stop_stream()
    stream.close()
    p.terminate()

def audio_sender_thread(sock, user_id, server_address, stop_event, input_device_index=None):
    """
    Captures audio from the specified input device, encodes it with Opus, and sends it
    to the server.
    """
    p = pyaudio.PyAudio()
    try:
        stream = p.open(format=FORMAT,
                        channels=CHANNELS,
                        rate=SAMPLE_RATE,
                        input=True,
                        frames_per_buffer=FRAMES_PER_BUFFER,
                        input_device_index=input_device_index)
    except Exception as e:
        print(f"Error opening audio input stream: {e}")
        print("Please check if the device index is correct and the microphone is available.")
        return

    encoder = opuslib.Encoder(SAMPLE_RATE, CHANNELS, opuslib.APPLICATION_VOIP)
    encoder.bitrate = OPUS_BITRATE
    
    sequence = 0
    sent_packet_count = 0
    print(f"Audio sender thread started on device index {input_device_index or 'default'}. Capturing audio...")

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
            sent_packet_count += 1
            if sent_packet_count % 100 == 0:
                print("S", end="", flush=True)
        except Exception as e:
            print(f"Sender error: {e}")
            break
    
    print("\nAudio sender stopping.")
    stream.stop_stream()
    stream.close()
    p.terminate()

# (create_login_request, create_room_action_request, receive_message are unchanged and omitted for brevity)
def create_login_request(username):
    login_req = message_pb2.LoginRequest()
    login_req.username = username
    login_req.password = "password123"
    echo_msg = message_pb2.EchoMsg()
    echo_msg.type = message_pb2.MT_LOGIN_REQUEST
    echo_msg.login_request.CopyFrom(login_req)
    serialized_msg = echo_msg.SerializeToString()
    return struct.pack("!I", len(serialized_msg)) + serialized_msg
def create_room_action_request(action_type, room_id, user_id):
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
    parser.add_argument("--list-devices", action="store_true", help="List available audio devices and exit.")
    parser.add_argument("--input-device", type=int, help="The index of the audio input device to use.")
    # Add optional positional arguments for easier use with list-devices
    parser.add_argument("username", nargs='?', default="user", help="Your username (required unless listing devices).")
    parser.add_argument("room_id", nargs='?', default="room", help="The room ID to join (required unless listing devices).")
    parser.add_argument("--mode", choices=["speaker", "listener"], default="speaker",
                        help="Run client in 'speaker' (records) or 'listener' (plays) mode.")
    args = parser.parse_args()

    if args.list_devices:
        list_audio_devices()
        sys.exit(0)

    user_id = 0
    stop_event = threading.Event()
    udp_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    udp_socket.settimeout(1.0)
    
    receiver = None
    sender = None

    try:
        # (TCP connection, login, join room logic is unchanged and omitted for brevity)
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as tcp_socket:
            tcp_socket.connect((TCP_HOST, TCP_PORT))
            print(f"Connected to TCP server at {TCP_HOST}:{TCP_PORT}")
            tcp_socket.sendall(create_login_request(args.username))
            login_response = receive_message(tcp_socket)
            if login_response and login_response.type == message_pb2.MT_LOGIN_RESPONSE and login_response.login_response.status_code == 0:
                user_id = login_response.login_response.user_id
                print(f"Login successful. UserID: {user_id}")
            else:
                raise Exception("Login failed.")

            # Step 2: Join Room and wait for confirmation
            tcp_socket.sendall(create_room_action_request(message_pb2.RA_JOIN, args.room_id, user_id))
            print(f"Sent join room request for room '{args.room_id}'.")

            join_response = receive_message(tcp_socket)
            if join_response and join_response.type == message_pb2.MT_ROOM_ACTION_RESPONSE and join_response.room_action_response.status_code == message_pb2.SC_OK:
                print("Successfully joined room.")
            else:
                error_msg = join_response.room_action_response.message if join_response else "No response"
                raise Exception(f"Failed to join room: {error_msg}")

            # Step 3: Start audio threads now that we are confirmed in the room
            server_udp_address = (UDP_HOST, UDP_PORT)
            
            if args.mode == "speaker":
                sender = threading.Thread(target=audio_sender_thread, args=(udp_socket, user_id, server_udp_address, stop_event, args.input_device))
                sender.start()
                print(f"Client running in SPEAKER mode. UserID: {user_id}.")
            elif args.mode == "listener":
                receiver = threading.Thread(target=audio_receiver_thread, args=(udp_socket, stop_event))
                receiver.start()
                print(f"Client running in LISTENER mode. UserID: {user_id}.")
                
                # Send a dummy UDP packet to register our address with the server
                dummy_header = struct.pack("!III", 0, int(time.time()), user_id)
                dummy_packet = dummy_header + b'' # Empty opus data
                udp_socket.sendto(dummy_packet, server_udp_address)
                print("Sent dummy UDP packet to server to register listener address.")
            
            print("\nVoice chat running. Press Ctrl+C to exit.")
            # We no longer need to listen for TCP messages here, just keep alive
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
        if receiver and receiver.is_alive(): receiver.join()
        if sender and sender.is_alive(): sender.join()
        udp_socket.close()
        print("Client shut down.")