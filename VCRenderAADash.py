import platform
import subprocess
import time
import struct
import os
import signal
import re
import glob
import unicodedata
import io

start_time = time.time()  # Record the start time

# setup final image size
width = 800
height = 480

# Data Position (global variables)
last_road = ""
last_turn_side = ""
last_event = ""
last_turn_angle = 0
last_turn_number = 0
last_valid = 0
last_distance_meters = 0
last_distance_seconds = 0
last_distance_valid = 0

debug = 0

# trace log parser
log_directory_path = '/fs/sda0/esotrace_SD'  # point it to place where .esotrace files are stored
next_turn_pattern = r'\[DSIAndroidAuto2Impl\] onJob_updateNavigationNextTurnEvent : road=\'([^\']*)\', turnSide=([A-Z]+), event=(.*[A-Z]+), turnAngle=(-?\d+), turnNumber=(-?\d+), valid=(\d)'
next_turn_distance_patttern = r'\[DSIAndroidAuto2Impl\] onJob_updateNavigationNextTurnDistance : distanceMeters=(-?\d+), timeSeconds=(-?\d+), valid=(\d)'

icons_folder_path = "icons"

speedPos = "i:1304:216"

# Define a simple 8x8 font for prepareing text (ASCII characters)
font = {
    ord(' '): [0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00],    ord('!'): [0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x00, 0x0C, 0x00],    ord('"'): [0x14, 0x14, 0x14, 0x00, 0x00, 0x00, 0x00, 0x00],    ord('#'): [0x14, 0x7E, 0x14, 0x14, 0x7E, 0x14, 0x14, 0x00],    ord('$'): [0x1C, 0xAC, 0xB0, 0x1C, 0x0C, 0xAC, 0x1C, 0x00],
    ord('%'): [0xC2, 0xC4, 0x08, 0x10, 0x20, 0x46, 0x82, 0x00],    ord('&'): [0x0C, 0x48, 0x0C, 0x36, 0x4A, 0x32, 0x74, 0x00],    ord('\''): [0x0C, 0x0C, 0x0C, 0x00, 0x00, 0x00, 0x00, 0x00],    ord('('): [0x10, 0x20, 0x40, 0x40, 0x40, 0x20, 0x10, 0x00],    ord(')'): [0x40, 0x20, 0x10, 0x10, 0x10, 0x20, 0x40, 0x00],
    ord('*'): [0x20, 0xAA, 0x70, 0x70, 0xAA, 0x20, 0x00, 0x00],    ord('+'): [0x00, 0x10, 0x10, 0x7C, 0x10, 0x10, 0x00, 0x00],    ord(','): [0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C, 0x18],    ord('-'): [0x00, 0x00, 0x00, 0x7C, 0x00, 0x00, 0x00, 0x00],    ord('.'): [0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C, 0x00],
    ord('/'): [0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x00],    ord('0'): [0x1E, 0x33, 0x3B, 0x3F, 0x37, 0x33, 0x1E, 0x00],    ord('1'): [0x0C, 0x1C, 0x0C, 0x0C, 0x0C, 0x0C, 0x3F, 0x00],    ord('2'): [0x1E, 0x33, 0x03, 0x0E, 0x18, 0x30, 0x3F, 0x00],    ord('3'): [0x1E, 0x33, 0x03, 0x0E, 0x03, 0x33, 0x1E, 0x00],
    ord('4'): [0x0E, 0x1E, 0x36, 0x66, 0x7F, 0x06, 0x06, 0x00],    ord('5'): [0x3F, 0x30, 0x30, 0x3E, 0x03, 0x33, 0x1E, 0x00],    ord('6'): [0x0C, 0x18, 0x30, 0x3E, 0x33, 0x33, 0x1E, 0x00],    ord('7'): [0x3F, 0x03, 0x03, 0x06, 0x0C, 0x18, 0x18, 0x00],    ord('8'): [0x1E, 0x33, 0x33, 0x1E, 0x33, 0x33, 0x1E, 0x00],
    ord('9'): [0x1E, 0x33, 0x33, 0x1F, 0x03, 0x06, 0x0C, 0x00],    ord(':'): [0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00, 0x00],    ord(';'): [0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x18, 0x30],    ord('<'): [0x08, 0x10, 0x20, 0x40, 0x20, 0x10, 0x08, 0x00],    ord('='): [0x00, 0x00, 0x7E, 0x00, 0x7E, 0x00, 0x00, 0x00],
    ord('>'): [0x20, 0x10, 0x08, 0x04, 0x08, 0x10, 0x20, 0x00],    ord('?'): [0x1C, 0x22, 0x22, 0x04, 0x08, 0x00, 0x08, 0x00],    ord('@'): [0x3C, 0x42, 0x42, 0x4E, 0x4A, 0x3C, 0x00, 0x00],    ord('A'): [0x1C, 0x22, 0x42, 0x42, 0x7E, 0x42, 0x42, 0x00],    ord('B'): [0x3C, 0x22, 0x22, 0x3C, 0x22, 0x22, 0x3C, 0x00],
    ord('C'): [0x0C, 0x10, 0x20, 0x20, 0x20, 0x10, 0x0C, 0x00],    ord('D'): [0x38, 0x24, 0x22, 0x22, 0x22, 0x24, 0x38, 0x00],    ord('E'): [0x3E, 0x20, 0x20, 0x3C, 0x20, 0x20, 0x3E, 0x00],    ord('F'): [0x3E, 0x20, 0x20, 0x3C, 0x20, 0x20, 0x20, 0x00],    ord('G'): [0x0C, 0x10, 0x20, 0x20, 0x26, 0x22, 0x1C, 0x00],
    ord('H'): [0x42, 0x42, 0x42, 0x7E, 0x42, 0x42, 0x42, 0x00],    ord('I'): [0x1C, 0x08, 0x08, 0x08, 0x08, 0x08, 0x1C, 0x00],    ord('J'): [0x0E, 0x04, 0x04, 0x04, 0x24, 0x24, 0x18, 0x00],    ord('K'): [0x22, 0x24, 0x28, 0x30, 0x28, 0x24, 0x22, 0x00],    ord('L'): [0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x3E, 0x00],
    ord('M'): [0x63, 0x77, 0x5D, 0x55, 0x49, 0x49, 0x41, 0x00],    ord('N'): [0xC6, 0xE6, 0xF6, 0xDE, 0xCE, 0xC6, 0xC6, 0x00],    ord('O'): [0x1C, 0x22, 0x42, 0x42, 0x42, 0x22, 0x1C, 0x00],    ord('P'): [0x3C, 0x22, 0x22, 0x3C, 0x20, 0x20, 0x20, 0x00],    ord('Q'): [0x1C, 0x22, 0x42, 0x42, 0x4A, 0x24, 0x1A, 0x00],
    ord('R'): [0x3C, 0x22, 0x22, 0x3C, 0x28, 0x24, 0x22, 0x00],    ord('S'): [0x1E, 0x20, 0x20, 0x0C, 0x02, 0x22, 0x1C, 0x00],    ord('T'): [0x3E, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x00],    ord('U'): [0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x1C, 0x00],    ord('V'): [0x22, 0x22, 0x22, 0x14, 0x14, 0x0C, 0x0C, 0x00],
    ord('W'): [0x41, 0x41, 0x49, 0x49, 0x55, 0x63, 0x41, 0x00],    ord('X'): [0x22, 0x22, 0x14, 0x0C, 0x14, 0x22, 0x22, 0x00],    ord('Y'): [0x22, 0x22, 0x14, 0x14, 0x08, 0x08, 0x08, 0x00],    ord('Z'): [0x3E, 0x02, 0x04, 0x08, 0x10, 0x20, 0x3E, 0x00],    ord('['): [0x1C, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1C, 0x00],
    ord('\\'): [0x20, 0x10, 0x08, 0x04, 0x02, 0x01, 0x00, 0x00],    ord(']'): [0x38, 0x08, 0x08, 0x08, 0x08, 0x08, 0x38, 0x00],    ord('^'): [0x08, 0x14, 0x22, 0x00, 0x00, 0x00, 0x00, 0x00],    ord('_'): [0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3E],    ord('`'): [0x10, 0x08, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00],
    ord('a'): [0x00, 0x00, 0x1C, 0x02, 0x1E, 0x12, 0x1E, 0x00],    ord('b'): [0x20, 0x20, 0x2C, 0x32, 0x22, 0x22, 0x3C, 0x00],    ord('c'): [0x00, 0x00, 0x1E, 0x20, 0x20, 0x20, 0x1E, 0x00],    ord('d'): [0x04, 0x04, 0x1C, 0x24, 0x24, 0x24, 0x1E, 0x00],    ord('e'): [0x00, 0x00, 0x1C, 0x22, 0x3E, 0x20, 0x1E, 0x00],
    ord('f'): [0x0C, 0x12, 0x10, 0x3C, 0x10, 0x10, 0x10, 0x00],    ord('g'): [0x00, 0x00, 0x1E, 0x24, 0x24, 0x1C, 0x04, 0x18],    ord('h'): [0x20, 0x20, 0x2C, 0x32, 0x22, 0x22, 0x22, 0x00],    ord('i'): [0x08, 0x00, 0x18, 0x08, 0x08, 0x08, 0x1C, 0x00],    ord('j'): [0x04, 0x00, 0x0C, 0x04, 0x04, 0x04, 0x24, 0x18],
    ord('k'): [0x20, 0x20, 0x24, 0x28, 0x30, 0x28, 0x24, 0x00],    ord('l'): [0x18, 0x08, 0x08, 0x08, 0x08, 0x08, 0x1C, 0x00],    ord('m'): [0x00, 0x00, 0x1E, 0x2A, 0x2A, 0x2A, 0x2A, 0x00],    ord('n'): [0x00, 0x00, 0x2C, 0x32, 0x22, 0x22, 0x22, 0x00],    ord('o'): [0x00, 0x00, 0x1C, 0x22, 0x22, 0x22, 0x1C, 0x00],
    ord('p'): [0x00, 0x00, 0x3C, 0x22, 0x22, 0x3C, 0x20, 0x20],    ord('q'): [0x00, 0x00, 0x1A, 0x26, 0x26, 0x1E, 0x04, 0x04],    ord('r'): [0x00, 0x00, 0x2C, 0x32, 0x20, 0x20, 0x20, 0x00],    ord('s'): [0x00, 0x00, 0x1E, 0x20, 0x1C, 0x02, 0x3C, 0x00],    ord('t'): [0x10, 0x10, 0x3E, 0x10, 0x10, 0x12, 0x0C, 0x00],
    ord('u'): [0x00, 0x00, 0x22, 0x22, 0x22, 0x22, 0x1E, 0x00],    ord('v'): [0x00, 0x00, 0x22, 0x22, 0x14, 0x14, 0x08, 0x00],    ord('w'): [0x00, 0x00, 0x41, 0x49, 0x49, 0x55, 0x36, 0x00],    ord('x'): [0x00, 0x00, 0x22, 0x14, 0x08, 0x14, 0x22, 0x00],    ord('y'): [0x00, 0x00, 0x22, 0x22, 0x1C, 0x04, 0x18, 0x00],
    ord('z'): [0x00, 0x00, 0x3E, 0x04, 0x08, 0x10, 0x3E, 0x00],    ord('{'): [0x0C, 0x10, 0x10, 0x20, 0x10, 0x10, 0x0C, 0x00],    ord('|'): [0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x00],    ord('}'): [0x30, 0x08, 0x08, 0x04, 0x08, 0x08, 0x30, 0x00],    ord('~'): [0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00],
    ord('^'): [0x08, 0x14, 0x22, 0x00, 0x00, 0x00, 0x00, 0x00],    ord('_'): [0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3E]
}

