#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <stdio.h>

int main(int argc, char* argv[])
{
	AVFormatContext* inAVFormatContext = NULL;
	AVFormatContext* outAVFormatContext = NULL;
	const char* inFileName;
	const char* outFileName;
	int returnCode;

	av_register_all();

	if(argc < 3)
	{
		printf("usage : %s [INPUT] [OUTPUT]\n", argv[0]);
		exit(EXIT_SUCCESS);
	}

	inFileName = argv[1];
	outFileName = argv[2];

	returnCode = avformat_open_input(&inAVFormatContext, inFileName, NULL, NULL);
	if(returnCode < 0)
	{
		printf("알려지지 않았거나 잘못된 파일 형식입니다.\n");
		exit(EXIT_SUCCESS);
	}

	returnCode = avformat_find_stream_info(inAVFormatContext, NULL);
	if(returnCode < 0)
	{
		printf("유료한 스트림 정보가 없습니다.\n");
		avformat_close_input(&inAVFormatContext);
		exit(EXIT_SUCCESS);
	}

	unsigned int index;

	int videoStreamIndex = -1;
	int audioStreamIndex = -1;

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
		avformat_close_input(&inAVFormatContext);
		exit(EXIT_SUCCESS);
	}

	// --- OUTPUT 파일 초기화
	returnCode = avformat_alloc_output_context2(&outAVFormatContext, NULL, NULL, outFileName);
	if(returnCode < 0)
	{
		printf("파일 생성에 실패하였습니다.");
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
			printf("새로운 스트림 생성에 실패하였습니다.");
			avformat_close_input(&inAVFormatContext);
			exit(EXIT_SUCCESS);
		}

		returnCode = avcodec_copy_context(outStream->codec, inStream->codec);
		if(returnCode < 0)
		{
			printf("스트림 정보를 복사하는데 실패하였습니다.");
			avformat_close_input(&inAVFormatContext);
			exit(EXIT_SUCCESS);
		}

		if(outAVFormatContext->oformat->flags & AVFMT_GLOBALHEADER)
		{
			outAVFormatContext->oformat->flags |= AVFMT_GLOBALHEADER;
		}
	}

	if(!(outAVFormatContext->oformat->flags & AVFMT_NOFILE))
	{
		// 경로에 있는 파일에 접근하기 위한 AVIOContext 초기화
		if(avio_open(&outAVFormatContext->pb, argv[2], AVIO_FLAG_WRITE) < 0)
		{
			printf("파일을 쓰기 위한 I/O 생성에 실패하였습니다.");
			avformat_close_input(&inAVFormatContext);
			exit(EXIT_SUCCESS);	
		}
	}

	// 헤더파일 쓰기
	returnCode = avformat_write_header(outAVFormatContext, NULL);
	if(returnCode < 0)
	{
			printf("파일 헤더 생성에 실패하였습니다.");
			avformat_close_input(&inAVFormatContext);

			if(outAVFormatContext != NULL && !(outAVFormatContext->oformat->flags & AVFMT_NOFILE))
			{
				avio_closep(&outAVFormatContext->pb);
			}
			avformat_free_context(outAVFormatContext);
			exit(EXIT_SUCCESS);		
	}

	// OUTPUT 파일에 대한 정보를 출력합니다.
	av_dump_format(outAVFormatContext, 0, argv[2], 1);

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

		AVStream* inStream = inAVFormatContext->streams[packet.stream_index];
		AVStream* outStream = outAVFormatContext->streams[packet.stream_index];

		// 패킷 정보 복사
		av_packet_rescale_ts(&packet, inStream->time_base, outStream->time_base);

		// 패킷 쓰기
		returnCode = av_interleaved_write_frame(outAVFormatContext, &packet);
		if(returnCode < 0)
		{
			printf("패킷 복사에 실패하였습니다.");
			break;
		}

		av_free_packet(&packet);
	}

	avformat_close_input(&inAVFormatContext);

	if(outAVFormatContext != NULL && !(outAVFormatContext->oformat->flags & AVFMT_NOFILE))
	{
		avio_closep(&outAVFormatContext->pb);
	}

	avformat_free_context(outAVFormatContext);

	return 0;
}