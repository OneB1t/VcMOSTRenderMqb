import platform
import subprocess
import time
import struct
import os
import signal

start_time = time.time ()  # Record the start time

# setup final image size
width = 800
height = 480

# Data Position
distancePos = "i:1304:213"
speedPos = "i:1304:216"


# Function to set a pixel to white (255) at the specified coordinates
def set_pixel(x, y):
    if 0 <= x < width and 0 <= y < height:
        pixel_index = (y * width + x) // 8
        bit_offset = 7 - ((y * width + x) % 8)  # Invert the bit order for 1-bit BMP
        pixels[pixel_index] |= (1 << bit_offset)


def prepare_text(text, x, y, scale=1):
    for char in text:
        char_representation = get_char_representation (char)
        char_height = len (char_representation)
        char_width = 8  # Assuming a fixed width of 8 for each character

        for row in reversed (range (char_height)):
            for col in reversed (range (char_width)):
                pixel = char_representation[row] & (1 << col)
                if pixel != 0:
                    # Scale the pixel coordinates by 'scale' to make the character bigger
                    for i in range (scale):
                        for j in range (scale):
                            set_pixel (x + (char_width - col - 1) * scale + i, y + (char_height - row - 1) * scale + j)

        x += char_width * scale  # Move to the next character, accounting for scaling


