/* Renders one T9 hit to a 48 kHz 16-bit WAV.
 *   render KIND DECAY COLOR SHAPE SWEEP CONTOUR NOTE MS out.wav     (KIND: bd sd hh rc tom cy)
 * The hats and cymbals play the sample bank named by $T9_BANK_FILE (tools/mksamples.py --bank).
 * Also prints the cost: peak and mean CPU cycles are not measurable here, only output.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/t9.h"

static void put32(FILE *f, unsigned v) { fputc(v, f); fputc(v >> 8, f); fputc(v >> 16, f); fputc(v >> 24, f); }
static void put16(FILE *f, unsigned v) { fputc(v, f); fputc(v >> 8, f); }

extern const unsigned char *t9_bank;

static void load_bank(void)
{
	static unsigned char bank[T9_BANK_SIZE];
	const char *name = getenv("T9_BANK_FILE");
	FILE *f = name ? fopen(name, "rb") : NULL;

	if (f) {
		fread(bank, 1, sizeof bank, f);
		fclose(f);
		t9_bank = bank;
	}
}

int main(int argc, char **argv)
{
	static const char *names[T9_KINDS] = { "bd", "sd", "hh", "rc", "tom", "cy" };
	struct t9_voice v;
	struct t9_knobs k;
	int kind = -1, i, j, blocks, peak = 0, raw = 0;
	short *pcm;
	FILE *f;

	if (argc != 10 && argc != 11) {
		fprintf(stderr, "usage: render KIND DECAY COLOR SHAPE SWEEP CONTOUR NOTE MS out.wav [ACCENT]\n");
		return 2;
	}
	for (i = 0; i < T9_KINDS; i++)
		if (!strcmp(argv[1], names[i]))
			kind = i;
	if (kind < 0)
		return 2;
	load_bank();
	k.decay = atoi(argv[2]); k.color = atoi(argv[3]); k.shape = atoi(argv[4]);
	k.sweep = atoi(argv[5]); k.contour = atoi(argv[6]); k.note = atoi(argv[7]) << 16;
	k.accent = argc == 11 && atoi(argv[10]);
	blocks = atoi(argv[8]) * 48 / T9_BLOCK;
	pcm = calloc(blocks * T9_BLOCK, sizeof *pcm);
	t9_init(&v, kind, 0x12345678);
	for (i = 0; i < blocks; i++) {
		int32_t out[T9_BLOCK];
		t9_update(&v, &k, i == 0);
		t9_render(&v, out);
		for (j = 0; j < T9_BLOCK; j++) {
			int32_t s = out[j];
			if (abs(s) > raw) raw = abs(s);
			if (s > 32767) s = 32767;
			if (s < -32768) s = -32768;
			if (abs(s) > peak) peak = abs(s);
			pcm[i * T9_BLOCK + j] = (short)s;
		}
	}
	f = fopen(argv[9], "wb");
	if (!f)
		return 1;
	fwrite("RIFF", 1, 4, f); put32(f, 36 + blocks * T9_BLOCK * 2); fwrite("WAVEfmt ", 1, 8, f);
	put32(f, 16); put16(f, 1); put16(f, 1); put32(f, 48000); put32(f, 96000); put16(f, 2); put16(f, 16);
	fwrite("data", 1, 4, f); put32(f, blocks * T9_BLOCK * 2);
	for (i = 0; i < blocks * T9_BLOCK; i++)
		put16(f, (unsigned short)pcm[i]);
	fclose(f);
	printf("%s: raw peak %.1f dBFS%s\n", argv[9], raw ? 20 * __builtin_log10(raw / 32768.0) : -999.0,
	       peak >= 32767 ? "  CLIPPED" : "");
	return 0;
}