def strip_accents(s):
    return ''.join(c for c in unicodedata.normalize('NFD', s)
                   if unicodedata.category(c) != 'Mn')

def find_newest_file(directory, extension=".esotrace"):
    # Create a list of all files with the specified extension in the directory and subdirectories
    files = [f for f in glob.iglob(os.path.join(directory, '**', f'*{extension}'), recursive=True) if os.path.isfile(f)]

    # Check if there are any matching files
    if not files:
        return None

    # Find the newest file based on modification time
    newest_file = max(files, key=os.path.getmtime)
    return newest_file


def parse_log_line_next_turn(line):
    global last_road, last_turn_side, last_event, last_turn_angle, last_turn_number, last_valid
    match = re.search(next_turn_pattern, line)
    if match:
        road, turn_side, event, turn_angle, turn_number, valid = match.groups()
        last_road = road
        last_turn_side = turn_side
        last_event = event
        last_turn_angle = turn_angle
        last_turn_number = turn_number
        last_valid = valid
    else:
        # Set default values or handle the case where the pattern is not found
        last_road = ""
        last_turn_side = ""
        last_event = ""
        last_turn_angle = 0
        last_turn_number = 0
        last_valid = 0


def parse_log_line_next_turn_distance(line):
    global last_distance_meters, last_distance_seconds, last_distance_valid
    match = re.search(next_turn_distance_patttern, line)
    if match:
        distancemeters, timeseconds, valid = match.groups()
        last_distance_meters = distancemeters
        last_distance_seconds = timeseconds
        last_distance_valid = valid
    else:
        # Set default values or handle the case where the pattern is not found
        last_distance_meters = 0
        last_distance_seconds = 0
        last_distance_valid = 0


