/* Keil keygen tune - 32-bit minimal PE player (no CRT, no audio files)
 * The original FC14 module is embedded; a Paula-model mixer streams
 * 44.1 kHz stereo straight to waveOut.
 */

#include <string.h>
#include "replayer.h"
#include "mod.h"

typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;
typedef void          *HWO;
typedef char          *LPSTR;

typedef struct {
	u16 wFormatTag, nChannels;
	u32 nSamplesPerSec, nAvgBytesPerSec;
	u16 nBlockAlign, wBitsPerSample, cbSize;
} WFX;

typedef struct {
	LPSTR lpData;
	u32   dwBufferLength, dwBytesRecorded, dwUser, dwFlags, dwLoops;
	void *lpNext;
	u32   reserved;
} HDR;

int __stdcall waveOutOpen(HWO *h, u32 id, const WFX *f, u32 cb, u32 inst, u32 flags);
int __stdcall waveOutPrepareHeader(HWO h, HDR *p, u32 n);
int __stdcall waveOutUnprepareHeader(HWO h, HDR *p, u32 n);
int __stdcall waveOutWrite(HWO h, HDR *p, u32 n);
int __stdcall waveOutReset(HWO h);
int __stdcall waveOutClose(HWO h);
u32 __stdcall GetTickCount(void);
void __stdcall Sleep(u32 ms);
int _kbhit(void);
int _getch(void);

#define CHUNK 22050
#define QN    4
#define WHDR_DONE 1

static HWO    g_hwo;
static HDR    g_hdr[QN];
static short  g_buf[QN][CHUNK * 2];
static short  g_tmp[2048];
static WFX    g_wf;

static void render(short *d, int n)
{
	int got = 0;
	while (got < n)
	{
		int s = fcGetFrame(g_tmp) / 4;
		if (s > n - got) s = n - got;
		memcpy(d + got * 2, g_tmp, s * 4);
		got += s;
	}
}

void _start(void)
{
	u32 t0;
	int i, fed = 0;

	if (!paulaInit(44100)) return;
	paulaSetStereoSeparation(20);
	paulaSetMasterVolume(256);
	if (!fcLoadFromRAM((const unsigned char *)g_mod, MOD_SIZE)) return;
	if (!fcPlay()) return;

	g_wf.wFormatTag = 1;
	g_wf.nChannels = 2;
	g_wf.nSamplesPerSec = 44100;
	g_wf.nAvgBytesPerSec = 44100 * 4;
	g_wf.nBlockAlign = 4;
	g_wf.wBitsPerSample = 16;
	g_wf.cbSize = 0;

	if (waveOutOpen(&g_hwo, (u32)-1, &g_wf, 0, 0, 0)) return;

	for (i = 0; i < QN; i++) waveOutPrepareHeader(g_hwo, &g_hdr[i], sizeof(HDR));

	t0 = GetTickCount();
	for (;;)
	{
		u32 want = (GetTickCount() - t0) / 500 + 2;

		while ((u32)fed < want)
		{
			HDR *h = &g_hdr[fed & (QN - 1)];
			if (fed >= QN && !(h->dwFlags & WHDR_DONE)) break;

			render(g_buf[fed & (QN - 1)], CHUNK);
			h->lpData = (LPSTR)g_buf[fed & (QN - 1)];
			h->dwBufferLength = CHUNK * 4;
			h->dwFlags &= ~WHDR_DONE;
			if (waveOutWrite(g_hwo, h, sizeof(HDR))) goto done;
			fed++;
		}

		if (_kbhit())
		{
			int k = _getch();
			if (k == 'q' || k == 'Q') break;
		}
		Sleep(20);
	}

done:
	waveOutReset(g_hwo);
	for (i = 0; i < QN; i++) waveOutUnprepareHeader(g_hwo, &g_hdr[i], sizeof(HDR));
	waveOutClose(g_hwo);
}
