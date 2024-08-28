import hashlib
import random
import requests
from requests.structures import CaseInsensitiveDict
import xml.etree.ElementTree as ET

#connect to MIB and print output

host = "http://192.168.1.230:25010"
headers = CaseInsensitiveDict()
headers["Content-Type"] = "application/xml"
headers["Accept"] = "application/xml"
data = "<Req id='11'><Authenticate phase='challenge'/></Req>"



response = requests.post(host, headers=headers, data=data)

print(response.status_code)


tree = ET.fromstring(response.content)
root = tree.getroot()

children = root.getchildren()
for child in children:
    ElementTree.dump(child)





# EXLAP AUTHENTICATION:
#1. HA1 = MD5(user+":"+password)
#2. HA2 = MD5(AsciiHex(nonce)+":"+AsciiHex(cnonce))
#3. DIGEST = MD5(AsciiHex(HA1)+":"+AsciiHex(HA2)).

#set username:password here
#default exlap username/pass is: RSE_3-DE1400:KozPo8iE0j72pkbWXKcP0QihpxgML3Opp8fNJZ0wN24=
#userpass = 'RSE_3-DE1400:KozPo8iE0j72pkbWXKcP0QihpxgML3Opp8fNJZ0wN24='
#nonce = input ("Please paste nonce: ")
#
#
#def generate_nonce(length=12):
#    """Generate a pseudorandom number."""
#    return ''.join([str(random.randint(0, 9)) for i in range(length)])
#
#
#
#HA1 = hashlib.md5(userpass).hexdigest()
#HA2 = 



















cnonce = generate_nonce(16)

HA1 = hashlib.md5()
HA1.update(userpass.encode('UTF-8'))
print("HA1 =", HA1.hexdigest())

HA2_temp = nonce+":"+ cnonce

HA2 = hashlib.md5()
HA2.update(HA2_temp.encode('UTF-8'))
print("HA2_temp = ", HA2_temp)
print("HA2 =", HA2.hexdigest())


DIGEST_temp = HA1 + ":" + HA2
DIGEST = hashlib.md5()
DIGEST.update(HA2_temp.encode('UTF-8'))
print("DIGEST_temp = ", DIGEST_temp)
print("DIGEST =", DIGEST.hexdigest())