# Define a simple 8x8 font for prepareing text (ASCII characters)
font = {
    ord(' '): [0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00],    ord('!'): [0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x00, 0x0C, 0x00],    ord('"'): [0x14, 0x14, 0x14, 0x00, 0x00, 0x00, 0x00, 0x00],    ord('#'): [0x14, 0x7E, 0x14, 0x14, 0x7E, 0x14, 0x14, 0x00],
    ord('$'): [0x1C, 0xAC, 0xB0, 0x1C, 0x0C, 0xAC, 0x1C, 0x00],    ord('%'): [0xC2, 0xC4, 0x08, 0x10, 0x20, 0x46, 0x82, 0x00],    ord('&'): [0x0C, 0x48, 0x0C, 0x36, 0x4A, 0x32, 0x74, 0x00],    ord('\''): [0x0C, 0x0C, 0x0C, 0x00, 0x00, 0x00, 0x00, 0x00],
    ord('('): [0x10, 0x20, 0x40, 0x40, 0x40, 0x20, 0x10, 0x00],    ord(')'): [0x40, 0x20, 0x10, 0x10, 0x10, 0x20, 0x40, 0x00],    ord('*'): [0x20, 0xAA, 0x70, 0x70, 0xAA, 0x20, 0x00, 0x00],    ord('+'): [0x00, 0x10, 0x10, 0x7C, 0x10, 0x10, 0x00, 0x00],
    ord(','): [0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C, 0x18],    ord('-'): [0x00, 0x00, 0x00, 0x7C, 0x00, 0x00, 0x00, 0x00],    ord('.'): [0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C, 0x00],    ord('/'): [0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x00],
    ord('0'): [0x1E, 0x33, 0x3B, 0x3F, 0x37, 0x33, 0x1E, 0x00],    ord('1'): [0x0C, 0x1C, 0x0C, 0x0C, 0x0C, 0x0C, 0x3F, 0x00],    ord('2'): [0x1E, 0x33, 0x03, 0x0E, 0x18, 0x30, 0x3F, 0x00],    ord('3'): [0x1E, 0x33, 0x03, 0x0E, 0x03, 0x33, 0x1E, 0x00],
    ord('4'): [0x0E, 0x1E, 0x36, 0x66, 0x7F, 0x06, 0x06, 0x00],    ord('5'): [0x3F, 0x30, 0x30, 0x3E, 0x03, 0x33, 0x1E, 0x00],    ord('6'): [0x0C, 0x18, 0x30, 0x3E, 0x33, 0x33, 0x1E, 0x00],    ord('7'): [0x3F, 0x03, 0x03, 0x06, 0x0C, 0x18, 0x18, 0x00],
    ord('8'): [0x1E, 0x33, 0x33, 0x1E, 0x33, 0x33, 0x1E, 0x00],    ord('9'): [0x1E, 0x33, 0x33, 0x1F, 0x03, 0x06, 0x0C, 0x00],    ord(':'): [0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00, 0x00],    ord(';'): [0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x18, 0x30],
    ord('<'): [0x08, 0x10, 0x20, 0x40, 0x20, 0x10, 0x08, 0x00],    ord('='): [0x00, 0x00, 0x7E, 0x00, 0x7E, 0x00, 0x00, 0x00],    ord('>'): [0x20, 0x10, 0x08, 0x04, 0x08, 0x10, 0x20, 0x00],    ord('?'): [0x1C, 0x22, 0x22, 0x04, 0x08, 0x00, 0x08, 0x00],
    ord('@'): [0x3C, 0x42, 0x42, 0x4E, 0x4A, 0x3C, 0x00, 0x00],    ord('A'): [0x1C, 0x22, 0x42, 0x42, 0x7E, 0x42, 0x42, 0x00],    ord('B'): [0x3C, 0x22, 0x22, 0x3C, 0x22, 0x22, 0x3C, 0x00],    ord('C'): [0x0C, 0x10, 0x20, 0x20, 0x20, 0x10, 0x0C, 0x00],
    ord('D'): [0x38, 0x24, 0x22, 0x22, 0x22, 0x24, 0x38, 0x00],    ord('E'): [0x3E, 0x20, 0x20, 0x3C, 0x20, 0x20, 0x3E, 0x00],    ord('F'): [0x3E, 0x20, 0x20, 0x3C, 0x20, 0x20, 0x20, 0x00],    ord('G'): [0x0C, 0x10, 0x20, 0x20, 0x26, 0x22, 0x1C, 0x00],
    ord('H'): [0x42, 0x42, 0x42, 0x7E, 0x42, 0x42, 0x42, 0x00],    ord('I'): [0x1C, 0x08, 0x08, 0x08, 0x08, 0x08, 0x1C, 0x00],    ord('J'): [0x0E, 0x04, 0x04, 0x04, 0x24, 0x24, 0x18, 0x00],    ord('K'): [0x22, 0x24, 0x28, 0x30, 0x28, 0x24, 0x22, 0x00],
    ord('L'): [0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x3E, 0x00],    ord('M'): [0x63, 0x77, 0x5D, 0x55, 0x49, 0x49, 0x41, 0x00],    ord('N'): [0x42, 0x42, 0x4A, 0x4A, 0x56, 0x52, 0x42, 0x00],    ord('O'): [0x1C, 0x22, 0x42, 0x42, 0x42, 0x22, 0x1C, 0x00],
    ord('P'): [0x3C, 0x22, 0x22, 0x3C, 0x20, 0x20, 0x20, 0x00],    ord('Q'): [0x1C, 0x22, 0x42, 0x42, 0x4A, 0x24, 0x1A, 0x00],    ord('R'): [0x3C, 0x22, 0x22, 0x3C, 0x28, 0x24, 0x22, 0x00],    ord('S'): [0x1E, 0x20, 0x20, 0x0C, 0x02, 0x22, 0x1C, 0x00],
    ord('T'): [0x3E, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x00],    ord('U'): [0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x1C, 0x00],    ord('V'): [0x22, 0x22, 0x22, 0x14, 0x14, 0x0C, 0x0C, 0x00],    ord('W'): [0x41, 0x41, 0x49, 0x49, 0x55, 0x63, 0x41, 0x00],
    ord('X'): [0x22, 0x22, 0x14, 0x0C, 0x14, 0x22, 0x22, 0x00],    ord('Y'): [0x22, 0x22, 0x14, 0x14, 0x08, 0x08, 0x08, 0x00],    ord('Z'): [0x3E, 0x02, 0x04, 0x08, 0x10, 0x20, 0x3E, 0x00],    ord('['): [0x1C, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1C, 0x00],
    ord('\\'): [0x20, 0x10, 0x08, 0x04, 0x02, 0x01, 0x00, 0x00],    ord(']'): [0x38, 0x08, 0x08, 0x08, 0x08, 0x08, 0x38, 0x00],    ord('^'): [0x08, 0x14, 0x22, 0x00, 0x00, 0x00, 0x00, 0x00],    ord('_'): [0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3E],
    ord('`'): [0x10, 0x08, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00],    ord('a'): [0x00, 0x00, 0x1C, 0x02, 0x1E, 0x12, 0x1E, 0x00],    ord('b'): [0x20, 0x20, 0x2C, 0x32, 0x22, 0x22, 0x3C, 0x00],    ord('c'): [0x00, 0x00, 0x1E, 0x20, 0x20, 0x20, 0x1E, 0x00],
    ord('d'): [0x04, 0x04, 0x1C, 0x24, 0x24, 0x24, 0x1E, 0x00],    ord('e'): [0x00, 0x00, 0x1C, 0x22, 0x3E, 0x20, 0x1E, 0x00],    ord('f'): [0x0C, 0x12, 0x10, 0x3C, 0x10, 0x10, 0x10, 0x00],    ord('g'): [0x00, 0x00, 0x1E, 0x24, 0x24, 0x1C, 0x04, 0x18],
    ord('h'): [0x20, 0x20, 0x2C, 0x32, 0x22, 0x22, 0x22, 0x00],    ord('i'): [0x08, 0x00, 0x18, 0x08, 0x08, 0x08, 0x1C, 0x00],    ord('j'): [0x04, 0x00, 0x0C, 0x04, 0x04, 0x04, 0x24, 0x18],    ord('k'): [0x20, 0x20, 0x24, 0x28, 0x30, 0x28, 0x24, 0x00],
    ord('l'): [0x18, 0x08, 0x08, 0x08, 0x08, 0x08, 0x1C, 0x00],    ord('m'): [0x00, 0x00, 0x1E, 0x2A, 0x2A, 0x2A, 0x2A, 0x00],    ord('n'): [0x00, 0x00, 0x2C, 0x32, 0x22, 0x22, 0x22, 0x00],    ord('o'): [0x00, 0x00, 0x1C, 0x22, 0x22, 0x22, 0x1C, 0x00],
    ord('p'): [0x00, 0x00, 0x3C, 0x22, 0x22, 0x3C, 0x20, 0x20],    ord('q'): [0x00, 0x00, 0x1A, 0x26, 0x26, 0x1E, 0x04, 0x04],    ord('r'): [0x00, 0x00, 0x2C, 0x32, 0x20, 0x20, 0x20, 0x00],    ord('s'): [0x00, 0x00, 0x1E, 0x20, 0x1C, 0x02, 0x3C, 0x00],
    ord('t'): [0x10, 0x10, 0x3E, 0x10, 0x10, 0x12, 0x0C, 0x00],    ord('u'): [0x00, 0x00, 0x22, 0x22, 0x22, 0x22, 0x1E, 0x00],    ord('v'): [0x00, 0x00, 0x22, 0x22, 0x14, 0x14, 0x08, 0x00],    ord('w'): [0x00, 0x00, 0x41, 0x49, 0x49, 0x55, 0x36, 0x00],
    ord('x'): [0x00, 0x00, 0x22, 0x14, 0x08, 0x14, 0x22, 0x00],    ord('y'): [0x00, 0x00, 0x22, 0x22, 0x1C, 0x04, 0x18, 0x00],    ord('z'): [0x00, 0x00, 0x3E, 0x04, 0x08, 0x10, 0x3E, 0x00],    ord('{'): [0x0C, 0x10, 0x10, 0x20, 0x10, 0x10, 0x0C, 0x00],
    ord('|'): [0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x00],    ord('}'): [0x30, 0x08, 0x08, 0x04, 0x08, 0x08, 0x30, 0x00],    ord('~'): [0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00],    ord('^'): [0x08, 0x14, 0x22, 0x00, 0x00, 0x00, 0x00, 0x00],
    ord('_'): [0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3E]
}

