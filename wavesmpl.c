//
// wavecuepoint.c
// Created by Jim McGowan on 29/11/12.
// jim@bleepsandpops.com
// jim@malkinware.com
// 
// This function reads a .wav file and a text file containing marker locations (specified as frame indexes, one per line)
// and creates a new .wav file with embedded cue points for each location.  The code is standard, portable C.
//
// For a full description see http://bleepsandpops.com/post/37792760450/adding-cue-points-to-wav-files-in-c
//
// THIS CODE IS GIVEN TO THE PUBLIC DOMAIN
//

// Modified by David Healey 18/05/2020
// Added SMPL loop chunk handling.
// Added ability to pass in individual start/end points for each wav file, rather than using a markers txt file.

// The main function takes a wav file, a loop start and loop end time, and an output file.

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char *argv[]) {
    addMarkersToWaveFile(argv[1], argv[2], argv[3], argv[4], 0);    
}

// Some Structs that we use to represent and manipulate Chunks in the Wave files

// The header of a wave file
typedef struct {
  char chunkID[4];		// Must be "RIFF" (0x52494646)
	char dataSize[4];		// Byte count for the rest of the file (i.e. file length - 8 bytes)
	char riffType[4];	// Must be "WAVE" (0x57415645)
} WaveHeader;


// The format chunk of a wave file
typedef struct {
	char chunkID[4];				// String: must be "fmt " (0x666D7420).
	char chunkDataSize[4];			// Unsigned 4-byte little endian int: Byte count for the remainder of the chunk: 16 + extraFormatbytes.
	char compressionCode[2];		// Unsigned 2-byte little endian int
	char numberOfChannels[2];		// Unsigned 2-byte little endian int
	char sampleRate[4];				// Unsigned 4-byte little endian int
	char averageBytesPerSecond[4];	// Unsigned 4-byte little endian int: This value indicates how many bytes of wave data must be streamed to a D/A converter per second in order to play the wave file. This information is useful when determining if data can be streamed from the source fast enough to keep up with playback. = SampleRate * BlockAlign.
	char blockAlign[2];				// Unsigned 2-byte little endian int: The number of bytes per sample slice. This value is not affected by the number of channels and can be calculated with the formula: blockAlign = significantBitsPerSample / 8 * numberOfChannels
	char significantBitsPerSample[2];// Unsigned 2-byte little endian int
} FormatChunk;


// CuePoint: each individual 'marker' in a wave file is represented by a cue point.
typedef struct {
	char cuePointID[4];			// a unique ID for the Cue Point.
	char playOrderPosition[4];	// Unsigned 4-byte little endian int: If a Playlist chunk is present in the Wave file, this the sample number at which this cue point will occur during playback of the entire play list as defined by the play list's order.  **Otherwise set to same as sample offset??***  Set to 0 when there is no playlist.
	char dataChunkID[4];		// Unsigned 4-byte little endian int: The ID of the chunk containing the sample data that corresponds to this cue point.  If there is no playlist, this should be 'data'.
	char chunkStart[4];			// Unsigned 4-byte little endian int: The byte offset into the Wave List Chunk of the chunk containing the sample that corresponds to this cue point. This is the same chunk described by the Data Chunk ID value. If no Wave List Chunk exists in the Wave file, this value is 0.
	char blockStart[4];			// Unsigned 4-byte little endian int: The byte offset into the "data" or "slnt" Chunk to the start of the block containing the sample. The start of a block is defined as the first byte in uncompressed PCM wave data or the last byte in compressed wave data where decompression can begin to find the value of the corresponding sample value.
	char frameOffset[4];		// Unsigned 4-byte little endian int: The offset into the block (specified by Block Start) for the sample that corresponds to the cue point.
} CuePoint;


// CuePoints are stored in a CueChunk
typedef struct {
	char chunkID[4];		// String: Must be "cue " (0x63756520).
	char chunkDataSize[4];	// Unsigned 4-byte little endian int: Byte count for the remainder of the chunk: 4 (size of cuePointsCount) + (24 (size of CuePoint struct) * number of CuePoints).
	char cuePointsCount[4];	// Unsigned 4-byte little endian int: Length of cuePoints[].
	CuePoint *cuePoints;
} CueChunk;


// Some chunks we don't care about the contents and will just copy them from the input file to the output,
// so this struct just stores positions of such chunks in the input file
typedef struct {
	long startOffset; // in bytes
	long size;		  // in bytes
} ChunkLocation;

