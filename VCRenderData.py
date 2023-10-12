import struct
import time
import random
import subprocess
import platform
import threading
import signal
import os

start_time = time.time()  # Record the start time

#setup
width = 1280
height = 640

def run_command():
    # Start the process (replace 'your_command' with the actual command you want to run)
    process = subprocess.Popen('/eso/bin/apps/loadandshowimage /tmp/render.bmp', shell=True)

    # Wait for the process to finish (optional)
    process.wait()

# Function to set a pixel to white (255) at the specified coordinates
def set_pixel(x, y):
    if 0 <= x < width and 0 <= y < height:
        pixel_index = (y * width + x) // 8
        bit_offset = 7 - ((y * width + x) % 8)  # Invert the bit order for 1-bit BMP
        pixels[pixel_index] |= (1 << bit_offset)

def draw_text(text, x, y, scale=2):
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





# Define a simple 8x8 font for drawing text (customize as needed)
# Define a simple 8x8 font for drawing text (ASCII characters)
font = {
    ord(' '): [
        0b00000000,
        0b00000000,
        0b00000000,
        0b00000000,
        0b00000000,
        0b00000000,
        0b00000000,
        0b00000000,
    ],
    ord('!'): [
        0b00110000,
        0b00110000,
        0b00110000,
        0b00110000,
        0b00110000,
        0b00000000,
        0b00110000,
        0b00000000,
    ],
    ord('"'): [
        0b01010100,
        0b01010100,
        0b01010100,
        0b00000000,
        0b00000000,
        0b00000000,
        0b00000000,
        0b00000000,
    ],
    ord('#'): [
        0b01010100,
        0b11111110,
        0b01010100,
        0b01010100,
        0b11111110,
        0b01010100,
        0b01010100,
        0b00000000,
    ],
    ord('$'): [
        0b00111000,
        0b10101100,
        0b10110000,
        0b00111000,
        0b00011000,
        0b10101100,
        0b00111000,
        0b00000000,
    ],
    ord('%'): [
        0b11000010,
        0b11000100,
        0b00001000,
        0b00010000,
        0b00100000,
        0b01000110,
        0b10000010,
        0b00000000,
    ],
    ord('&'): [
        0b00110000,
        0b01001000,
        0b00110000,
        0b01011000,
        0b10100100,
        0b10011000,
        0b01110100,
        0b00000000,
    ],
    ord("'"): [
        0b00110000,
        0b00110000,
        0b00110000,
        0b00000000,
        0b00000000,
        0b00000000,
        0b00000000,
        0b00000000,
    ],
    ord('('): [
        0b00010000,
        0b00100000,
        0b01000000,
        0b01000000,
        0b01000000,
        0b00100000,
        0b00010000,
        0b00000000,
    ],
    ord(')'): [
        0b01000000,
        0b00100000,
        0b00010000,
        0b00010000,
        0b00010000,
        0b00100000,
        0b01000000,
        0b00000000,
    ],
    ord('*'): [
        0b00100000,
        0b10101000,
        0b01110000,
        0b01110000,
        0b10101000,
        0b00100000,
        0b00000000,
        0b00000000,
    ],
    ord('+'): [
        0b00000000,
        0b00010000,
        0b00010000,
        0b11111000,
        0b00010000,
        0b00010000,
        0b00000000,
        0b00000000,
    ],
    ord(','): [
        0b00000000,
        0b00000000,
        0b00000000,
        0b00000000,
        0b00000000,
        0b00110000,
        0b00110000,
        0b01100000,
    ],
    ord('-'): [
        0b00000000,
        0b00000000,
        0b00000000,
        0b11111000,
        0b00000000,
        0b00000000,
        0b00000000,
        0b00000000,
    ],
    ord('.'): [
        0b00000000,
        0b00000000,
        0b00000000,
        0b00000000,
        0b00000000,
        0b00110000,
        0b00110000,
        0b00000000,
    ],
    ord('/'): [
        0b00000010,
        0b00000100,
        0b00001000,
        0b00010000,
        0b00100000,
        0b01000000,
        0b10000000,
        0b00000000,
    ],
    ord('0'): [
        0b00111000,
        0b01000100,
        0b10001010,
        0b10010010,
        0b10100010,
        0b11000010,
        0b00111000,
        0b00000000,
    ],
    ord('1'): [
        0b00010000,
        0b00110000,
        0b01010000,
        0b00010000,
        0b00010000,
        0b00010000,
        0b11111000,
        0b00000000,
    ],
    ord('2'): [
        0b01111000,
        0b10000100,
        0b00000100,
        0b00001000,
        0b00010000,
        0b00100000,
        0b11111100,
        0b00000000,
    ],
    ord('3'): [
        0b11111000,
        0b00000100,
        0b00000100,
        0b00011000,
        0b00000100,
        0b10000100,
        0b01111000,
        0b00000000,
    ],
    ord('4'): [
        0b00001000,
        0b00011000,
        0b00101000,
        0b01001000,
        0b11111100,
        0b00001000,
        0b00001000,
        0b00000000,
    ],
    ord('5'): [
        0b11111100,
        0b10000000,
        0b10000000,
        0b11111000,
        0b00000100,
        0b10000100,
        0b01111000,
        0b00000000,
    ],
    ord('6'): [
        0b00111000,
        0b01000000,
        0b10000000,
        0b11111000,
        0b10000100,
        0b10000100,
        0b01111000,
        0b00000000,
    ],
    ord('7'): [
        0b11111100,
        0b00000100,
        0b00001000,
        0b00010000,
        0b00100000,
        0b01000000,
        0b01000000,
        0b00000000,
    ],
    ord('8'): [
        0b00111000,
        0b01000100,
        0b10000100,
        0b01111000,
        0b10000100,
        0b10000100,
        0b01111000,
        0b00000000,
    ],
    ord('9'): [
        0b00111000,
        0b01000100,
        0b10000100,
        0b10000100,
        0b01111100,
        0b00000100,
        0b00111000,
        0b00000000,
    ],
    ord(':'): [
        0b00000000,
        0b00110000,
        0b00110000,
        0b00000000,
        0b00110000,
        0b00110000,
        0b00000000,
        0b00000000,
    ],
    ord(';'): [
        0b00000000,
        0b00110000,
        0b00110000,
        0b00000000,
        0b00110000,
        0b00110000,
        0b01100000,
        0b00000000,
    ],
    ord('<'): [
        0b00001000,
        0b00010000,
        0b00100000,
        0b01000000,
        0b00100000,
        0b00010000,
        0b00001000,
        0b00000000,
    ],
    ord('='): [
        0b00000000,
        0b00000000,
        0b11111000,
        0b00000000,
        0b11111000,
        0b00000000,
        0b00000000,
        0b00000000,
    ],
    ord('>'): [
        0b01000000,
        0b00100000,
        0b00010000,
        0b00001000,
        0b00010000,
        0b00100000,
        0b01000000,
        0b00000000,
    ],
    ord('?'): [
        0b01110000,
        0b10001000,
        0b10001000,
        0b00010000,
        0b00100000,
        0b00000000,
        0b00100000,
        0b00000000,
    ],
    ord('@'): [
        0b00111000,
        0b01000100,
        0b10011010,
        0b10101010,
        0b10111010,
        0b10000000,
        0b01111000,
        0b00000000,
    ],
    ord('A'): [
        0b00110000,
        0b01001000,
        0b10000100,
        0b10000100,
        0b11111100,
        0b10000100,
        0b10000100,
        0b00000000,
    ],
    ord('B'): [
        0b11111000,
        0b10000100,
        0b10000100,
        0b11111000,
        0b10000100,
        0b10000100,
        0b11111000,
        0b00000000,
    ],
    ord('C'): [
        0b01111000,
        0b10000100,
        0b10000000,
        0b10000000,
        0b10000000,
        0b10000100,
        0b01111000,
        0b00000000,
    ],
    ord('D'): [
        0b11110000,
        0b10001000,
        0b10000100,
        0b10000100,
        0b10000100,
        0b10001000,
        0b11110000,
        0b00000000,
    ],
    ord('E'): [
        0b11111100,
        0b10000000,
        0b10000000,
        0b11111000,
        0b10000000,
        0b10000000,
        0b11111100,
        0b00000000,
    ],
    ord('F'): [
        0b11111100,
        0b10000000,
        0b10000000,
        0b11111000,
        0b10000000,
        0b10000000,
        0b10000000,
        0b00000000,
    ],
    ord('G'): [
        0b01111000,
        0b10000100,
        0b10000000,
        0b10000000,
        0b10001100,
        0b10000100,
        0b01111000,
        0b00000000,
    ],
    ord('H'): [
        0b10000100,
        0b10000100,
        0b10000100,
        0b11111100,
        0b10000100,
        0b10000100,
        0b10000100,
        0b00000000,
    ],
    ord('I'): [
        0b01111000,
        0b00100000,
        0b00100000,
        0b00100000,
        0b00100000,
        0b00100000,
        0b01111000,
        0b00000000,
    ],
    ord('J'): [
        0b00001100,
        0b00000100,
        0b00000100,
        0b00000100,
        0b00000100,
        0b10000100,
        0b01111000,
        0b00000000,
    ],
    ord('K'): [
        0b10001000,
        0b10010000,
        0b10100000,
        0b11000000,
        0b10100000,
        0b10010000,
        0b10001000,
        0b00000000,
    ],
    ord('L'): [
        0b10000000,
        0b10000000,
        0b10000000,
        0b10000000,
        0b10000000,
        0b10000000,
        0b11111100,
        0b00000000,
    ],
    ord('M'): [
        0b10000010,
        0b11000110,
        0b10101010,
        0b10010010,
        0b10000010,
        0b10000010,
        0b10000010,
        0b00000000,
    ],
    ord('N'): [
        0b10000010,
        0b10000010,
        0b11000010,
        0b10100010,
        0b10010010,
        0b10001010,
        0b10000010,
        0b00000000,
    ],
    ord('O'): [
        0b01111000,
        0b10000100,
        0b10000100,
        0b10000100,
        0b10000100,
        0b10000100,
        0b01111000,
        0b00000000,
    ],
    ord('P'): [
        0b11111000,
        0b10000100,
        0b10000100,
        0b11111000,
        0b10000000,
        0b10000000,
        0b10000000,
        0b00000000,
    ],
    ord('Q'): [
        0b01111000,
        0b10000100,
        0b10000100,
        0b10000100,
        0b10010100,
        0b10001000,
        0b01110100,
        0b00000000,
    ],
    ord('R'): [
        0b11111000,
        0b10000100,
        0b10000100,
        0b11111000,
        0b10100000,
        0b10010000,
        0b10001000,
        0b00000000,
    ],
    ord('S'): [
        0b01111000,
        0b10000100,
        0b10000000,
        0b01111000,
        0b00000100,
        0b10000100,
        0b01111000,
        0b00000000,
    ],
    ord('T'): [
        0b11111000,
        0b00100000,
        0b00100000,
        0b00100000,
        0b00100000,
        0b00100000,
        0b00100000,
        0b00000000,
    ],
    ord('U'): [
        0b10000100,
        0b10000100,
        0b10000100,
        0b10000100,
        0b10000100,
        0b10000100,
        0b01111000,
        0b00000000,
    ],
    ord('V'): [
        0b10000100,
        0b10000100,
        0b10000100,
        0b10000100,
        0b10000100,
        0b01001000,
        0b00110000,
        0b00000000,
    ],
    ord('W'): [
        0b10000010,
        0b10000010,
        0b10000010,
        0b10010010,
        0b10101010,
        0b11000110,
        0b10000010,
        0b00000000,
    ],
    ord('X'): [
        0b10000010,
        0b10000010,
        0b01000100,
        0b00101000,
        0b01000100,
        0b10000010,
        0b10000010,
        0b00000000,
    ],
    ord('Y'): [
        0b10000100,
        0b10000100,
        0b10000100,
        0b01001000,
        0b00110000,
        0b00100000,
        0b00100000,
        0b00000000,
    ],
    ord('Z'): [
        0b11111100,
        0b00000100,
        0b00001000,
        0b00010000,
        0b00100000,
        0b01000000,
        0b11111100,
        0b00000000,
    ],
    ord('['): [
        0b01111000,
        0b01000000,
        0b01000000,
        0b01000000,
        0b01000000,
        0b01000000,
        0b01111000,
        0b00000000,
    ],
    ord('\\'): [
        0b10000000,
        0b01000000,
        0b01000000,
        0b00100000,
        0b00100000,
        0b00010000,
        0b00010000,
        0b00001000,
    ],
    ord(']'): [
        0b01111000,
        0b00001000,
        0b00001000,
        0b00001000,
        0b00001000,
        0b00001000,
        0b01111000,
        0b00000000,
    ],
    ord('^'): [
        0b00100000,
        0b01010000,
        0b10001000,
        0b00000000,
        0b00000000,
        0b00000000,
        0b00000000,
        0b00000000,
    ],
    ord('_'): [
        0b00000000,
        0b00000000,
        0b00000000,
        0b00000000,
        0b00000000,
        0b00000000,
        0b11111100,
        0b00000000,
    ],
    ord('`'): [
        0b01000000,
        0b00100000,
        0b00010000,
        0b00000000,
        0b00000000,
        0b00000000,
        0b00000000,
        0b00000000,
    ],
    ord('a'): [
        0b00000000,
        0b00000000,
        0b01111000,
        0b00000100,
        0b01111100,
        0b10000100,
        0b01111100,
        0b00000000,
    ],
    ord('b'): [
        0b10000000,
        0b10000000,
        0b10111000,
        0b11000100,
        0b10000100,
        0b10000100,
        0b11111000,
        0b00000000,
    ],
    ord('c'): [
        0b00000000,
        0b00000000,
        0b01111000,
        0b10000100,
        0b10000000,
        0b10000100,
        0b01111000,
        0b00000000,
    ],
    ord('d'): [
        0b00000100,
        0b00000100,
        0b01110100,
        0b10001100,
        0b10000100,
        0b10000100,
        0b01111000,
        0b00000000,
    ],
    ord('e'): [
        0b00000000,
        0b00000000,
        0b01111000,
        0b10000100,
        0b11111100,
        0b10000000,
        0b01111000,
        0b00000000,
    ],
    ord('f'): [
        0b00011100,
        0b00100000,
        0b00100000,
        0b01111000,
        0b00100000,
        0b00100000,
        0b00100000,
        0b00000000,
    ],
    ord('g'): [
        0b00000000,
        0b01111000,
        0b10000100,
        0b10000100,
        0b01111100,
        0b00000100,
        0b01111000,
        0b00000000,
    ],
    ord('h'): [
        0b10000000,
        0b10000000,
        0b10111000,
        0b11000100,
        0b10000100,
        0b10000100,
        0b10000100,
        0b00000000,
    ],
    ord('i'): [
        0b00100000,
        0b00000000,
        0b00100000,
        0b00100000,
        0b00100000,
        0b00100000,
        0b00100000,
        0b00000000,
    ],
    ord('j'): [
        0b00010000,
        0b00000000,
        0b00010000,
        0b00010000,
        0b00010000,
        0b10010000,
        0b01100000,
        0b00000000,
    ],
    ord('k'): [
        0b10000000,
        0b10000000,
        0b10001000,
        0b10010000,
        0b10100000,
        0b11010000,
        0b10001000,
        0b00000000,
    ],
    ord('l'): [
        0b00100000,
        0b00100000,
        0b00100000,
        0b00100000,
        0b00100000,
        0b00100000,
        0b00011100,
        0b00000000,
    ],
    ord('m'): [
        0b00000000,
        0b00000000,
        0b10111000,
        0b11010100,
        0b10010100,
        0b10010100,
        0b10010100,
        0b00000000,
    ],
    ord('n'): [
        0b00000000,
        0b00000000,
        0b10111000,
        0b11000100,
        0b10000100,
        0b10000100,
        0b10000100,
        0b00000000,
    ],
    ord('o'): [
        0b00000000,
        0b00000000,
        0b01111000,
        0b10000100,
        0b10000100,
        0b10000100,
        0b01111000,
        0b00000000,
    ],
    ord('p'): [
        0b00000000,
        0b00000000,
        0b11111000,
        0b10000100,
        0b10000100,
        0b11111000,
        0b10000000,
        0b10000000,
    ],
    ord('q'): [
        0b00000000,
        0b00000000,
        0b01110100,
        0b10001100,
        0b10000100,
        0b01111000,
        0b00000100,
        0b00000100,
    ],
    ord('r'): [
        0b00000000,
        0b00000000,
        0b10111000,
        0b11000100,
        0b10000000,
        0b10000000,
        0b10000000,
        0b00000000,
    ],
    ord('s'): [
        0b00000000,
        0b00000000,
        0b01111000,
        0b10000000,
        0b01111000,
        0b00000100,
        0b11111000,
        0b00000000,
    ],
    ord('t'): [
        0b00100000,
        0b00100000,
        0b01111000,
        0b00100000,
        0b00100000,
        0b00100000,
        0b00011000,
        0b00000000,
    ],
    ord('u'): [
        0b00000000,
        0b00000000,
        0b10000100,
        0b10000100,
        0b10000100,
        0b10001100,
        0b01110100,
        0b00000000,
    ],
    ord('v'): [
        0b00000000,
        0b00000000,
        0b10000100,
        0b10000100,
        0b01001000,
        0b01001000,
        0b00110000,
        0b00000000,
    ],
    ord('w'): [
        0b00000000,
        0b00000000,
        0b10000100,
        0b10000100,
        0b10010100,
        0b10101000,
        0b01000100,
        0b00000000,
    ],
    ord('x'): [
        0b00000000,
        0b00000000,
        0b10000100,
        0b01001000,
        0b00110000,
        0b01001000,
        0b10000100,
        0b00000000,
    ],
    ord('y'): [
        0b00000000,
        0b00000000,
        0b10000100,
        0b10000100,
        0b10000100,
        0b01111000,
        0b00001000,
        0b11110000,
    ],
    ord('z'): [
        0b00000000,
        0b00000000,
        0b11111000,
        0b00010000,
        0b00100000,
        0b01000000,
        0b11111000,
        0b00000000,
    ],
    ord('{'): [
        0b00011000,
        0b00100000,
        0b00100000,
        0b11000000,
        0b00100000,
        0b00100000,
        0b00011000,
        0b00000000,
    ],
    ord('|'): [
        0b00100000,
        0b00100000,
        0b00100000,
        0b00100000,
        0b00100000,
        0b00100000,
        0b00100000,
        0b00000000,
    ],
    ord('}'): [
        0b01100000,
        0b00100000,
        0b00100000,
        0b00011000,
        0b00100000,
        0b00100000,
        0b01100000,
        0b00000000,
    ],
    ord('~'): [
        0b00000000,
        0b00000000,
        0b00100000,
        0b01010000,
        0b10001000,
        0b00000000,
        0b00000000,
        0b00000000,
    ],
}

