// C port of Future Composer v1.4's replayer

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <math.h> // ceil()
#include "replayer.h"
#include "paula.h"

static volatile bool songReady;
static uint8_t *fileAllocPtr, *fileBuffer, spdtemp, spdtemp2, respcnt;
static uint8_t *SEQpoint, *PATpoint, *FRQpoint, *VOLpoint;
static uint16_t audtemp; // Amiga DMA bits
static uint32_t renderLoopCounter;
static soundInfo_t samples[NUM_SAMPLES + NUM_WAVEFORMS];

// globalized
bool isV14Module;
volatile bool isRecordingToWAV;
uint8_t fcErrCode, repspd;
uint32_t renderLoopTimes, numSequences;
fcChannel_t Channel[PAULA_VOICES];
// ------------

static const uint8_t emptySeqTable[8] = { 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xE1 };

static const uint16_t periods[128] =
{
	// 1.0..1.3 periods
	0x06B0,0x0650,0x05F4,0x05A0,0x054C,0x0500,0x04B8,0x0474,0x0434,0x03F8,0x03C0,0x038A,
	0x0358,0x0328,0x02FA,0x02D0,0x02A6,0x0280,0x025C,0x023A,0x021A,0x01FC,0x01E0,0x01C5,
	0x01AC,0x0194,0x017D,0x0168,0x0153,0x0140,0x012E,0x011D,0x010D,0x00FE,0x00F0,0x00E2,
	0x00D6,0x00CA,0x00BE,0x00B4,0x00AA,0x00A0,0x0097,0x008F,0x0087,0x007F,0x0078,0x0071,
	0x0071,0x0071,0x0071,0x0071,0x0071,0x0071,0x0071,0x0071,0x0071,0x0071,0x0071,0x0071,

	// 1.4 periods (one extra octave)
	0x0D60,0x0CA0,0x0BE8,0x0B40,0x0A98,0x0A00,0x0970,0x08E8,0x0868,0x07F0,0x0780,0x0714,
	0x06B0,0x0650,0x05F4,0x05A0,0x054C,0x0500,0x04B8,0x0474,0x0434,0x03F8,0x03C0,0x038A,
	0x0358,0x0328,0x02FA,0x02D0,0x02A6,0x0280,0x025C,0x023A,0x021A,0x01FC,0x01E0,0x01C5,
	0x01AC,0x0194,0x017D,0x0168,0x0153,0x0140,0x012E,0x011D,0x010D,0x00FE,0x00F0,0x00E2,
	0x00D6,0x00CA,0x00BE,0x00B4,0x00AA,0x00A0,0x0097,0x008F,0x0087,0x007F,0x0078,0x0071,
	0x0071,0x0071,0x0071,0x0071,0x0071,0x0071,0x0071,0x0071
};

static void restart_song(void)
{
	memset(Channel, 0, sizeof (Channel));

	fcChannel_t *ch = Channel;
	for (uint8_t i = 0; i < PAULA_VOICES; i++, ch++)
	{
		ch->voiceIndex = i;
		ch->volTabPtr = ch->freqTabPtr = emptySeqTable;
		ch->volDelayCounter = ch->volDelayLength = 1;
		ch->pitchBendDelay = true;
		ch->seqPos = SEQ_SIZE; // yes
		ch->seqStartPtr = &SEQpoint[3 * i];
		ch->patPtr = &PATpoint[ch->seqStartPtr[0] << 6];
		ch->noteTranspose = (int8_t)ch->seqStartPtr[1];
		ch->soundTranspose = (int8_t)ch->seqStartPtr[2];
	}

	repspd = (SEQpoint[12] > 0) ? SEQpoint[12] : 3;
	respcnt = repspd;
	spdtemp = spdtemp2 = 0;
}

