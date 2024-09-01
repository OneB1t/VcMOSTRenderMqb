import websocket

def on_message(ws, message):
    '''
        This method is invoked when ever the client
        receives any message from server
    '''
    print("received message as {}".format(message))
    ws.send("hello again")
    print("sending 'hello again'")

def on_error(ws, error):
    '''
        This method is invoked when there is an error in connectivity
    '''
    print("received error as {}".format(error))

def on_close(ws):
    '''
        This method is invoked when the connection between the 
        client and server is closed
    '''
    #print("Connection closed")

def on_open(ws):
    '''
        This method is invoked as soon as the connection between 
		client and server is opened and only for the first time
    '''
    ws.send("<Req id='11'><Authenticate phase='challenge'/></Req>")
    print("sent Authenticate message on open")


if __name__ == "__main__":
    websocket.enableTrace(True)
    print("Hello")

    ws = websocket.WebSocketApp("ws://192.168.193.206:25010",
                              on_message = on_message,
                              on_error = on_error,
                              on_close = on_close)
    ws.on_open = on_open
    ws.run_forever