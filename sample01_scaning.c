#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <stdio.h>

AVFormatContext* inAVFormatContext = NULL;
const char* inFileName;

static int openInputFile()
{
	unsigned int index;
	int returnCode = avformat_open_input(&inAVFormatContext, inFileName, NULL, NULL);
	if(returnCode < 0)
	{
		fprintf(stderr, "Could not open input file %s\n", inFileName);
		return -1;
	}

	returnCode = avformat_find_stream_info(inAVFormatContext, NULL);
	if(returnCode < 0)
	{
		fprintf(stderr, "Failed to retrieve input stream information\n");
		return -2;
	}

	for(index = 0; index < inAVFormatContext->nb_streams; index++)
	{
		AVCodecContext* avCodecContext = inAVFormatContext->streams[index]->codec;
		if(avCodecContext->codec_type == AVMEDIA_TYPE_VIDEO)
		{
			fprintf(stdout, "------- Video info -------\n");
			fprintf(stdout, "codec_id : %d\n", avCodecContext->codec_id);
			fprintf(stdout, "bitrate : %d\n", avCodecContext->bit_rate);
			fprintf(stdout, "width : %d / height : %d\n", avCodecContext->width, avCodecContext->height);
		}
		else if(avCodecContext->codec_type == AVMEDIA_TYPE_AUDIO)
		{
			fprintf(stdout, "------- Audio info -------\n");
			fprintf(stdout, "codec_id : %d\n", avCodecContext->codec_id);
			fprintf(stdout, "bitrate : %d\n", avCodecContext->bit_rate);
			fprintf(stdout, "sample_rate : %d\n", avCodecContext->sample_rate);
			fprintf(stdout, "number of channels : %d\n", avCodecContext->channels);
		}
	}

	return 0;
}

static void release()
{
	if(inAVFormatContext != NULL)
	{
		avformat_close_input(&inAVFormatContext);
	}
}

int main(int argc, char* argv[])
{
	int returnCode;

	av_register_all();

	if(argc < 2)
	{
		fprintf(stderr, "usage : %s <input>\n", argv[0]);
		exit(EXIT_SUCCESS);
	}

	inFileName = argv[1];
	returnCode = openInputFile();
	if(returnCode < 0)
	{
		release();
		exit(EXIT_SUCCESS);
	}

	release();
	return 0;
}