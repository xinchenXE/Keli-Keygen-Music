#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "paula.h"

enum
{
	ERR_SUCCESS          = 0,
	ERR_OUT_OF_MEMORY    = 1,
	ERR_AUDIO_DEVICE     = 2,
	ERR_FILE_IO          = 3,
	ERR_NOT_AN_FC_MODULE = 4,
	ERR_SONG_NOT_LOADED  = 5
};

#define SEQ_SIZE 13
#define PAT_END_MARKER 0x49
#define NUM_SAMPLES 10
#define NUM_WAVEFORMS 80
#define NUM_WAVEFORMS_SMOD 47

// added padding needed because FC's replayer is buggy and tends to go out of bounds
#define MODULE_PROTECTION_PADDING 16384

typedef struct soundInfo_t // do not touch!
{
	int8_t *data;
	uint16_t length;
	int8_t *repeat;
	uint16_t replen;
} soundInfo_t;

typedef struct fcChannel_t
{
	bool vibratoUp, portaDelay, pitchBendDelay, volSlideDelay;
	int8_t pitchBendValue, pitchBendCounter, note, noteTranspose;
	int8_t soundTranspose, *loopStart, volume, periodTranspose;
	const uint8_t *freqTabPtr, *volTabPtr;
	uint8_t voiceIndex, *seqStartPtr, *patPtr;
	uint8_t freqSusCounter, volSusCounter;
	uint8_t vibratoSpeed, vibratoDepth, vibratoCounter;
	uint8_t vibratoDelay, volSlideSpeed;
	uint8_t volSlideCounter, portaParam, volDelayCounter, dataSwapDelayCounter;
	uint8_t volDelayLength;
	int16_t portaValue;
	uint16_t loopLength, freqTabPos, volTabPos, patPos;
	uint32_t seqPos;
} fcChannel_t;

#define PTR2LONG(x) ((uint32_t *)(x))
#define PTR2WORD(x) ((uint16_t *)(x))
#define SWAP16(x) ((uint16_t)(((x) << 8) | ((x) >> 8)))
#define SWAP32(value) \
( \
	(((uint32_t)((value) & 0x000000FF)) << 24) | \
	(((uint32_t)((value) & 0x0000FF00)) <<  8) | \
	(((uint32_t)((value) & 0x00FF0000)) >>  8) | \
	(((uint32_t)((value) & 0xFF000000)) >> 24)   \
)

#define CLAMP(x, low, high) (((x) > (high)) ? (high) : (((x) < (low)) ? (low) : (x)))

// replayer.c
extern bool isV14Module;
extern volatile bool isRecordingToWAV;
extern uint8_t fcErrCode, repspd;
extern uint32_t renderLoopTimes, numSequences;
extern fcChannel_t Channel[PAULA_VOICES];
// ------------

bool fcLoadFromRAM(const uint8_t *data, uint32_t filesize);
bool fcLoad(const char *filename);
void fcNextPattern(void);
void fcPrevPattern(void);

// masterVol = 0..256 (default = 256), stereoSeparation = 0..100 (percentage, default = 20)
bool fcInit(int32_t audioFreq, int32_t audioBufferSize, int32_t masterVol, int32_t stereoSeparation);

void fcFree(void);
void fcClose(void);

bool fcPlay(void);

// masterVol = 0..256 (default = 256), stereoSeparation = 0..100 (percentage, default = 20)
bool fcRecordWAVFromRAM(const uint8_t *data, const char *fileOut, uint32_t filesize,
	int32_t songLoopTimes, int32_t audioFreq, int32_t masterVol, int32_t stereoSeparation);

// masterVol = 0..256 (default = 256), stereoSeparation = 0..100 (percentage, default = 20)
bool fcRecordWAV(const char *fileIn, const char *fileOut,
	int32_t songLoopTimes, int32_t audioFreq, int32_t masterVol, int32_t stereoSeparation);

int32_t fcGetErrorCode(void);

void tickReplayer(void);
// mixes up to one replayer tick, returns bytes written
int32_t fcGetFrame(int16_t *streamOut);
