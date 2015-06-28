#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <stdio.h>

typedef struct _FileContext
{
	AVFormatContext* avFormatContext;
	const char* fileName;
} FileContext;

static FileContext inputFile;

static int openInputFile(const char* fileName)
{
	unsigned int index;
	int returnCode;

	inputFile.avFormatContext = NULL;
	inputFile.fileName = fileName;

	// 주어진 파일 이름으로부터 AVFormatContext를 가져옵니다. 
	returnCode = avformat_open_input(&inputFile.avFormatContext, inputFile.fileName, NULL, NULL);
	if(returnCode < 0)
	{
		fprintf(stderr, "Could not open input file %s\n", inputFile.fileName);
		return -1;
	}

	//주어진 AVFormatContext로부터 유효한 스트림이 있는지 찾습니다.
	returnCode = avformat_find_stream_info(inputFile.avFormatContext, NULL);
	if(returnCode < 0)
	{
		fprintf(stderr, "Failed to retrieve input stream information\n");
		return -2;
	}

	// avFormatContext->nb_streams : 컨테이너가 저장하고 있는 총 스트림 갯수
	for(index = 0; index < inputFile.avFormatContext->nb_streams; index++)
	{
		AVCodecContext* avCodecContext = inputFile.avFormatContext->streams[index]->codec;
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
	} // for

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

	// FFmpeg 라이브러리 레벨에서 디버깅 로그를 출력하도록 합니다.
	av_log_set_level(AV_LOG_DEBUG);

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

	release();
	return 0;
}