// sampleLoop: each individual 'marker' in a wave file is represented by a sampleLoop.
typedef struct {
	char cuePointID[4];			// a unique ID for the Cue Point.
	char type[4];	// Unsigned 4-byte little endian int: If a Playlist chunk is present in the Wave file, this the sample number at which this cue point will occur during playback of the entire play list as defined by the play list's order.  **Otherwise set to same as sample offset??***  Set to 0 when there is no playlist.
	char start[4];		// Unsigned 4-byte little endian int: The ID of the chunk containing the sample data that corresponds to this cue point.  If there is no playlist, this should be 'data'.
	char end[4];			// Unsigned 4-byte little endian int: The byte offset into the Wave List Chunk of the chunk containing the sample that corresponds to this cue point. This is the same chunk described by the Data Chunk ID value. If no Wave List Chunk exists in the Wave file, this value is 0.
	char fraction[4];			// Unsigned 4-byte little endian int: The byte offset into the "data" or "slnt" Chunk to the start of the block containing the sample. The start of a block is defined as the first byte in uncompressed PCM wave data or the last byte in compressed wave data where decompression can begin to find the value of the corresponding sample value.
	char playCount[4];		// Unsigned 4-byte little endian int: The offset into the block (specified by Block Start) for the sample that corresponds to the cue point.
} SampleLoop;

// sampleLoops are stored in a SMPLChunk
typedef struct {
	char chunkID[4];		// String: Must be "cue " (0x63756520).
	char chunkDataSize[4];	// Unsigned 4-byte little endian int: Byte count for the remainder of the chunk: 4 (size of cuePointsCount) + (24 (size of CuePoint struct) * number of CuePoints).
	char manufacturer[4];
	char product[4];
	char samplePeriod[4];
	char midiUnityNote[4];
	char midiPitchFraction[4];
	char smpteFormat[4];
	char smpteOffset[4];
	char numSampleLoops[4];
	char samplerData[4];
	SampleLoop *sampleLoops;
} SMPLChunk;


// For such chunks that we will copy over from input to output, this function does that in 1MB pieces
int writeChunkLocationFromInputFileToOutputFile(ChunkLocation chunk, FILE *inputFile, FILE *outputFile);



// All data in a Wave file must be little endian.
// These are functions to convert 2- and 4-byte unsigned ints to and from little endian, if needed

enum HostEndiannessType {
    EndiannessUndefined = 0,
    LittleEndian,
    BigEndian
};

static enum HostEndiannessType HostEndianness = EndiannessUndefined;

enum HostEndiannessType getHostEndianness();
uint32_t littleEndianBytesToUInt32(char littleEndianBytes[4]);
void uint32ToLittleEndianBytes(uint32_t uInt32Value, char out_LittleEndianBytes[4]);
uint16_t littleEndianBytesToUInt16(char littleEndianBytes[2]);
void uint16ToLittleEndianBytes(uint16_t uInt16Value, char out_LittleEndianBytes[2]);



// The main function

enum CuePointMergingOption {
	MergeWithAnyExistingCuePoints = 0,
	ReplaceAnyExistingCuePoints
};