def find_last_occurrence(log_file_path, pattern):
    if not log_file_path:
        return None
    with io.open(log_file_path, 'r', encoding='utf-8') as file:
        lines = file.readlines()

    for line in reversed(lines):
        if re.search(pattern, line):
            return line.strip()

    return None

# Function to set a pixel to white (255) at the specified coordinates
def set_pixel(x, y):
    if 0 <= x < width and 0 <= y < height:
        pixel_index = (y * width + x) // 8
        bit_offset = 7 - ((y * width + x) % 8)  # Invert the bit order for 1-bit BMP
        pixels[pixel_index] |= (1 << bit_offset)


def prepare_text(text, x, y, scale=1):
    for char in text:
        char_representation = get_char_representation(char)
        char_height = len(char_representation)
        char_width = 8  # Assuming a fixed width of 8 for each character

        for row in reversed(range(char_height)):
            for col in reversed(range(char_width)):
                pixel = char_representation[row] & (1 << col)
                if pixel != 0:
                    # Scale the pixel coordinates by 'scale' to make the character bigger
                    for i in range(scale):
                        for j in range(scale):
                            set_pixel(x + (char_width - col - 1) * scale + i, y + (char_height - row - 1) * scale + j)

        x += char_width * scale  # Move to the next character, accounting for scaling





