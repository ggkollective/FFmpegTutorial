// PART2/demuxing-02.c
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <stdio.h>

int main(int argc, char* argv[])
{
	AVFormatContext* avFormatContext = NULL;

	av_register_all();

	if(argc < 2)
	{
		printf("파일 이름을 입력하세요.\n");
		exit(EXIT_SUCCESS);
	}

	if(avformat_open_input(&avFormatContext, argv[1], NULL, NULL) < 0)
	{
		printf("알려지지 않았거나 잘못된 파일 형식입니다.\n");
		exit(EXIT_SUCCESS);
	}

	if(avformat_find_stream_info(avFormatContext, NULL) < 0)
	{
		printf("유료한 스트림 정보가 없습니다.\n");
		exit(EXIT_SUCCESS);
	}

	unsigned int index;

	// 스트림 index를 미리 저장하기 위해 사용합니다.
	// 이 정보는 리먹싱, 디코딩과 인코딩시 데이터를 추적하는데 유용하게 사용할 수 있습니다.
	int videoStreamIndex = -1;
	int audioStreamIndex = -1;

	for(index = 0; index < avFormatContext->nb_streams; index++)
	{
		AVCodecContext* pCodecContext = avFormatContext->streams[index]->codec;
		if(pCodecContext->codec_type == AVMEDIA_TYPE_VIDEO)
		{
			// 이 컨테이너에는 적어도 한개 이상의 비디오 코덱정보가 있습니다.
			videoStreamIndex = index;
		}
		else if(pCodecContext->codec_type == AVMEDIA_TYPE_AUDIO)
		{
			// 이 컨테이너에는 적어도 한개 이상의 오디오 코덱정보가 있습니다.
			audioStreamIndex = index;
		}
	}

	if(videoStreamIndex < 0 && audioStreamIndex < 0)
	{
		printf("이 컨테이너에는 비디오/오디오 스트림 정보가 없습니다.\n");
		exit(EXIT_SUCCESS);
	}

	// 디코딩되지 않은 데이터는 AVPacket을 통해 읽어올 수 있습니다.
	AVPacket packet;
	int returnCode;

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
	
	printf("End of file...\n");

	// avformat_open_input으로부터 할당된 자원 해제
	avformat_close_input(&avFormatContext);

	return 0;
}