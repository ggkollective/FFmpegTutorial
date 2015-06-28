#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <stdio.h>

typedef struct _FileContext
{
	AVFormatContext* avFormatContext;
	const char* fileName;
	int audioIndex;
	int videoIndex;
} FileContext;

static FileContext inputFile;

static int openInputFile(const char* fileName)
{
	unsigned int index;
	int returnCode;

	inputFile.avFormatContext = NULL;
	inputFile.fileName = fileName;
	inputFile.audioIndex = -1;
	inputFile.videoIndex = -1;

	returnCode = avformat_open_input(&inputFile.avFormatContext, inputFile.fileName, NULL, NULL);
	if(returnCode < 0)
	{
		fprintf(stderr, "Could not open input file %s\n", inputFile.fileName);
		return -1;
	}

	returnCode = avformat_find_stream_info(inputFile.avFormatContext, NULL);
	if(returnCode < 0)
	{
		fprintf(stderr, "Failed to retrieve input stream information\n");
		return -2;
	}

	for(index = 0; index < inputFile.avFormatContext->nb_streams; index++)
	{
		AVCodecContext* avCodecContext = inputFile.avFormatContext->streams[index]->codec;
		if(avCodecContext->codec_type == AVMEDIA_TYPE_VIDEO && inputFile.videoIndex < 0)
		{
			inputFile.videoIndex = index;
		}
		else if(avCodecContext->codec_type == AVMEDIA_TYPE_AUDIO && inputFile.audioIndex < 0)
		{
			inputFile.audioIndex = index;
		}
	} // for

	if(inputFile.videoIndex < 0 && inputFile.audioIndex < 0)
	{
		fprintf(stderr, "Failed to retrieve input stream information\n");
		return -3;
	}

	return 0;
}

static void release()
{
	if(inputFile.avFormatContext != NULL)
	{
		avformat_close_input(&inputFile.avFormatContext);
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

	returnCode = openInputFile(argv[1]);
	if(returnCode < 0)
	{
		release();
		exit(EXIT_SUCCESS);
	}

	AVPacket packet;

	while(1)
	{
		returnCode = av_read_frame(inputFile.avFormatContext, &packet);
		if(returnCode == AVERROR_EOF)
		{
			fprintf(stdout, "End of frame\n");
			break;
		}

		if(packet.stream_index == inputFile.videoIndex)
		{
			fprintf(stdout, "Video packet\n");
		}
		else if(packet.stream_index == inputFile.audioIndex)
		{
			fprintf(stdout, "Audio packet\n");
		}

		av_free_packet(&packet);
	} // while

	release();

	return 0;
}