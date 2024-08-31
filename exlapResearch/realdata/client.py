import socket
import time
import codecs
from hashlib import sha256
import xml.etree.ElementTree as ET
import sys

HOST = '127.0.0.1'  # The server's hostname or IP address
PORT = 25010  # The port used by the server

# Requests
reqCapabilities = "<Req id='105'><Protocol version='1' returnCapabilities='true'/></Req>"
reqAuthenticateChallenge = "<Req id='106'><Authenticate phase='challenge'/></Req>"
reqDir = "<Req id='109'><Dir urlPattern='*' fromEntry='1' numOfEntries='999999999'/></Req>"
reqSubcribeVIN = "<Req id='111'><Subscribe url='Car_vehicleInformation'/></Req>"

# printing authentication menu
#print("1: Test_TB-105000\n2: RSE_L-CA2000\n3: RSE_3-DE1400\n4: ML_74-125000")
menu_user = 3


def send_and_print_response(request):
 #   print(request)
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
#print("Selected ", username)
#print("Trying to connect to host, please wait.\n")

# connect to the host
s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.connect((HOST, PORT))

# first, ask what is possible
#print("Requesting capabilities.\n")
send_and_print_response(reqCapabilities)

# second, authenticate
#print("Requesting authentication.\n")
XMLdata = ET.fromstring(send_and_print_response(reqAuthenticateChallenge))
challengeNonce = XMLdata.find('./Challenge').attrib['nonce']
cnonce = "mwIu24FMls5goqJE1estsg==" #not exactly random, but who's asking ;)
encoding_string = username + ":" + password + ":" + challengeNonce + ":" + cnonce
digest = (codecs.encode(codecs.decode(sha256(encoding_string.encode('utf-8')).hexdigest(), 'hex'),'base64').decode()).rstrip()
reqAuthenticateChallenge = "<Req id='3'><Authenticate phase='response' cnonce='" + cnonce + "' digest='" + digest + "' user='" + username + "'/></Req>"
send_and_print_response(reqAuthenticateChallenge)

#print("Requesting supported capabilities.\n")

