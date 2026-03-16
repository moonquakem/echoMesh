import asyncio
import grpc
import time
import argparse
import random
import uuid
import sys
import os

# Set up path to find proto modules
current_dir = os.path.dirname(os.path.abspath(__file__))
if current_dir not in sys.path:
    sys.path.append(current_dir)

from proto import message_pb2
from proto import message_pb2_grpc

async def run_client(server_addr, room_name, client_id, duration):
    """Simulates a single client: Login, Join Room, and Bi-directional Audio Stream."""
    try:
        async with grpc.aio.insecure_channel(server_addr) as channel:
            stub = message_pb2_grpc.EchoMeshServiceStub(channel)
            
            # 1. Login
            login_req = message_pb2.LoginRequest(username=f"user_{client_id}", password="password")
            login_resp = await stub.Login(login_req)
            token = login_resp.session_token
            # Metadata key must match server expectation
            metadata = [('session-token', token)]

            # 2. Join Room (using correct action and field name)
            join_req = message_pb2.RoomActionRequest(
                room_id=room_name, 
                action_type=message_pb2.RA_CREATE_OR_JOIN
            )
            await stub.ManageRoom(join_req, metadata=metadata)

            # 3. Stream Audio
            sent_count = 0
            recv_count = 0
            start_time = time.time()

            # The generator for sending packets
            async def audio_generator():
                nonlocal sent_count
                while time.time() - start_time < duration:
                    # Field name in VoicePacket is 'audio_data'
                    packet = message_pb2.VoicePacket(audio_data=random.randbytes(160), user_id=client_id)
                    yield packet
                    sent_count += 1
                    await asyncio.sleep(0.02)

            stream = stub.StreamAudio(audio_generator(), metadata=metadata)
            
            # Consumer for receiving packets
            try:
                async for _ in stream:
                    recv_count += 1
            except grpc.aio.AioRpcError:
                pass # Stream closed
            
            return sent_count, recv_count
    except Exception as e:
        print(f"Client {client_id} error: {e}")
        return 0, 0

async def run_room_test(server_addr, room_name, clients_per_room, duration, base_id):
    """Runs load test for a single room."""
    tasks = []
    for i in range(clients_per_room):
        tasks.append(run_client(server_addr, room_name, base_id + i, duration))
    
    results = await asyncio.gather(*tasks)
    
    room_sent = sum(r[0] for r in results)
    room_recv = sum(r[1] for r in results)
    return room_sent, room_recv

async def main():
    parser = argparse.ArgumentParser(description="EchoMesh Multi-Room Load Tester")
    parser.add_argument("--host", default="localhost", help="Server host")
    parser.add_argument("--port", default="8888", help="Server port")
    parser.add_argument("--rooms", type=int, default=2, help="Number of rooms")
    parser.add_argument("--clients-per-room", type=int, default=10, help="Clients per room")
    parser.add_argument("--duration", type=int, default=10, help="Test duration in seconds")
    
    args = parser.parse_args()
    server_addr = f"{args.host}:{args.port}"

    print(f"🚀 Starting Multi-Room Load Test:")
    print(f"   Rooms: {args.rooms}")
    print(f"   Clients Per Room: {args.clients_per_room}")
    print(f"   Total Clients: {args.rooms * args.clients_per_room}")
    print(f"   Duration: {args.duration}s\n")

    start_wall_time = time.time()
    
    room_tasks = []
    for r in range(args.rooms):
        room_name = f"room_{r+1}"
        # Offset IDs to ensure uniqueness
        room_base_id = (r + 1) * 1000 
        room_tasks.append(run_room_test(server_addr, room_name, args.clients_per_room, args.duration, room_base_id))

    # Run all rooms in parallel
    all_results = await asyncio.gather(*room_tasks)
    
    end_wall_time = time.time()
    real_duration = end_wall_time - start_wall_time

    total_sent = sum(r[0] for r in all_results)
    total_recv = sum(r[1] for r in all_results)
    
    # Expected received packets calculation:
    expected_recv = 0
    for r_idx in range(args.rooms):
        room_sent_packets = all_results[r_idx][0]
        expected_recv += room_sent_packets * (args.clients_per_room - 1)
    
    loss_rate = (1 - total_recv / expected_recv) * 100 if expected_recv > 0 else 0

    print("-" * 40)
    print(f"📊 Global Results Summary:")
    print(f"   Wall Time: {real_duration:.2f}s")
    print(f"   Total Packets Sent: {total_sent}")
    print(f"   Total Packets Received: {total_recv}")
    print(f"   Expected Received: {expected_recv}")
    print(f"   Average Loss Rate: {loss_rate:.2f}%")
    print(f"   Avg Global Sent Rate: {total_sent / real_duration:.2f} pkts/s")
    print("-" * 40)

if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        pass
