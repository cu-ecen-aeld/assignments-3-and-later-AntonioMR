#!/bin/sh

# Exit inmediatelly in case of error
set -e

# Check script parameters available
if [ $# -ne 2 ]; then
    echo "Error: 2 parameters expected, $# provided"
    exit 1
fi

# Assing variable to the input parameter for better readability
filesdir=$1
searchstr=$2

if [ -d $filesdir ]
then
    #find type regular files
    filesnumber=$(find $filesdir -type f | wc -l)
    
    # find recursive and exclude binary files
    matchinglines=$(grep -r -I $searchstr $filesdir | wc -l)

    # output the assignment requested text
    echo "The number of files are $filesnumber and the number of matching lines are $matchinglines"
else
    # Not a valid directory provided in shell script parameter 1
    echo "Error: \"$filesdir\" must be a directory"
    exit 1
fi

exit 0