/*
	feedseek: test program for libmpg123, showing how to use fuzzy seeking in feeder mode
	copyright 2008 by the mpg123 project - free software under the terms of the LGPL 2.1
	see COPYING and AUTHORS files in distribution or http://mpg123.org	
*/

#include "mpg123.h"
#include <stdio.h>
#include <sys/time.h>

#define INBUFF 1024

FILE *out;
size_t totaloffset, dataoffset;
long rate;
int channels, enc;
unsigned short bitspersample, wavformat;

int64_t TimeGetTickCount()
{
	struct timeval g_tv;
	struct timezone g_tz;
	gettimeofday(&g_tv, &g_tz);
	return (int64_t)g_tv.tv_sec * 1000000LL + g_tv.tv_usec;
}

int main(int argc, char **argv)
{
	unsigned char buf[INBUFF];
	unsigned char *audio;
	FILE *in;
	mpg123_handle *m;
	int ret, state;
	size_t inc, outc;
	off_t len, num;
	size_t bytes;
	off_t inoffset=0;
    int frames=0;
    int64_t total_time=0;
	inc = outc = 0;

	if(argc < 3)
	{
		printf("Please supply in and out filenames\n");
		return -1;
	}

	mpg123_init();

	m = mpg123_new(NULL, &ret);
	if(m == NULL)
	{
		printf("Unable to create mpg123 handle: %s\n", mpg123_plain_strerror(ret));
		return -1;
	}

	mpg123_param(m, MPG123_VERBOSE, 2, 0);

	ret = mpg123_open_feed(m);
	if(ret != MPG123_OK)
	{
		printf("Unable open feed: %s\n", mpg123_plain_strerror(ret));
		return -1;
	}

	in = fopen(argv[1], "rb");
	if(in == NULL)
	{
		printf("Unable to open input file %s\n", argv[1]);
		return -1;
	}
	
	out = fopen(argv[2], "wb");
	if(out == NULL)
	{
		fclose(in);
		printf("Unable to open output file %s\n", argv[2]);
		return -1;
	}
	
	printf( "Starting decode...\n");
    int64_t samples=0;
	while(1)
	{
        int64_t time_start;
		len = fread(buf, sizeof(unsigned char), INBUFF, in);
		if(len <= 0)
			break;
		inc += len;
        time_start = TimeGetTickCount();
		ret = mpg123_feed(m, buf, len);
        total_time +=  (TimeGetTickCount() - time_start);

		while(ret != MPG123_ERR && ret != MPG123_NEED_MORE)
		{
            time_start = TimeGetTickCount();
			ret = mpg123_decode_frame(m, &num, &audio, &bytes);
			if(ret == MPG123_NEW_FORMAT)
			{
				mpg123_getformat(m, &rate, &channels, &enc);
				printf( "New format: %li Hz, %i channels, encoding value %i\n", rate, channels, enc);
			}
            total_time +=  (TimeGetTickCount() - time_start);
            if (ret == MPG123_NEED_MORE) {
                break;
            }
            frames++;
			fwrite(audio, sizeof(unsigned char), bytes, out);
			outc += bytes;
            samples += bytes/sizeof(short)/channels;
		}

		if(ret == MPG123_ERR)
		{
			printf( "Error: %s", mpg123_strerror(m));
			break; 
		}
	}
    int64_t duration = samples*1000000LL/rate;
    printf("Decoded %d frames duration %lld us, take %lld us, duty cycle %f%%\n", frames, duration,total_time, total_time*100.0/duration);

	fclose(out);
	fclose(in);
	mpg123_delete(m);
	mpg123_exit();
	return 0;
}

