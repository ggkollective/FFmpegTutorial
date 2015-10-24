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
	// Find a decoder by codec ID
	AVCodec* decoder = avcodec_find_decoder(codec_ctx->codec_id);
	if(decoder == NULL)
	{
		return -1;
	}

	// Open the codec using decoder
	if(avcodec_open2(codec_ctx, decoder, NULL) < 0)
	{
		return -2;
	}

	return 0;
}

static int open_input(const char* filename)
{
	unsigned int index;

	inputFile.fmt_ctx = NULL;
	inputFile.a_index = inputFile.v_index = -1;

	if(avformat_open_input(&inputFile.fmt_ctx, filename, NULL, NULL) < 0)
	{
		printf("Could not open input file %s\n", filename);
		return -1;
	}

	if(avformat_find_stream_info(inputFile.fmt_ctx, NULL) < 0)
	{
		printf("Failed to retrieve input stream information\n");
		return -2;
	}

	for(index = 0; index < inputFile.fmt_ctx->nb_streams; index++)
	{
		AVCodecContext* codec_ctx = inputFile.fmt_ctx->streams[index]->codec;
		if(codec_ctx->codec_type == AVMEDIA_TYPE_VIDEO && inputFile.v_index < 0)
		{
			if(open_decoder(codec_ctx) < 0)
			{
				break;
			}

			inputFile.v_index = index;
		}
		else if(codec_ctx->codec_type == AVMEDIA_TYPE_AUDIO && inputFile.a_index < 0)
		{
			if(open_decoder(codec_ctx) < 0)
			{
				break;
			}

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

	// Decide which is needed for decoding pakcet. 
	decode_func = (codec_ctx->codec_type == AVMEDIA_TYPE_VIDEO) ? avcodec_decode_video2 : avcodec_decode_audio4;
	decoded_size = decode_func(codec_ctx, *frame, got_frame, pkt);
	if(*got_frame)
	{
		// This adjust PTS/DTS automatically in frame.
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
		return 0;
	}

	if(open_input(argv[1]) < 0)
	{
		goto main_end;
	}

	// AVFrame is used to store raw frame, which is decoded from packet.
	AVFrame* decoded_frame = av_frame_alloc();
	if(decoded_frame == NULL) goto main_end;
	
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

		AVStream* avStream = inputFile.fmt_ctx->streams[pkt.stream_index];
		AVCodecContext* codec_ctx = avStream->codec;
		got_frame = 0;

		av_packet_rescale_ts(&pkt, avStream->time_base, codec_ctx->time_base);

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

main_end:
	release();

	return 0;
}