int addMarkersToWaveFile(char *inFilePath, char *markerStart, char *markerEnd, char *outFilePath, enum CuePointMergingOption mergeOption)
{

	int returnCode = 0;

	// Prepare some variables to hold data read from the input file
	FILE            *inputFile            = NULL;
	WaveHeader      *waveHeader           = NULL;
	FormatChunk     *formatChunk          = NULL;
	ChunkLocation   formatChunkExtraBytes = {0,0};
	CueChunk        existingCueChunk      = {{0}};
	existingCueChunk.cuePoints            = NULL;
	ChunkLocation   dataChunkLocation     = {0,0};
	const int       maxOtherChunks        = 256;   // How many other chunks can we expect to find?  Who knows! So lets pull 256 out of the air.  That's a nice computery number.
	int             otherChunksCount      = 0;
	ChunkLocation   otherChunkLocations[maxOtherChunks];

	//FILE            *markersFile          = NULL;
	CuePoint        *cuePoints            = NULL;
	CueChunk        cueChunk              = {{0}};
	SampleLoop      *sampleLoops            = NULL;
	SMPLChunk        smplChunk              = {{0}};

	FILE            *outputFile           = NULL;



	// Open the Input File
	inputFile = fopen(inFilePath, "rb");
	if (inputFile == NULL)
	{
		fprintf(stderr, "Could not open input file %s\n", inFilePath);
		returnCode = -1;
		goto CleanUpAndExit;
	}


	// Open the Markers file
	/*markersFile = fopen(markerFilePath, "rb");
	if (markersFile == NULL)
	{
		fprintf(stderr, "Could not open marker file %s\n", inFilePath);
		returnCode = -1;
		goto CleanUpAndExit;
	}*/



	// Get & check the input file header
	fprintf(stdout, "Reading input wave file.\n");

	waveHeader = (WaveHeader *)malloc(sizeof(WaveHeader));
	if (waveHeader == NULL)
	{
		fprintf(stderr, "Memory Allocation Error: Could not allocate memory for Wave File Header\n");
		returnCode = -1;
		goto CleanUpAndExit;
	}

	fread(waveHeader, sizeof(WaveHeader), 1, inputFile);
	if (ferror(inputFile) != 0)
	{
		fprintf(stderr, "Error reading input file %s\n", inFilePath);
		returnCode = -1;
		goto CleanUpAndExit;
	}

	if (strncmp(&(waveHeader->chunkID[0]), "RIFF", 4) != 0)
	{
		fprintf(stderr, "Input file is not a RIFF file\n");
		returnCode = -1;
		goto CleanUpAndExit;
	}

	if (strncmp(&(waveHeader->riffType[0]), "WAVE", 4) != 0)
	{
		fprintf(stderr, "Input file is not a WAVE file\n");
		returnCode = -1;
		goto CleanUpAndExit;
	}

	uint32_t remainingFileSize = littleEndianBytesToUInt32(waveHeader->dataSize) - sizeof(waveHeader->riffType); // dataSize does not counf the chunkID or the dataSize, so remove the riffType size to get the length of the rest of the file.

	if (remainingFileSize <= 0)
	{
		fprintf(stderr, "Input file is an empty WAVE file\n");
		returnCode = -1;
		goto CleanUpAndExit;
	}





	// Start reading in the rest of the wave file
	while (1)
	{
		char nextChunkID[4];

		// Read the ID of the next chunk in the file, and bail if we hit End Of File
		fread(&nextChunkID[0], sizeof(nextChunkID), 1, inputFile);
		if (feof(inputFile))
		{
			break;
		}

		if (ferror(inputFile) != 0)
		{
			fprintf(stderr, "Error reading input file %s\n", inFilePath);
			returnCode = -1;
			goto CleanUpAndExit;
		}


		// See which kind of chunk we have

		if (strncmp(&nextChunkID[0], "fmt ", 4) == 0)
		{
			// We found the format chunk

			formatChunk = (FormatChunk *)malloc(sizeof(FormatChunk));
			if (formatChunk == NULL)
			{
				fprintf(stderr, "Memory Allocation Error: Could not allocate memory for Wave File Format Chunk\n");
				returnCode = -1;
				goto CleanUpAndExit;
			}

			fseek(inputFile, -4, SEEK_CUR);
			fread(formatChunk, sizeof(FormatChunk), 1, inputFile);
			if (ferror(inputFile) != 0)
			{
				fprintf(stderr, "Error reading input file %s\n", inFilePath);
				returnCode = -1;
				goto CleanUpAndExit;
			}

			if (littleEndianBytesToUInt16(formatChunk->compressionCode) != (uint16_t)1)
			{
				fprintf(stderr, "Compressed audio formats are not supported\n");
				returnCode = -1;
				goto CleanUpAndExit;
			}

			// Note: For compressed audio data there may be extra bytes appended to the format chunk,
			// but as we are only handling uncompressed data we shouldn't encounter them

			// There may or may not be extra data at the end of the fomat chunk.  For uncompressed audio there should be no need, but some files may still have it.
			// if formatChunk.chunkDataSize > 16 (16 = the number of bytes for the format chunk, not counting the 4 byte ID and the chunkDataSize itself) there is extra data
			uint32_t extraFormatBytesCount = littleEndianBytesToUInt32(formatChunk->chunkDataSize) - 16;
			if (extraFormatBytesCount > 0)
			{
				formatChunkExtraBytes.startOffset = ftell(inputFile);
				formatChunkExtraBytes.size = extraFormatBytesCount;
				fseek(inputFile, extraFormatBytesCount, SEEK_CUR);
				if (extraFormatBytesCount % 2 != 0)
				{
					fseek(inputFile, 1, SEEK_CUR);
				}
			}


			printf("Got Format Chunk\n");
		}

		else if (strncmp(&nextChunkID[0], "data", 4) == 0)
		{
			// We found the data chunk
			dataChunkLocation.startOffset = ftell(inputFile) - sizeof(nextChunkID);

			// The next 4 bytes are the chunk data size - the size of the sample data
			char sampleDataSizeBytes[4];
			fread(sampleDataSizeBytes, sizeof(char), 4, inputFile);
			if (ferror(inputFile) != 0)
			{
				fprintf(stderr, "Error reading input file %s\n", inFilePath);
				returnCode = -1;
				goto CleanUpAndExit;
			}
			uint32_t sampleDataSize = littleEndianBytesToUInt32(sampleDataSizeBytes);

			dataChunkLocation.size = sizeof(nextChunkID) + sizeof(sampleDataSizeBytes) + sampleDataSize;


			// Skip to the end of the chunk.  Chunks must be aligned to 2 byte boundaries, but any padding at the end of a chunk is not included in the chunkDataSize
			fseek(inputFile, sampleDataSize, SEEK_CUR);
			if (sampleDataSize % 2 != 0)
			{
				fseek(inputFile, 1, SEEK_CUR);
			}

			printf("Got Data Chunk\n");
		}

		else if (strncmp(&nextChunkID[0], "cue ", 4) == 0)
		{
			// We found an existing Cue Chunk

			char cueChunkDataSizeBytes[4];
			fread(cueChunkDataSizeBytes, sizeof(char), 4, inputFile);
			if (ferror(inputFile) != 0)
			{
				fprintf(stderr, "Error reading input file %s\n", inFilePath);
				returnCode = -1;
				goto CleanUpAndExit;
			}
			uint32_t cueChunkDataSize = littleEndianBytesToUInt32(cueChunkDataSizeBytes);

			char cuePointsCountBytes[4];
			fread(cuePointsCountBytes, sizeof(char), 4, inputFile);
			if (ferror(inputFile) != 0)
			{
				fprintf(stderr, "Error reading input file %s\n", inFilePath);
				returnCode = -1;
				goto CleanUpAndExit;
			}
			uint32_t cuePointsCount = littleEndianBytesToUInt16(cuePointsCountBytes);

			// Read in the existing cue points into CuePoint Structs
			CuePoint *existingCuePoints = (CuePoint *)malloc(sizeof(CuePoint) * cuePointsCount);
			for (uint32_t cuePointIndex = 0; cuePointIndex < cuePointsCount; cuePointIndex++)
			{
				fread(&existingCuePoints[cuePointIndex], sizeof(CuePoint), 1, inputFile);
				if (ferror(inputFile) != 0)
				{
					fprintf(stderr, "Error reading input file %s\n", inFilePath);
					returnCode = -1;
					goto CleanUpAndExit;
				}
			}

			// Populate the existingCueChunk struct
			existingCueChunk.chunkID[0] = 'c';
			existingCueChunk.chunkID[1] = 'u';
			existingCueChunk.chunkID[2] = 'e';
			existingCueChunk.chunkID[3] = ' ';
			uint32ToLittleEndianBytes(cueChunkDataSize, existingCueChunk.chunkDataSize);
			uint32ToLittleEndianBytes(cuePointsCount, existingCueChunk.cuePointsCount);
			existingCueChunk.cuePoints = existingCuePoints;

			printf("Found Existing Cue Chunk\n");
		}

		else
		{
			// We have found a chunk type that we are not going to work with.  Just note the location so we can copy it to the output file later

			if (otherChunksCount >= maxOtherChunks)
			{
				fprintf(stderr, "Input file has more chunks than the maximum supported by this program (%d)\n", maxOtherChunks);
				returnCode = -1;
				goto CleanUpAndExit;
			}

			otherChunkLocations[otherChunksCount].startOffset = ftell(inputFile) - sizeof(nextChunkID);

			char chunkDataSizeBytes[4] = {0};
			fread(chunkDataSizeBytes, sizeof(char), 4, inputFile);
			if (ferror(inputFile) != 0)
			{
				fprintf(stderr, "Error reading input file %s\n", inFilePath);
				returnCode = -1;
				goto CleanUpAndExit;
			}
			uint32_t chunkDataSize = littleEndianBytesToUInt32(chunkDataSizeBytes);

			otherChunkLocations[otherChunksCount].size = sizeof(nextChunkID) + sizeof(chunkDataSizeBytes) + chunkDataSize;


			// Skip over the chunk's data, and any padding byte
			fseek(inputFile, chunkDataSize, SEEK_CUR);
			if (chunkDataSize % 2 != 0)
			{
				fseek(inputFile, 1, SEEK_CUR);
			}

			otherChunksCount++;

			fprintf(stdout, "Found chunk type \'%c%c%c%c\', size: %d bytes\n", nextChunkID[0], nextChunkID[1], nextChunkID[2], nextChunkID[3], chunkDataSize);
		}
	}



	// Did we get enough data from the input file to proceed?

	if ((formatChunk == NULL) || (dataChunkLocation.size == 0))
	{
		fprintf(stderr, "Input file did not contain any format data or did not contain any sample data\n");
		returnCode = -1;
		goto CleanUpAndExit;
	}

	// Read in the Markers File
	fprintf(stdout, "Reading markers file.\n");

	// The markers file should contain a locations for each marker (cue point), one location per line
	uint32_t cueLocations[2] = {0};
	cueLocations[0] = strtol(markerStart, NULL, 10);
	cueLocations[1] = strtol(markerEnd, NULL, 10);
	int cueLocationsCount = 2;

	// Did we get any cueLocations?
	if (cueLocationsCount < 1)
	{
		fprintf(stderr, "Did not find any cue point locations in the markers file\n");
		returnCode = -1;
		goto CleanUpAndExit;
	}

	fprintf(stdout, "Read %d cue locations from markers file.\nPreparing new cue chunk.\n", cueLocationsCount);


	// Create CuePointStructs for each cue location
	cuePoints = malloc(sizeof(CuePoint) * cueLocationsCount);
	if (cuePoints == NULL)
	{
		fprintf(stderr, "Memory Allocation Error: Could not allocate memory for Cue Points data\n");
		returnCode = -1;
		goto CleanUpAndExit;
	}

    //	uint16_t bitsPerSample = littleEndianBytesToUInt16(formatChunk->significantBitsPerSample);
    //	uint16_t bytesPerSample = bitsPerSample / 8;

	for (uint32_t i = 0; i < cueLocationsCount; i++)
	{
		uint32ToLittleEndianBytes(i + 1, cuePoints[i].cuePointID);
		uint32ToLittleEndianBytes(0, cuePoints[i].playOrderPosition);
		cuePoints[i].dataChunkID[0] = 'd';
		cuePoints[i].dataChunkID[1] = 'a';
		cuePoints[i].dataChunkID[2] = 't';
		cuePoints[i].dataChunkID[3] = 'a';
		uint32ToLittleEndianBytes(0, cuePoints[i].chunkStart);
		uint32ToLittleEndianBytes(0, cuePoints[i].blockStart);
		uint32ToLittleEndianBytes(cueLocations[i], cuePoints[i].frameOffset);
	}
	
	// Create SampleLoopStructs for each cue location
	sampleLoops = malloc(sizeof(SampleLoop) * cueLocationsCount);
	if (sampleLoops == NULL)
	{
		fprintf(stderr, "Memory Allocation Error: Could not allocate memory for Sample Loop data\n");
		returnCode = -1;
		goto CleanUpAndExit;
	}	

	for (uint32_t i = 0; i < cueLocationsCount; i++)
	{
		uint32ToLittleEndianBytes(i + 1, sampleLoops[i].cuePointID);
		uint32ToLittleEndianBytes(0, sampleLoops[i].type);
		uint32ToLittleEndianBytes(cueLocations[i], sampleLoops[i].start);
		uint32ToLittleEndianBytes(cueLocations[i+1], sampleLoops[i].end);
		uint32ToLittleEndianBytes(0, sampleLoops[i].fraction);
		uint32ToLittleEndianBytes(0, sampleLoops[i].playCount);
	}

	// If necesary, merge the cuePoints with any existing cue points from the input file
	if ( (mergeOption == MergeWithAnyExistingCuePoints) && (existingCueChunk.cuePoints != NULL) )
	{
		//...
	}

	// Populate the CueChunk Struct
	cueChunk.chunkID[0] = 'c';
	cueChunk.chunkID[1] = 'u';
	cueChunk.chunkID[2] = 'e';
	cueChunk.chunkID[3] = ' ';
	uint32ToLittleEndianBytes(4 + (sizeof(CuePoint) * cueLocationsCount), cueChunk.chunkDataSize);// See struct definition
	uint32ToLittleEndianBytes(cueLocationsCount, cueChunk.cuePointsCount);
	cueChunk.cuePoints = cuePoints;
	
	// Populate the SMPLChunk Struct
	smplChunk.chunkID[0] = 's';
	smplChunk.chunkID[1] = 'm';
	smplChunk.chunkID[2] = 'p';
	smplChunk.chunkID[3] = 'l';
	uint32ToLittleEndianBytes(36 + (sizeof(SampleLoop) * cueLocationsCount), smplChunk.chunkDataSize);// See struct definition
	uint32ToLittleEndianBytes(0, smplChunk.manufacturer);
	uint32ToLittleEndianBytes(0, smplChunk.product);
	uint32ToLittleEndianBytes(0, smplChunk.samplePeriod);
	uint32ToLittleEndianBytes(0, smplChunk.midiUnityNote);
	uint32ToLittleEndianBytes(0, smplChunk.midiPitchFraction);
	uint32ToLittleEndianBytes(0, smplChunk.smpteFormat);
	uint32ToLittleEndianBytes(0, smplChunk.smpteOffset);
	uint32ToLittleEndianBytes(cueLocationsCount, smplChunk.numSampleLoops);
	uint32ToLittleEndianBytes(0, smplChunk.samplerData);
	smplChunk.sampleLoops = sampleLoops;

	// Open the output file for writing
	outputFile = fopen(outFilePath, "wb");
	if (outputFile == NULL)
	{
		fprintf(stderr, "Could not open output file %s\n", outFilePath);
		returnCode = -1;
		goto CleanUpAndExit;
	}

	fprintf(stdout, "Writing output file.\n");

	// Update the file header chunk to have the new data size
	uint32_t fileDataSize = 0;
	fileDataSize += 4; // the 4 bytes for the Riff Type "WAVE"
	fileDataSize += sizeof(FormatChunk);
	fileDataSize += formatChunkExtraBytes.size;
	if (formatChunkExtraBytes.size % 2 != 0)
	{
		fileDataSize++; // Padding byte for 2byte alignment
	}

	fileDataSize += dataChunkLocation.size;
	if (dataChunkLocation.size % 2 != 0)
	{
		fileDataSize++;
	}

	for (int i = 0; i < otherChunksCount; i++)
	{
		fileDataSize += otherChunkLocations[i].size;
		if (otherChunkLocations[i].size % 2 != 0)
		{
			fileDataSize ++;
		}
	}
	fileDataSize += 4; // 4 bytes for CueChunk ID "cue "
	fileDataSize += 4; // UInt32 for CueChunk.chunkDataSize
	fileDataSize += 4; // UInt32 for CueChunk.cuePointsCount
	fileDataSize += (sizeof(CuePoint) * cueLocationsCount);

	fileDataSize += 4; // 4 bytes for SMPLChunk ID "smpl"
	fileDataSize += 4; // UInt32 for SMPLChunk.chunkDataSize
	fileDataSize += 4; // UInt32 for SMPLChunk.manufacturer
	fileDataSize += 4; // UInt32 for SMPLChunk.product
	fileDataSize += 4; // UInt32 for SMPLChunk.samplePeriod
	fileDataSize += 4; // UInt32 for SMPLChunk.midiUnityNote
	fileDataSize += 4; // UInt32 for SMPLChunk.midiPitchFraction
	fileDataSize += 4; // UInt32 for SMPLChunk.smpteFormat
	fileDataSize += 4; // UInt32 for SMPLChunk.smpteOffset
	fileDataSize += (sizeof(SampleLoop) * cueLocationsCount);
	fileDataSize += 4; // UInt32 for SMPLChunk.samplerData

	uint32ToLittleEndianBytes(fileDataSize, waveHeader->dataSize);

	// Write out the header to the new file
	if (fwrite(waveHeader, sizeof(*waveHeader), 1, outputFile) < 1)
	{
		fprintf(stderr, "Error writing header to output file.\n");
		returnCode = -1;
		goto CleanUpAndExit;
	}


	// Write out the format chunk
	if (fwrite(formatChunk, sizeof(FormatChunk), 1, outputFile) < 1)
	{
		fprintf(stderr, "Error writing format chunk to output file.\n");
		returnCode = -1;
		goto CleanUpAndExit;
	}
	else if (formatChunkExtraBytes.size > 0)
	{
		if (writeChunkLocationFromInputFileToOutputFile(formatChunkExtraBytes, inputFile, outputFile) < 0)
		{
			returnCode = -1;
			goto CleanUpAndExit;
		}
		if (formatChunkExtraBytes.size % 2 != 0)
		{
			if (fwrite("\0", sizeof(char), 1, outputFile) < 1)
			{
				fprintf(stderr, "Error writing padding character to output file.\n");
				returnCode = -1;
				goto CleanUpAndExit;

			}
		}
	}


	// Write out the start of new Cue Chunk: chunkID, dataSize and cuePointsCount
	if (fwrite(&cueChunk, sizeof(cueChunk.chunkID) + sizeof(cueChunk.chunkDataSize)+ sizeof(cueChunk.cuePointsCount), 1, outputFile) < 1)
	{
		fprintf(stderr, "Error writing cue chunk header to output file.\n");
		returnCode = -1;
		goto CleanUpAndExit;
	}

	// Write out the Cue Points
	for (uint32_t i = 0; i < littleEndianBytesToUInt32(cueChunk.cuePointsCount); i++)
	{
		if (fwrite(&(cuePoints[i]), sizeof(CuePoint), 1, outputFile) < 1)
		{
			fprintf(stderr, "Error writing cue point to output file.\n");
			returnCode = -1;
			goto CleanUpAndExit;
		}
	}
	
	// Write out the start of new SMPL Chunk
	if (fwrite(&smplChunk, sizeof(smplChunk.chunkID) + sizeof(smplChunk.chunkDataSize) + sizeof(smplChunk.manufacturer) + sizeof(smplChunk.product) + sizeof(smplChunk.samplePeriod) + sizeof(smplChunk.midiUnityNote) + sizeof(smplChunk.midiPitchFraction) + sizeof(smplChunk.smpteFormat) + sizeof(smplChunk.smpteOffset) + sizeof(smplChunk.numSampleLoops) + sizeof(smplChunk.samplerData), 1, outputFile) < 1)
	{
		fprintf(stderr, "Error writing smpl chunk header to output file.\n");
		returnCode = -1;
		goto CleanUpAndExit;
	}

	// Write out the Sample Loops
	for (uint32_t i = 0; i < littleEndianBytesToUInt32(smplChunk.numSampleLoops); i++)
	{
		if (fwrite(&(sampleLoops[i]), sizeof(SampleLoop), 1, outputFile) < 1)
		{
			fprintf(stderr, "Error writing sample loop to output file.\n");
			returnCode = -1;
			goto CleanUpAndExit;
		}
	}

	// Write out the other chunks from the input file
	for (int i = 0; i < otherChunksCount; i++)
	{
		if (writeChunkLocationFromInputFileToOutputFile(otherChunkLocations[i], inputFile, outputFile) < 0)
		{
			returnCode = -1;
			goto CleanUpAndExit;
		}
		if (otherChunkLocations[i].size % 2 != 0)
		{
			if (fwrite("\0", sizeof(char), 1, outputFile) < 1)
			{
				fprintf(stderr, "Error writing padding character to output file.\n");
				returnCode = -1;
				goto CleanUpAndExit;

			}
		}
	}


	// Write out the data chunk
	if (writeChunkLocationFromInputFileToOutputFile(dataChunkLocation, inputFile, outputFile) < 0)
	{
		returnCode = -1;
		goto CleanUpAndExit;
	}
	if (dataChunkLocation.size % 2 != 0)
	{
		if (fwrite("\0", sizeof(char), 1, outputFile) < 1)
		{
			fprintf(stderr, "Error writing padding character to output file.\n");
			returnCode = -1;
			goto CleanUpAndExit;

		}
	}


	printf("Finished.\n");


CleanUpAndExit:

	if (inputFile != NULL) fclose(inputFile);
	if (waveHeader != NULL) free(waveHeader);
	if (formatChunk != NULL) free(formatChunk);
	if (existingCueChunk.cuePoints != NULL) free(existingCueChunk.cuePoints);
	//if (markersFile != NULL) fclose(markersFile);
	if (cuePoints != NULL) free(cuePoints);
	if (sampleLoops != NULL) free(sampleLoops);
	if (outputFile != NULL) fclose(outputFile);

	return returnCode;
}






