#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <stdio.h>

int main(int argc, char* argv[])
{
	AVFormatContext* avFormatContext = NULL;
	const char* inFileName;
	int returnCode;

	av_register_all();

	if(argc < 2)
	{
		printf("파일 이름을 입력하세요.\n");
		exit(EXIT_SUCCESS);
	}

	inFileName = argv[1];

	returnCode = avformat_open_input(&avFormatContext, inFileName, NULL, NULL);
	if(returnCode < 0)
	{
		printf("알려지지 않았거나 잘못된 파일 형식입니다.\n");
		exit(EXIT_SUCCESS);
	}

	returnCode = avformat_find_stream_info(avFormatContext, NULL);
	if(returnCode < 0)
	{
		printf("유료한 스트림 정보가 없습니다.\n");
		avformat_close_input(&avFormatContext);
		exit(EXIT_SUCCESS);
	}

	unsigned int index;

	int videoStreamIndex = -1;
	int audioStreamIndex = -1;

	for(index = 0; index < avFormatContext->nb_streams; index++)
	{
		AVCodecContext* pCodecContext = avFormatContext->streams[index]->codec;
		if(pCodecContext->codec_type == AVMEDIA_TYPE_VIDEO)
		{
			videoStreamIndex = index;
		}
		else if(pCodecContext->codec_type == AVMEDIA_TYPE_AUDIO)
		{
			audioStreamIndex = index;
		}
	}

	if(videoStreamIndex < 0 && audioStreamIndex < 0)
	{
		printf("이 컨테이너에는 비디오/오디오 스트림 정보가 없습니다.\n");
		avformat_close_input(&avFormatContext);
		exit(EXIT_SUCCESS);
	}

	// 디코딩되지 않은 데이터는 AVPacket을 통해 읽어올 수 있습니다.
	AVPacket packet;

	// 데이터 읽기 시작
	while(1)
	{
		returnCode = av_read_frame(avFormatContext, &packet);
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

	avformat_close_input(&avFormatContext);

	return 0;
}