def get_char_representation(char):
    return font.get(ord(char),
                    [0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000])


def execute_initial_commands():
    commands = [
        ("/scripts/activateSDCardEsotrace.sh", "Cannot activate SD card trace log"),
        ("on -f mmx /net/mmx/mnt/app/eso/bin/apps/pc i:1304:210 1", "Cannot enable AA sensors data"),
        ("/bin/slay loadandshowimage", "Cannot kill all loadandshowimage"),
        ("/eso/bin/apps/dmdt sc 4 -9", "Set context of display 4 failed with error"),
        ("/eso/bin/apps/dmdt sb 0", "Switch buffer on display 0 failed with error"),
        ("/eso/bin/apps/dmdt sc 0 71", "Switch context to 71 on display 0 failed with error"),
    ]

    for i, (command, error_message) in enumerate(commands):
        try:
            print(f"Executing '{command}'")
            subprocess.Popen(command, stdout=subprocess.PIPE, shell=True, preexec_fn=os.setsid)
        except subprocess.CalledProcessError as e:
            print(f"{error_message}: {e.returncode}")

        if i == 2:  # Insert time.sleep(2) after the third command
            time.sleep(2)


def read_data(position):
    command = ""
    if platform.system() == "Windows":
        return "0"
    else:  # Assuming we are on QNX
        command = "on -f mmx /net/mmx/mnt/app/eso/bin/apps/pc " + position

    try:
        process = subprocess.Popen(command, stdout=subprocess.PIPE, shell=True, preexec_fn=os.setsid)
        output, _ = process.communicate()
        print(output)
        return output.decode('utf-8')  # Decode the binary output to a string

    except subprocess.CalledProcessError as e:
        print("Error: " + str(e.returncode))
        return "0"


if platform.system() == "Windows":
    output_file = "render.bmp"
else:  # Assuming we are on QNX
    output_file = "/tmp/render.bmp"


def draw_text(text, x, y, scale=1):
    text = strip_accents(text)
    for char in text:
        char_representation = get_char_representation(char)
        char_height = len(char_representation)
        char_width = 8  # Assuming a fixed width of 8 for each character

        for row in reversed(range(char_height)):
            for col in reversed(range(char_width)):
                pixel = char_representation[row] & (1 << col)
                if pixel != 0:
                    # Scale the pixel coordinates by 'scale' to make the character bigger
                    for i in range(scale):
                        for j in range(scale):
                            set_pixel(x + (char_width - col - 1) * scale + i, y + (char_height - row - 1) * scale + j)

        x += char_width * scale  # Move to the next character, accounting for scaling