int writeChunkLocationFromInputFileToOutputFile(ChunkLocation chunk, FILE *inputFile, FILE *outputFile)
{
	// note the position of he input filr to restore later
	long inputFileOrigLocation = ftell(inputFile);

	if (fseek(inputFile, chunk.startOffset, SEEK_SET) < 0)
	{
		fprintf(stderr, "Error: could not seek input file to location %ld", chunk.startOffset);
		return -1;
	}

	long remainingBytesToWrite = chunk.size;
	while (remainingBytesToWrite >=1024)
	{
		char buffer[1024];

		fread(buffer, sizeof(char), 1024, inputFile);
		if (ferror(inputFile) != 0)
		{
			fprintf(stderr, "Copy chunk: Error reading input file");
			fseek(inputFile, inputFileOrigLocation, SEEK_SET);
			return -1;
		}

		if (fwrite(buffer, sizeof(char), 1024, outputFile) < 1)
		{
			fprintf(stderr, "Copy chunk: Error writing output file");
			fseek(inputFile, inputFileOrigLocation, SEEK_SET);
			return -1;
		}
		remainingBytesToWrite -= 1024;
	}

	if (remainingBytesToWrite > 0)
	{
		char buffer[remainingBytesToWrite];

		fread(buffer, sizeof(char), remainingBytesToWrite, inputFile);
		if (ferror(inputFile) != 0)
		{
			fprintf(stderr, "Copy chunk: Error reading input file");
			fseek(inputFile, inputFileOrigLocation, SEEK_SET);
			return -1;
		}

		if (fwrite(buffer, sizeof(char), remainingBytesToWrite, outputFile) < 1)
		{
			fprintf(stderr, "Copy chunk: Error writing output file");
			fseek(inputFile, inputFileOrigLocation, SEEK_SET);
			return -1;
		}
	}

	return 0;
}




