import websocket
 
ws = websocket.WebSocket()
ws.connect("ws://192.168.1.230:25010")
 
ws.send_binary([100, 220, 130])
binAnswer = ws.recv_frame()
 
print ("----BINARY---")
print (websocket.ABNF.OPCODE_MAP[binAnswer.opcode])
 
for byte in bytearray(binAnswer.data):
    print (byte)
 
ws.send("Hello world")
txtAnswer = ws.recv_frame()
 
print ("\n----TEXT---")
print (websocket.ABNF.OPCODE_MAP[txtAnswer.opcode])
print (txtAnswer.data)
 
ws.close()