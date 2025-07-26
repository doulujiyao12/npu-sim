#!/bin/bash

if [ $# -ne 1 ]; then
    echo "Usage: $0 <filename>"
    exit 1
fi

filename=$1
tempfile=$(mktemp)

while IFS= read -r line; do
    trimmed_line=$(echo "$line" | sed -e 's/^[[:space:]]*//' -e 's/[[:space:]]*$//')
    
    if [[ "$trimmed_line" == \"dest\":* ]]; then
        num=$(echo "$trimmed_line" | grep -oE '[0-9]+')
        line=$(echo "$line" | sed "s/\"dest\": [0-9]\+/\"dest\": *${num}*/")
    fi
    
    if [[ "$trimmed_line" == \"id\":* ]]; then
        num=$(echo "$trimmed_line" | grep -oE '[0-9]+')
        line=$(echo "$line" | sed "s/\"id\": [0-9]\+/\"id\": *${num}*/")
    fi
    
    echo "$line"
done < "$filename" > "$tempfile"

mv "$tempfile" "$filename"

echo "处理完成"