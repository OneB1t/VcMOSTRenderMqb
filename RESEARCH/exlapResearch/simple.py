import socket
import time
import re

HOST = '192.168.193.206'  # The server's hostname or IP address
PORT = 25010        # The port used by the server

command = ("<Req id='105'><Protocol version='1' returnCapabilities='true'/></Req>").encode("UTF-8")

s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.connect((HOST, PORT))

s.send(command)

time.sleep(1)
resp = s.recv(3000)

print (resp.decode())

command = ("<Req id='106'><Authenticate phase='challenge'/></Req>").encode("UTF-8")

s.send(command)

time.sleep(1)
resp = s.recv(3000)

print (resp.decode())