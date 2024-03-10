import cv2
import time

def display_image(image_path):
    # Load the image
    img = cv2.imread(image_path)

    # Check if the image was loaded successfully
    if img is None:
        print("Error: Could not load image")
        return

    # Display the image
    cv2.imshow('VCSimulator', img)
    cv2.setWindowProperty('VCSimulator', cv2.WND_PROP_TOPMOST, 1)
    cv2.waitKey(1)

def main():
    image_path = "render.bmp"
    interval = 1 # Interval in seconds

    while True:
        display_image(image_path)
        time.sleep(interval)

        # Check if a key is pressed
        if cv2.waitKey(1) & 0xFF == ord('q'):
            break

    cv2.destroyAllWindows()

if __name__ == "__main__":
    main()
