#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <stdio.h>

AVFormatContext* inAVFormatContext = NULL;
AVFormatContext* outAVFormatContext = NULL;
const char* inFileName;
const char* outFileName;

int videoStreamIndex = -1;
int audioStreamIndex = -1;

static int openInputFile()
{
	unsigned int index;
	int returnCode = avformat_open_input(&inAVFormatContext, inFileName, NULL, NULL);
	if(returnCode < 0)
	{
		fprintf(stderr, "Could not open input file %s\n", inFileName);
		return -1;
	}

	returnCode = avformat_find_stream_info(inAVFormatContext, NULL);
	if(returnCode < 0)
	{
		fprintf(stderr, "Failed to retrieve input stream information\n");
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
		fprintf(stderr, "Failed to retrieve input stream information\n");
		return -3;
	}

	return 0;
}

static int createOutputFile()
{
	unsigned int index;
	int returnCode = avformat_alloc_output_context2(&outAVFormatContext, NULL, NULL, outFileName);
	if(returnCode < 0)
	{
		fprintf(stderr, "Could not create output context\n");
		return -1;
	}

	for(index = 0; index < inAVFormatContext->nb_streams; index++)
	{
		AVStream* inStream = inAVFormatContext->streams[index];
		AVCodecContext* inCodecContext = inStream->codec;

		AVStream* outStream = avformat_new_stream(outAVFormatContext, inCodecContext->codec);
		if(outStream == NULL)
		{
			fprintf(stderr, "Failed to allocate output stream\n");
			return -2;
		}
		AVCodecContext* outCodecContext = outStream->codec;

		returnCode = avcodec_copy_context(outCodecContext, inCodecContext);
		if(returnCode < 0)
		{
			fprintf(stderr, "Error occurred while copying context\n");
			return -3;
		}

		// Deprecated된 AVCodecContext 대신 AVStream을 사용.
		outStream->time_base = inStream->time_base;
		// Codec간 호환성을 맞추기 위해 코덱 태그정보 삭제
		outCodecContext->codec_tag = 0;

		if(outAVFormatContext->oformat->flags & AVFMT_GLOBALHEADER)
		{
			outCodecContext->flags |= CODEC_FLAG_GLOBAL_HEADER;
		}
	}

	if(!(outAVFormatContext->oformat->flags & AVFMT_NOFILE))
	{
		// 실질적으로 파일을 오픈하는 시점입니다.
		if(avio_open(&outAVFormatContext->pb, outFileName, AVIO_FLAG_WRITE) < 0)
		{
			fprintf(stderr, "Failed to create output file %s\n", outFileName);
			return -4;
		}
	}

	returnCode = avformat_write_header(outAVFormatContext, NULL);
	if(returnCode < 0)
	{
		fprintf(stderr, "Failed writing header into output file\n");
		return -5;	
	}

	return 0;
}

static void release()
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
		fprintf(stderr, "usage : %s <input> <output>\n", argv[0]);
		exit(EXIT_SUCCESS);
	}

	inFileName = argv[1];
	returnCode = openInputFile();
	if(returnCode < 0)
	{
		release();
		exit(EXIT_SUCCESS);
	}

	outFileName = argv[2];
	returnCode = createOutputFile();
	if(returnCode < 0)
	{
		release();
		exit(EXIT_SUCCESS);
	}

	// OUTPUT 파일에 대한 정보를 출력합니다.
	av_dump_format(outAVFormatContext, 0, outFileName, 1);

	AVPacket packet;

	while(1)
	{
		returnCode = av_read_frame(inAVFormatContext, &packet);
		if(returnCode == AVERROR_EOF)
		{
			fprintf(stdout, "End of frame\n");
			break;
		}

		AVStream* inStream = inAVFormatContext->streams[packet.stream_index];
		AVStream* outStream = outAVFormatContext->streams[packet.stream_index];

		av_packet_rescale_ts(&packet, inStream->time_base, outStream->time_base);

		returnCode = av_interleaved_write_frame(outAVFormatContext, &packet);
		if(returnCode < 0)
		{
			fprintf(stderr, "Error occurred when writing packet into file\n");
			break;
		}

		av_free_packet(&packet);
	}

	// 파일을 쓰는 시점에서 마무리하지 못한 정보를 정리하는 시점입니다.
	av_write_trailer(outAVFormatContext);

	release();

	return 0;
}