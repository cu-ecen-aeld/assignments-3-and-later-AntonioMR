#!/bin/bash

# Exit inmediatelly in case of error
set -e

# Check script parameters available
if [ $# -ne 2 ]; then
    echo "Error: 2 parameters expected, $# provided"
    exit 1
fi

# Assing variable to the input parameter for better readability
writefile=$1
writestr=$2

# Check that argument 1 is not an directory
if [ -d $writefile ]; then
    echo "Error: First argument can't be a directory"
    exit 1
fi

# Create the route expecified. -p option to create parents directories 
# and don't get an error if the route exits.
# Touch the file to create it if not exits
mkdir -p $(dirname $writefile) && touch $writefile

# Overwrite the file content with the text provided
echo $writestr > $writefile

exit 0