#!/bin/bash

# Check if ImageMagick is installed
if ! command -v convert &> /dev/null; then
    echo "Error: ImageMagick is not installed. Install it using 'sudo apt install imagemagick' or equivalent."
    exit 1
fi

# Directory to search for images
BASE_DIR="${1:-.}"  # Default to current directory if no argument is provided

# Find and resize all PNG images
find "$BASE_DIR" -type f -iname '*.png' | while read -r image; do
    echo "Resizing: $image"
    convert "$image" -resize 224x224\! "$image"  # Resize and overwrite the original
done

echo "All PNG images have been resized to 224x224 pixels."