enum HostEndiannessType getHostEndianness()
{
    int i = 1;
    char *p = (char *)&i;

    if (p[0] == 1)
        return LittleEndian;
    else
        return BigEndian;
}


uint32_t littleEndianBytesToUInt32(char littleEndianBytes[4])
{
	if (HostEndianness == EndiannessUndefined)
    {
        HostEndianness = getHostEndianness();
    }

	uint32_t uInt32Value;
	char *uintValueBytes = (char *)&uInt32Value;

    if (HostEndianness == LittleEndian)
    {
		uintValueBytes[0] = littleEndianBytes[0];
		uintValueBytes[1] = littleEndianBytes[1];
		uintValueBytes[2] = littleEndianBytes[2];
		uintValueBytes[3] = littleEndianBytes[3];
    }
	else
	{
		uintValueBytes[0] = littleEndianBytes[3];
		uintValueBytes[1] = littleEndianBytes[2];
		uintValueBytes[2] = littleEndianBytes[1];
		uintValueBytes[3] = littleEndianBytes[0];
	}

	return uInt32Value;
}


void uint32ToLittleEndianBytes(uint32_t uInt32Value, char out_LittleEndianBytes[4])
{
	if (HostEndianness == EndiannessUndefined)
    {
        HostEndianness = getHostEndianness();
    }

	char *uintValueBytes = (char *)&uInt32Value;

	if (HostEndianness == LittleEndian)
    {
		out_LittleEndianBytes[0] = uintValueBytes[0];
		out_LittleEndianBytes[1] = uintValueBytes[1];
		out_LittleEndianBytes[2] = uintValueBytes[2];
		out_LittleEndianBytes[3] = uintValueBytes[3];
	}
	else
	{
		out_LittleEndianBytes[0] = uintValueBytes[3];
		out_LittleEndianBytes[1] = uintValueBytes[2];
		out_LittleEndianBytes[2] = uintValueBytes[1];
		out_LittleEndianBytes[3] = uintValueBytes[0];
	}
}