def prepare_text(text, textScale, width, height, offsetx, offsety):
    text_x = (width - len(text) * 8 * textScale) // 2  # Assuming 8 pixels per character
    text_y = height // 2 - 8  # Assuming 8-pixel font height

    draw_text(text, text_x + offsetx, text_y + offsety, textScale)


def read_bmp(file_path):
    with open(file_path, 'rb') as file:
        # Read BMP header (24 bytes)
        header = file.read(24)
        offset = int.from_bytes(header[10:14], byteorder='little')

        # Check if it's a BMP file (signature "BM" at the beginning)
        if header[:2] != b'BM':
            raise ValueError("Not a valid BMP file")

        # Extract width and height from the header
        width_offset = 18
        height_offset = 22
        width = int.from_bytes(header[width_offset:width_offset + 4], byteorder='little')
        height = int.from_bytes(header[height_offset:height_offset + 4], byteorder='little')
        file.seek(offset)
        # Read the image data
        data = bytes(list(file.read()))

    return width, height, data


def read_all_bmp_files(folder_path):
    bmp_files_data = []

    for filename in os.listdir(folder_path):
        file_path = os.path.join(folder_path, filename)

        # Check if it's a file and has a BMP extension
        if os.path.isfile(file_path) and file_path.lower().endswith(".bmp"):
            try:
                width, height, data = read_bmp(file_path)
                bmp_files_data.append({'filename': filename, 'width': width, 'height': height, 'data': data})
            except Exception as e:
                print(f"Error reading {filename}: {e}")

    return bmp_files_data

def convert_to_km(meters):
    if int(meters) >= 1000:
        return str("{:.1f}".format(int(meters) / 1000)) + " Km"  # Convert meters to kilometers
    else:
        return meters + " m"  # Return 1000 meters if less than 1000 meters

def overlay_icon_on_bw_bmp(selectedicon, overlay_position):
    # Overlay the icon onto the larger canvas
    for y in range(bmp_files_data[selectedicon]['height']):
        for x in range(bmp_files_data[selectedicon]['width']):
            icon_index = (y * bmp_files_data[selectedicon]['width'] + x)
            if bmp_files_data[selectedicon]['data'][icon_index] > 0:
                set_pixel(x + overlay_position[0], y + overlay_position[1])


bmp_files_data = read_all_bmp_files(icons_folder_path)
# Now bmp_files_data is a list of dictionaries, each containing width, height, data, and filename
for file_data in bmp_files_data:
    print(
        f"File: {file_data['filename']}, Width: {file_data['width']}, Height: {file_data['height']}, Size: {len(file_data['data'])} bytes")
