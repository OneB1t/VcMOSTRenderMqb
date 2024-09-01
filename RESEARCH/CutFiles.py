import os

def split_file(input_file, output_directory, chunk_size):
    with open(input_file, 'rb') as f:
        chunk_number = 1
        while True:
            chunk = f.read(chunk_size)
            if not chunk:
                break
            output_file = os.path.join(output_directory, f"{chunk_number}_{os.path.basename(input_file)}")
            with open(output_file, 'wb') as out:
                out.write(chunk)
            chunk_number += 1

# Example usage:
input_file = '/fs/sda0/esotrace_SD/001_20240312_12-58-19/log_0010.esotrace'  # Replace with the path to your 10MB file
output_directory = '/fs/sda0/esotrace_SD/cut'   # Replace with the directory where you want to save the 1MB files
chunk_size = 1024 * 128  # 1MB in bytes

split_file(input_file, output_directory, chunk_size)
