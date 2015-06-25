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
		return -1;
	}

	returnCode = avformat_find_stream_info(inAVFormatContext, NULL);
	if(returnCode < 0)
	{
		return -2;
	}

	for(index = 0; index < inAVFormatContext->nb_streams; index++)
	{
		AVCodecContext* avCodecContext = inAVFormatContext->streams[index]->codec;
		if(avCodecContext->codec_type == AVMEDIA_TYPE_VIDEO)
		{
			printf("------- Video info -------\n");
			printf("codec_id : %d\n", avCodecContext->codec_id);
			printf("bitrate : %d\n", avCodecContext->bit_rate);
			printf("width : %d / height : %d\n", avCodecContext->width, avCodecContext->height);
		}
		else if(avCodecContext->codec_type == AVMEDIA_TYPE_AUDIO)
		{
			printf("------- Audio info -------\n");
			printf("codec_id : %d\n", avCodecContext->codec_id);
			printf("bitrate : %d\n", avCodecContext->bit_rate);
			printf("sample_rate : %d\n", avCodecContext->sample_rate);
			printf("number of channels : %d\n", avCodecContext->channels);
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