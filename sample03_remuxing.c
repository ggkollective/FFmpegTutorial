#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <stdio.h>

typedef struct _FileContext
{
	AVFormatContext* fmt_ctx;
	int v_index;
	int a_index;
} FileContext;

static FileContext inputFile, outputFile;

static int open_input(const char* fileName)
{
	unsigned int index;
	int ret;

	inputFile.fmt_ctx = NULL;
	inputFile.a_index = inputFile.v_index = -1;

	ret = avformat_open_input(&inputFile.fmt_ctx, fileName, NULL, NULL);
	if(ret < 0)
	{
		printf("Could not open input file %s\n", fileName);
		return -1;
	}

	ret = avformat_find_stream_info(inputFile.fmt_ctx, NULL);
	if(ret < 0)
	{
		printf("Failed to retrieve input stream information\n");
		return -2;
	}

	for(index = 0; index < inputFile.fmt_ctx->nb_streams; index++)
	{
		AVCodecContext* codec_ctx = inputFile.fmt_ctx->streams[index]->codec;
		if(codec_ctx->codec_type == AVMEDIA_TYPE_VIDEO && inputFile.v_index < 0)
		{
			inputFile.v_index = index;
		}
		else if(codec_ctx->codec_type == AVMEDIA_TYPE_AUDIO && inputFile.a_index < 0)
		{
			inputFile.a_index = index;
		}
	} // for

	if(inputFile.v_index < 0 && inputFile.a_index < 0)
	{
		printf("Failed to retrieve input stream information\n");
		return -3;
	}

	return 0;
}

static int create_output(const char* fileName)
{
	unsigned int index;
	int out_index;
	int ret;

	outputFile.fmt_ctx = NULL;
	outputFile.a_index = outputFile.v_index = -1;

	ret = avformat_alloc_output_context2(&outputFile.fmt_ctx, NULL, NULL, fileName);
	if(ret < 0)
	{
		printf("Could not create output context\n");
		return -1;
	}

	// 스트림 인덱스는 0부터 시작합니다.
	out_index = 0;
	// INPUT 파일에 있는 컨텍스트를 복사하는 과정입니다.
	for(index = 0; index < inputFile.fmt_ctx->nb_streams; index++)
	{
		// Input 파일로부터 읽어드린 스트림만 추가합니다.
		if(index == inputFile.v_index || index == inputFile.a_index)
		{
			AVStream* in_stream = inputFile.fmt_ctx->streams[index];
			AVCodecContext* in_codec_ctx = in_stream->codec;

			AVStream* out_stream = avformat_new_stream(outputFile.fmt_ctx, in_codec_ctx->codec);
			if(out_stream == NULL)
			{
				printf("Failed to allocate output stream\n");
				return -2;
			}

			AVCodecContext* outCodecContext = out_stream->codec;
			ret = avcodec_copy_context(outCodecContext, in_codec_ctx);
			if(ret < 0)
			{
				printf("Error occurred while copying context\n");
				return -3;
			}

			// Deprecated된 AVCodecContext 대신 AVStream을 사용.
			out_stream->time_base = in_stream->time_base;
			// FFmpeg에서 지원하는 코덱과 호환성을 맞추기 위해 코덱 태그정보 삭제
			outCodecContext->codec_tag = 0;
			if(outputFile.fmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
			{
				outCodecContext->flags |= CODEC_FLAG_GLOBAL_HEADER;
			}

			if(index == inputFile.v_index)
			{
				outputFile.v_index = out_index++;
			}
			else
			{
				outputFile.a_index = out_index++;
			}
		} // if
	} // for

	if(!(outputFile.fmt_ctx->oformat->flags & AVFMT_NOFILE))
	{
		// 실질적으로 파일을 오픈하는 시점입니다.
		if(avio_open(&outputFile.fmt_ctx->pb, fileName, AVIO_FLAG_WRITE) < 0)
		{
			printf("Failed to create output file %s\n", fileName);
			return -4;
		}
	}

	// 컨테이너의 헤더파일을 쓰는 함수입니다.
	ret = avformat_write_header(outputFile.fmt_ctx, NULL);
	if(ret < 0)
	{
		printf("Failed writing header into output file\n");
		return -5;	
	}

	return 0;
}

static void release()
{
	if(inputFile.fmt_ctx != NULL)
	{
		avformat_close_input(&inputFile.fmt_ctx);
	}

	if(outputFile.fmt_ctx != NULL)
	{
		if(!(outputFile.fmt_ctx->oformat->flags & AVFMT_NOFILE))
		{
			avio_closep(&outputFile.fmt_ctx->pb);
		}
		avformat_free_context(outputFile.fmt_ctx);
	}
}

int main(int argc, char* argv[])
{
	int ret;

	av_register_all();
	av_log_set_level(AV_LOG_DEBUG);

	if(argc < 3)
	{
		printf("usage : %s <input> <output>\n", argv[0]);
		return 0;
	}

	ret = open_input(argv[1]);
	if(ret < 0)
	{
		goto main_end;
	}

	ret = create_output(argv[2]);
	if(ret < 0)
	{
		goto main_end;
	}

	// OUTPUT 파일에 대한 정보를 출력합니다.
	av_dump_format(outputFile.fmt_ctx, 0, outputFile.fmt_ctx->filename, 1);

	AVPacket pkt;
	int stream_index;

	while(1)
	{
		ret = av_read_frame(inputFile.fmt_ctx, &pkt);
		if(ret == AVERROR_EOF)
		{
			printf("End of frame\n");
			break;
		}

		if(pkt.stream_index != inputFile.v_index && 
			pkt.stream_index != inputFile.a_index)
		{
			av_free_packet(&pkt);
			continue;
		}

		AVStream* in_stream = inputFile.fmt_ctx->streams[pkt.stream_index];
		stream_index = (pkt.stream_index == inputFile.v_index) ? 
						outputFile.v_index : outputFile.a_index;
		AVStream* out_stream = outputFile.fmt_ctx->streams[stream_index];

		av_packet_rescale_ts(&pkt, in_stream->time_base, out_stream->time_base);

		pkt.stream_index = stream_index;

		ret = av_interleaved_write_frame(outputFile.fmt_ctx, &pkt);
		if(ret < 0)
		{
			printf("Error occurred when writing packet into file\n");
			break;
		}		
	} // while

	// 파일을 쓰는 시점에서 마무리하지 못한 정보를 정리하는 시점입니다.
	av_write_trailer(outputFile.fmt_ctx);

main_end:
	release();

	return 0;
}