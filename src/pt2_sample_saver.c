#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <ctype.h> // tolower()
#include <sys/stat.h>
#include "pt2_header.h"
#include "pt2_structs.h"
#include "pt2_textout.h"
#include "pt2_mouse.h"
#include "pt2_visuals.h"
#include "pt2_helpers.h"
#include "pt2_diskop.h"

#define PLAYBACK_FREQ 16574 /* C-3, period 214 */

static void removeSampleFileExt(char *text) // for sample saver
{
	uint32_t fileExtPos;
	uint32_t filenameLength;

	if (text == NULL || text[0] == '\0')
		return;
	
	filenameLength = (uint32_t)strlen(text);
	if (filenameLength < 5)
		return;

	// remove .wav/.iff/from end of sample name (if present)
	fileExtPos = filenameLength - 4;
	if (fileExtPos > 0 && (!strncmp(&text[fileExtPos], ".wav", 4) || !strncmp(&text[fileExtPos], ".iff", 4)))
		text[fileExtPos] = '\0';
}

static void iffWriteChunkHeader(FILE *f, char *chunkName, uint32_t chunkLen)
{
	fwrite(chunkName, sizeof (int32_t), 1, f);
	chunkLen = SWAP32(chunkLen);
	fwrite(&chunkLen, sizeof (int32_t), 1, f);
}


static void iffWriteUint32(FILE *f, uint32_t value)
{
	value = SWAP32(value);
	fwrite(&value, sizeof (int32_t), 1, f);
}

static void iffWriteUint16(FILE *f, uint16_t value)
{
	value = SWAP16(value);
	fwrite(&value, sizeof (int16_t), 1, f);
}

static void iffWriteUint8(FILE *f, const uint8_t value)
{
	fwrite(&value, sizeof (int8_t), 1, f);
}

static void iffWriteChunkData(FILE *f, const void *data, size_t length)
{
	fwrite(data, sizeof (int8_t), length, f);
	if (length & 1) fputc(0, f); // write pad byte if chunk size is uneven
}

