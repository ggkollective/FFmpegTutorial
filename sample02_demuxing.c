#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <stdio.h>

typedef struct _FileContext
{
	AVFormatContext* fmt_ctx;
	int v_index;
	int a_index;
} FileContext;

static FileContext input_ctx;

static int open_input(const char* filename)
{
	unsigned int index;
	int ret;

	input_ctx.fmt_ctx = NULL;
	input_ctx.v_index = input_ctx.a_index = -1;

	ret = avformat_open_input(&input_ctx.fmt_ctx, filename, NULL, NULL);
	if(ret < 0)
	{
		printf("Could not open input file %s\n", filename);
		return -1;
	}

	ret = avformat_find_stream_info(input_ctx.fmt_ctx, NULL);
	if(ret < 0)
	{
		printf("Failed to retrieve input stream information\n");
		return -2;
	}

	for(index = 0; index < input_ctx.fmt_ctx->nb_streams; index++)
	{
		AVCodecContext* codec_ctx = input_ctx.fmt_ctx->streams[index]->codec;
		if(codec_ctx->codec_type == AVMEDIA_TYPE_VIDEO && input_ctx.v_index < 0)
		{
			input_ctx.v_index = index;
		}
		else if(codec_ctx->codec_type == AVMEDIA_TYPE_AUDIO && input_ctx.a_index < 0)
		{
			input_ctx.a_index = index;
		}
	} // for

	if(input_ctx.v_index < 0 && input_ctx.a_index < 0)
	{
		printf("Failed to retrieve input stream information\n");
		return -3;
	}

	return 0;
}

static void release()
{
	if(input_ctx.fmt_ctx != NULL)
	{
		avformat_close_input(&input_ctx.fmt_ctx);
	}
}

int main(int argc, char* argv[])
{
	int ret;

	av_register_all();
	av_log_set_level(AV_LOG_DEBUG);

	if(argc < 2)
	{
		printf("usage : %s <input>\n", argv[0]);
		return 0;
	}

	ret = open_input(argv[1]);
	if(ret < 0)
	{
		goto main_end;
	}

	// AVPacket은 코덱으로 압축된 스트림 데이터를 저장하는데 사용됩니다.
	AVPacket pkt;

	while(1)
	{
		ret = av_read_frame(input_ctx.fmt_ctx, &pkt);
		if(ret == AVERROR_EOF)
		{
			// 더 이상 읽어올 패킷이 없습니다.
			printf("End of frame\n");
			break;
		}

		if(pkt.stream_index == input_ctx.v_index)
		{
			printf("Video packet\n");
		}
		else if(pkt.stream_index == input_ctx.a_index)
		{
			printf("Audio packet\n");
		}

		av_free_packet(&pkt);
	} // while

main_end:
	release();

	return 0;
}