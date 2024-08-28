import asyncio
import websockets

async def hello(websocket, path):
    request = await websocket.recv()
    print(f"< {request}")
    
    if "Protocol version='1' returnCapabilities='true'" in request:
        response = "<Rsp id='105'>\n<Capabilities description='Dies ist der VW Standard Application Interface Server. API Level 5' service='VW SAI-Server HIGH' version='5.0' id='crqmZX6WojbemmQiDMwoXKBNFNnJn8gp'>\n<Supports protocol='1.3' interface='true' authenticate='true' heartbeat='true' datTimeStamp='true'/>\n </Capabilities></Rsp>"
    elif "Authenticate phase='challenge'" in request:
        response = f"<Rsp id='106'>\n<Challenge nonce='qQYJzPmPkFwgr6AQ3LNtig=='/>\n</Rsp>"
    else: 
        response = f"Syntax error\n"



    await websocket.send(response)
    print(f"> {response}")

start_server = websockets.serve(hello, "localhost", 8765)

asyncio.get_event_loop().run_until_complete(start_server)
asyncio.get_event_loop().run_forever()