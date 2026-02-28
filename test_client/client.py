import threading
import argparse
import pyaudio
import opuslib
import time
import sys
import grpc

from proto import message_pb2
from proto import message_pb2_grpc

# --- Audio Constants ---
SAMPLE_RATE = 48000
CHANNELS = 1
FRAMES_PER_BUFFER = 960  # For 20ms audio frames
FORMAT = pyaudio.paInt16
OPUS_BITRATE = 64000

# --- Server Address ---
GRPC_SERVER_ADDRESS = 'localhost:8888'

# Global variable to store session token and user ID after login
# This will be used to attach metadata to subsequent RPCs
global_session_token = None
global_user_id = 0

def _get_metadata_with_token():
    """Returns metadata containing the global_session_token if available."""
    if global_session_token:
        return [('session-token', global_session_token)]
    return []

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

class EchoMeshClient:
    def __init__(self, username, room_id, input_device_index=None):
        self.username = username
        self.room_id = room_id
        self.input_device_index = input_device_index
        
        self.user_id = 0
        self.session_token = None

        self.pyaudio_instance = pyaudio.PyAudio()
        self.audio_output_stream = None
        self.opus_decoder = opuslib.Decoder(SAMPLE_RATE, CHANNELS)

        self.stop_event = threading.Event()
        self.audio_receive_thread = None

        self.channel = grpc.insecure_channel(GRPC_SERVER_ADDRESS)
        self.stub = message_pb2_grpc.EchoMeshServiceStub(self.channel)

    def _init_audio_output(self):
        """Initializes the PyAudio output stream for playback."""
        if not self.audio_output_stream:
            self.audio_output_stream = self.pyaudio_instance.open(
                format=FORMAT,
                channels=CHANNELS,
                rate=SAMPLE_RATE,
                output=True,
                frames_per_buffer=FRAMES_PER_BUFFER
            )

    def _audio_receiver(self, response_iterator):
        """Receives and plays audio packets from the server stream."""
        self._init_audio_output()
        print("Audio receiver thread started. Waiting for audio from server...")
        received_packet_count = 0
        try:
            for voice_packet in response_iterator:
                if self.stop_event.is_set():
                    break
                
                received_packet_count += 1
                if received_packet_count % 50 == 0:
                    print("R", end="", flush=True)
                
                try:
                    # Ignore packets from self
                    if voice_packet.user_id == self.user_id:
                        continue

                    pcm_data = self.opus_decoder.decode(
                        voice_packet.audio_data, FRAMES_PER_BUFFER
                    )
                    self.audio_output_stream.write(pcm_data)
                except opuslib.exceptions.OpusError as e:
                    print(f"Opus decode error: {e}", file=sys.stderr)
                except Exception as e:
                    print(f"Audio playback error: {e}", file=sys.stderr)
        except grpc.RpcError as e:
            if e.code() == grpc.StatusCode.UNAUTHENTICATED:
                print("Audio stream unauthenticated. Session expired?", file=sys.stderr)
            elif e.code() == grpc.StatusCode.CANCELLED:
                print("Audio stream cancelled (server or client shutdown).", file=sys.stderr)
            else:
                print(f"Audio receiver RPC error: {e}", file=sys.stderr)
        except Exception as e:
            print(f"Unexpected error in audio receiver: {e}", file=sys.stderr)
        finally:
            print("\nAudio receiver stopping.")
            if self.audio_output_stream:
                self.audio_output_stream.stop_stream()
                self.audio_output_stream.close()

    def _generate_audio_requests(self):
        """Generator that captures audio and yields VoicePacket objects."""
        opus_encoder = opuslib.Encoder(SAMPLE_RATE, CHANNELS, opuslib.APPLICATION_VOIP)
        opus_encoder.bitrate = OPUS_BITRATE

        try:
            audio_input_stream = self.pyaudio_instance.open(
                format=FORMAT,
                channels=CHANNELS,
                rate=SAMPLE_RATE,
                input=True,
                frames_per_buffer=FRAMES_PER_BUFFER,
                input_device_index=self.input_device_index
            )
        except Exception as e:
            print(f"Error opening audio input stream: {e}", file=sys.stderr)
            print("Please check if the device index is correct and the microphone is available.", file=sys.stderr)
            self.stop_event.set() # Stop the whole client
            return

        print(f"Audio sender started on device index {self.input_device_index or 'default'}.")
        sent_packet_count = 0
        while not self.stop_event.is_set():
            try:
                pcm_data = audio_input_stream.read(FRAMES_PER_BUFFER)
                opus_data = opus_encoder.encode(pcm_data, FRAMES_PER_BUFFER)
                
                sent_packet_count += 1
                if sent_packet_count % 50 == 0:
                    print("S", end="", flush=True)
                
                yield message_pb2.VoicePacket(audio_data=opus_data, user_id=self.user_id)
            except pyaudio.PyAudioError as e:
                print(f"PyAudio input error: {e}", file=sys.stderr)
                break
            except Exception as e:
                print(f"Audio sender error: {e}", file=sys.stderr)
                break
        
        print("\nAudio sender stopping.")
        audio_input_stream.stop_stream()
        audio_input_stream.close()

    def _stream_audio_loop(self):
        """Manages the bidirectional audio stream RPC."""
        try:
            # The stub call returns a response iterator which also handles sending requests
            response_iterator = self.stub.StreamAudio(
                self._generate_audio_requests(),
                metadata=_get_metadata_with_token()
            )
            
            # Start a separate thread to handle receiving audio from the response_iterator
            self.audio_receive_thread = threading.Thread(
                target=self._audio_receiver, args=(response_iterator,)
            )
            self.audio_receive_thread.start()
            
            # The main part of this thread (or a loop) just waits for the receiver thread to finish
            # or for the stop_event to be set. The requests are sent by the generator itself.
            self.audio_receive_thread.join()

        except grpc.RpcError as e:
            if e.code() == grpc.StatusCode.UNAUTHENTICATED:
                print("Audio stream RPC unauthenticated. Login required or session expired.", file=sys.stderr)
            elif e.code() == grpc.StatusCode.CANCELLED:
                print("Audio stream RPC cancelled (server or client shutdown).", file=sys.stderr)
            elif e.code() == grpc.StatusCode.FAILED_PRECONDITION:
                print(f"Audio stream RPC failed: {e.details()} (Are you in a room?)", file=sys.stderr)
            else:
                print(f"Audio stream RPC error: {e.details()}", file=sys.stderr)
        except Exception as e:
            print(f"Unexpected error in audio stream loop: {e}", file=sys.stderr)
        finally:
            self.stop_event.set() # Ensure all other threads stop
            print("Audio streaming loop finished.")


    def start(self):
        global global_session_token, global_user_id
        try:
            print(f"Attempting to login as '{self.username}'...")
            login_request = message_pb2.LoginRequest(username=self.username, password="password123")
            login_response = self.stub.Login(login_request)

            if login_response.status_code == message_pb2.SC_OK:
                self.user_id = login_response.user_id
                self.session_token = login_response.session_token
                global_session_token = self.session_token # Set global for metadata helper
                global_user_id = self.user_id
                print(f"Login successful. UserID: {self.user_id}, Session Token: {self.session_token[:8]}...")
            else:
                print(f"Login failed: {login_response.message}", file=sys.stderr)
                return

            print(f"Attempting to join room '{self.room_id}'...")
            room_action_request = message_pb2.RoomActionRequest(
                action_type=message_pb2.RA_CREATE_OR_JOIN, 
                room_id=self.room_id
            )
            room_response = self.stub.ManageRoom(room_action_request, metadata=_get_metadata_with_token())

            if room_response.status_code == message_pb2.SC_OK:
                print(f"Successfully joined room '{self.room_id}'.")
            else:
                print(f"Failed to join room '{self.room_id}': {room_response.message}", file=sys.stderr)
                return

            print("\nStarting audio stream. Press Ctrl+C to exit.")
            self._stream_audio_loop() # This will block until stream ends or stop_event is set

        except grpc.RpcError as e:
            print(f"RPC error during client startup: {e.details()}", file=sys.stderr)
        except KeyboardInterrupt:
            print("\nExiting client gracefully.")
        except Exception as e:
            print(f"An unexpected error occurred: {e}", file=sys.stderr)
        finally:
            self.stop_event.set() # Signal all threads to stop
            if self.audio_receive_thread and self.audio_receive_thread.is_alive():
                self.audio_receive_thread.join(timeout=1.0)
            self.channel.close()
            self.pyaudio_instance.terminate()
            print("Client shut down.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="EchoMesh Client (gRPC)")
    parser.add_argument("--list-devices", action="store_true", help="List available audio devices and exit.")
    parser.add_argument("--input-device", type=int, help="The index of the audio input device to use. Defaults to system default.")
    parser.add_argument("username", nargs='?', default="user", help="Your username.")
    parser.add_argument("room_id", nargs='?', default="room", help="The room ID to join.")
    # Mode argument is now obsolete as client always sends and receives
    # parser.add_argument("--mode", choices=["speaker", "listener"], default="speaker",
    #                     help="Run client in 'speaker' (records) or 'listener' (plays) mode.")
    args = parser.parse_args()

    if args.list_devices:
        list_audio_devices()
        sys.exit(0)

    # Validate arguments for non-list-devices mode
    if not args.username or not args.room_id:
        parser.error("Username and Room ID are required unless --list-devices is used.")

    client = EchoMeshClient(args.username, args.room_id, args.input_device)
    client.start()