static void new_note(fcChannel_t *ch)
{
	uint8_t *tmpPatPtr = &ch->patPtr[ch->patPos]; // temp pattern pointer

	if ((isV14Module && (tmpPatPtr[0] & 127) == PAT_END_MARKER) || ch->patPos == 64)
	{
		ch->patPos = 0;
		if (ch->seqPos >= numSequences)
		{
			// added this in fc14play (for WAV rendering)
			if (ch == &Channel[PAULA_VOICES-1]) // last channel = handle song looping
			{
				if (renderLoopCounter >= renderLoopTimes)
					isRecordingToWAV = false;
				else
					renderLoopCounter++;
			}

			ch->seqPos = 0;
		}

		uint8_t *tmpSeqPtr = &ch->seqStartPtr[ch->seqPos];
		ch->patPtr = &PATpoint[tmpSeqPtr[0] << 6];

		ch->noteTranspose = (int8_t)tmpSeqPtr[1];
		ch->soundTranspose = (int8_t)tmpSeqPtr[2];

		if (++spdtemp == 4)
		{
			spdtemp = 0;

			// we've read all channels now, let's increase the pos used for RS
			if (++spdtemp2 == numSequences/SEQ_SIZE) // numSequences is a multiple of SEQ_SIZE
				spdtemp2 = 0; // wrap sequence position
		}

		// read current RS (replay speed. only update if non-zero)
		if (SEQpoint[(spdtemp2 * 13) + 12] != 0)
		{
			repspd = SEQpoint[(spdtemp2 * 13) + 12];
			respcnt = repspd;
		}

		ch->seqPos += SEQ_SIZE;
		tmpPatPtr = ch->patPtr; // set new temp pattern pointer
	}

	uint8_t note = tmpPatPtr[0];
	uint8_t info = tmpPatPtr[1];

	if (note == 0)
	{
		info &= 0xC0;
		if (info != 0)
		{
			ch->portaParam = 0;
			if (info & 128)
				ch->portaParam = tmpPatPtr[3];
		}
	}
	else
	{
		ch->portaValue = ch->portaParam = 0;
		if (info & 128)
			ch->portaParam = tmpPatPtr[3];
	}

	note &= 127;
	if (note != 0)
	{
		uint8_t *data = &VOLpoint[((tmpPatPtr[1] & 63) + ch->soundTranspose) << 6];
		ch->volDelayLength = data[0];
		ch->freqTabPtr = &FRQpoint[(data[1] & 63) << 6];
		ch->vibratoSpeed = data[2];
		ch->vibratoDepth = data[3];
		ch->vibratoDelay = data[4];
		ch->volTabPtr = &data[5];

		ch->note = note;
		ch->volTabPos = 0;
		ch->freqTabPos = 0;
		ch->volDelayCounter = ch->volDelayLength;
		ch->vibratoCounter = ch->vibratoDepth;
		ch->freqSusCounter = ch->volSusCounter = 0;
		ch->vibratoUp = true; // default initial state on new note

		paulaSetDMACON(1 << ch->voiceIndex); // stop voice DMA
	}

	ch->patPos += 2;
}