icons = {
    ord('0'): [64, 0, 0, 0, 1, 0, 0, 0, 1, 64, 0, 0, 0, 1, 64, 0, 0, 5, 65, 0, 0, 0, 1, 64, 0, 0, 21, 65, 0, 0, 0, 1, 68, 0, 0, 42, 65, 0, 0, 0, 1, 4, 0, 0, 80, 65, 0, 0, 0, 8, 132, 0, 0, 0, 32, 0, 0, 0, 16, 128, 0, 0, 0, 160, 0, 0, 0, 32, 128, 0, 0, 4, 162, 0, 0, 0, 0, 136, 0, 0, 10, 130, 0, 0, 1, 0, 136, 0, 0, 4, 2, 32, 0, 2, 0, 8, 0, 0, 20, 0, 32, 0, 4, 0, 16, 0, 0, 16, 0, 80, 0, 4, 0, 8, 0, 0, 16, 0, 32, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0],
    ord('1'): [32, 0, 0, 0, 8, 128, 20, 0, 2, 32, 0, 0, 0, 136, 0, 20, 0, 2, 0, 0, 0, 5, 8, 0, 20, 0, 10, 0, 0, 0, 10, 10, 128, 40, 0, 10, 2, 0, 0, 2, 10, 136, 20, 0, 10, 2, 64, 0, 2, 1, 17, 40, 0, 9, 0, 64, 0, 0, 32, 1, 84, 0, 8, 128, 64, 0, 0, 32, 1, 80, 0, 0, 128, 0, 128, 0, 32, 0, 160, 0, 0, 0, 0, 128, 0, 0, 0, 32, 0, 0, 0, 0, 128, 0, 0, 0, 36, 0, 0, 0, 0, 0, 0, 0, 0, 20, 0, 0, 0, 0, 0, 0, 0, 0, 40, 0, 0, 0, 0, 0, 0, 0, 0, 20, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0],
    ord('2'): [21, 85, 0, 0, 0, 0, 0, 0, 0, 21, 85, 0, 0, 0, 0, 0, 0, 0, 21, 64, 0, 128, 0, 0, 0, 32, 0, 42, 160, 0, 160, 0, 0, 128, 160, 0, 20, 64, 0, 128, 0, 2, 162, 160, 0, 16, 0, 0, 0, 0, 129, 85, 64, 0, 16, 0, 0, 0, 0, 0, 85, 0, 0, 0, 0, 0, 0, 0, 0, 42, 0, 0, 0, 0, 0, 0, 0, 0, 40, 0, 0, 0, 0, 64, 0, 0, 0, 16, 0, 0, 0, 0, 64, 0, 0, 0, 34, 0, 0, 0, 0, 128, 0, 0, 0, 16, 0, 0, 0, 0, 64, 0, 0, 0, 34, 0, 0, 0, 0, 128, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0],
    ord('3'): [40, 0, 0, 21, 64, 0, 0, 4, 1, 84, 0, 1, 21, 64, 40, 0, 4, 1, 80, 0, 5, 0, 128, 168, 0, 4, 5, 64, 32, 10, 2, 2, 168, 128, 0, 10, 0, 32, 10, 1, 5, 8, 0, 0, 4, 8, 32, 10, 0, 2, 0, 0, 0, 0, 8, 0, 10, 0, 4, 64, 0, 0, 0, 16, 0, 21, 0, 4, 0, 0, 0, 0, 16, 0, 5, 0, 20, 0, 0, 4, 4, 16, 0, 2, 41, 20, 0, 0, 16, 36, 0, 0, 0, 80, 16, 0, 0, 0, 168, 0, 0, 0, 0, 0, 0, 0, 0, 80, 0, 0, 0, 0, 0, 0, 0, 0, 168, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0],
    ord('4'): [64, 0, 0, 0, 1, 0, 0, 0, 1, 64, 0, 0, 0, 1, 64, 0, 0, 5, 65, 0, 0, 0, 1, 64, 0, 0, 21, 65, 0, 0, 0, 1, 68, 0, 0, 42, 65, 0, 0, 0, 1, 4, 0, 0, 80, 65, 0, 0, 0, 1, 4, 0, 0, 0, 128, 0, 0, 0, 5, 0, 0, 0, 0, 0, 0, 0, 0, 4, 0, 0, 0, 0, 32, 0, 0, 0, 8, 0, 0, 0, 0, 32, 0, 0, 0, 1, 0, 0, 0, 0, 128, 0, 0, 0, 2, 0, 0, 0, 0, 64, 0, 0, 0, 1, 0, 0, 0, 0, 64, 0, 0, 0, 1, 0, 0, 0, 0, 64, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0],
    ord('5'): [20, 0, 0, 0, 0, 0, 0, 0, 0, 84, 0, 0, 0, 0, 0, 0, 0, 1, 80, 0, 0, 0, 0, 0, 0, 0, 5, 73, 0, 0, 0, 2, 37, 85, 0, 10, 136, 0, 8, 0, 2, 42, 170, 64, 10, 136, 0, 2, 128, 2, 0, 0, 128, 1, 0, 0, 2, 0, 5, 0, 0, 16, 0, 10, 0, 4, 0, 4, 0, 0, 16, 0, 20, 0, 4, 0, 0, 0, 0, 16, 0, 0, 0, 4, 0, 0, 0, 0, 16, 0, 0, 0, 2, 0, 0, 0, 0, 8, 0, 0, 0, 4, 0, 0, 0, 0, 40, 0, 0, 0, 0, 0, 0, 0, 0, 80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0],
    ord('6'): [42, 170, 0, 0, 0, 0, 0, 0, 0, 42, 170, 0, 0, 0, 0, 0, 0, 0, 42, 128, 0, 0, 0, 0, 0, 0, 0, 85, 64, 0, 0, 0, 1, 0, 0, 0, 40, 128, 0, 0, 0, 5, 64, 0, 0, 32, 0, 0, 0, 1, 2, 144, 0, 0, 32, 2, 0, 0, 0, 0, 68, 0, 0, 0, 1, 0, 0, 0, 0, 4, 0, 0, 0, 2, 16, 0, 0, 0, 8, 0, 0, 0, 0, 80, 0, 0, 0, 0, 0, 0, 0, 0, 168, 0, 0, 0, 0, 0, 0, 0, 0, 80, 0, 0, 0, 0, 0, 0, 0, 0, 168, 0, 0, 0, 0, 0, 0, 0, 0, 80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0],
}

