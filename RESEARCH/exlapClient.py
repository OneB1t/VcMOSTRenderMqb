import socket
import base64
import os
import struct
import random



# Define the WebSocket server URL and port
server_url = "localhost"
server_port = 25010

# Function to establish a WebSocket connection
def websocket_handshake(server_url, server_port):
    # Create a socket connection to the server
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.connect((server_url, server_port))

    # Perform the WebSocket handshake
    key = base64.b64encode(os.urandom(16)).decode()
    request = (
        f"GET / HTTP/1.1\r\n"
        f"Host: {server_url}\r\n"
        f"Upgrade: websocket\r\n"
        f"Connection: Upgrade\r\n"
        f"Sec-WebSocket-Key: {key}\r\n"
        f"Sec-WebSocket-Version: 13\r\n"
        f"\r\n"
    )
    s.send(request.encode())

    response = s.recv(1024).decode()
    if "101 Switching Protocols" not in response:
        raise Exception("WebSocket handshake failed")

    return s

# Function to send a WebSocket message
def send_websocket_message(sock, message):
    # Prepare the WebSocket frame
    frame = bytearray()
    frame.append(0x81)  # Text frame (0x81) with FIN bit set

    message_bytes = message.encode()
    frame_length = len(message_bytes)

    if frame_length <= 125:
        frame.append(0x80 | frame_length)  # 0x80 means FIN bit set
    else:
        frame.append(0x80 | 126)
        frame.extend(struct.pack(">H", frame_length))

    # Generate a random 4-byte mask key
    mask_key = struct.pack(">I", random.getrandbits(32))
    frame.extend(mask_key)

    # Mask the message data
    masked_data = bytearray(message_bytes[i] ^ mask_key[i % 4] for i in range(frame_length))
    frame.extend(masked_data)

    sock.send(frame)

# Function to receive a WebSocket message
def receive_websocket_message(sock):
    header = sock.recv(2)
    if len(header) < 2:
        return None

    opcode = header[0] & 0x0F
    is_masked = header[1] & 0x80
    payload_length = header[1] & 0x7F

    if payload_length == 126:
        payload_length = int.from_bytes(sock.recv(2), byteorder="big")
    elif payload_length == 127:
        payload_length = int.from_bytes(sock.recv(8), byteorder="big")

    if is_masked:
        mask = sock.recv(4)
        payload = bytearray(sock.recv(payload_length))
        for i in range(payload_length):
            payload[i] ^= mask[i % 4]
    else:
        payload = bytearray(sock.recv(payload_length))

    if opcode == 1:
        # Text frame
        return payload.decode("utf-8")
    elif opcode == 2:
        # Binary frame
        return payload
    else:
        raise Exception(f"Unsupported WebSocket frame opcode: {opcode}")




if __name__ == "__main__":
    try:
        # Establish the WebSocket connection
        websocket = websocket_handshake(server_url, server_port)
        print("WebSocket connection established.")

        while True:
            message = input("Enter a message (or type 'exit' to quit): ")
            if message.lower() == "exit":
                break

            send_websocket_message(websocket, message)
            print(f"> Sent: {message}")

            response = receive_websocket_message(websocket)
            print(f"< Received: {response}")

    except Exception as e:
        print(f"Error: {e}")

    finally:
        websocket.close()
