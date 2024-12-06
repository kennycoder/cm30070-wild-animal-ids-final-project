#!/bin/bash

# Function to create a CSV file with path and label
create_csv() {
  local output_csv="file_paths_and_labels.csv"

  # Write the header to the CSV file
  echo "path,label" > "$output_csv"

  # Iterate through all subfolders and their files
  find . -type d | while read -r folder; do
    local label=$(basename "$folder")

    for file in "$folder"/*; do
      if [ -f "$file" ]; then
        # Append the file path and label to the CSV
        echo "$(realpath "$file"),$label" >> "$output_csv"
      fi
    done
  done

  echo "CSV file created: $output_csv"
}

# Main script execution
create_csv