def expand_icon(icon_data):
    # Calculate the width and height of the icon
    icon_width = 36
    icon_height = 36

    # Convert the compressed icon data to binary representation
    binary_representation = ''.join([format(byte, '08b') for byte in icon_data])

    # Ensure that the binary representation is padded to the correct length
    binary_representation = binary_representation.ljust(icon_width * icon_height, '0')

    # Create a 2D binary matrix
    binary_matrix = [[int(binary_representation[row * icon_width + col]) for col in range(icon_width)] for row in range(icon_height)]

    return binary_matrix

# Print the expanded binary representation of each icon
for key, value in icons.items():
    binary_matrix = expand_icon(value)
    print(f"Icon '{chr(key)}':")
    for row in binary_matrix:
        print(' '.join(map(str, row)))
    print()

# Print the expanded binary representation of each icon
for key, value in icons.items():
    binary_matrix = expand_icon(value)
    print(f"Icon '{chr(key)}':")
    for row in binary_matrix:
        print(' '.join(map(str, row)))
    print()


def get_char_representation(char):
    return font.get (ord (char),
                     [0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000])

def get_icon_representation(char):
    return font.get (ord (char),
                     [0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000])


def execute_initial_commands():
    commandenableAAdata = "on -f mmx /net/mmx/mnt/app/eso/bin/apps/pc i:1304:210 1"
    try:
        print ("Executing '{}'".format (commandenableAAdata))
        subprocess.Popen (commandenableAAdata, stdout=subprocess.PIPE, shell=True, preexec_fn=os.setsid)
    except subprocess.CalledProcessError as e:
        print ("Cannot enable AA data: " + {e.returncode})

    commandslay = "/bin/slay loadandshowimage"
    try:
        print ("Executing '{}'".format (commandslay))
        subprocess.Popen (commandslay, stdout=subprocess.PIPE, shell=True, preexec_fn=os.setsid)
    except subprocess.CalledProcessError as e:
        print ("Kill all runnnig loadandshowimage: " + {e.returncode})

    commandcontext = "/eso/bin/apps/dmdt sc 4 -9"
    try:
        print ("Executing '{}'".format (commandcontext))
        subprocess.Popen (commandcontext, stdout=subprocess.PIPE, shell=True, preexec_fn=os.setsid)
    except subprocess.CalledProcessError as e:
        print ("Set context of display 4 failed with error: " + {e.returncode})

    time.sleep (2)

    commandbuffer = "/eso/bin/apps/dmdt sb 0"
    try:
        print ("Executing '{}'".format (commandbuffer))
        subprocess.Popen (commandbuffer, stdout=subprocess.PIPE, shell=True, preexec_fn=os.setsid)
    except subprocess.CalledProcessError as e:
        print ("Switch buffer on display 0 failed with error:" + {e.returncode})

    commandbuffer2 = "/eso/bin/apps/dmdt sc 0 71"
    try:
        print ("Executing '{}'".format (commandbuffer2))
        subprocess.Popen (commandbuffer2, stdout=subprocess.PIPE, shell=True, preexec_fn=os.setsid)
    except subprocess.CalledProcessError as e:
        print ("Switch buffer on display 0 failed with error:" + {e.returncode})


