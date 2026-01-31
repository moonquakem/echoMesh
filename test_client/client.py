import socket
import struct
import threading
import argparse
import pyaudio
import opuslib # Import the opus library
from proto import message_pb2

# Audio constants
SAMPLE_RATE = 48000
CHANNELS = 1
FRAMES_PER_BUFFER = 960  # 20ms at 48kHz
FORMAT = pyaudio.paInt16

def udp_audio_thread(stop_event):
    """
    This thread listens for incoming UDP packets, decodes the Opus audio,
    and plays it back.
    """
    UDP_IP = "127.0.0.1"
    UDP_PORT = 12345

    # Initialize PyAudio for playback
    p = pyaudio.PyAudio()
    stream = p.open(format=FORMAT,
                    channels=CHANNELS,
                    rate=SAMPLE_RATE,
                    output=True,
                    frames_per_buffer=FRAMES_PER_BUFFER)

    # Initialize Opus decoder
    try:
        decoder = opuslib.Decoder(SAMPLE_RATE, CHANNELS)
    except Exception as e:
        print(f"Failed to initialize Opus decoder: {e}")
        print("Please ensure the Opus library is correctly installed on your system (e.g., 'sudo apt install libopus0')")
        return

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((UDP_IP, UDP_PORT))
    sock.settimeout(1.0)

    print(f"UDP audio listener started on {UDP_IP}:{UDP_PORT}. Waiting for audio...")

    while not stop_event.is_set():
        try:
            data, addr = sock.recvfrom(2048)
            if data:
                # First 12 bytes are custom header (seq, ts, userId)
                opus_data = data[12:]
                
                # Decode the Opus data to PCM
                pcm_data = decoder.decode(opus_data, FRAMES_PER_BUFFER)
                
                # Write the decoded PCM data to the audio stream
                stream.write(pcm_data)

        except socket.timeout:
            continue
        except Exception as e:
            # This can happen if a packet is corrupted or invalid
            # print(f"Opus decoding error: {e}")
            pass
            
    print("UDP audio listener stopping.")
    stream.stop_stream()
    stream.close()
    p.terminate()
    sock.close()


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
    if not len_bytes:
        return None
    length = struct.unpack("!I", len_bytes)[0]
    
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
    parser = argparse.ArgumentParser(description="EchoMesh Client")
    parser.add_argument("username", help="Your username")
    parser.add_argument("room_id", help="The room ID to join")
    args = parser.parse_args()

    HOST = 'localhost'
    PORT = 8888

    stop_audio_thread = threading.Event()
    audio_thread = threading.Thread(target=udp_audio_thread, args=(stop_audio_thread,), daemon=True)
    audio_thread.start()

    user_id = 0
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            s.connect((HOST, PORT))
            print(f"Connected to {HOST}:{PORT}")

            # Step 1: Login
            login_packet = create_login_request(args.username)
            s.sendall(login_packet)
            print(f"Sent login request for user '{args.username}'.")

            login_response = receive_message(s)
            if login_response and login_response.type == message_pb2.MT_LOGIN_RESPONSE and login_response.login_response.status_code == 0:
                user_id = login_response.login_response.user_id
                print(f"Login successful. UserID: {user_id}")
            else:
                print("Login failed.")

            # Step 2: Join Room
            if user_id:
                print(f"Attempting to join room '{args.room_id}'...")
                join_room_packet = create_room_action_request(message_pb2.RA_JOIN, args.room_id, user_id)
                s.sendall(join_room_packet)
                print(f"Sent join room request.")
                
                print("\nClient is running. Listening for voice chat.")
                print("Press Ctrl+C to exit.")
                while True:
                    threading.Event().wait(1)

    except ConnectionRefusedError:
        print(f"Connection refused. Is the server running on {HOST}:{PORT}?")
    except KeyboardInterrupt:
        print("\nExiting client.")
    except Exception as e:
        print(f"An error occurred: {e}")
    finally:
        stop_audio_thread.set()
        if 'audio_thread' in locals() and audio_thread.is_alive():
            audio_thread.join()
        print("Client shut down.")
