#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <stdio.h>

static AVFormatContext* fmt_ctx = NULL;

int main(int argc, char* argv[])
{
	unsigned int index;

	av_register_all();

	// Print debug log in library level.
	av_log_set_level(AV_LOG_DEBUG);

	if(argc < 2)
	{
		printf("usage : %s <input>\n", argv[0]);
		return 0;
	}

	// Get fmt_ctx from given file path. 
	if(avformat_open_input(&fmt_ctx, argv[1], NULL, NULL) < 0)
	{
		printf("Could not open input file %s\n", argv[1]);
		return -1;
	}

	// Find stream information from given fmt_ctx.
	if(avformat_find_stream_info(fmt_ctx, NULL) < 0)
	{
		printf("Failed to retrieve input stream information\n");
		return -2;
	}

	// fmt_ctx->nb_streams : number of total streams in video file.
	for(index = 0; index < fmt_ctx->nb_streams; index++)
	{
		AVCodecContext* avCodecContext = fmt_ctx->streams[index]->codec;
		if(avCodecContext->codec_type == AVMEDIA_TYPE_VIDEO)
		{
			printf("------- Video info -------\n");
			printf("codec_id : %d\n", avCodecContext->codec_id);
			printf("bitrate : %d\n", avCodecContext->bit_rate);
			printf("width : %d / height : %d\n", avCodecContext->width, avCodecContext->height);
		}
		else if(avCodecContext->codec_type == AVMEDIA_TYPE_AUDIO)
		{
			printf("------- Audio info -------\n");
			printf("codec_id : %d\n", avCodecContext->codec_id);
			printf("bitrate : %d\n", avCodecContext->bit_rate);
			printf("sample_rate : %d\n", avCodecContext->sample_rate);
			printf("number of channels : %d\n", avCodecContext->channels);
		}
	} // for

	if(fmt_ctx != NULL)
	{
		avformat_close_input(&fmt_ctx);
	}

	return 0;
}