def read_data(position):
    command = ""
    if platform.system () == "Windows":
        return "0"
    else:  # Assuming we are on QNX
        command = "on -f mmx /net/mmx/mnt/app/eso/bin/apps/pc " + position

    try:
        process = subprocess.Popen (command, stdout=subprocess.PIPE, shell=True, preexec_fn=os.setsid)
        output, _ = process.communicate()
        print(output)
        return output.decode ('utf-8')  # Decode the binary output to a string

    except subprocess.CalledProcessError as e:
        print ("Error: " + str (e.returncode))
        return "0"


if platform.system () == "Windows":
    output_file = "render.bmp"
else:  # Assuming we are on QNX
    output_file = "/tmp/render.bmp"

def draw_text(text, x, y, scale=1):
    for char in text:
        char_representation = get_char_representation(char)
        char_height = len (char_representation)
        char_width = 8  # Assuming a fixed width of 8 for each character

        for row in reversed (range (char_height)):
            for col in reversed (range (char_width)):
                pixel = char_representation[row] & (1 << col)
                if pixel != 0:
                    # Scale the pixel coordinates by 'scale' to make the character bigger
                    for i in range(scale):
                        for j in range(scale):
                            set_pixel(x + (char_width - col - 1) * scale + i, y + (char_height - row - 1) * scale + j)

        x += char_width * scale  # Move to the next character, accounting for scaling


