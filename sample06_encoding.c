#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/common.h>
#include <libavutil/avutil.h>
#include <libavutil/pixdesc.h>
#include <stdio.h>

#include <libavfilter/avfilter.h>
#include <libavfilter/avfiltergraph.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>

typedef struct _FileContext
{
	AVFormatContext* fmt_ctx;
	int v_index;
	int a_index;
} FileContext;

typedef struct _FilterContext
{
	AVFilterGraph* filter_graph;
	AVFilterContext* src_ctx;
	AVFilterContext* sink_ctx;
} FilterContext;

static FileContext inputFile, outputFile;
static FilterContext vfilter_ctx, afilter_ctx;

static const int dst_width = 480;
static const int dst_height = 320;
static const int dst_vbit_rate = 1500000;
static const int dst_abit_rate = 128000;
static const int64_t dst_ch_layout = AV_CH_LAYOUT_STEREO;
static const int dst_sample_rate = 32000;

static int open_decoder(AVCodecContext* codec_ctx)
{
	AVCodec* decoder = avcodec_find_decoder(codec_ctx->codec_id);
	if(decoder == NULL)
	{
		return -1;
	}

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

static int create_output(const char* filename)
{
	unsigned int index;
	int out_index;

	outputFile.fmt_ctx = NULL;
	outputFile.a_index = outputFile.v_index = -1;

	if(avformat_alloc_output_context2(&outputFile.fmt_ctx, NULL, NULL, filename) < 0)
	{
		printf("Could not create output context\n");
		return -1;
	}

	out_index = 0;
	for(index = 0; index < inputFile.fmt_ctx->nb_streams; index++)
	{
		if(index != inputFile.v_index && index != inputFile.a_index)
		{
			continue;
		}

		AVStream* stream;
		AVCodecContext* in_codec_ctx = inputFile.fmt_ctx->streams[index]->codec;
		AVCodecContext* out_codec_ctx;
		AVCodec* encoder;
		
		encoder = avcodec_find_encoder((index == inputFile.v_index) ? AV_CODEC_ID_H264 : AV_CODEC_ID_AAC);
		if(encoder == NULL)
		{
			break;
		}

		stream = avformat_new_stream(outputFile.fmt_ctx, encoder);
		if(stream == NULL)
		{
			break;
		}

		out_codec_ctx = stream->codec;

		if(index == inputFile.v_index)
		{
			out_codec_ctx->bit_rate = dst_vbit_rate;
			out_codec_ctx->width = dst_width;
			out_codec_ctx->height = dst_height;
			out_codec_ctx->time_base = in_codec_ctx->time_base;
			out_codec_ctx->sample_aspect_ratio = in_codec_ctx->sample_aspect_ratio;
			out_codec_ctx->pix_fmt = avcodec_default_get_format(out_codec_ctx, encoder->pix_fmts);

			outputFile.v_index = out_index++;
		}
		else if(index == inputFile.a_index)
		{
			out_codec_ctx->bit_rate = dst_abit_rate;
			out_codec_ctx->sample_rate = dst_sample_rate;
			out_codec_ctx->channel_layout = dst_ch_layout;
			out_codec_ctx->channels = av_get_channel_layout_nb_channels(dst_ch_layout);
			out_codec_ctx->sample_fmt = encoder->sample_fmts[0];
			out_codec_ctx->time_base = (AVRational){1, dst_sample_rate};

			outputFile.a_index = out_index++;
		}

		if(outputFile.fmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
		{
			out_codec_ctx->flags |= CODEC_FLAG_GLOBAL_HEADER;
		}

		if(avcodec_open2(out_codec_ctx, encoder, NULL) < 0) 
		{
			return -2;
		}
	} // for

	if(!(outputFile.fmt_ctx->oformat->flags & AVFMT_NOFILE))
	{
		if(avio_open(&outputFile.fmt_ctx->pb, filename, AVIO_FLAG_WRITE) < 0)
		{
			printf("Failed to create output file %s\n", filename);
			return -4;
		}
	}

	if(avformat_write_header(outputFile.fmt_ctx, NULL) < 0)
	{
		printf("Failed writing header into output file\n");
		return -5;	
	}

	return 0;
}

static int init_video_filter()
{
	AVStream* in_stream = inputFile.fmt_ctx->streams[inputFile.v_index];
	AVStream* out_stream = outputFile.fmt_ctx->streams[outputFile.v_index];
	AVCodecContext* in_codec_ctx = in_stream->codec;
	AVCodecContext* out_codec_ctx = in_stream->codec;
	AVFilterContext* rescale_filter;	
	AVFilterContext* format_filter;

	AVFilterInOut *inputs, *outputs;
	char args[512];

	vfilter_ctx.filter_graph = NULL;
	vfilter_ctx.src_ctx = NULL;
	vfilter_ctx.sink_ctx = NULL;

	vfilter_ctx.filter_graph = avfilter_graph_alloc();
	if(vfilter_ctx.filter_graph == NULL)
	{
		return -1;
	}

	if(avfilter_graph_parse2(vfilter_ctx.filter_graph, "null", &inputs, &outputs) < 0)
	{
		printf("Failed to parse video filtergraph\n");
		return -2;
	}

	snprintf(args, sizeof(args), "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d"
		, in_codec_ctx->width, in_codec_ctx->height
		, in_codec_ctx->pix_fmt
		, in_stream->time_base.num, in_stream->time_base.den
		, in_codec_ctx->sample_aspect_ratio.num, in_codec_ctx->sample_aspect_ratio.den);

	if(avfilter_graph_create_filter(
					&vfilter_ctx.src_ctx
					, avfilter_get_by_name("buffer")
					, "in", args, NULL, vfilter_ctx.filter_graph) < 0)
	{
		printf("Failed to create video buffer source\n");
		return -3;
	}

	if(avfilter_link(vfilter_ctx.src_ctx, 0, inputs->filter_ctx, 0) < 0)
	{
		printf("Failed to link video buffer source\n");
		return -4;
	}

	if(avfilter_graph_create_filter(
					&vfilter_ctx.sink_ctx
					, avfilter_get_by_name("buffersink")
					, "out", NULL, NULL, vfilter_ctx.filter_graph) < 0)
	{
		printf("Failed to create video buffer sink\n");
		return -3;
	}

	snprintf(args, sizeof(args), "%d:%d", dst_width, dst_height);

	if(avfilter_graph_create_filter(
					&rescale_filter
					, avfilter_get_by_name("scale")
					, "scale", args, NULL, vfilter_ctx.filter_graph) < 0)
	{
		printf("Failed to create video scale filter\n");
		return -4;
	}

	if(avfilter_link(outputs->filter_ctx, 0, rescale_filter, 0) < 0)
	{
		printf("Failed to link video format filter\n");
		return -4;
	}

	if(avfilter_graph_create_filter(
						&format_filter
						, avfilter_get_by_name("format")
						, "format"
						, av_get_pix_fmt_name(out_codec_ctx->pix_fmt)
						, NULL, vfilter_ctx.filter_graph) < 0)
	{
		printf("Failed to create video format filter\n");
		return -4;
	}

	if(avfilter_link(rescale_filter, 0, format_filter, 0) < 0)
	{
		printf("Failed to link video format filter\n");
		return -4;
	}

	if(avfilter_link(format_filter, 0, vfilter_ctx.sink_ctx, 0) < 0)
	{
		printf("Failed to link video format filter\n");
		return -4;
	}

	if(avfilter_graph_config(vfilter_ctx.filter_graph, NULL) < 0)
	{
		printf("Failed to configure video filter context\n");
		return -5;
	}

	av_buffersink_set_frame_size(vfilter_ctx.sink_ctx, in_codec_ctx->frame_size);

	avfilter_inout_free(&inputs);
	avfilter_inout_free(&outputs);
}

static int init_audio_filter()
{
	AVStream* in_stream = inputFile.fmt_ctx->streams[inputFile.a_index];
	AVCodecContext* in_codec_ctx = in_stream->codec;
	AVFilterInOut *inputs, *outputs;
	AVFilterContext* resample_filter;
	char args[512];
	int ret;

	afilter_ctx.filter_graph = NULL;
	afilter_ctx.src_ctx = NULL;
	afilter_ctx.sink_ctx = NULL;

	afilter_ctx.filter_graph = avfilter_graph_alloc();
	if(afilter_ctx.filter_graph == NULL)
	{
		return -1;
	}

	ret = avfilter_graph_parse2(afilter_ctx.filter_graph, "anull", &inputs, &outputs);
	if(ret < 0)
	{
		printf("Failed to parse audio filtergraph\n");
		return -2;
	}

	snprintf(args, sizeof(args), "time_base=%d/%d:sample_rate=%d:sample_fmt=%s:channel_layout=0x%"PRIx64
		, in_stream->time_base.num, in_stream->time_base.den
		, in_codec_ctx->sample_rate
		, av_get_sample_fmt_name(in_codec_ctx->sample_fmt)
		, in_codec_ctx->channel_layout);

	if(avfilter_graph_create_filter(
					&afilter_ctx.src_ctx
					, avfilter_get_by_name("abuffer")
					, "in", args, NULL, afilter_ctx.filter_graph) < 0)
	{
		printf("Failed to create audio buffer source\n");
		return -3;
	}

	if(avfilter_link(afilter_ctx.src_ctx, 0, inputs->filter_ctx, 0) < 0)
	{
		printf("Failed to link audio buffer source\n");
		return -4;
	}

	if(avfilter_graph_create_filter(
					&afilter_ctx.sink_ctx
					, avfilter_get_by_name("abuffersink")
					, "out", NULL, NULL, afilter_ctx.filter_graph) < 0)
	{
		printf("Failed to create audio buffer sink\n");
		return -3;
	}

	snprintf(args, sizeof(args), "sample_fmts=%s:sample_rates=%d:channel_layouts=0x%"PRIx64
		, av_get_sample_fmt_name(outputFile.fmt_ctx->streams[outputFile.a_index]->codec->sample_fmt)
		, dst_sample_rate
		, dst_ch_layout);

	if(avfilter_graph_create_filter(
					&resample_filter
					, avfilter_get_by_name("aformat")
					, "aformat", args, NULL, afilter_ctx.filter_graph) < 0)
	{
		printf("Failed to create audio format filter\n");
		return -4;
	}

	if(avfilter_link(outputs->filter_ctx, 0, resample_filter, 0) < 0)
	{
		printf("Failed to link audio format filter\n");
		return -4;
	}

	if(avfilter_link(resample_filter, 0, afilter_ctx.sink_ctx, 0) < 0)
	{
		printf("Failed to link audio format filter\n");
		return -4;
	}

	if(avfilter_graph_config(afilter_ctx.filter_graph, NULL) < 0)
	{
		printf("Failed to configure audio filter context\n");
		return -5;
	}

	av_buffersink_set_frame_size(afilter_ctx.sink_ctx, in_codec_ctx->frame_size);

	avfilter_inout_free(&inputs);
	avfilter_inout_free(&outputs);
}

static void release()
{
	unsigned int index;
	if(inputFile.fmt_ctx != NULL)
	{
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

	if(outputFile.fmt_ctx != NULL)
	{
		for(index = 0; index < outputFile.fmt_ctx->nb_streams; index++)
		{
			AVCodecContext* codec_ctx = outputFile.fmt_ctx->streams[index]->codec;
			avcodec_close(codec_ctx);
		}

		if(!(outputFile.fmt_ctx->oformat->flags & AVFMT_NOFILE))
		{
			avio_closep(&outputFile.fmt_ctx->pb);
		}
		avformat_free_context(outputFile.fmt_ctx);
	}

	if(afilter_ctx.filter_graph != NULL)
	{
		avfilter_graph_free(&afilter_ctx.filter_graph);
	}

	if(vfilter_ctx.filter_graph != NULL)
	{
		avfilter_graph_free(&vfilter_ctx.filter_graph);
	}
}

static int decode_packet(AVCodecContext* codec_ctx, AVPacket* pkt, AVFrame** frame, int* got_frame)
{
	int (*decode_func)(AVCodecContext *, AVFrame *, int *, const AVPacket *);
	int decoded_size;

	decode_func = (codec_ctx->codec_type == AVMEDIA_TYPE_VIDEO) ? avcodec_decode_video2 : avcodec_decode_audio4;
	decoded_size = decode_func(codec_ctx, *frame, got_frame, pkt);
	if(*got_frame)
	{
		(*frame)->pts = av_frame_get_best_effort_timestamp(*frame);
	}

	return decoded_size;
}

static int encode_write_frame(AVFrame* frame, int out_stream_index, int* got_packet)
{
	AVStream* stream = outputFile.fmt_ctx->streams[out_stream_index];
	AVCodecContext* codec_ctx = stream->codec;
	int (*encode_func)(AVCodecContext*, AVPacket*, const AVFrame*, int *);
	AVPacket encoded_pkt;
	
	av_init_packet(&encoded_pkt);
	encoded_pkt.data = NULL;
	encoded_pkt.size = 0;
	
	encode_func = (out_stream_index == outputFile.v_index) ? avcodec_encode_video2 : avcodec_encode_audio2;
	*got_packet = 0;

	if(frame != NULL) frame->pict_type = AV_PICTURE_TYPE_NONE;

	if(encode_func(codec_ctx, &encoded_pkt, frame, got_packet) < 0)
	{
		printf("Error occurred when encoding frame\n");
		return -1;
	}

	if(*got_packet)
	{
		encoded_pkt.stream_index = out_stream_index;
		av_packet_rescale_ts(&encoded_pkt, codec_ctx->time_base, stream->time_base);

		if(av_interleaved_write_frame(outputFile.fmt_ctx, &encoded_pkt) < 0)
		{
			printf("Error occurred when writing packet into file\n");
			return -2;
		}

		av_free_packet(&encoded_pkt);
	}

	return 0;
}

static int filter_encode_write_frame(AVFrame* frame, int out_stream_index)
{
	AVStream* out_stream = outputFile.fmt_ctx->streams[out_stream_index];
	AVCodecContext* out_codec_ctx = out_stream->codec;
	FilterContext* filterContext = (out_stream_index == outputFile.v_index) ? 
										&vfilter_ctx : &afilter_ctx;
	int got_packet;

	AVFrame* filtered_frame = av_frame_alloc();
	if(filtered_frame == NULL)
	{
		return -1;
	}

	if(av_buffersrc_add_frame(filterContext->src_ctx, frame) < 0)
	{
		printf("Error occurred when putting frame into filter context\n");
		return -2;
	}

	while(1)
	{
		if(av_buffersink_get_frame(filterContext->sink_ctx, filtered_frame) < 0)
		{
			break;
		}

		if(encode_write_frame(filtered_frame, out_stream_index, &got_packet) < 0)
		{
			break;
		}

		av_frame_unref(filtered_frame);
	} // while

	av_frame_free(&filtered_frame);
	return 0;
}

int main(int argc, char* argv[])
{
	int ret;

	av_register_all();
	avfilter_register_all();
	av_log_set_level(AV_LOG_DEBUG);

	if(argc < 3)
	{
		printf("usage : %s <input>\n", argv[0]);
		return 0;
	}

	if(open_input(argv[1]) < 0 || create_output(argv[2]) < 0)
	{
		goto main_end;
	}

	if(init_video_filter() < 0 || init_audio_filter() < 0)
	{
		goto main_end;
	}

	AVFrame* decoded_frame = av_frame_alloc();
	if(decoded_frame == NULL)
	{
		goto main_end;
	}
	
	AVPacket pkt;
	int got_frame;
	int out_stream_index;

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
		AVCodecContext* in_codec_ctx = in_stream->codec;

		out_stream_index = (pkt.stream_index == inputFile.v_index) ? 
						outputFile.v_index : outputFile.a_index;

		got_frame = 0;		
		av_packet_rescale_ts(&pkt, in_stream->time_base, in_codec_ctx->time_base);
		
		ret = decode_packet(in_codec_ctx, &pkt, &decoded_frame, &got_frame);
		if(ret >= 0 && got_frame)
		{
			ret = filter_encode_write_frame(decoded_frame, out_stream_index);
			av_frame_unref(decoded_frame);
			if(ret < 0)
			{
				av_free_packet(&pkt);
				break;
			}
		} // if

		av_free_packet(&pkt);
	} // while

	// Flush all remaining frames in encoder and filter.
	int index, got_packet;
	for(index = 0; index < inputFile.fmt_ctx->nb_streams; index++)
	{
		if(pkt.stream_index != inputFile.v_index && 
			pkt.stream_index != inputFile.a_index)
		{
			continue;
		}

		// Flush filter
		out_stream_index = (index == inputFile.v_index) ? 
						outputFile.v_index : outputFile.a_index;
		ret = filter_encode_write_frame(NULL, out_stream_index);
		if(ret < 0)
		{
			printf("Error occurred while flusing filter context\n");
			break;
		}

		// flush encoder
		while(1)
		{
			ret = encode_write_frame(NULL, out_stream_index, &got_packet);
			if(ret < 0 || got_packet == 0)
			{
				break;
			}
		}
	}
	
	// Writing trailer.
	av_write_trailer(outputFile.fmt_ctx);
	av_frame_free(&decoded_frame);
main_end:
	release();

	return 0;
}
