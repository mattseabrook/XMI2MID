#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <memory.h>

unsigned char midiheader[] = {'M','T','h','d', 0, 0, 0, 6, 0, 0, 0, 1, 0, 60, 'M', 'T', 'r', 'k'};

#define DEFAULT_TEMPO 120UL // MIDI tempo default
#define XMI_FREQ 120UL // XMI Frequency
#define DEFAULT_TIMEBASE (XMI_FREQ*60UL/DEFAULT_TEMPO) // Must be 60
#define DEFAULT_QN (60UL * 1000000UL / DEFAULT_TEMPO) // Must be 500000

unsigned short timebase = 960;
unsigned qnlen = DEFAULT_QN; // quarter note length

struct NOEVENTS {
	unsigned delta;
	unsigned char off[3];
} off_events[1000] = { { 0xFFFFFFFFL, { 0, 0, 0 } } };

int comp_events(struct NOEVENTS *a, struct NOEVENTS *b)
{
	if (a->delta < b->delta) {
		return -1;
	}
	else if (a->delta > b->delta) {
		return 1;
	}
	else {
		return 0;
	}

}


int main(int argc, char **argv)
{
	FILE *pFi, *pFo;

	if (argc != 2) {
		fprintf(stderr, "Usage:%s infile\n", argv[0]);
		exit(-1);
	}

	if (fopen_s(&pFi, argv[1], "rb")){
		fprintf(stderr, "File %s cannot open\n", argv[1]);
		exit(errno);
	}

	fseek(pFi, 0, SEEK_END);
	unsigned char *midi_data;
	long fsize = ftell(pFi);
	fseek(pFi, 0, SEEK_SET);

	printf("DEBUG: fsize %ld bytes\n", fsize);
	if (NULL == (midi_data = malloc(fsize))) {
		fprintf(stderr, "Memory allocation error\n");
		fclose(pFi);
		exit(errno);
	}

	if (fsize != fread_s(midi_data, fsize, sizeof(signed char), fsize, pFi)) {
		fprintf(stderr, "File read error\n");
		fclose(pFi);
		exit(errno);
	}
	fclose(pFi);
	printf("DEBUG: file read complete goto on-memory\n");

	unsigned char *cur = midi_data;
	if (memcmp(cur, "FORM", 4)) {
		fprintf(stderr, "Not XMIDI file (FORM)\n");
	}
	cur += 4;

	unsigned lFORM = _byteswap_ulong(*((unsigned *)cur));
	cur += 4;

	if (memcmp(cur, "XDIR", 4)) {
		fprintf(stderr, "Not XMIDI file (XDIR)\n");
	}
	cur += 4;

	if (memcmp(cur, "INFO", 4)) {
		fprintf(stderr, "Not XMIDI file (INFO)\n");
	}
	cur += 4;

	unsigned lINFO = _byteswap_ulong(*((unsigned *)cur));
	cur += 4;

	unsigned short seqCount = *((unsigned short *)cur);
	cur += 2;

	printf("seqCount: %d\n", seqCount);

	if (memcmp(cur, "CAT ", 4)) {
		fprintf(stderr, "Not XMIDI file (CAT )\n");
	}
	cur += 4;

	unsigned lCAT = _byteswap_ulong(*((unsigned *)cur));
	cur += 4;

	if (memcmp(cur, "XMID", 4)) {
		fprintf(stderr, "Not XMIDI file (XMID)\n");
	}
	cur += 4;

	if (memcmp(cur, "FORM", 4)) {
		fprintf(stderr, "Not XMIDI file (FORM)\n");
	}
	cur += 4;

	unsigned lFORM2 = _byteswap_ulong(*((unsigned *)cur));
	cur += 4;

	if (memcmp(cur, "XMID", 4)) {
		fprintf(stderr, "Not XMIDI file (XMID)\n");
	}
	cur += 4;

	if (memcmp(cur, "TIMB", 4)) {
		fprintf(stderr, "Not XMIDI file (TIMB)\n");
	}
	cur += 4;

	unsigned lTIMB = _byteswap_ulong(*((unsigned *)cur));
	cur += 4;

	for (unsigned i = 0; i < lTIMB; i += 2) {
		printf("patch@bank: %3d@%3d\n", *cur, *(cur+1));
		cur += 2;
	}

	if (!memcmp(cur, "RBRN", 4)) {
		cur += 4;
		printf("(RBRN)\n");
		unsigned lRBRN = _byteswap_ulong(*((unsigned *)cur));
		cur += 4;

		unsigned short nBranch = *((unsigned short *)cur);
		cur += 2;

		for (unsigned i = 0; i < nBranch; i++) {
			unsigned short id = *(unsigned short *)cur;
			cur += 2;
			unsigned dest = *(unsigned *)cur;
			cur += 4;
			printf("id/dest: %04X@%08X\n", id, dest);
		}
	}

	if (memcmp(cur, "EVNT", 4)) {
		fprintf(stderr, "Not XMIDI file (EVNT)\n");
	}
	cur += 4;

	unsigned lEVNT = _byteswap_ulong(*((unsigned *)cur));
	cur += 4;
	printf("whole event length: %d\n", lEVNT);



	unsigned char *midi_decode;

	if (NULL == (midi_decode = malloc(fsize*2))) {
		fprintf(stderr, "Memory (decode buffer) allocation error\n");
		exit(errno);
	}
	unsigned char *dcur = midi_decode;

	int next_is_delta = 1;
	unsigned char *st = cur;
	unsigned oevents = 0;
	while (cur - st < lEVNT) {
//		printf("%6d:", cur - st);

		if (*cur < 0x80) {
			unsigned delay = 0;
			while (*cur == 0x7F) {
				delay += *cur++;
			}
			delay += *cur++;
			//			printf("delay:%d\n", delay);

			while (delay > off_events[0].delta) {
//				printf("insert note off\n");
//				for (unsigned i = 0; i < oevents; i++) {
//					printf("event %d d=%d:%02X:%02X:%02X\n", i, off_events[i].delta, off_events[i].off[0], off_events[i].off[1], off_events[i].off[2]);
//				}
				unsigned no_delta = off_events[0].delta;
				unsigned tdelay =  no_delta & 0x7F;

				while ((no_delta >>= 7)) {
					tdelay <<= 8;
					tdelay |= (no_delta & 0x7F) | 0x80;
				}

				while (1) {
					*dcur++ = tdelay & 0xFF;
					if (tdelay & 0x80) {
						tdelay >>= 8;
					}
					else {
						break;
					}
				}
				*dcur++ = off_events[0].off[0] & 0x8F;
				*dcur++ = off_events[0].off[1];
				*dcur++ = 0x7F;

				delay -= off_events[0].delta;
				for (unsigned i=1;i < oevents;i++) {
					off_events[i].delta -= off_events[0].delta;
				}
				off_events[0].delta = 0xFFFFFFFFL;

				qsort(off_events, oevents, sizeof(struct NOEVENTS), (int(*)(const void*, const void*))comp_events);

				oevents--;
			}
//			printf("delay:%d\n", delay);
			for (unsigned i = 0; i < oevents; i++) {
				off_events[i].delta -= delay;
//				printf("event %d d=%d:%02X:%02X:%02X\n", i, off_events[i].delta, off_events[i].off[0], off_events[i].off[1], off_events[i].off[2]);
			}

			unsigned tdelay = delay & 0x7F;

			while ((delay >>= 7)) {
				tdelay <<= 8;
				tdelay |= (delay & 0x7F) | 0x80;
			}

			while (1) {
				*dcur++ = tdelay & 0xFF;
				if (tdelay & 0x80) {
					tdelay >>= 8;
				}
				else {
					break;
				}
			}
			next_is_delta = 0;
		}
		else {
			if (next_is_delta) {
				if (*cur >= 0x80) {
					*dcur++ = 0;
				}
			}

			next_is_delta = 1;
			if (*cur == 0xFF) {
//				printf("META\n");
				if (*(cur + 1) == 0x2F) {
					printf("flush %3d note offs\n", oevents);
					for (unsigned i = 0; i < oevents; i++) {
						*dcur++ = off_events[i].off[0] & 0x8F;
						*dcur++ = off_events[i].off[1];
						*dcur++ = 0x7F;
						*dcur++ = 0;
					}
					*dcur++ = *cur++;
					*dcur++ = *cur++;
					*dcur++ = 0;
					printf("Track Ends\n");
					break;
				}
				*dcur++ = *cur++;
				*dcur++ = *cur++;
				unsigned textlen = *cur + 1;
				while (textlen--) {
					*dcur++ = *cur++;
				}
			}
			else if (0x80 == (*cur & 0xF0)) {
//				printf("Note off\n");
				*dcur++ = *cur++;
				*dcur++ = *cur++;
				*dcur++ = *cur++;
			}
			else if (0x90 == (*cur & 0xF0)) {
				*dcur++ = *cur++;
				*dcur++ = *cur++;
				*dcur++ = *cur++;
				unsigned delta = *cur & 0x7F;

				while (*cur++ > 0x80) {
					delta <<= 7;
					delta += *cur;
				}
//				printf("Note on, delta:%d\n", delta);

				off_events[oevents].delta = delta;
				off_events[oevents].off[0] = *(dcur - 3);
				off_events[oevents].off[1] = *(dcur - 2);

				oevents++;

				qsort(off_events, oevents, sizeof(struct NOEVENTS), (int(*)(const void*, const void*))comp_events);
			}
			else if (0xA0 == (*cur & 0xF0)) {
//				printf("Key pressure\n");
				*dcur++ = *cur++;
				*dcur++ = *cur++;
				*dcur++ = *cur++;
			}
			else if (0xB0 == (*cur & 0xF0)) {
//				printf("control change\n");
				*dcur++ = *cur++;
				*dcur++ = *cur++;
				*dcur++ = *cur++;
			}
			else if (0xC0 == (*cur & 0xF0)) {
//				printf("program change\n");
				*dcur++ = *cur++;
				*dcur++ = *cur++;
			}
			else if (0xD0 == (*cur & 0xF0)) {
//				printf("channel pressure\n");
				*dcur++ = *cur++;
				*dcur++ = *cur++;
			}
			else if (0xE0 == (*cur & 0xF0)) {
//				printf("pitch bend\n");
				*dcur++ = *cur++;
				*dcur++ = *cur++;
				*dcur++ = *cur++;
			}
			else {
				printf("wrong event\n");
				cur++;
			}
		}
	}

	unsigned dlen = dcur - midi_decode;
	printf("%7d\n", dlen);

	unsigned char *midi_write;

	if (NULL == (midi_write = malloc(fsize * 2))) {
		fprintf(stderr, "Memory (write buffer) allocation error\n");
		exit(errno);
	}
	unsigned char *tcur = midi_write;

	unsigned char *pos = midi_decode;

	while (pos < dcur) {
// first delta-time
		unsigned delta = 0;
		while (*pos & 0x80) {
			delta += *pos++ & 0x7F;
			delta <<= 7;
		}
		delta += *pos++ & 0x7F;


		// change delta here!!
		double factor = (double)timebase * DEFAULT_QN / ((double)qnlen * DEFAULT_TIMEBASE);
		delta = (double) delta*factor + 0.5;
//		printf("%lf\n", factor);

		unsigned tdelta = delta & 0x7F;
		while ((delta >>= 7)) {
			tdelta <<= 8;
			tdelta |= (delta & 0x7F) | 0x80;
		}
		while (1) {
		*tcur++ = tdelta & 0xFF;
			if (tdelta & 0x80) {
				tdelta >>= 8;
			}
			else {
				break;
			}
		}
// last -  event
		if (0x80 == (*pos & 0xF0)) {
//			printf("Note off\n");
			*tcur++=*pos++;
			*tcur++=*pos++;
			*tcur++=*pos++;
		}
		else if (0x90 == (*pos & 0xF0)) {
//			printf("Note on\n");
			*tcur++=*pos++;
			*tcur++=*pos++;
			*tcur++=*pos++;
		}
		else if (0xA0 == (*pos & 0xF0)) {
//			printf("Key pressure\n");
			*tcur++=*pos++;
			*tcur++=*pos++;
			*tcur++=*pos++;
		}
		else if (0xB0 == (*pos & 0xF0)) {
//			printf("control change\n");
			*tcur++=*pos++;
			*tcur++=*pos++;
			*tcur++=*pos++;
		}
		else if (0xC0 == (*pos & 0xF0)) {
//			printf("program change\n");
			*tcur++=*pos++;
			*tcur++=*pos++;
		}
		else if (0xD0 == (*pos & 0xF0)) {
//			printf("channel pressure\n");
			*tcur++=*pos++;
			*tcur++=*pos++;
		}
		else if (0xE0 == (*pos & 0xF0)) {
//			printf("pitch bend\n");
			*tcur++=*pos++;
			*tcur++=*pos++;
			*tcur++=*pos++;
		}
		else if (0xF0 == *pos) {
			unsigned exlen = 0;
			*tcur++ = *pos++;
			while (*pos < 0) {
				exlen += *pos & 0x7F;
				exlen <<= 7;
				*tcur++ = *pos++;
			}
			exlen += *pos & 0x7F;
//			printf("F0 Exlen %ld\n", exlen);
			*tcur++ = *pos++;
			while (exlen--) {
				*tcur++ = *pos++;
			}
		}
		else if (0xF7 == *pos) {
			unsigned exlen = 0;
			*tcur++ = *pos++;
			while (*pos < 0) {
				exlen += *pos & 0x7F;
				exlen <<= 7;
				*tcur++ = *pos++;
			}
			exlen += *pos & 0x7F;
//			printf("F7 Exlen %ld\n", exlen);
			*tcur++ = *pos++;
			while (exlen--) {
				*tcur++ = *pos++;
			}
		}
		else if (0xFF == *pos) {
//			printf("META\n");
			*tcur++ = *pos++;
			if (0x51 == *pos) {
				*tcur++ = *pos++;
				*tcur++ = *pos++;
				qnlen = (*(unsigned char *)(pos) << 16) + (*(unsigned char *)(pos + 1) << 8) + *(unsigned char *)(pos + 2);
				*tcur++ = *pos++;
				*tcur++ = *pos++;
				*tcur++ = *pos++;
			} else {
				*tcur++ = *pos++;
				unsigned textlen = *pos;
				*tcur++ = *pos++;
				while (textlen--) {
					*tcur++ = *pos++;
				}
			}
		}
		else {
			printf("Bad event %02x at %04x\n", *pos, pos-midi_decode);
		}
	}
	unsigned tlen = tcur - midi_write;
	printf("%7d\n", tlen);
	unsigned char pt[_MAX_PATH], fn[_MAX_FNAME];

	_splitpath_s(argv[1], NULL, 0, NULL, 0, fn, _MAX_FNAME, NULL, 0);
	_makepath_s(pt, _MAX_PATH, NULL, NULL, fn, ".mid");

	// output
	if (fopen_s(&pFo, pt, "wb")){
		fprintf(stderr, "File %s cannot open\n", pt);
		exit(errno);
	}

	unsigned short *mh_timebase = &midiheader[12];
	*mh_timebase = _byteswap_ushort(timebase);

	if (18 != fwrite(midiheader, sizeof(unsigned char), 18, pFo)) {
		fprintf(stderr, "File write error\n");
		fclose(pFo);
		exit(errno);
	}

	unsigned bs_tlen = _byteswap_ulong(tlen);
	if (1 != fwrite(&bs_tlen, sizeof(unsigned), 1, pFo)) {
		fprintf(stderr, "File write error\n");
		fclose(pFo);
		exit(errno);
	}

	if (tlen != fwrite(midi_write, sizeof(unsigned char), tlen, pFo)) {
		fprintf(stderr, "File write error\n");
		fclose(pFo);
		exit(errno);
	}
	fclose(pFo);
}