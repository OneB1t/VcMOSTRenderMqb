import re

# Define the pattern to search for
pattern = re.compile(
    rb'\[DSIAndroidAuto2Impl\] onJob_updateNavigationNextTurnEvent : road=(.*), turnSide=(.*), event=(.*), turnAngle=(-?\d+), turnNumber=(-?\d+), valid=(\d)')

# Open the binary file in binary read mode
with open('/fs/sda0/esotrace_SD\somerandomfolder\log_0010.esotrace', 'rb') as file:
    # Read the entire binary content
    binary_data = file.read()

    # Search for the pattern in the binary data
    matches = pattern.findall(binary_data)

    # Output the matches found
    for match in matches:
        print(match)