static void doFreqModulation(fcChannel_t *ch)
{
	if (ch->freqSusCounter != 0)
	{
		ch->freqSusCounter--;
		return;
	}

	while (true)
	{
		const uint8_t *fxData = &ch->freqTabPtr[ch->freqTabPos];

		if (fxData[0] == 0xE1) // end of sequence
			break;

		if (fxData[0] == 0xE0) // freq pos jump
		{
			ch->freqTabPos = fxData[1] & 63;
			fxData = &ch->freqTabPtr[ch->freqTabPos];
		}

		if (fxData[0] == 0xE2) // set waveform
		{
			if (fxData[1] < NUM_SAMPLES+NUM_WAVEFORMS) // added protection not present in FC
			{
				soundInfo_t *sample = &samples[fxData[1]];
				ch->loopStart = sample->repeat;
				ch->loopLength = sample->replen;

				audtemp |= 1 << ch->voiceIndex;
				paulaSetDMACON(1 << ch->voiceIndex); // stop voice DMA

				paulaSetData(ch->voiceIndex, sample->data);
				paulaSetLength(ch->voiceIndex, sample->length);

				ch->volTabPos = 0;
				ch->volDelayCounter = 1;
				ch->dataSwapDelayCounter = 3;
			}

			ch->freqTabPos += 2;
		}
		else if (fxData[0] == 0xE4) // update waveform
		{
			if (fxData[1] < NUM_SAMPLES+NUM_WAVEFORMS) // added protection not present in FC
			{
				soundInfo_t *sample = &samples[fxData[1]];
				ch->loopStart = sample->repeat;
				ch->loopLength = sample->replen;

				paulaSetData(ch->voiceIndex, sample->data);
				paulaSetLength(ch->voiceIndex, sample->length);

				ch->dataSwapDelayCounter = 3;
			}

			ch->freqTabPos += 2;
		}
		else if (fxData[0] == 0xE9) // set packed waveform
		{
			if (fxData[1] < NUM_SAMPLES+NUM_WAVEFORMS) // added protection not present in FC
			{
				soundInfo_t *sample = &samples[fxData[1]];
				if (*PTR2LONG(sample->data) == 0x504D5353) // "SSMP"
				{
					uint8_t *tmpPtr = (uint8_t *)&sample->data[4 + (fxData[2] << 4)];

					ch->loopStart = &sample->data[(324 + *PTR2LONG(&tmpPtr[0])) + *PTR2WORD(&tmpPtr[6])];
					ch->loopLength = *PTR2WORD(&tmpPtr[8]);

					// clear first two bytes on non-looping samples to prevent beep (FC doesn't do this)
					if (ch->loopLength <= 1)
					{
						ch->loopLength = 1;
						if (*PTR2WORD(&tmpPtr[4]) >= 1) // sample length
							*PTR2WORD(&sample->data[324 + *PTR2LONG(&tmpPtr[0])]) = 0;
					}

					audtemp |= 1 << ch->voiceIndex;
					paulaSetDMACON(1 << ch->voiceIndex); // stop voice DMA

					paulaSetData(ch->voiceIndex,  &sample->data[324 + *PTR2LONG(&tmpPtr[0])]);
					paulaSetLength(ch->voiceIndex, *PTR2WORD(&tmpPtr[4]));

					ch->volTabPos = 0;
					ch->volDelayCounter = 1;
					ch->dataSwapDelayCounter = 3;
				}
			}

			ch->freqTabPos += 3;
		}
		else if (fxData[0] == 0xE7) // new freq pos
		{
			fxData = &FRQpoint[(fxData[1] & 63) << 6];
			ch->freqTabPtr = fxData;
			ch->freqTabPos = 0;

			continue;
		}
		else if (fxData[0] == 0xEA) // set pitch bend
		{
			ch->pitchBendValue = (int8_t)fxData[1];
			ch->pitchBendCounter = (int8_t)fxData[2];
			ch->freqTabPos += 3;
		}
		else if (fxData[0] == 0xE8) // set new freq sustain
		{
			ch->freqSusCounter = fxData[1];
			ch->freqTabPos += 2;

			if (ch->freqSusCounter != 0)
			{
				ch->freqSusCounter--;
				return; // stop handling frequency modulation
			}

			continue;
		}
		else if (fxData[0] == 0xE3) // set vibrato
		{
			ch->vibratoSpeed = fxData[1];
			ch->vibratoDepth = fxData[2];
			ch->freqTabPos += 3;
		}

		ch->periodTranspose = (int8_t)ch->freqTabPtr[ch->freqTabPos];
		ch->freqTabPos++;
		break;
	}
}

static void doVolumeBend(fcChannel_t *ch)
{
	ch->volSlideDelay = !ch->volSlideDelay;
	if (ch->volSlideDelay)
	{
		ch->volSlideCounter--;

		ch->volume += ch->volSlideSpeed;
		if (ch->volume < 0)
		{
			ch->volume = 0;
			ch->volSlideCounter = 0;
		}
	}
}

static void doVolModulation(fcChannel_t *ch)
{
	if (ch->volSusCounter != 0)
	{
		ch->volSusCounter--;
		return;
	}

	if (ch->volSlideCounter != 0)
	{
		doVolumeBend(ch);
		return;
	}

	ch->volDelayCounter--;
	if (ch->volDelayCounter != 0)
		return;

	ch->volDelayCounter = ch->volDelayLength;

	while (true)
	{
		const uint8_t *fxData = &ch->volTabPtr[ch->volTabPos];

		if (fxData[0] == 0xE1) // end of sequence
			break;

		if (fxData[0] == 0xEA) // set vol slide
		{
			ch->volSlideSpeed = fxData[1];
			ch->volSlideCounter = fxData[2];
			ch->volTabPos += 3;
			doVolumeBend(ch);
		}
		else if (fxData[0] == 0xE8) // set vol sustain
		{
			ch->volSusCounter = fxData[1];
			ch->volTabPos += 2;
		}
		else if (fxData[0] == 0xE0) // set vol pos
		{
			// this is buggy and can result in 0..255, instead of 0..63
			ch->volTabPos = ((fxData[1] & 63) - 5) & 0x00FF;
			continue;
		}
		else // set volume
		{
			ch->volume = (int8_t)fxData[0];
			ch->volTabPos++;
		}

		break;
	}
}

