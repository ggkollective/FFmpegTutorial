#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/common.h>
#include <libavutil/avutil.h>
#include <stdio.h>

typedef struct _FileContext
{
	AVFormatContext* fmt_ctx;
	int v_index;
	int a_index;
} FileContext;

static FileContext inputFile;

static int open_decoder(AVCodecContext* codec_ctx)
{
	// 코덱 ID를 통해 FFmpeg 라이브러리가 자동으로 코덱을 찾도록 합니다.
	AVCodec* decoder = avcodec_find_decoder(codec_ctx->codec_id);
	if(decoder == NULL)
	{
		return -1;
	}

	// 찾아낸 디코더를 통해 코덱을 엽니다.
	if(avcodec_open2(codec_ctx, decoder, NULL) < 0)
	{
		return -2;
	}

	return 0;
}

static int open_input(const char* filename)
{
	unsigned int index;
	int ret;

	inputFile.fmt_ctx = NULL;
	inputFile.a_index = inputFile.v_index = -1;

	ret = avformat_open_input(&inputFile.fmt_ctx, filename, NULL, NULL);
	if(ret < 0)
	{
		printf("Could not open input file %s\n", filename);
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
			ret = open_decoder(codec_ctx);
			if(ret < 0)	break;

			inputFile.v_index = index;
		}
		else if(codec_ctx->codec_type == AVMEDIA_TYPE_AUDIO && inputFile.a_index < 0)
		{
			ret = open_decoder(codec_ctx);
			if(ret < 0) break;

			inputFile.a_index = index;
		}
	} // for

	if(inputFile.a_index < 0 && inputFile.a_index < 0)
	{
		printf("Failed to retrieve input stream information\n");
		return -3;
	}

	return 0;
}

static void release()
{
	if(inputFile.fmt_ctx != NULL)
	{
		unsigned int index;
		for(index = 0; index < inputFile.fmt_ctx->nb_streams; index++)
		{
			AVCodecContext* codec_ctx = inputFile.fmt_ctx->streams[index]->codec;
			if(index == inputFile.v_index || index == inputFile.a_index)
			{
				avcodec_close(codec_ctx);
			}
		}

		avformat_close_input(&inputFile.fmt_ctx);
	}
}

static int decode_packet(AVCodecContext* codec_ctx, AVPacket* pkt, AVFrame** frame, int* got_frame)
{
	int (*decode_func)(AVCodecContext *, AVFrame *, int *, const AVPacket *);
	int decoded_size;

	// 비디오인지 오디오인지에 따라 디코딩할 함수를 정합니다.
	decode_func = (codec_ctx->codec_type == AVMEDIA_TYPE_VIDEO) ? avcodec_decode_video2 : avcodec_decode_audio4;
	decoded_size = decode_func(codec_ctx, *frame, got_frame, pkt);
	if(*got_frame)
	{
		// packet에 있는 PTS와 DTS를 자동으로 frame으로 넘겨주는 작업입니다.
		(*frame)->pts = av_frame_get_best_effort_timestamp(*frame);
	}

	return decoded_size;
}

int main(int argc, char* argv[])
{
	int ret;

	av_register_all();
	av_log_set_level(AV_LOG_DEBUG);

	if(argc < 2)
	{
		printf("usage : %s <input>\n", argv[0]);
		exit(EXIT_SUCCESS);
	}

	ret = open_input(argv[1]);
	if(ret < 0)
	{
		release();
		exit(EXIT_SUCCESS);
	}

	// AVFrame은 디코딩된, 즉 압축되지 않은 raw 데이터를 담는데 사용합니다.
	AVFrame* decoded_frame = av_frame_alloc();
	if(decoded_frame == NULL)
	{
		release();
		exit(EXIT_SUCCESS);
	}
	
	AVPacket pkt;
	int got_frame;
	
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

		AVCodecContext* codec_ctx = inputFile.fmt_ctx->streams[pkt.stream_index]->codec;
		got_frame = 0;

		ret = decode_packet(codec_ctx, &pkt, &decoded_frame, &got_frame);
		if(ret >= 0 && got_frame)
		{
			printf("-----------------------\n");
			if(codec_ctx->codec_type == AVMEDIA_TYPE_VIDEO)
			{
				printf("Video : frame->width, height : %dx%d\n", 
					decoded_frame->width, decoded_frame->height);
				printf("Video : frame->sample_aspect_ratio : %d/%d\n", 
					decoded_frame->sample_aspect_ratio.num, decoded_frame->sample_aspect_ratio.den);
			}
			else
			{
				printf("Audio : frame->nb_samples : %d\n", 
					decoded_frame->nb_samples);
				printf("Audio : frame->channels : %d\n", 
					decoded_frame->channels);
			}

			av_frame_unref(decoded_frame);
		} // if

		av_free_packet(&pkt);
	} // while

	av_frame_free(&decoded_frame);

	release();

	return 0;
}