execute_once = True
while True:

    start_time = time.time()  # Record the start time
    # Create a blank monochromatic image of size 800x480 (all pixels initialized to 0)
    pixels = bytearray([0] * (width * height // 8))  # 1 byte per 8 pixels

    log_file_path = find_newest_file(log_directory_path)
    print("Using log file: ", log_file_path)
    last_occurrence_line_next_turn = find_last_occurrence(log_file_path, next_turn_pattern)
    last_occurrence_line_next_turn_distance = find_last_occurrence(log_file_path, next_turn_distance_patttern)

    if last_occurrence_line_next_turn:
        parse_log_line_next_turn(last_occurrence_line_next_turn)
        print("Parsed next turn data:")
        print("Road:", last_road)
        print("Turn Side:", last_turn_side)
        print("Event:", last_event)
        print("Turn Angle:", last_turn_angle)
        print("Turn Number:", last_turn_number)
        print("Valid:", last_valid)
    else:
        print("Next turn pattern not found in the log file.")

    if last_occurrence_line_next_turn_distance:
        parse_log_line_next_turn_distance(last_occurrence_line_next_turn_distance)
        print("Parsed distance data:")
        print("Distance meters:", str(last_distance_meters))
        print("Distance seconds:", str(last_distance_seconds))
        print("Valid:", str(last_distance_valid))
    else:
        print("Distance pattern not found in the log file.")
    ############################################### RENDER ##################################

    overlay_position = (400 - 96, 200)
    iconindex = 0

    if last_event == "UNKNOWN":
        iconindex = 31
    if last_event == "DEPART":
        iconindex = 0
    if last_event == "NAME_CHANGE":
        iconindex = 0
    if last_event == "SLIGHT_TURN":
        if last_turn_side == "LEFT":
            iconindex = 36
        else:
            iconindex = 37
    if last_event == "TURN":
        if last_turn_side == "LEFT":
            iconindex = 30
        else:
            iconindex = 29
    if last_event == "SHARP_TURN":
        if last_turn_side == "LEFT":
            iconindex = 34
        else:
            iconindex = 35
    if last_event == "U_TURN":
        if last_turn_side == "LEFT":
            iconindex = 38
        else:
            iconindex = 39
    if last_event == "ON_RAMP":
        iconindex = 2
    if last_event == "OFF_RAMP":
        iconindex = 2
    if last_event == "FORK":
        if last_turn_side == "LEFT":
            iconindex = 6
        else:
            iconindex = 7
    if last_event == "MERGE":
        if last_turn_side == "UNSPECIFIED":
            iconindex = 8
        if last_turn_side == "LEFT":
            iconindex = 9
        if last_turn_side == "RIGHT":
            iconindex = 10
    if last_event == "ROUNDABOUT_ENTER":
        iconindex = 12
    if last_event == "ROUNDABOUT_EXIT":
        iconindex = 14
    if last_event == "ROUNDABOUT_ENTER_AND_EXIT":
        iconindex = 12
    if last_event == "STRAIGHT":
        iconindex = 31
    if last_event == "FERRY_BOAT":
        iconindex = 4
    if last_event == "FERRY_TRAIN":
        iconindex = 5
    if last_event == "DESTINATION":
        if last_turn_side == "UNSPECIFIED":
            iconindex = 1
        if last_turn_side == "LEFT":
            iconindex = 2
        if last_turn_side == "RIGHT":
            iconindex = 3

    overlay_icon_on_bw_bmp(iconindex, overlay_position)

    if last_event.__contains__("ROUNDABOUT"):  # draw turn number for roundabounts
        prepare_text(str(last_turn_number), 5, width, height, 85, 0)

    prepare_text(convert_to_km(last_distance_meters), 4, width, height, 0, 180)

    # normal dashboard
    if debug == 0:
        prepare_text(read_data(speedPos)[0:3], 6, width, height, 0, -180)
        prepare_text("Km/h", 2, width, height, 120, -170)
        prepare_text(last_road, 2, width, height, 0, -60)
    # debug data
    if debug == 1:
        prepare_text("Turn Side: " + last_turn_side, 2, width, height, 0, -80)
        prepare_text("Event: " + last_event, 2, width, height, 0, -100)
        prepare_text("Turn Angle:" + str(last_turn_angle), 2, width, height, 0, -120)
        prepare_text("Turn Number: " + str(last_turn_number), 2, width, height, 0, -140)
        prepare_text("Distance meters: " + str(last_distance_meters), 2, width, height, 0, -160)
        prepare_text("Distance seconds: " + str(last_distance_seconds), 2, width, height, 0, -180)


    # BMP header for a monochromatic (1-bit) BMP
    bmp_header = struct.pack('<2sIHHI', b'BM', len(pixels) + 62, 0, 0, 62)

    # Bitmap info header
    bmp_info_header = struct.pack('<IiiHHIIIIII', 40, width, height, 1, 1, 0, len(pixels), 0, 2, 2, 0)

    # Color palette for monochromatic BMP (black and white)
    color_palette = struct.pack('<II', 0x00000000, 0x00FFFFFF)



    # Create and save the BMP file
    with open(output_file, 'wb') as bmp_file:
        bmp_file.write(bmp_header)
        bmp_file.write(bmp_info_header)
        bmp_file.write(color_palette)
        bmp_file.write(pixels)

    end_time = time.time()  # Record the end time
    elapsed_time = end_time - start_time
    print("Time taken to generate BMP to path '{}' is {:.6f} seconds".format(output_file, elapsed_time))

    # Command to run the external process (replace with your command)
    command = ["/eso/bin/apps/loadandshowimage /tmp/render.bmp"]

    # Start the external process
    if platform.system() != "Windows":
        print("Executing '{}'".format(command))
        process = subprocess.Popen(command, stdout=subprocess.PIPE, shell=True, preexec_fn=os.setsid)

    # Sleep for 2 seconds
    time.sleep(2)

    # Send a SIGINT signal to the process
    # Start the external process
    if platform.system() != "Windows":
        os.killpg(os.getpgid(process.pid), signal.SIGTERM)
        # Wait for the process to finish
        process.wait()

    if execute_once:
        if platform.system() != "Windows":
            execute_initial_commands()
        execute_once = False  # Set the control variable to False after execution
