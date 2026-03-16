import grpc
import sys
import os
import time

# Get the absolute path of the 'test_client' directory
current_dir = os.path.dirname(os.path.abspath(__file__))

# Add 'test_client' to sys.path so we can import 'proto' as a package
sys.path.append(os.path.dirname(current_dir)) 
# Also add the directory itself to support direct imports if needed
sys.path.append(current_dir)

# Fix for gRPC generated code: import the package itself
try:
    from proto import message_pb2
    from proto import message_pb2_grpc
except ImportError:
    # Fallback for different environments
    import message_pb2
    import message_pb2_grpc

def run_test():
    # Make sure to point to where your server is listening (from main.cpp)
    channel = grpc.insecure_channel('localhost:8888')
    stub = message_pb2_grpc.EchoMeshServiceStub(channel)
    
    test_user = "user_test_" + str(int(time.time()))
    test_pass = "password123"
    test_room = "room_alpha"

    print(f"--- 1. Testing Login (Registration) for '{test_user}' ---")
    try:
        response = stub.Login(message_pb2.LoginRequest(
            username=test_user,
            password=test_pass
        ))
        print(f"Status: {response.status_code}")
        print(f"Message: {response.message}")
        print(f"User ID: {response.user_id}")
        token = response.session_token
        
        if response.status_code != message_pb2.SC_OK:
            print("Login failed, stopping test.")
            return

        print("\n--- 2. Testing Secondary Login (Database Retrieval) ---")
        response2 = stub.Login(message_pb2.LoginRequest(
            username=test_user,
            password=test_pass
        ))
        print(f"Status: {response2.status_code}")
        print(f"Message: {response2.message}")
        print(f"New Token: {response2.session_token}")

        print("\n--- 3. Testing Room Management (Optimized Sharded Lock) ---")
        # Metadata is used for token authentication in ManageRoom
        metadata = (('session-token', token),)
        
        room_action = stub.ManageRoom(
            message_pb2.RoomActionRequest(
                action_type=message_pb2.RA_CREATE_OR_JOIN,
                room_id=test_room
            ),
            metadata=metadata
        )
        print(f"Status: {room_action.status_code}")
        print(f"Message: {room_action.message}")

        print("\n--- Success! System verified. ---")

    except grpc.RpcError as e:
        print(f"gRPC Error: {e.code()} - {e.details()}")
        print("Tip: Make sure the server is running on localhost:8888")

if __name__ == "__main__":
    run_test()