static void effects(fcChannel_t *ch)
{
	int8_t tmpNote;
	int16_t tmpVibPeriod, tmpPeriod;
	uint16_t tmpVibNote;

	doFreqModulation(ch);
	doVolModulation(ch);

	// get period from note and transposes...
	tmpNote = ch->periodTranspose;
	if (tmpNote >= 0)
	{
		tmpNote += ch->note;
		tmpNote += ch->noteTranspose;
	}
	tmpNote &= 127;

	tmpPeriod = periods[tmpNote];

	// apply vibrato to period
	if (ch->vibratoDelay > 0)
	{
		ch->vibratoDelay--;
	}
	else
	{
		tmpVibPeriod = ch->vibratoCounter;
		if (!ch->vibratoUp)
		{
			tmpVibPeriod -= ch->vibratoSpeed;
			if (tmpVibPeriod < 0)
			{
				tmpVibPeriod = 0;
				ch->vibratoUp = true;
			}
		}
		else
		{
			tmpVibPeriod += ch->vibratoSpeed;
			if (tmpVibPeriod > ch->vibratoDepth*2)
			{
				tmpVibPeriod = ch->vibratoDepth*2;
				ch->vibratoUp = false;
			}
		}
		ch->vibratoCounter = (uint8_t)tmpVibPeriod;

		tmpVibPeriod -= ch->vibratoDepth;

		tmpVibNote = tmpNote * 2;
		while (tmpVibNote < 12*8)
		{
			tmpVibPeriod *= 2;
			tmpVibNote += 12*2;
		}

		tmpPeriod += tmpVibPeriod;
	}

	// update portamento value (twice as slow on FC1.4)
	ch->portaDelay = !ch->portaDelay;
	if (!isV14Module || ch->portaDelay)
	{
		if (ch->portaParam > 0)
		{
			if (ch->portaParam > 0x1F)
				ch->portaValue += ch->portaParam & 0x1F;
			else
				ch->portaValue -= ch->portaParam;
		}
	}

	// apply pitch bend to portamento value
	ch->pitchBendDelay = !ch->pitchBendDelay;
	if (ch->pitchBendDelay)
	{
		if (ch->pitchBendCounter > 0)
		{
			ch->pitchBendCounter--;
			ch->portaValue -= ch->pitchBendValue;
		}
	}

	tmpPeriod += ch->portaValue;
	tmpPeriod = CLAMP(tmpPeriod, 113, isV14Module ? 3424 : 1712);

	paulaSetPeriod(ch->voiceIndex, tmpPeriod);
	paulaSetVolume(ch->voiceIndex, ch->volume);
}

void tickReplayer(void)
{
	fcChannel_t *ch;

	if (!songReady)
		return;

	if (--respcnt == 0)
	{
		respcnt = repspd;

		ch = Channel;
		for (int32_t i = 0; i < PAULA_VOICES; i++, ch++)
			new_note(ch);
	}

	audtemp = 0;

	ch = Channel;
	for (int32_t i = 0; i < PAULA_VOICES; i++, ch++)
		effects(ch);

	paulaSetDMACON(0x8000 | audtemp); // start selected voice DMAs

	ch = Channel;
	for (int32_t i = 0; i < PAULA_VOICES; i++, ch++)
	{
		if (ch->dataSwapDelayCounter != 0)
		{
			ch->dataSwapDelayCounter--;
			if (ch->dataSwapDelayCounter == 1)
			{
				ch->dataSwapDelayCounter = 0;

				// these take effect when the current DMA cycle is done
				paulaSetData(i, ch->loopStart);
				paulaSetLength(i, ch->loopLength);
			}
		}
	}
}

/***************************************************************************
 *        PLAYER LOADER ROUTINES                                           *
 ***************************************************************************/

