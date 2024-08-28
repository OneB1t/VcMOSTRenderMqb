import socket
import time
import codecs
from hashlib import sha256
import xml.etree.ElementTree as ET

HOST = '192.168.193.206'  # The server's hostname or IP address
PORT = 25010  # The port used by the server

# Requests
reqCapabilities = "<Req id='105'><Protocol version='1' returnCapabilities='true'/></Req>"
reqAuthenticateChallenge = "<Req id='106'><Authenticate phase='challenge'/></Req>"
reqDir = "<Req id='109'><Dir urlPattern='*' fromEntry='1' numOfEntries='999999999'/></Req>"
reqSubcribeVIN = "<Req id='111'><Subscribe url='Car_vehicleInformation'/></Req>"


print(
    " _____ __ __ __    _____ _____     _ _         _\n|   __|  |  |  |  |  _  |  _  |___| |_|___ ___| |_\n|   __|-   -|  |__|     |   __|  _| | | -_|   |  _|\n|_____|__|__|_____|__|__|__|  |___|_|_|___|_|_|_|")
time.sleep(0.5)

print("\nWelcome to Exlap Client v1.0\n")
time.sleep(0.5)

# printing authentication menu
print("1: Test_TB-105000\n2: RSE_L-CA2000\n3: RSE_3-DE1400\n4: ML_74-125000")
menu_user = input("Select a user to use for authentication: ")


def send_and_print_response(request):
    print(request)
    command = request.encode("UTF-8")
    s.send(command)
    time.sleep(1)
    resp = s.recv(3000)
    print(resp.decode())
    return resp.decode()

def make_subscribe_request(query):
    requestID =100
    requestID += 1
    request =  "<Req id='" + str(requestID) + "'><Subscribe url='" + query + "'/></Req>"
    return (request)
# menu
if menu_user == '1':
    username = "Test_TB-105000"
    password = "s4T2K6BAv0a7LQvrv3vdaUl17xEl2WJOpTmAThpRZe0=="
elif menu_user == '2':
    username = "RSE_L-CA2000"
    password = "T53Facvq51jO8vQJrBNx3MqLWmPcHf/hkow7yLu7SuA=="
elif menu_user == '3':
    username = "RSE_3-DE1400"
    password = "KozPo8iE0j72pkbWXKcP0QihpxgML3Opp8fNJZ0wN24="
elif menu_user == '4':
    username = "ML_74-125000"
    password = "Fo7arEpPhAgMMznzxRlV8B7eeZgNDIYQcy0Gr7Ad1Fg=="
else:
    username = "RSE_3-DE1400"
    password = "KozPo8iE0j72pkbWXKcP0QihpxgML3Opp8fNJZ0wN24="
print("Selected ", username)
print("Trying to connect to host, please wait.\n")

# connect to the host
s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.connect((HOST, PORT))

# first, ask what is possible
print("Requesting capabilities.\n")
send_and_print_response(reqCapabilities)

# second, authenticate
print("Requesting authentication.\n")

XMLdata = ET.fromstring(send_and_print_response(reqAuthenticateChallenge))

challengeNonce = XMLdata.find('./Challenge').attrib['nonce']
cnonce = "mwIu24FMls5goqJE1estsg=="
encoding_string = username + ":" + password + ":" + challengeNonce + ":" + cnonce
digest = (codecs.encode(codecs.decode(sha256(encoding_string.encode('utf-8')).hexdigest(), 'hex'),
                        'base64').decode()).rstrip()
reqAuthenticateChallenge = "<Req id='3'><Authenticate phase='response' cnonce='" + cnonce + "' digest='" + digest + "' user='" + username + "'/></Req>"
send_and_print_response(reqAuthenticateChallenge)
send_and_print_response(reqDir)
send_and_print_response(make_subscribe_request("vehicleIdenticationNumber"))
send_and_print_response(make_subscribe_request("oilLevel"))
send_and_print_response(make_subscribe_request("outsideTemperature"))
send_and_print_response(make_subscribe_request("Radio_Text"))



# todo: add try/catch error handling in case authentication goes wrong
