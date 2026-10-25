// Offline "driver" - no audio device at all.
// Only used for fcRecordWAV(), which pulls samples through fcGetFrame().
#include "nulldriver.h"

void lockMixer(void) {}
void unlockMixer(void) {}

bool openMixer(int32_t mixingFrequency, int32_t mixingBufferSize)
{
	(void)mixingFrequency;
	(void)mixingBufferSize;
	return true;
}

void closeMixer(void) {}
