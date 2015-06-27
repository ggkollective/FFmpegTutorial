#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/common.h>
#include <libavutil/avutil.h>

#include <libavfilter/avfilter.h>
#include <libavfilter/avfiltergraph.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>

#include <stdio.h>

AVFormatContext* inAVFormatContext = NULL;
AVFormatContext* outAVFormatContext = NULL;

const char* inFileName;
const char* outFileName;

int inAudioStreamIndex = -1;
int outAudioStreamIndex = -1;

// 필터 컨텍스트
AVFilterContext* audioBufferSrcContext = NULL;
AVFilterContext* audioBufferSinkContext = NULL;
AVFilterGraph* filterGraph = NULL;

#define OUTPUT_SAMPLE_RATE 	44100
#define OUTPUT_CHANNELS 	1

static int openInputFile()
{
	unsigned int index;
	int returnCode;

	returnCode = avformat_open_input(&inAVFormatContext, inFileName, NULL, NULL);
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
		if(avCodecContext->codec_type == AVMEDIA_TYPE_AUDIO)
		{
			AVCodec* decoder = avcodec_find_decoder(avCodecContext->codec_id);
			if(decoder == NULL)
			{
				fprintf(stderr, "Could not find decoder for stream #%d\n", index);
				break;
			}

			if(avcodec_open2(avCodecContext, decoder, NULL) < 0)
			{
				fprintf(stderr, "Failed opening codec for stream #%d\n", index);
				break;
			}

			inAudioStreamIndex = index;
		}
	}

	if(inAudioStreamIndex < 0)
	{
		fprintf(stderr, "Failed to retrieve input stream information\n");
		return -3;
	}

	return 0;
}

static int createOutputFile()
{
	AVStream* inStream = NULL;
	AVStream* outStream = NULL;
	AVCodecContext* inCodecContext = NULL;
	AVCodecContext* outCodecContext = NULL;
	AVCodec* encoder = NULL;

	int returnCode = avformat_alloc_output_context2(&outAVFormatContext, NULL, NULL, outFileName);
	if(returnCode < 0)
	{
		fprintf(stderr, "Could not create output context\n");
		return -1;
	}

	// 스트림이 추가될 때마다 이 인덱스가 증가합니다.
	unsigned int addedStreamIndex = 0;
	unsigned int index;
	for(index = 0; index < inAVFormatContext->nb_streams; index++)
	{
		inStream = inAVFormatContext->streams[index];
		inCodecContext = inStream->codec;

		if(inCodecContext->codec_type == AVMEDIA_TYPE_AUDIO)
		{
			encoder = avcodec_find_encoder(inCodecContext->codec_id);
			outStream = avformat_new_stream(outAVFormatContext, encoder);
			if(outStream == NULL)
			{
				fprintf(stderr, "Failed to allocate output stream\n");
				return -2;
			}

			outCodecContext = outStream->codec;

			outCodecContext->sample_rate = OUTPUT_SAMPLE_RATE;
			outCodecContext->channels = OUTPUT_CHANNELS;
			outCodecContext->channel_layout = av_get_default_channel_layout(outCodecContext->channels);

			// 인코더에서 지원하는 포맷 중 가장 첫번째를 사용합니다. 
			outCodecContext->sample_fmt = encoder->sample_fmts[0];
			outCodecContext->time_base = inCodecContext->time_base;

			outStream->time_base = inStream->time_base;
			outCodecContext->codec_tag = 0;
			if(outAVFormatContext->oformat->flags & AVFMT_GLOBALHEADER)
			{
				outCodecContext->flags |= CODEC_FLAG_GLOBAL_HEADER;
			}

			returnCode = avcodec_open2(outCodecContext, encoder, NULL);
			if(returnCode < 0)
			{
				fprintf(stderr, "Failed to open encoder for stream #%d\n", index);
				return -4;
			}

			outAudioStreamIndex = addedStreamIndex++;
		}
	}

	return 0;
}

