#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <stdio.h>

int main(int argc, char* argv[])
{
	AVFormatContext* avFormatContext = NULL;
	const char* inFileName;
	int returnCode;

	// 컨테이너, 코덱 정보를 일괄적으로 등록합니다.
	// 이 함수는 여러번 호출되어도 내부적으로는 한번만 등록됩니다.
	av_register_all();

	if(argc < 2)
	{
		printf("파일 이름을 입력하세요.\n");
		exit(EXIT_SUCCESS);
	}

	inFileName = argv[1];

	// 주어진 파일 이름으로부터 AVFormatContext를 가져옵니다.
	returnCode = avformat_open_input(&avFormatContext, inFileName, NULL, NULL);
	if(returnCode < 0)
	{
		printf("알려지지 않았거나 잘못된 파일 형식입니다.\n");
		exit(EXIT_SUCCESS);
	}

	//주어진 AVFormatContext로부터 유효한 스트림이 있는지 찾습니다.
	returnCode = avformat_find_stream_info(avFormatContext, NULL);
	if(returnCode < 0)
	{
		printf("유료한 스트림 정보가 없습니다.\n");
		avformat_close_input(&avFormatContext);
		exit(EXIT_SUCCESS);
	}

	unsigned int index;

	// 스트림 index를 미리 저장하기 위해 사용합니다.
	// 이 정보는 리먹싱, 디코딩과 인코딩시 데이터를 추적하는데 유용하게 사용할 수 있습니다.
	int videoStreamIndex = -1;
	int audioStreamIndex = -1;

	// avFormatContext->nb_streams : 컨테이너가 저장하고 있는 총 스트림 갯수
	for(index = 0; index < avFormatContext->nb_streams; index++)
	{
		// 이 과정에서 스트림 내부에 있는 코덱 정보를 가져올 수 있습니다.
		AVCodecContext* pCodecContext = avFormatContext->streams[index]->codec;

		// pCodecContext->codec_type : 코덱의 타입을 알 수 있습니다.
		// 가장 많이 볼 수 있는 타입은 비디오와 오디오입니다.
		if(pCodecContext->codec_type == AVMEDIA_TYPE_VIDEO)
		{
			// 이 컨테이너에는 적어도 한개 이상의 비디오 코덱정보가 있습니다.
			videoStreamIndex = index;

			printf("------- video information -------\n");
			printf("codec_id : %d\n", pCodecContext->codec_id);
			printf("bitrate : %d\n", pCodecContext->bit_rate);
			printf("width : %d / height : %d\n", pCodecContext->width, pCodecContext->height);
		}
		else if(pCodecContext->codec_type == AVMEDIA_TYPE_AUDIO)
		{
			// 이 컨테이너에는 적어도 한개 이상의 오디오 코덱정보가 있습니다.
			audioStreamIndex = index;

			printf("-------Audio information -------\n");
			printf("codec_id : %d\n", pCodecContext->codec_id);
			printf("bitrate : %d\n", pCodecContext->bit_rate);
			printf("sample_rate : %d\n", pCodecContext->sample_rate);
			printf("number of channels : %d\n", pCodecContext->channels);
		}
	}

	if(videoStreamIndex < 0 && audioStreamIndex < 0)
	{
		printf("이 컨테이너에는 비디오/오디오 스트림 정보가 없습니다.\n");
		avformat_close_input(&avFormatContext);
		exit(EXIT_SUCCESS);
	}

	// avformat_open_input으로부터 할당된 자원 해제
	avformat_close_input(&avFormatContext);

	return 0;
}