static bool fcInitModule(const uint8_t *moduleData)
{
	soundInfo_t *s;

	isV14Module = true; // this build only carries an FC14 module

	// setup pointers...
	SEQpoint = (uint8_t *)&moduleData[180];
	PATpoint = (uint8_t *)&moduleData[SWAP32(*PTR2LONG(&moduleData[8]))];
	FRQpoint = (uint8_t *)&moduleData[SWAP32(*PTR2LONG(&moduleData[16]))];
	VOLpoint = (uint8_t *)&moduleData[SWAP32(*PTR2LONG(&moduleData[24]))];

	// load samples

	int8_t *sampleDataPtr = (int8_t *)&moduleData[SWAP32(*PTR2LONG(&moduleData[32]))];
	uint8_t *sampleHdrPtr = (uint8_t *)&moduleData[40];

	s = samples;
	for (int32_t i = 0; i < NUM_SAMPLES; i++, s++)
	{
		s->data = sampleDataPtr;
		s->length = SWAP16(*PTR2WORD(sampleHdrPtr)); sampleHdrPtr += 2;
		s->repeat = &s->data[SWAP16(*PTR2WORD(sampleHdrPtr))]; sampleHdrPtr += 2;
		s->replen = SWAP16(*PTR2WORD(sampleHdrPtr)); sampleHdrPtr += 2;

		// fix endless beep on non-looping samples (FC14 doesn't do this)
		if (s->replen <= 1)
		{
			s->replen = 1;
			if (s->length >= 1)
			{
				if (*PTR2LONG(s->data) != 0x504D5353) // "SSMP"
					*PTR2WORD(s->data) = 0;
			}
		}

		sampleDataPtr += (s->length * 2) + 2;
	}

	// load waveforms (FC14 layout only)

	sampleDataPtr = (int8_t *)&moduleData[SWAP32(*PTR2LONG(&moduleData[36]))];
	sampleHdrPtr = (uint8_t *)&moduleData[100];

	s = &samples[NUM_SAMPLES];
	for (int32_t i = 0; i < NUM_WAVEFORMS; i++, s++)
	{
		s->data = s->repeat = sampleDataPtr;
		s->length = *sampleHdrPtr++;
		s->replen = s->length;

		sampleDataPtr += s->length * 2;
	}

	// get number of sequences and make it a multiple of 13 (SEQ_SIZE)
	numSequences = (uint32_t)(SWAP32(*PTR2LONG(&moduleData[4])) / SEQ_SIZE) * SEQ_SIZE;

	return true;
}

static bool allocDataForModule(uint32_t filesize)
{
	fileAllocPtr = (uint8_t *)malloc(MODULE_PROTECTION_PADDING + filesize + MODULE_PROTECTION_PADDING);
	if (fileAllocPtr == NULL)
	{
		fcErrCode = ERR_OUT_OF_MEMORY;
		return false;
	}

	/* Fill left/right padding area of module with 0xE1 (end of sequence marker)
	** This is needed for safety, as the replayer tends to go out of bounds.
	*/
	memset(fileAllocPtr, 0xE1, MODULE_PROTECTION_PADDING);
	memset(fileAllocPtr + MODULE_PROTECTION_PADDING + filesize, 0xE1, MODULE_PROTECTION_PADDING);

	fileBuffer = fileAllocPtr + MODULE_PROTECTION_PADDING;
	return true;
}

bool fcLoadFromRAM(const uint8_t *data, uint32_t filesize)
{
	songReady = false;
	fcErrCode = ERR_SUCCESS;

	if (!allocDataForModule(filesize))
	{
		fcErrCode = ERR_OUT_OF_MEMORY;
		return false;
	}

	memcpy(fileBuffer, data, filesize);

	if (!fcInitModule(fileBuffer))
	{
		free(fileAllocPtr);
		return false;
	}

	return true;
}

#ifndef MINI_PLAYER
bool fcLoad(const char *filename)
{
	songReady = false;
	fcErrCode = ERR_SUCCESS;

	FILE *f = fopen(filename, "rb");
	if (f == NULL)
	{
		fcErrCode = ERR_FILE_IO;
		return false;
	}

	fseek(f, 0, SEEK_END);
	const uint32_t filesize = (uint32_t)ftell(f);
	rewind(f);

	if (!allocDataForModule(filesize))
	{
		fclose(f);
		fcErrCode = ERR_OUT_OF_MEMORY;
		return false;
	}

	if (fread(fileBuffer, 1, filesize, f) != filesize)
	{
		free(fileAllocPtr);
		fclose(f);
		fcErrCode = ERR_FILE_IO;
		return false;
	}

	fclose(f);

	if (!fcInitModule(fileBuffer))
	{
		free(fileAllocPtr);
		return false;
	}

	return true;
}