def get_char_representation(char):
    return font.get(ord(char), [0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000])

def execute_dmdt_commands():
    commandcontext = "/eso/bin/apps/dmdt sc 4 -9"
    try:
        subprocess.run(commandcontext, shell=True, check=True)
    except subprocess.CalledProcessError as e:
        print(f"Set context of display 4 failed with error: {e.returncode}")

    time.sleep(2)

    commandbuffer = "/eso/bin/apps/dmdt sb 0"
    try:
        subprocess.run(commandbuffer, shell=True, check=True)
    except subprocess.CalledProcessError as e:
        print(f"Switch buffer on display 0 failed with error: {e.returncode}")

if platform.system() == "Windows":
    output_file = "render.bmp"
else:  # Assuming we are on QNX
    output_file = "/tmp/render.bmp"

execute_once = True
while True:

    start_time = time.time()  # Record the start time

    # Define the text to be added
    random_value = random.randint(1, 100)
    text = "This is the first prototype of text rendering - {}".format(random_value)

    # Create a blank monochromatic image of size 1280x640 (all pixels initialized to 0)
    pixels = bytearray([0] * (width * height // 8))  # 1 byte per 8 pixels


    # Calculate the position to center the text
    text_x = (width - len(text) * 16) // 2  # Assuming 8 pixels per character
    text_y = height // 2 - 16  # Assuming 8-pixel font height


    # Draw the text on the image
    draw_text(text, text_x, text_y)

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
    print("Time taken to generate BMP: {:.6f} seconds".format(elapsed_time))

    # Command to run the external process (replace with your command)
    command = ["/eso/bin/apps/loadandshowimage /tmp/render.bmp"]

    # Start the external process
    if platform.system() != "Windows":
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
        execute_dmdt_commands()
        execute_once = False  # Set the control variable to False after execution
