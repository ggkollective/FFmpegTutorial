#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <stdio.h>

AVFormatContext* inAVFormatContext = NULL;
AVFormatContext* outAVFormatContext = NULL;
const char* inFileName;
const char* outFileName;

int videoStreamIndex = -1;
int audioStreamIndex = -1;

static int AVOpenInput(const char* fileName)
{
	unsigned int index;
	int returnCode;

	returnCode = avformat_open_input(&inAVFormatContext, fileName, NULL, NULL);
	if(returnCode < 0)
	{
		printf("알려지지 않았거나 잘못된 파일 형식입니다.\n");
		return -1;
	}

	returnCode = avformat_find_stream_info(inAVFormatContext, NULL);
	if(returnCode < 0)
	{
		printf("유료한 스트림 정보가 없습니다.\n");
		return -2;
	}

	for(index = 0; index < inAVFormatContext->nb_streams; index++)
	{
		AVCodecContext* pCodecContext = inAVFormatContext->streams[index]->codec;
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
		return -3;
	}

	return 0;
}

static int AVOpenOutput(const char* fileName)
{
	unsigned int index;
	int returnCode;

	// --- OUTPUT 파일 초기화
	returnCode = avformat_alloc_output_context2(&outAVFormatContext, NULL, NULL, fileName);
	if(returnCode < 0)
	{
		printf("파일 생성에 실패하였습니다.\n");
		avformat_close_input(&inAVFormatContext);
		exit(EXIT_SUCCESS);
	}

	// INPUT 파일에 있는 컨텍스트를 복사하는 과정
	for(index = 0; index < inAVFormatContext->nb_streams; index++)
	{
		AVStream* inStream = inAVFormatContext->streams[index];
		// 새로운 스트림을 inStream->codec->codec에 있는 코덱 정보를 기반으로 생성하여 outAVFormatContext에 링크합니다.
		// inStream->codec : 코덱의 정보, inStream->codec->codec : 코덱 자체의 정보
		AVStream* outStream = avformat_new_stream(outAVFormatContext, inStream->codec->codec);
		if(outStream == NULL)
		{
			printf("새로운 스트림 생성에 실패하였습니다.\n");
			return -1;
		}

		// 이전까지는 outStream->codec의 time_base를 참조하였지만
		// 현재는 Deprecated 된 관계로 AVStream에도 설정을 합니다.
		outStream->time_base = inStream->time_base;

		returnCode = avcodec_copy_context(outStream->codec, inStream->codec);
		if(returnCode < 0)
		{
			printf("스트림 정보를 복사하는데 실패하였습니다.\n");
			return -2;
		}

		if(outAVFormatContext->oformat->flags & AVFMT_GLOBALHEADER)
		{
			outAVFormatContext->oformat->flags |= AVFMT_GLOBALHEADER;
		}
	}

	if(!(outAVFormatContext->oformat->flags & AVFMT_NOFILE))
	{
		// 경로에 있는 파일에 접근하기 위한 AVIOContext 초기화
		if(avio_open(&outAVFormatContext->pb, fileName, AVIO_FLAG_WRITE) < 0)
		{
			printf("파일을 쓰기 위한 I/O 생성에 실패하였습니다.\n");
			return -3;
		}
	}

	// 헤더파일 쓰기
	returnCode = avformat_write_header(outAVFormatContext, NULL);
	if(returnCode < 0)
	{
		printf("파일 헤더 생성에 실패하였습니다.\n");
		return -4;	
	}

	return 0;
}

static void AVRelease()
{
	if(inAVFormatContext != NULL)
	{
		avformat_close_input(&inAVFormatContext);
	}

	if(outAVFormatContext != NULL)
	{
		if(!(outAVFormatContext->oformat->flags & AVFMT_NOFILE))
		{
			avio_closep(&outAVFormatContext->pb);
		}
		avformat_free_context(outAVFormatContext);
	}
}

int main(int argc, char* argv[])
{
	int returnCode;

	av_register_all();

	if(argc < 3)
	{
		printf("usage : %s [INPUT] [OUTPUT]\n", argv[0]);
		exit(EXIT_SUCCESS);
	}

	inFileName = argv[1];
	outFileName = argv[2];

	returnCode = AVOpenInput(inFileName);
	if(returnCode < 0)
	{
		AVRelease();
		exit(EXIT_SUCCESS);
	}

	returnCode = AVOpenOutput(outFileName);
	if(returnCode < 0)
	{
		AVRelease();
		exit(EXIT_SUCCESS);
	}

	// OUTPUT 파일에 대한 정보를 출력합니다.
	av_dump_format(outAVFormatContext, 0, outFileName, 1);

	// 디코딩되지 않은 데이터는 AVPacket을 통해 읽어올 수 있습니다.
	AVPacket packet;

	while(1)
	{
		returnCode = av_read_frame(inAVFormatContext, &packet);
		if(returnCode == AVERROR_EOF)
		{
			break;
		}

		AVStream* inStream = inAVFormatContext->streams[packet.stream_index];
		AVStream* outStream = outAVFormatContext->streams[packet.stream_index];

		// 패킷 정보 복사
		av_packet_rescale_ts(&packet, inStream->time_base, outStream->time_base);

		// 패킷 쓰기
		returnCode = av_interleaved_write_frame(outAVFormatContext, &packet);
		if(returnCode < 0)
		{
			printf("패킷 복사에 실패하였습니다.\n");
			break;
		}

		av_free_packet(&packet);
	}

	AVRelease();

	return 0;
}