#endif

/***************************************************************************
 *        PLAYER INTERFACING ROUTINES                                      *
 ***************************************************************************/



// masterVol = 0..256 (default = 256), stereoSeparation = 0..100 (percentage, default = 20)
bool fcInit(int32_t audioFreq, int32_t audioBufferSize, int32_t masterVol, int32_t stereoSeparation)
{
	if (!paulaInit(audioFreq))
	{
		paulaClose();
		fcErrCode = ERR_OUT_OF_MEMORY;
		return false;
	}

	paulaSetStereoSeparation(stereoSeparation);
	paulaSetMasterVolume(masterVol);

	if (!openMixer(audioFreq, audioBufferSize))
	{
		closeMixer();
		paulaClose();
		fcErrCode = ERR_AUDIO_DEVICE;
		return false;
	}

	renderLoopCounter = 0;

	return true;
}

void fcFree(void)
{
	songReady = false;

	if (fileAllocPtr != NULL)
	{
		free(fileAllocPtr);
		fileAllocPtr = NULL;
	}
}

void fcClose(void)
{
	closeMixer();
	paulaClose();
	fcFree();
}

bool fcPlay(void)
{
	lockMixer();

	restart_song();

	paulaSetDMACON(0x000F); // disable audio DMA
	audtemp = 0;

	songReady = true;
	unlockMixer();
	
	return true;
}

/***************************************************************************
 *        WAV DUMPING ROUTINES                                             *
 ***************************************************************************/

#ifndef MINI_PLAYER
static void writeWAVHeader(FILE *f, int32_t audioFrequency)
{
	uint16_t w;
	uint32_t l;

	// 12 bytes

	const uint32_t RIFF = 0x46464952; // "RIFF"
	fwrite(&RIFF, 4, 1, f);
	fseek(f, 4, SEEK_CUR);
	const uint32_t WAVE = 0x45564157; // "WAVE"
	fwrite(&WAVE, 4, 1, f);

	// 24 bytes

	const uint32_t fmt = 0x20746D66; // " fmt"
	fwrite(&fmt, 4, 1, f);
	l = 16; fwrite(&l, 4, 1, f);
	w = 1; fwrite(&w, 2, 1, f);
	w = 2; fwrite(&w, 2, 1, f);
	l = audioFrequency; fwrite(&l, 4, 1, f);
	l = audioFrequency*2*2; fwrite(&l, 4, 1, f);
	w = 2*2; fwrite(&w, 2, 1, f);
	w = 8*2; fwrite(&w, 2, 1, f);

	// 8 bytes

	const uint32_t DATA = 0x61746164; // "DATA"
	fwrite(&DATA, 4, 1, f);
	fseek(f, 4, SEEK_CUR);
}

static void finishWAVHeader(FILE *f, uint32_t numDataBytes)
{
	fseek(f, 4, SEEK_SET);
	uint32_t l = numDataBytes+4+24+8;
	fwrite(&l, 4, 1, f);
	fseek(f, 12+24+4, SEEK_SET);
	fwrite(&numDataBytes, 4, 1, f);
}

#endif

int32_t fcGetFrame(int16_t *streamOut) // returns bytes mixed
{
	if (audio.tickSampleCounter <= 0) // new replayer tick
	{
		tickReplayer();

		audio.tickSampleCounter = audio.samplesPerTickInt;

		audio.tickSampleCounterFrac += audio.samplesPerTickFrac;
		if (audio.tickSampleCounterFrac >= BPM_FRAC_SCALE)
		{
			audio.tickSampleCounterFrac &= BPM_FRAC_MASK;
			audio.tickSampleCounter++;
		}
	}

	const int32_t samplesToMix = audio.tickSampleCounter;

	paulaMixSamples(streamOut, samplesToMix);
	streamOut += samplesToMix * 2;

	audio.tickSampleCounter -= samplesToMix;

	return samplesToMix * 2 * sizeof (short);
}

