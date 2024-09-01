import re
import hashlib
import random
import codecs
from hashlib import sha256

#username = "RSE_3-DE1400"
#password = "KozPo8iE0j72pkbWXKcP0QihpxgML3Opp8fNJZ0wN24="
print ("_____ __ __ __    _____ _____     _ _         _\n|   __|  |  |  |  |  _  |  _  |___| |_|___ ___| |_\n|   __|-   -|  |__|     |   __|  _| | | -_|   |  _|\n|_____|__|__|_____|__|__|__|  |___|_|_|___|_|_|_|" 

 

                                                   


print("1: Test_TB-105000\n2: RSE_L-CA2000\n3: RSE_3-DE1400\n4: ML_74-125000")
#print(menu)
menu_user = input("Select a user to use for authentication: ")

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
else:
    username = "RSE_3-DE1400"
    password = "KozPo8iE0j72pkbWXKcP0QihpxgML3Opp8fNJZ0wN24="

resp = "<Rsp id='106'>\n  <Challenge nonce='MPi+oxTC1OXNOXq9NWzE1g=='/>\n</Rsp>"
nonce = re.findall(r"<Challenge nonce='(.*)'/>", resp)
print ("Challenge nonce: ", nonce[0])