#!/bin/bash


# $1 $2 and $3 together specificy ClueWebID - directory, file, ID respectively.
# $4 is the session directory where the seed positives doc lives

# Get offset
OFFSET=$(grep clueweb12-$1-$2-$3 /mnt/a536sing/*/Disk*/$1/*$2.warc.gz.idx | awk '{print $2}')

# Get warc-file
WFILE=$(ls /mnt/a536sing/*/Disk*/$1/*$2.warc.gz)

# Run extraction
/zpipe/zchunk -s "$OFFSET" < "$WFILE.x" > "$WFILE.idx.dir/$OFFSET"

echo "$WFILE.idx.dir/$OFFSET" >> "$4seed.posfiles"
