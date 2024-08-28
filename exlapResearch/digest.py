import hashlib
import random
import codecs
import requests
from hashlib import sha256
from requests.structures import CaseInsensitiveDict
import xml.etree.ElementTree as ET

username = "RSE_3-DE1400"
password = "KozPo8iE0j72pkbWXKcP0QihpxgML3Opp8fNJZ0wN24="


print("1: Test_TB-105000\n2: RSE_L-CA2000\n3: RSE_3-DE1400\n4: ML_74-125000")
#print(menu)
menu_user = input("Select a user : ")

if menu_user == '1':
    username ="Test_TB-105000"
    password = "s4T2K6BAv0a7LQvrv3vdaUl17xEl2WJOpTmAThpRZe0=="
elif menu_user == '2':
    username ="RSE_L-CA2000"
    password = "T53Facvq51jO8vQJrBNx3MqLWmPcHf/hkow7yLu7SuA=="
elif menu_user == '3':
    username ="RSE_3-DE1400"
    password = "KozPo8iE0j72pkbWXKcP0QihpxgML3Opp8fNJZ0wN24="
elif menu_user == '4':
    username ="ML_74-125000"
    password = "Fo7arEpPhAgMMznzxRlV8B7eeZgNDIYQcy0Gr7Ad1Fg=="

print ("Selected", username)
#set username and password here
#default exlap username:pass is: RSE_3-DE1400:KozPo8iE0j72pkbWXKcP0QihpxgML3Opp8fNJZ0wN24=
#username = "RSE_3-DE1400"
password = "KozPo8iE0j72pkbWXKcP0QihpxgML3Opp8fNJZ0wN24="
host = "http://192.168.1.230:25010"
#host = "http://google.com"
curlRequest = "curl --http0.9 -X POST http://192.168.155.206:25010 -H \"Content-Type: application/xml\" -H \"Accept: application/xml\" -d "


headers = CaseInsensitiveDict()
headers["Content-Type"] = "application/xml"
headers["Accept"] = "application/xml"
data = "<Req id='11'><Authenticate phase='challenge'/></Req>"

nonce   = "veTM8tYSEL299kaSSTaVwA=="
nonce = input ("Please paste challenge nonce: ")
cnonce  = "qdnpqMqBRpVneAc/PP2mHQ=="
encoding_string = username+":"+password+":"+nonce+":"+cnonce

digest = (codecs.encode(codecs.decode(sha256(encoding_string.encode('utf-8')).hexdigest(), 'hex'), 'base64').decode()).rstrip()
print("DIGEST =", digest)


authenticationRequest = "<Req id='3'><Authenticate phase='response' cnonce='" + cnonce + "' digest='"+digest + "' user='" + username + "'/></Req>"
print ("\n" + curlRequest + "\"" + authenticationRequest + "\"") 


response = requests.post(host, headers=headers, data=data)

print(response.status_code)