// masterVol = 0..256 (default = 256), stereoSeparation = 0..100 (percentage, default = 20)
#ifndef MINI_PLAYER
bool fcRecordWAVFromRAM(const uint8_t *data, const char *fileOut, uint32_t filesize,
	int32_t songLoopTimes, int32_t audioFreq, int32_t masterVol, int32_t stereoSeparation)
{
	fcErrCode = ERR_SUCCESS;

	if (!paulaInit(audioFreq))
	{
		fcErrCode = ERR_OUT_OF_MEMORY;
		return false;
	}

	paulaSetStereoSeparation(stereoSeparation);
	paulaSetMasterVolume(masterVol);

	if (!fcLoadFromRAM(data, filesize)) // modifies error code
	{
		fcFree();
		paulaClose();
		return false;
	}

	const int32_t maxSamplesPerTick = (int32_t)ceil(audio.outputFreq / AMIGA_PAL_VBLANK_HZ);

	int16_t *outputBuffer = (int16_t *)malloc(maxSamplesPerTick * (2 * sizeof (int16_t)));
	if (outputBuffer == NULL)
	{
		fcFree();
		paulaClose();
		fcErrCode = ERR_OUT_OF_MEMORY;
		return false;
	}

	FILE *f = fopen(fileOut, "wb");
	if (f == NULL)
	{
		fcFree();
		paulaClose();
		free(outputBuffer);
		fcErrCode = ERR_FILE_IO;
		return false;
	}

	writeWAVHeader(f, audioFreq);

	isRecordingToWAV = true;
	if (!fcPlay()) // modifies error code (also resets audio.tickSampleCounter64)
	{
		isRecordingToWAV = false;
		fclose(f);
		fcFree();
		paulaClose();
		free(outputBuffer);
		return false;
	}

	renderLoopTimes = songLoopTimes;

	uint32_t totalBytes = 0;
	while (isRecordingToWAV)
	{
		const int32_t bytesMixed = fcGetFrame(outputBuffer);
		fwrite(outputBuffer, 1, bytesMixed, f);
		totalBytes += bytesMixed;
	}

	finishWAVHeader(f, totalBytes);
	isRecordingToWAV = false;

	fclose(f);
	fcFree();
	paulaClose();
	free(outputBuffer);

	return true;
}

// masterVol = 0..256 (default = 256), stereoSeparation = 0..100 (percentage, default = 20)
bool fcRecordWAV(const char *fileIn, const char *fileOut,
	int32_t songLoopTimes, int32_t audioFreq, int32_t masterVol, int32_t stereoSeparation)
{
	fcErrCode = ERR_SUCCESS;

	if (!paulaInit(audioFreq))
	{
		fcErrCode = ERR_OUT_OF_MEMORY;
		return false;
	}

	paulaSetStereoSeparation(stereoSeparation);
	paulaSetMasterVolume(masterVol);

	if (!fcLoad(fileIn)) // modifies error code
	{
		fcFree();
		paulaClose();
		return false;
	}

	const int32_t maxSamplesPerTick = (int32_t)ceil(audio.outputFreq / AMIGA_PAL_VBLANK_HZ);

	int16_t *outputBuffer = (int16_t *)malloc(maxSamplesPerTick * (2 * sizeof (int16_t)));
	if (outputBuffer == NULL)
	{
		fcFree();
		paulaClose();
		fcErrCode = ERR_OUT_OF_MEMORY;
		return false;
	}

	FILE *f = fopen(fileOut, "wb");
	if (f == NULL)
	{
		fcFree();
		paulaClose();
		free(outputBuffer);
		fcErrCode = ERR_FILE_IO;
		return false;
	}

	writeWAVHeader(f, audioFreq);

	isRecordingToWAV = true;
	if (!fcPlay()) // modifies error code (also resets audio.tickSampleCounter64)
	{
		isRecordingToWAV = false;
		fclose(f);
		fcFree();
		paulaClose();
		free(outputBuffer);
		return false;
	}

	renderLoopCounter = 0;
	renderLoopTimes = songLoopTimes;

	uint32_t totalBytes = 0;
	while (isRecordingToWAV)
	{
		const uint32_t size = fcGetFrame(outputBuffer);
		fwrite(outputBuffer, 1, size, f);
		totalBytes += size;
	}

	finishWAVHeader(f, totalBytes);
	isRecordingToWAV = false;

	fclose(f);
	fcFree();
	paulaClose();
	free(outputBuffer);

	return true;
}

#endif

int32_t fcGetErrorCode(void)
{
	return fcErrCode;
}