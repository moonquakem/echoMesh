import asyncio
import argparse
import time
import random
import sys
import grpc
from proto import message_pb2
from proto import message_pb2_grpc

# --- Constants ---
SAMPLE_RATE = 48000
CHANNELS = 1
FRAMES_PER_BUFFER = 960  # 20ms
GRPC_SERVER_ADDRESS = 'localhost:8888'
DUMMY_AUDIO_DATA = b'\x00' * 100 # Approx size of an Opus-encoded silent frame

class HeadlessClient:
    def __init__(self, client_id, room_id, server_address):
        self.client_id = client_id
        self.username = f"user_{client_id}"
        self.room_id = room_id
        self.server_address = server_address
        
        self.user_id = 0
        self.session_token = None
        self.stop_event = asyncio.Event()
        
        self.sent_count = 0
        self.recv_count = 0
        self.errors = 0
        self.latencies = []

    async def _audio_generator(self):
        """Generates dummy audio packets at a fixed interval."""
        while not self.stop_event.is_set():
            self.sent_count += 1
            # We can embed a timestamp in the 'audio_data' for latency measurement 
            # if we control both sides, but here we just send dummy data.
            # However, for RTT, we'd need the server to echo or some other trick.
            # For now, let's just send.
            yield message_pb2.VoicePacket(audio_data=DUMMY_AUDIO_DATA, user_id=self.user_id)
            await asyncio.sleep(0.02) # 20ms interval

    async def _audio_receiver(self, response_iterator):
        """Receives audio packets from the server."""
        try:
            async for voice_packet in response_iterator:
                if self.stop_event.is_set():
                    break
                self.recv_count += 1
        except grpc.aio.AioRpcError as e:
            if not self.stop_event.is_set():
                print(f"Client {self.username} receiver error: {e.code()}")
                self.errors += 1

    async def run(self, duration):
        async with grpc.aio.insecure_channel(self.server_address) as channel:
            stub = message_pb2_grpc.EchoMeshServiceStub(channel)
            
            # 1. Login
            try:
                login_req = message_pb2.LoginRequest(username=self.username, password="password")
                login_resp = await stub.Login(login_req)
                if login_resp.status_code != message_pb2.SC_OK:
                    print(f"Client {self.username} login failed: {login_resp.message}")
                    return
                self.user_id = login_resp.user_id
                self.session_token = login_resp.session_token
            except Exception as e:
                print(f"Client {self.username} login exception: {e}")
                return

            # 2. Join Room
            try:
                metadata = [('session-token', self.session_token)]
                room_req = message_pb2.RoomActionRequest(
                    action_type=message_pb2.RA_CREATE_OR_JOIN,
                    room_id=self.room_id
                )
                room_resp = await stub.ManageRoom(room_req, metadata=metadata)
                if room_resp.status_code != message_pb2.SC_OK:
                    print(f"Client {self.username} join room failed: {room_resp.message}")
                    return
            except Exception as e:
                print(f"Client {self.username} join room exception: {e}")
                return

            # 3. Stream Audio
            try:
                stream = stub.StreamAudio(self._audio_generator(), metadata=metadata)
                receiver_task = asyncio.create_task(self._audio_receiver(stream))
                
                # Run for specified duration
                await asyncio.sleep(duration)
                
                self.stop_event.set()
                await receiver_task
            except Exception as e:
                print(f"Client {self.username} stream exception: {e}")
                self.errors += 1

async def run_load_test(num_clients, room_id, duration, server_address):
    print(f"Starting load test with {num_clients} clients in room '{room_id}' for {duration}s...")
    clients = [HeadlessClient(i, room_id, server_address) for i in range(num_clients)]
    
    start_time = time.time()
    await asyncio.gather(*(c.run(duration) for c in clients))
    end_time = time.time()
    
    total_sent = sum(c.sent_count for c in clients)
    total_recv = sum(c.recv_count for c in clients)
    total_errors = sum(c.errors for c in clients)
    
    print("\n--- Load Test Results ---")
    print(f"Duration: {end_time - start_time:.2f}s")
    print(f"Total Clients: {num_clients}")
    print(f"Total Packets Sent: {total_sent}")
    print(f"Total Packets Received: {total_recv}")
    print(f"Total Errors: {total_errors}")
    
    # Expected received packets:
    # Each packet sent by one client is broadcast to (N-1) other clients.
    # Total expected received = Total Sent * (num_clients - 1)
    if num_clients > 1:
        expected_recv = total_sent * (num_clients - 1)
        loss_rate = (1 - (total_recv / expected_recv)) * 100 if expected_recv > 0 else 0
        print(f"Expected Received: {expected_recv}")
        print(f"Loss Rate: {loss_rate:.2f}%")
    
    avg_sent_rate = total_sent / (end_time - start_time)
    print(f"Average Packet Rate (Sent): {avg_sent_rate:.2f} pkts/s")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="EchoMesh gRPC Load Tester")
    parser.add_argument("--clients", type=int, default=10, help="Number of concurrent clients.")
    parser.add_argument("--room", type=str, default="perf_test", help="Room ID for testing.")
    parser.add_argument("--duration", type=int, default=10, help="Duration of the test in seconds.")
    parser.add_argument("--server", type=str, default="localhost:8888", help="Server address.")
    
    args = parser.parse_args()
    
    try:
        asyncio.run(run_load_test(args.clients, args.room, args.duration, args.server))
    except KeyboardInterrupt:
        pass
