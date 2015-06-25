#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <stdio.h>

AVFormatContext* inAVFormatContext = NULL;
const char* inFileName;

int videoStreamIndex = -1;
int audioStreamIndex = -1;

static int openInputFile(const char* fileName)
{
	unsigned int index;
	int returnCode = avformat_open_input(&inAVFormatContext, fileName, NULL, NULL);
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
		if(avCodecContext->codec_type == AVMEDIA_TYPE_VIDEO && videoStreamIndex < 0)
		{
			videoStreamIndex = index;
		}
		else if(avCodecContext->codec_type == AVMEDIA_TYPE_AUDIO && audioStreamIndex < 0)
		{
			audioStreamIndex = index;
		}
	}

	if(videoStreamIndex < 0 && audioStreamIndex < 0)
	{
		return -3;
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
		printf("파일 이름을 입력하세요.\n");
		exit(EXIT_SUCCESS);
	}

	inFileName = argv[1];

	returnCode = openInputFile(inFileName);
	if(returnCode < 0)
	{
		release();
		exit(EXIT_SUCCESS);
	}

	// 디코딩되지 않은 데이터는 AVPacket을 통해 읽어올 수 있습니다.
	AVPacket packet;

	// 데이터 읽기 시작
	while(1)
	{
		returnCode = av_read_frame(inAVFormatContext, &packet);
		if(returnCode == AVERROR_EOF)
		{
			// 더 이상 읽어올 패킷이 없습니다.
			break;
		}

		if(packet.stream_index == videoStreamIndex)
		{
			printf("Video packet\n");
		}
		else if(packet.stream_index == audioStreamIndex)
		{
			printf("Audio packet\n");
		}

		av_free_packet(&packet);
	}

	release();

	return 0;
}