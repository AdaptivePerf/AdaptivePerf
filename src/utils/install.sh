#!/bin/bash
set -e

# This script is not meant to be run directly!
# Please use "make install prefix=<install prefix>" or "make uninstall".

if [[ "$1" == "uninstall" ]]; then
    if [[ -f to_remove.txt ]]; then
        rm $(cat to_remove.txt) to_remove.txt
    else
        echo "No to_remove.txt found! Have you installed the utilities before?"
        exit 1
    fi
else
    rm -f to_remove.txt
    for util in *.py; do
        new_path="$1"/bin/${util:0:-3}
        cp $util $new_path
        chmod +x $new_path
        echo $new_path >> to_remove.txt
    done
fi