bool saveSample(bool checkIfFileExist, bool giveNewFreeFilename)
{
	char fileName[128], tmpBuffer[64];
	uint32_t i, j, chunkLen;
	FILE *f;
	struct stat statBuffer;
	wavHeader_t wavHeader;
	samplerChunk_t samplerChunk;
	mptExtraChunk_t mptExtraChunk;

	const moduleSample_t *s = &song->samples[editor.currSample];

	if (s->length == 0)
	{
		statusSampleIsEmpty();
		return false;
	}

	// get sample filename
	if (s->text[0] == '\0')
	{
		strcpy(fileName, "untitled");
	}
	else
	{
		for (i = 0; i < 22; i++)
		{
			tmpBuffer[i] = (char)tolower(song->samples[editor.currSample].text[i]);
			if (tmpBuffer[i] == '\0') break;
			sanitizeFilenameChar(&tmpBuffer[i]);
		}

		strcpy(fileName, tmpBuffer);
	}
	
	removeSampleFileExt(fileName);
	addSampleFileExt(fileName);

	// if the user picked "no" to overwriting the file, generate a new filename
	if (giveNewFreeFilename && stat(fileName, &statBuffer) == 0)
	{
		for (j = 1; j <= 999; j++)
		{
			if (s->text[0] == '\0')
			{
				sprintf(fileName, "untitled-%d", j);
			}
			else
			{
				for (i = 0; i < 22; i++)
				{
					tmpBuffer[i] = (char)tolower(song->samples[editor.currSample].text[i]);
					if (tmpBuffer[i] == '\0') break;
					sanitizeFilenameChar(&tmpBuffer[i]);
				}

				removeSampleFileExt(tmpBuffer);
				sprintf(fileName, "%s-%d", tmpBuffer, j);
			}

			addSampleFileExt(fileName);

			if (stat(fileName, &statBuffer) != 0)
				break; // this filename can be used
		}
	}

	// check if we need to overwrite file...

	if (checkIfFileExist && stat(fileName, &statBuffer) == 0)
	{
		ui.askScreenShown = true;
		ui.askScreenType = ASK_SAVESMP_OVERWRITE;
		pointerSetMode(POINTER_MODE_MSG1, NO_CARRY);
		setStatusMessage("OVERWRITE FILE ?", NO_CARRY);
		renderAskDialog();
		return -1;
	}

	if (ui.askScreenShown)
	{
		ui.answerNo = false;
		ui.answerYes = false;
		ui.askScreenShown = false;
	}

	f = fopen(fileName, "wb");
	if (f == NULL)
	{
		displayErrorMsg("FILE I/O ERROR !");
		return false;
	}

	const int8_t *sampleData = &song->sampleData[s->offset];
	const uint32_t sampleLength = s->length;
	const uint32_t loopStart = s->loopStart & 0xFFFE;
	const uint32_t loopLength = s->loopLength & 0xFFFE;

	switch (diskop.smpSaveType)
	{
		default:
		case DISKOP_SMP_WAV:
		{
			wavHeader.format = 0x45564157; // "WAVE"
			wavHeader.chunkID = 0x46464952; // "RIFF"
			wavHeader.subchunk1ID = 0x20746D66; // "fmt "
			wavHeader.subchunk2ID = 0x61746164; // "data"
			wavHeader.subchunk1Size = 16;
			wavHeader.subchunk2Size = sampleLength;
			wavHeader.chunkSize = 36 + wavHeader.subchunk2Size;
			wavHeader.audioFormat = 1;
			wavHeader.numChannels = 1;
			wavHeader.bitsPerSample = 8;
			wavHeader.sampleRate = PLAYBACK_FREQ;
			wavHeader.byteRate = wavHeader.sampleRate * wavHeader.numChannels * wavHeader.bitsPerSample / 8;
			wavHeader.blockAlign = wavHeader.numChannels * wavHeader.bitsPerSample / 8;

			// set "sampler" chunk if loop is enabled
			if (loopStart+loopLength > 2) // loop enabled?
			{
				wavHeader.chunkSize += sizeof (samplerChunk_t);
				memset(&samplerChunk, 0, sizeof (samplerChunk_t));
				samplerChunk.chunkID = 0x6C706D73; // "smpl"
				samplerChunk.chunkSize = 60;
				samplerChunk.dwSamplePeriod = 1000000000 / PLAYBACK_FREQ;
				samplerChunk.dwMIDIUnityNote = 60; // 60 = MIDI middle-C
				samplerChunk.cSampleLoops = 1;
				samplerChunk.loop.dwStart = loopStart;
				samplerChunk.loop.dwEnd = (loopStart + loopLength) - 1;
			}

			// set ModPlug Tracker chunk (used for sample volume only in this case)
			wavHeader.chunkSize += sizeof (mptExtraChunk);
			memset(&mptExtraChunk, 0, sizeof (mptExtraChunk));
			mptExtraChunk.chunkID = 0x61727478; // "xtra"
			mptExtraChunk.chunkSize = sizeof (mptExtraChunk) - 4 - 4;
			mptExtraChunk.defaultPan = 128; // 0..255
			mptExtraChunk.defaultVolume = s->volume * 4; // 0..256
			mptExtraChunk.globalVolume = 64; // 0..64

			fwrite(&wavHeader, sizeof (wavHeader_t), 1, f);

			for (i = 0; i < sampleLength; i++)
				fputc((uint8_t)(sampleData[i] + 128), f);

			if (sampleLength & 1)
				fputc(0, f); // pad align byte

			if (loopStart+loopLength > 2) // loop enabled?
				fwrite(&samplerChunk, sizeof (samplerChunk), 1, f);

			fwrite(&mptExtraChunk, sizeof (mptExtraChunk), 1, f);
		}
		break;

		case DISKOP_SMP_IFF:
		{
			// "FORM" chunk
			iffWriteChunkHeader(f, "FORM", 0); // "FORM" chunk size is overwritten later
			iffWriteUint32(f, 0x38535658); // "8SVX"

			// "VHDR" chunk
			iffWriteChunkHeader(f, "VHDR", 20);

			if (loopStart+loopLength > 2) // loop enabled?
			{
				iffWriteUint32(f, loopStart); // oneShotHiSamples
				iffWriteUint32(f, loopLength); // repeatHiSamples
			}
			else
			{
				iffWriteUint32(f, 0); // oneShotHiSamples
				iffWriteUint32(f, 0); // repeatHiSamples
			}

			iffWriteUint32(f, 0); // samplesPerHiCycle
			iffWriteUint16(f, PLAYBACK_FREQ); // samplesPerSec
			iffWriteUint8(f, 1); // ctOctave (number of samples)
			iffWriteUint8(f, 0); // sCompression
			iffWriteUint32(f, s->volume * 1024); // volume (max: 65536/0x10000)

			// "NAME" chunk
			chunkLen = (uint32_t)strlen(s->text);
			if (chunkLen > 0)
			{
				iffWriteChunkHeader(f, "NAME", chunkLen);
				iffWriteChunkData(f, s->text, chunkLen);
			}

			// "ANNO" chunk (we put the program name here)
			const char annoStr[] = "ProTracker 2 clone";
			chunkLen = sizeof (annoStr) - 1;
			iffWriteChunkHeader(f, "ANNO", chunkLen);
			iffWriteChunkData(f, annoStr, chunkLen);

			// "BODY" chunk
			chunkLen = sampleLength;
			iffWriteChunkHeader(f, "BODY", chunkLen);
			iffWriteChunkData(f, sampleData, chunkLen);

			// go back and fill in "FORM" chunk size
			chunkLen = ftell(f) - 8;
			fseek(f, 4, SEEK_SET);
			iffWriteUint32(f, chunkLen);
		}
		break;

		case DISKOP_SMP_RAW:
			fwrite(sampleData, 1, sampleLength, f);
		break;
	}

	fclose(f);

	displayMsg("SAMPLE SAVED !");
	setMsgPointer();

	diskop.cached = false;
	if (ui.diskOpScreenShown)
		ui.updateDiskOpFileList = true;

	return true;
}
