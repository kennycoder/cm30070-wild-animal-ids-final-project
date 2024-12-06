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
    convert "$image" -set colorspace Gray -separate -average -resize 96x96\! "$image"  # Resize and overwrite the original
done

echo "All PNG images have been resized to 96x96 pixels."

