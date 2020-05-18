#!/bin/bash
# Author: David Healey
# Written: 05/2020
# License: Public Domain

# -- Adds loop points to a folder of wav files -- #

#First argument is an Ardour session file containing LOOP START LOOP END markers.There should be two positions for each wav file.
#Second argument is a folder containing wav files

#Ardour session file
markersFile=$1

#get list of wavs
declare -a wavs

wav_dir=$2

for entry in "$wav_dir"/*".wav"
do
  wavs+=("$entry")
done

#parse file into array
markers=($(grep "LOOP START\|LOOP END" "$markersFile" | cut -d '"' -f4 | cut -d '-' -f2))

#write each marker to wav
start=""
end=""
wav=""
count=0

for i in "${markers[@]}"
do
  if [ -z $start ]
  then
  	start=$i
  else
  	end=$i
  fi

  if [ $start ] && [ $end ]
  then
	dir=$(dirname "${wavs[$count]}")
 	file=$(basename "${wavs[$count]}")
 	echo $file - $start : $end

  	fullPath="${wavs[$count]}"
  	
	if [ -z $fullPath ]
	then
	  echo "Reached last input file before last loop marker"
	  break
	fi

 	./wavesmpl $fullPath $start $end "$fullPath-cuepoints"

 	#rename the output file, replacing the original, with a safety check to make sure the output was created
 	test -f "$fullPath-cuepoints" && mv -f "$fullPath-cuepoints" "$fullPath"

  	unset start
  	unset end
  	((count++))
  fi

done
