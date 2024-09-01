import os
from PIL import Image

def convert_to_bw_bmp_and_dict(input_path):
    # Extract the filename without extension
    filename = os.path.splitext(os.path.basename(input_path))[0]

    # Open the PNG image
    original_image = Image.open(input_path)

    # Resize the image to 36x36 pixels
    resized_image = original_image.resize((36, 36))

    # Convert the image to black and white
    bw_image = resized_image.convert('1')  # '1' mode is for 1-bit pixels (black and white)

    # Get the pixel data as a 2D list
    pixel_data = list(bw_image.getdata())
    width, height = bw_image.size
    pixels = [pixel_data[i:i + width] for i in range(0, width * height, width)]

    # Create a dictionary for the icon
    icon_dict = {}

    # Convert the entire binary representation and add it to the dictionary
    binary_representation = ''.join(['1' if pixel == 255 else '0' for row in pixels for pixel in row])
    binary_representation_hex = hex(int(binary_representation, 2))[2:].upper().zfill((len(binary_representation) + 3) // 4)

    # Condense the hex representation (remove leading zeros)
    condensed_hex_representation = binary_representation_hex.lstrip('0x').zfill(len(binary_representation_hex)//2)

    # Convert the condensed hex representation back to a list of integers
    icon_dict[filename] = [int(condensed_hex_representation[i:i + 2], 16) for i in range(0, len(condensed_hex_representation), 2)]

    return icon_dict

def process_folder(folder_path):
    # Get a list of all PNG files in the folder
    png_files = [f for f in os.listdir(folder_path) if f.lower().endswith('.png')]

    # Create a dictionary for all icons
    icons = {}

    # Process each PNG file in the folder
    for png_file in png_files:
        file_path = os.path.join(folder_path, png_file)
        print(f"\nProcessing {png_file}:\n")
        icon_dict = convert_to_bw_bmp_and_dict(file_path)
        icons.update(icon_dict)

    # Print the dictionary
    print("icons = {")
    for key, value in icons.items():
        print(f"    ord('{key}'): {value},")
    print("}")

if __name__ == "__main__":
    folder_path = 'icons'  # Replace with the path to your folder containing PNG files
    process_folder(folder_path)