send_and_print_response(make_subscribe_request(sys.argv[1]))
#send_and_print_response(make_subscribe_request("WebRadio_TuneStation"))
#send_and_print_response(make_subscribe_request("tankLevelSecondary"))
#send_and_print_response(make_subscribe_request("unitTemperature"))
#send_and_print_response(make_subscribe_request("lateralAcceleration"))
#send_and_print_response(make_subscribe_request("Sound_BalanceFader"))
#send_and_print_response(make_subscribe_request("MediaBrowser_Path"))
#send_and_print_response(make_subscribe_request("Media_Track"))
#send_and_print_response(make_subscribe_request("Radio_SeekBackward"))
#send_and_print_response(make_subscribe_request("WebRadio_PodcastEpisodes"))
#send_and_print_response(make_subscribe_request("temperatureRearRight"))
#send_and_print_response(make_subscribe_request("Radio_AvailableBands"))
#send_and_print_response(make_subscribe_request("WebRadio_LastListened"))
#send_and_print_response(make_subscribe_request("unitVolume"))
#send_and_print_response(make_subscribe_request("acceleratorPosition"))
#send_and_print_response(make_subscribe_request("Car_vehicleInformation"))
#send_and_print_response(make_subscribe_request("Radio_Tuner"))
#send_and_print_response(make_subscribe_request("MediaBrowser_SetSource"))
#send_and_print_response(make_subscribe_request("recommendedGear"))
#send_and_print_response(make_subscribe_request("temperatureRearLeft"))
#send_and_print_response(make_subscribe_request("ExAc_TouchToken"))
#send_and_print_response(make_subscribe_request("consumptionLongTermGeneral"))
#send_and_print_response(make_subscribe_request("Media_SetPlayMode"))
#send_and_print_response(make_subscribe_request("currentConsumptionSecondary"))
#send_and_print_response(make_subscribe_request("powermeter"))
#send_and_print_response(make_subscribe_request("tankLevelPrimary"))
#send_and_print_response(make_subscribe_request("Radio_SeekForward"))
#send_and_print_response(make_subscribe_request("Radio_TuneStation"))
#send_and_print_response(make_subscribe_request("WebRadio_CurrentStation"))
#send_and_print_response(make_subscribe_request("WebRadio_TopStations"))
#send_and_print_response(make_subscribe_request("fuelWarningSecondaryTank"))
#send_and_print_response(make_subscribe_request("Radio_DABServiceComponent"))
#send_and_print_response(make_subscribe_request("Sound_SetBalanceFader"))
#send_and_print_response(make_subscribe_request("wheelAngle"))
#send_and_print_response(make_subscribe_request("accIsActive"))
#send_and_print_response(make_subscribe_request("Sound_Volume"))
#send_and_print_response(make_subscribe_request("currentOutputPower"))
#send_and_print_response(make_subscribe_request("ExAc_ReleaseToken"))
#send_and_print_response(make_subscribe_request("Radio_TA"))
#send_and_print_response(make_subscribe_request("currentConsumptionPrimary"))
#send_and_print_response(make_subscribe_request("suspensionProfile"))
#send_and_print_response(make_subscribe_request("Nav_StopGuidance"))
#send_and_print_response(make_subscribe_request("Nav_ResolveAddress"))
#send_and_print_response(make_subscribe_request("suspensionStates"))
#send_and_print_response(make_subscribe_request("batteryVoltage"))
#send_and_print_response(make_subscribe_request("dayMilage"))
#send_and_print_response(make_subscribe_request("ambienceLight_profiles"))
#send_and_print_response(make_subscribe_request("tyrePressures"))
#send_and_print_response(make_subscribe_request("Radio_SDARSPresets"))
#send_and_print_response(make_subscribe_request("Radio_AMStations"))
#send_and_print_response(make_subscribe_request("Radio_DABEnsembles"))
#send_and_print_response(make_subscribe_request("fuelWarningPrimaryTank"))
#send_and_print_response(make_subscribe_request("serviceOil"))
#send_and_print_response(make_subscribe_request("Radio_DABServices"))
#send_and_print_response(make_subscribe_request("Nav_ResolveLastDestination"))
#send_and_print_response(make_subscribe_request("Media_PlayMode"))
#send_and_print_response(make_subscribe_request("Radio_FrequencyDecrease"))
#send_and_print_response(make_subscribe_request("yawRate"))
#send_and_print_response(make_subscribe_request("driveMode"))
#send_and_print_response(make_subscribe_request("lightState_rear"))
#send_and_print_response(make_subscribe_request("maxOutputPower"))
#send_and_print_response(make_subscribe_request("Sound_Unmute"))
#send_and_print_response(make_subscribe_request("System_Language"))
#send_and_print_response(make_subscribe_request("displayNightDesign"))
#send_and_print_response(make_subscribe_request("shortTermConsumptionPrimary"))
#send_and_print_response(make_subscribe_request("serviceInspection"))
#send_and_print_response(make_subscribe_request("Radio_PresetStore"))
#send_and_print_response(make_subscribe_request("Nav_GpxImport"))
#send_and_print_response(make_subscribe_request("SPI_ConnectedDevice"))
#send_and_print_response(make_subscribe_request("EcoHMI_Score"))
#send_and_print_response(make_subscribe_request("Radio_ActivateBand"))
#send_and_print_response(make_subscribe_request("Nav_GeoPosition"))
#send_and_print_response(make_subscribe_request("Car_vehicleState"))
#send_and_print_response(make_subscribe_request("System_HMISkin"))
#send_and_print_response(make_subscribe_request("unitTimeFormat"))
#send_and_print_response(make_subscribe_request("MediaBrowser_Play"))
#send_and_print_response(make_subscribe_request("gearboxOilTemperature"))
#send_and_print_response(make_subscribe_request("ExAc_Resources"))
#send_and_print_response(make_subscribe_request("MediaBrowser_FollowMode"))
#send_and_print_response(make_subscribe_request("relChargingAirPressure"))
#send_and_print_response(make_subscribe_request("reverseGear"))
#send_and_print_response(make_subscribe_request("Nav_CurrentPosition"))
#send_and_print_response(make_subscribe_request("vehicleDate"))
#send_and_print_response(make_subscribe_request("System_RestrictionMode"))
#send_and_print_response(make_subscribe_request("acceleratorKickDown"))
#send_and_print_response(make_subscribe_request("Radio_SDARSStations"))
#send_and_print_response(make_subscribe_request("Sound_BalanceFaderSetup"))
#send_and_print_response(make_subscribe_request("Media_AvailableSources"))
#send_and_print_response(make_subscribe_request("Nav_GuidanceDestination"))
#send_and_print_response(make_subscribe_request("unitDateFormat"))
#send_and_print_response(make_subscribe_request("Radio_FrequencyIncrease"))
#send_and_print_response(make_subscribe_request("longTermConsumptionPrimary"))
#send_and_print_response(make_subscribe_request("outsideTemperature"))
#send_and_print_response(make_subscribe_request("clampState"))
#send_and_print_response(make_subscribe_request("shortTermConsumptionSecondary"))
#send_and_print_response(make_subscribe_request("Nav_StartGuidance"))
#send_and_print_response(make_subscribe_request("combustionEngineDisplacement"))
#send_and_print_response(make_subscribe_request("oilTemperature"))
#send_and_print_response(make_subscribe_request("Media_NextTrack"))
#send_and_print_response(make_subscribe_request("MediaBrowser_FollowModeOn"))
#send_and_print_response(make_subscribe_request("GetImageInformationByLocator"))
#send_and_print_response(make_subscribe_request("oilLevel"))
#send_and_print_response(make_subscribe_request("Nav_GuidanceRemaining"))
#send_and_print_response(make_subscribe_request("hevacConfiguration"))
#send_and_print_response(make_subscribe_request("Radio_FrequencyRanges"))
#send_and_print_response(make_subscribe_request("Nav_Altitude"))
#send_and_print_response(make_subscribe_request("Media_SwitchSource"))
#send_and_print_response(make_subscribe_request("Sound_Mute"))
#send_and_print_response(make_subscribe_request("tyreTemperatures"))
#send_and_print_response(make_subscribe_request("driverIsBraking"))
#send_and_print_response(make_subscribe_request("Sound_IncreaseVolume"))
#send_and_print_response(make_subscribe_request("ambienceLight_sets"))
#send_and_print_response(make_subscribe_request("engineTypes"))
#send_and_print_response(make_subscribe_request("gearTransmissionMode"))
#send_and_print_response(make_subscribe_request("Radio_StationNext"))
#send_and_print_response(make_subscribe_request("blinkingState"))
#send_and_print_response(make_subscribe_request("longTermConsumptionSecondary"))
#send_and_print_response(make_subscribe_request("vehicleTime"))
#send_and_print_response(make_subscribe_request("coastingIsActive"))
#send_and_print_response(make_subscribe_request("Sound_VolumeSetup"))
#send_and_print_response(make_subscribe_request("WebRadio_Presets"))
#send_and_print_response(make_subscribe_request("GetImageByLocator"))
#send_and_print_response(make_subscribe_request("seatHeater_zone1"))
#send_and_print_response(make_subscribe_request("seatHeater_zone2"))
#send_and_print_response(make_subscribe_request("Nav_Heading"))
#send_and_print_response(make_subscribe_request("seatHeater_zone3"))
#send_and_print_response(make_subscribe_request("Media_Play"))
#send_and_print_response(make_subscribe_request("Car_ambienceLightColour"))
#send_and_print_response(make_subscribe_request("seatHeater_zone4"))
#send_and_print_response(make_subscribe_request("hevOperationMode"))
#send_and_print_response(make_subscribe_request("Radio_DABPresets"))
#send_and_print_response(make_subscribe_request("Radio_TuneFrequency"))
#send_and_print_response(make_subscribe_request("combustionEngineInjection"))
#send_and_print_response(make_subscribe_request("Media_Pause"))
#send_and_print_response(make_subscribe_request("MediaBrowser_GetList"))
#send_and_print_response(make_subscribe_request("ExAc_GetToken"))
#send_and_print_response(make_subscribe_request("currentGear"))
#send_and_print_response(make_subscribe_request("Nav_GuidanceState"))
#send_and_print_response(make_subscribe_request("fuelLevelState"))
#send_and_print_response(make_subscribe_request("Radio_USLPresets"))
#send_and_print_response(make_subscribe_request("MediaBrowser_PathCurrentTrack"))
#send_and_print_response(make_subscribe_request("Media_PlayerState"))




# todo: add try/catch error handling in case authentication goes wrong
