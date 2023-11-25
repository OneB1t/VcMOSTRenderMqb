import os

from PIL import Image

def convert_to_bw_bmp_and_print(input_path):
    # Open the PNG image
    original_image = Image.open(input_path)

    # Resize the image to 36x36 pixels
    resized_image = original_image.resize((36, 36))

    # Convert the image to black and white
    bw_image = resized_image.convert('1')  # '1' mode is for 1-bit pixels (black and white)

    # Get the pixel data as a 2D list
    pixel_data = list(bw_image.getdata())
    width, height = bw_image.size
    pixels = [pixel_data[i:i+width] for i in range(0, width*height, width)]

    # Print the binary array to the console
    for row in pixels:
        row_str = ''.join(['1' if pixel == 0 else '0' for pixel in row])
        print(row_str)

def process_folder(folder_path):
    # Get a list of all PNG files in the folder
    png_files = [f for f in os.listdir(folder_path) if f.lower().endswith('.png')]

    # Process each PNG file in the folder
    for png_file in png_files:
        file_path = os.path.join(folder_path, png_file)
        convert_to_bw_bmp_and_print(file_path)

if __name__ == "__main__":
    folder_path = 'icons'  # Replace with the path to your folder containing PNG files

    process_folder(folder_path)