uint16_t littleEndianBytesToUInt16(char littleEndianBytes[2])
{
	if (HostEndianness == EndiannessUndefined)
    {
        HostEndianness = getHostEndianness();
    }

	uint32_t uInt16Value;
	char *uintValueBytes = (char *)&uInt16Value;

    if (HostEndianness == LittleEndian)
    {
		uintValueBytes[0] = littleEndianBytes[0];
		uintValueBytes[1] = littleEndianBytes[1];
    }
	else
	{
		uintValueBytes[0] = littleEndianBytes[1];
		uintValueBytes[1] = littleEndianBytes[0];
	}

	return uInt16Value;
}


void uint16ToLittleEndianBytes(uint16_t uInt16Value, char out_LittleEndianBytes[2])
{
	if (HostEndianness == EndiannessUndefined)
    {
        HostEndianness = getHostEndianness();
    }

	char *uintValueBytes = (char *)&uInt16Value;

	if (HostEndianness == LittleEndian)
    {
		out_LittleEndianBytes[0] = uintValueBytes[0];
		out_LittleEndianBytes[1] = uintValueBytes[1];
	}
	else
	{
		out_LittleEndianBytes[0] = uintValueBytes[1];
		out_LittleEndianBytes[1] = uintValueBytes[0];
	}
}

