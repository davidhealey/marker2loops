#!/bin/bash
# Author: David Healey
# Written: 05/2020
# License: Public Domain

# -- Adds loop points to a folder of wav files -- #

#First argument is an Ardour session file containing LOOP START LOOP END markers.There should be two positions for each wav file.
#Second argument is a folder containing wav files

#Ardour session file
ardourSession=$1

#folder containing wavs
dir=$2

#get list of wavs
declare -a wavs

wav_dir=$2

for entry in "$wav_dir"/*".wav"
do
  wavs+=("$entry")
done

#get loop markers
markStarts=($(grep "LOOP START" "$ardourSession" | cut -d '"' -f6 | cut -d '-' -f2 | cut -d "." -f 1)) #loop marker position in session
loopStarts=($(grep "LOOP START" "$ardourSession" | cut -d '"' -f4 | cut -d '-' -f2 | cut -d "." -f 1)) #loop start in region
loopEnds=($(grep "LOOP END" "$ardourSession" | cut -d '"' -f4 | cut -d '-' -f2 | cut -d "." -f 1)) #loop end in region

#get ranges
rangeStarts=($(grep "IsRangeMarker" "$ardourSession" | cut -d '"' -f6)) #range start in session
rangeEnds=($(grep "IsRangeMarker" "$ardourSession" | cut -d '"' -f8)) #range end in session
rangeNames=($(grep "IsRangeMarker" "$ardourSession" | cut -d '"' -f4)) #range name

#get region name associated with each loop marker
for i in "${!markStarts[@]}"
do  
  for j in "${!rangeStarts[@]}"
  do
	if [ $(("${markStarts[$i]}" > "${rangeStarts[$j]}" && "${markStarts[$i]}" < "${rangeEnds[$j]}")) == 1 ]
	then
	  name="${rangeNames[$j]}" #range name should be same as wav (or very close) 
	  start="${loopStarts[$i]}"
	  end="${loopEnds[$i]}"

	  #get wav file that contains range name
	  wav=""
	  for w in "${wavs[@]}"
	  do
	  	if [[ $w == *"$name"* ]]
	  	then
		  wav=$w
		  break
	  	fi
	  done

	  if [[ -f "$wav" ]] #check the file exists
	  then
		echo $wav : $start : $end
	  
		./wavesmpl $wav $start $end "$wav-cuepoints"

		#rename the output file, replacing the original, with a safety check to make sure the output was created
		test -f "$wav-cuepoints" && mv -f "$wav-cuepoints" "$wav"
	  fi
	  break
	fi	
  done
  
done