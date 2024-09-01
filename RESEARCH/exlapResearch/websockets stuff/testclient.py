#!/usr/bin/env python

# WS client example

import asyncio
import websockets

async def hello():
    uri = "ws://192.168.193.206:25010"
    async with websockets.connect(uri) as websocket:
        request = input("request")
        await websocket.send(request)
        print(f"> {request}")

        response = await websocket.recv()
        print(f"< {response}")
        
        request = input("request")
        await websocket.send(request)
        print(f"> {request}")

        response = await websocket.recv()
        print(f"< {response}")
        
        request = input("request")
        await websocket.send(request)
        print(f"> {request}")

        response = await websocket.recv()
        print(f"< {response}")
        
        

asyncio.get_event_loop().run_until_complete(hello())