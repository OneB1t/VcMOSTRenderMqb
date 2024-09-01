import asyncio
import websockets

# Define the WebSocket server functionality
async def websocket_handler(websocket, path):
    try:
        # Print a message when a new connection is established
        print(f"New connection established with {websocket.remote_address}")

        # Enter a loop to handle messages
        async for message in websocket:
            # Handle incoming message
            print(f"Received from {websocket.remote_address}: {message}")

            # Send a response back to the client
            response = f"Received from {websocket.remote_address}: {message}"
            await websocket.send(response)

    except websockets.exceptions.ConnectionClosed as e:
        print(f"Connection closed with {websocket.remote_address}, code: {e.code}, reason: {e.reason}")

# Create a WebSocket server
start_server = websockets.serve(websocket_handler, "localhost", 25010)

# Print a message when the server starts
print("WebSocket server started. Listening on ws://localhost:25010")

# Start the server
asyncio.get_event_loop().run_until_complete(start_server)
asyncio.get_event_loop().run_forever()
