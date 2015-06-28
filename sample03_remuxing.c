#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <stdio.h>

typedef struct _FileContext
{
	AVFormatContext* avFormatContext;
	const char* fileName;
	int videoIndex;
	int audioIndex;
} FileContext;

static FileContext inputFile, outputFile;

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

static int createOutputFile(const char* fileName)
{
	unsigned int index;
	int returnCode;

	outputFile.avFormatContext = NULL;
	outputFile.fileName = fileName;

	returnCode = avformat_alloc_output_context2(&outputFile.avFormatContext, NULL, NULL, outputFile.fileName);
	if(returnCode < 0)
	{
		fprintf(stderr, "Could not create output context\n");
		return -1;
	}

	// INPUT 파일에 있는 컨텍스트를 복사하는 과정입니다.
	for(index = 0; index < inputFile.avFormatContext->nb_streams; index++)
	{
		// Input 파일로부터 읽어드린 스트림만 추가합니다.
		if(index == inputFile.videoIndex || index == inputFile.audioIndex)
		{
			AVStream* inAVStream = inputFile.avFormatContext->streams[index];
			AVCodecContext* inAVCodecContext = inAVStream->codec;

			AVStream* outAVStream = avformat_new_stream(outputFile.avFormatContext, inAVCodecContext->codec);
			if(outAVStream == NULL)
			{
				fprintf(stderr, "Failed to allocate output stream\n");
				return -2;
			}
			AVCodecContext* outCodecContext = outAVStream->codec;

			returnCode = avcodec_copy_context(outCodecContext, inAVCodecContext);
			if(returnCode < 0)
			{
				fprintf(stderr, "Error occurred while copying context\n");
				return -3;
			}

			// Deprecated된 AVCodecContext 대신 AVStream을 사용.
			outAVStream->time_base = inAVStream->time_base;
			// FFmpeg에서 지원하는 코덱과 호환성을 맞추기 위해 코덱 태그정보 삭제
			outCodecContext->codec_tag = 0;

			if(outputFile.avFormatContext->oformat->flags & AVFMT_GLOBALHEADER)
			{
				outCodecContext->flags |= CODEC_FLAG_GLOBAL_HEADER;
			}
		} // if
	} // for

	if(!(outputFile.avFormatContext->oformat->flags & AVFMT_NOFILE))
	{
		// 실질적으로 파일을 오픈하는 시점입니다.
		if(avio_open(&outputFile.avFormatContext->pb, outputFile.fileName, AVIO_FLAG_WRITE) < 0)
		{
			fprintf(stderr, "Failed to create output file %s\n", outputFile.fileName);
			return -4;
		}
	}

	// 컨테이너의 헤더파일을 쓰는 함수입니다.
	returnCode = avformat_write_header(outputFile.avFormatContext, NULL);
	if(returnCode < 0)
	{
		fprintf(stderr, "Failed writing header into output file\n");
		return -5;	
	}

	return 0;
}

static void release()
{
	if(inputFile.avFormatContext != NULL)
	{
		avformat_close_input(&inputFile.avFormatContext);
	}

	if(outputFile.avFormatContext != NULL)
	{
		if(!(outputFile.avFormatContext->oformat->flags & AVFMT_NOFILE))
		{
			avio_closep(&outputFile.avFormatContext->pb);
		}
		avformat_free_context(outputFile.avFormatContext);
	}
}

int main(int argc, char* argv[])
{
	int returnCode;

	av_register_all();
	av_log_set_level(AV_LOG_DEBUG);

	if(argc < 3)
	{
		fprintf(stderr, "usage : %s <input> <output>\n", argv[0]);
		exit(EXIT_SUCCESS);
	}

	returnCode = openInputFile(argv[1]);
	if(returnCode < 0)
	{
		release();
		exit(EXIT_SUCCESS);
	}

	returnCode = createOutputFile(argv[2]);
	if(returnCode < 0)
	{
		release();
		exit(EXIT_SUCCESS);
	}

	// OUTPUT 파일에 대한 정보를 출력합니다.
	av_dump_format(outputFile.avFormatContext, 0, outputFile.fileName, 1);

	AVPacket packet;

	while(1)
	{
		returnCode = av_read_frame(inputFile.avFormatContext, &packet);
		if(returnCode == AVERROR_EOF)
		{
			fprintf(stdout, "End of frame\n");
			break;
		}

		if(packet.stream_index != inputFile.videoIndex && 
			packet.stream_index != inputFile.audioIndex)
		{
			av_free_packet(&packet);
			continue;
		}

		AVStream* inAVStream = inputFile.avFormatContext->streams[packet.stream_index];
		AVStream* outAVtStream = outputFile.avFormatContext->streams[packet.stream_index];

		av_packet_rescale_ts(&packet, inAVStream->time_base, outAVtStream->time_base);

		returnCode = av_interleaved_write_frame(outputFile.avFormatContext, &packet);
		if(returnCode < 0)
		{
			fprintf(stderr, "Error occurred when writing packet into file\n");
			break;
		}		
	} // while

	// 파일을 쓰는 시점에서 마무리하지 못한 정보를 정리하는 시점입니다.
	av_write_trailer(outputFile.avFormatContext);

	release();

	return 0;
}