static int initAudioFilter()
{
	AVStream* inAudioStream = inAVFormatContext->streams[inAudioStreamIndex];
	AVStream* outAudioStream = outAVFormatContext->streams[outAudioStreamIndex];
	AVCodecContext* inAudioCodecContext = inAudioStream->codec;
	AVCodecContext* outAudioCodecContext = outAudioStream->codec;
	AVFilterInOut *inputs, *outputs;
	char args[512];
	int returnCode;

	filterGraph = avfilter_graph_alloc();
	if(filterGraph == NULL)
	{
		return -1;
	}

	returnCode = avfilter_graph_parse2(filterGraph, "anull", &inputs, &outputs);
	if(returnCode < 0)
	{
		return -2;
	}
	
	// In 필터 설정
	snprintf(args, sizeof(args), "time_base=%d/%d:sample_rate=%d:sample_fmt=%s:channel_layout=0x%"PRIx64
		, inAudioStream->time_base.num, inAudioStream->time_base.den
		, inAudioCodecContext->sample_rate
		, av_get_sample_fmt_name(inAudioCodecContext->sample_fmt)
		, inAudioCodecContext->channel_layout);

	returnCode = avfilter_graph_create_filter(&audioBufferSrcContext, avfilter_get_by_name("abuffer"), "in", args, NULL, filterGraph);
	if(returnCode < 0)
	{
		fprintf(stderr, "Cannot create audio buffer source\n");
		return -3;
	}

	// src -> input
	returnCode = avfilter_link(audioBufferSrcContext, 0, inputs->filter_ctx, 0);
	if(returnCode < 0)
	{
		fprintf(stderr, "Cannot link audio buffer source\n");
		return -4;
	}

	//-----------------------------------------------------------------

	// Out 필터 설정
	returnCode = avfilter_graph_create_filter(&audioBufferSinkContext, avfilter_get_by_name("abuffersink"), "out", NULL, NULL, filterGraph);
	if(returnCode < 0)
	{
		fprintf(stderr, "Cannot create audio buffer sink\n");
		return -3;
	}

	AVFilterContext* aformatFilter;

	snprintf(args, sizeof(args), "sample_fmts=%s:sample_rates=%d:channel_layouts=0x%"PRIx64
		, av_get_sample_fmt_name(outAudioCodecContext->sample_fmt)
		, outAudioCodecContext->sample_rate
		, outAudioCodecContext->channel_layout);
	
	returnCode = avfilter_graph_create_filter(&aformatFilter, avfilter_get_by_name("aformat"), "aformat", args, NULL, filterGraph);
	if(returnCode < 0)
	{
		fprintf(stderr, "Cannot create audio format filter\n");
		return -4;
	}

	// out -> aformat filter
	returnCode = avfilter_link(outputs->filter_ctx, 0, aformatFilter, 0);
	if(returnCode < 0)
	{
		fprintf(stderr, "Cannot link audio format filter\n");
		return -4;
	}

	// aformat filter -> sink
	returnCode = avfilter_link(aformatFilter, 0, audioBufferSinkContext, 0);
	if(returnCode < 0)
	{
		fprintf(stderr, "Cannot link audio format filter\n");
		return -4;
	}

	returnCode = avfilter_graph_config(filterGraph, NULL);
	if(returnCode < 0)
	{
		fprintf(stderr, "Failed to configure audio filter context\n");
		return -5;
	}

	av_buffersink_set_frame_size(audioBufferSinkContext, inAudioCodecContext->frame_size);

	avfilter_inout_free(&inputs);
	avfilter_inout_free(&outputs);
}

static void release()
{
	unsigned int index;

	if(inAVFormatContext != NULL)
	{
		for(index = 0; index < inAVFormatContext->nb_streams; index++)
		{
			AVCodecContext* avCodecContext = inAVFormatContext->streams[index]->codec;
			if(avCodecContext->codec_type == AVMEDIA_TYPE_AUDIO)
			{
				avcodec_close(avCodecContext);
			}
		}

		avformat_close_input(&inAVFormatContext);
	}

	if(outAVFormatContext != NULL)
	{
		for(index = 0; index < outAVFormatContext->nb_streams; index++)
		{
			AVCodecContext* avCodecContext = outAVFormatContext->streams[index]->codec;
			if(avCodecContext != NULL)
			{
				avcodec_close(avCodecContext);
			}
		}
		avformat_free_context(outAVFormatContext);
	}

	if(filterGraph != NULL)
	{
		avfilter_graph_free(&filterGraph);
	}
}

static int decodePacket(AVCodecContext* avCodecContext, const enum AVMediaType streamType, AVPacket* packet, AVFrame** frame, int* gotFrame)
{
	int (*decodeFunc)(AVCodecContext *, AVFrame *, int *, const AVPacket *);
	int decodedPacketSize;
	
	*gotFrame = 0;

	decodeFunc = (streamType == AVMEDIA_TYPE_VIDEO) ? avcodec_decode_video2 : avcodec_decode_audio4;

	decodedPacketSize = decodeFunc(avCodecContext, *frame, gotFrame, packet);
	if(*gotFrame)
	{
		(*frame)->pts = av_frame_get_best_effort_timestamp(*frame);
	}

	return decodedPacketSize;
}

int main(int argc, char* argv[])
{
	int returnCode;

	av_log_set_level(AV_LOG_DEBUG);

	av_register_all();
	avfilter_register_all();

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

	returnCode = initAudioFilter();
	if(returnCode < 0)
	{
		release();
		exit(EXIT_SUCCESS);
	}

	av_dump_format(outAVFormatContext, 0, outFileName, 1);

	AVFrame* decodedFrame = av_frame_alloc();
	AVFrame* filteredFrame = av_frame_alloc();
	if(decodedFrame == NULL || filteredFrame == NULL)
	{
		release();
		exit(EXIT_SUCCESS);
	}
	
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

		AVCodecContext* inCodecContext = inStream->codec;
		AVCodecContext* outCodecContext = outStream->codec;
		
		const enum AVMediaType streamType = inCodecContext->codec_type;

		int gotFrame;

		if(streamType == AVMEDIA_TYPE_AUDIO)
		{
			returnCode = decodePacket(inCodecContext, streamType, &packet, &decodedFrame, &gotFrame);
			if(gotFrame)
			{
				fprintf(stdout, "[before filtering] fmt : %d / sample_rate : %d / channels : %d\n"
					, (int)decodedFrame->format, decodedFrame->sample_rate, decodedFrame->channels);

				returnCode = av_buffersrc_add_frame(audioBufferSrcContext, decodedFrame);
				if(returnCode < 0)
				{
					fprintf(stderr, "Error occurred when putting frame into filter context\n");
					break;
				}

				while(1)
				{
					returnCode = av_buffersink_get_frame(audioBufferSinkContext, filteredFrame);
					if(returnCode < 0)
					{
						break;
					}

					fprintf(stdout, "[after filtering] fmt : %d / sample_rate : %d / channels : %d\n"
						, (int)filteredFrame->format, filteredFrame->sample_rate, filteredFrame->channels);

					av_frame_unref(filteredFrame);
				}

				av_frame_unref(decodedFrame);
			}
		}

		av_free_packet(&packet);
	}

	av_frame_free(&decodedFrame);
	av_frame_free(&filteredFrame);

	release();

	return 0;
}