def draw_icon(icon, x, y, scale=1):
    icon_representation = get_icon_representation(icon)
    icon_height = 36
    icon_width = 36  # Assuming a fixed width of 36 for each icon

    for row in reversed (range (icon_height)):
        for col in reversed (range (icon_width)):
            pixel = icon_representation[row] & (1 << col)
            if pixel != 0:
                # Scale the pixel coordinates by 'scale' to make the character bigger
                for i in range(scale):
                    for j in range(scale):
                        set_pixel(x + (icon_width - col - 1) * scale + i, y + (icon_height - row - 1) * scale + j)

    x += icon_width * scale  # Move to the next character, accounting for scaling

def prepare_text(text, textScale, width, height, offsetx, offsety):
    text_x = (width - len (text) * 8 * textScale) // 2  # Assuming 8 pixels per character
    text_y = height // 2 - 8  # Assuming 8-pixel font height

    draw_text (text, text_x + offsetx, text_y + offsety, textScale)


execute_once = True
while True:

    start_time = time.time ()  # Record the start time
    # Create a blank monochromatic image of size 800x480 (all pixels initialized to 0)
    pixels = bytearray ([0] * (width * height // 8))  # 1 byte per 8 pixels

    ############################################### RENDER ##################################
    # Left column
    prepare_text("D:" + read_data(distancePos)[0:3], 8, width, height, -150, -60)
    prepare_text(read_data(speedPos)[0:3], 8, width, height, -150, 30)

    # Right column
    prepare_text("GPS lat    : " + read_data("arrow"), 2, width, height, 150, -60)
    expand_icon("0")
    draw_icon("0",150,-60,1)

    # BMP header for a monochromatic (1-bit) BMP
    bmp_header = struct.pack ('<2sIHHI', b'BM', len (pixels) + 62, 0, 0, 62)

    # Bitmap info header
    bmp_info_header = struct.pack ('<IiiHHIIIIII', 40, width, height, 1, 1, 0, len (pixels), 0, 2, 2, 0)

    # Color palette for monochromatic BMP (black and white)
    color_palette = struct.pack ('<II', 0x00000000, 0x00FFFFFF)

    # Create and save the BMP file
    with open (output_file, 'wb') as bmp_file:
        bmp_file.write (bmp_header)
        bmp_file.write (bmp_info_header)
        bmp_file.write (color_palette)
        bmp_file.write (pixels)

    end_time = time.time ()  # Record the end time
    elapsed_time = end_time - start_time
    print ("Time taken to generate BMP to path '{}' is {:.6f} seconds".format (output_file, elapsed_time))

    # Command to run the external process (replace with your command)
    command = ["/eso/bin/apps/loadandshowimage /tmp/render.bmp"]

    # Start the external process
    if platform.system () != "Windows":
        print ("Executing '{}'".format (command))
        process = subprocess.Popen (command, stdout=subprocess.PIPE, shell=True, preexec_fn=os.setsid)

    # Sleep for 2 seconds
    time.sleep (2)

    # Send a SIGINT signal to the process
    # Start the external process
    if platform.system () != "Windows":
        os.killpg (os.getpgid (process.pid), signal.SIGTERM)
        # Wait for the process to finish
        process.wait ()

    if execute_once:
        if platform.system () != "Windows":
            execute_initial_commands ()
        execute_once = False  # Set the control variable to False after execution
