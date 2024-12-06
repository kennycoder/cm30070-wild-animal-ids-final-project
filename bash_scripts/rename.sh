#!/bin/bash

generate_uuid() {
    uuidgen
}

# Function to rename files in a given folder, handling multiple extensions
rename_files() {
  local folder="$1"

  # Extract folder name
  local foldername=$(basename "$folder")

  # Iterate through files with specified extensions
  for file in "$folder"/*.{png,jpg,jpeg,bmp,tiff}; do
    if [ -f "$file" ]; then
      # Construct the new filename
      local newname="${foldername}_$(generate_uuid).$(echo "$file" | awk -F. '{print $NF}')"
      local newpath="$folder/$newname"

      # Rename the file
      mv "$file" "$newpath"
      echo "Renamed $file to $newpath"
    fi
  done
}

# Main script execution
main_folder="$(pwd)"  # Start from the current directory

# Find all subfolders and process them
find "$main_folder" -type d | while read -r subfolder; do
  rename_files "$subfolder"
done

