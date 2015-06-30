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
	AVFormatContext* avFormatContext;
	const char* fileName;
	int videoIndex;
	int audioIndex;
} FileContext;

typedef struct _FilterContext
{
	AVFilterGraph* filterGraph;
	AVFilterContext* srcContext;
	AVFilterContext* sinkContext;
} FilterContext;

static FileContext inputFile, outputFile;
static FilterContext videoFilterContext, audioFilterContext;
static const int dstWidth = 480;
static const int dstHeight = 320;
static const int videoBitrate = (1500*1000);
static const int audioBitrate = (128*1000);
static const int64_t dstChannelLayout = AV_CH_LAYOUT_STEREO;
static const int dstSamplerate = 32000;

static int openDecoder(AVCodecContext* avCodecContext)
{
	AVCodec* decoder = avcodec_find_decoder(avCodecContext->codec_id);
	if(decoder == NULL)
	{
		return -1;
	}

	if(avcodec_open2(avCodecContext, decoder, NULL) < 0)
	{
		return -2;
	}

	return 0;
}

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
			returnCode = openDecoder(avCodecContext);
			if(returnCode < 0)
			{
				break;
			}

			inputFile.videoIndex = index;
		}
		else if(avCodecContext->codec_type == AVMEDIA_TYPE_AUDIO && inputFile.audioIndex < 0)
		{
			returnCode = openDecoder(avCodecContext);
			if(returnCode < 0)
			{
				break;
			}

			inputFile.audioIndex = index;
		}
	} // for

	if(inputFile.audioIndex < 0 && inputFile.audioIndex < 0)
	{
		fprintf(stderr, "Failed to retrieve input stream information\n");
		return -3;
	}

	return 0;
}

static int createOutputFile(const char* fileName)
{
	unsigned int index;
	int streamIndex;
	int returnCode;

	outputFile.avFormatContext = NULL;
	outputFile.fileName = fileName;
	outputFile.audioIndex = -1;
	outputFile.videoIndex = -1;

	returnCode = avformat_alloc_output_context2(&outputFile.avFormatContext, NULL, NULL, outputFile.fileName);
	if(returnCode < 0)
	{
		fprintf(stderr, "Could not create output context\n");
		return -1;
	}

	streamIndex = 0;
	for(index = 0; index < inputFile.avFormatContext->nb_streams; index++)
	{
		AVStream* stream;
		AVCodecContext* inCodecContext = inputFile.avFormatContext->streams[index]->codec;
		AVCodecContext* outCodecContext;
		AVCodec* encoder;
		if(index == inputFile.videoIndex)
		{
			encoder = avcodec_find_encoder(AV_CODEC_ID_H264);
			if(encoder == NULL) break;

			stream = avformat_new_stream(outputFile.avFormatContext, encoder);
			if(stream == NULL) break;

			outCodecContext = stream->codec;

			outCodecContext->bit_rate = videoBitrate;
			outCodecContext->width = dstWidth;
			outCodecContext->height = dstHeight;

			outCodecContext->time_base = inCodecContext->time_base;
			outCodecContext->sample_aspect_ratio = inCodecContext->sample_aspect_ratio;
			outCodecContext->pix_fmt = avcodec_default_get_format(outCodecContext, encoder->pix_fmts);

			if(outputFile.avFormatContext->oformat->flags & AVFMT_GLOBALHEADER)
			{
				outCodecContext->flags |= CODEC_FLAG_GLOBAL_HEADER;
			}

			if(avcodec_open2(outCodecContext, encoder, NULL) < 0) 
			{
				return -2;
			}

			outputFile.videoIndex = streamIndex++;
		}
		else if(index == inputFile.audioIndex)
		{
			encoder = avcodec_find_encoder(AV_CODEC_ID_AAC);
			if(encoder == NULL) break;

			stream = avformat_new_stream(outputFile.avFormatContext, encoder);
			if(stream == NULL) break;

			outCodecContext = stream->codec;

			outCodecContext->bit_rate = audioBitrate;
			outCodecContext->sample_rate = dstSamplerate;
			outCodecContext->channel_layout = dstChannelLayout;
			outCodecContext->channels = av_get_channel_layout_nb_channels(dstChannelLayout);

			outCodecContext->sample_fmt = encoder->sample_fmts[0];
			outCodecContext->time_base = (AVRational){1,dstSamplerate};

			if(outputFile.avFormatContext->oformat->flags & AVFMT_GLOBALHEADER)
			{
				outCodecContext->flags |= CODEC_FLAG_GLOBAL_HEADER;
			}

			if(avcodec_open2(outCodecContext, encoder, NULL) < 0) 
			{
				return -2;
			}

			outputFile.audioIndex = streamIndex++;
		}

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

static int initVideoFilter()
{
	AVStream* avStream = inputFile.avFormatContext->streams[inputFile.videoIndex];
	AVCodecContext* avCodecContext = avStream->codec;
	AVFilterContext* rescaleFilter;
	AVFilterInOut *inputs, *outputs;
	char args[512];
	int returnCode;

	videoFilterContext.filterGraph = NULL;
	videoFilterContext.srcContext = NULL;
	videoFilterContext.sinkContext = NULL;

	// 필터그래프를 위한 메모리를 할당합니다.
	videoFilterContext.filterGraph = avfilter_graph_alloc();
	if(videoFilterContext.filterGraph == NULL)
	{
		return -1;
	}

	// 필터그래프에 input과 output을 연결
	returnCode = avfilter_graph_parse2(videoFilterContext.filterGraph, "null", &inputs, &outputs);
	if(returnCode < 0)
	{
		fprintf(stderr, "Failed to parse video filtergraph\n");
		return -2;
	}

	// Input 필터 생성
	// Buffer Source -> input 필터 생성
	snprintf(args, sizeof(args), "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d"
		, avCodecContext->width, avCodecContext->height
		, avCodecContext->pix_fmt
		, avStream->time_base.num, avStream->time_base.den
		, avCodecContext->sample_aspect_ratio.num, avCodecContext->sample_aspect_ratio.den);

	// Buffer Source 필터 생성
	returnCode = avfilter_graph_create_filter(
					&videoFilterContext.srcContext
					, avfilter_get_by_name("buffer")
					, "in", args, NULL, videoFilterContext.filterGraph);
	if(returnCode < 0)
	{
		fprintf(stderr, "Cannot create video buffer source\n");
		return -3;
	}

	// Buffer Source 필터를 필터그래프의 input으로 연결합니다.
	returnCode = avfilter_link(
					videoFilterContext.srcContext,
					0, inputs->filter_ctx, 0);
	if(returnCode < 0)
	{
		fprintf(stderr, "Cannot link video buffer source\n");
		return -4;
	}

	// Output 필터 생성
	// Buffer Sink 필터 생성
	returnCode = avfilter_graph_create_filter(
					&videoFilterContext.sinkContext
					, avfilter_get_by_name("buffersink")
					, "out", NULL, NULL, videoFilterContext.filterGraph);
	if(returnCode < 0)
	{
		fprintf(stderr, "Cannot create video buffer sink\n");
		return -3;
	}

	// 비디오 프레임 해상도 변경을 위한 리스케일 필터 생성
	snprintf(args, sizeof(args), "%d:%d", dstWidth, dstHeight);

	returnCode = avfilter_graph_create_filter(
					&rescaleFilter
					, avfilter_get_by_name("scale")
					, "scale", args, NULL, videoFilterContext.filterGraph);
	if(returnCode < 0)
	{
		fprintf(stderr, "Cannot create video scale filter\n");
		return -4;
	}

	// 필터그래프의 output을 aformat 필터로 연결합니다.
	returnCode = avfilter_link(outputs->filter_ctx, 0, rescaleFilter, 0);
	if(returnCode < 0)
	{
		fprintf(stderr, "Cannot link video format filter\n");
		return -4;
	}

	AVFilterContext* formatFilter;
	const enum AVPixelFormat pix_fmt = outputFile.avFormatContext->streams[outputFile.videoIndex]->codec->pix_fmt;

	returnCode = avfilter_graph_create_filter(
						&formatFilter
						, avfilter_get_by_name("format")
						, "format"
						, av_get_pix_fmt_name(pix_fmt)
						, NULL, videoFilterContext.filterGraph);

	
	// aformat 필터는 Buffer Sink 필터와 연결합니다.
	returnCode = avfilter_link(rescaleFilter, 0, formatFilter, 0);
	if(returnCode < 0)
	{
		fprintf(stderr, "Cannot link video format filter\n");
		return -4;
	}

	// aformat 필터는 Buffer Sink 필터와 연결합니다.
	returnCode = avfilter_link(formatFilter, 0, videoFilterContext.sinkContext, 0);
	if(returnCode < 0)
	{
		fprintf(stderr, "Cannot link video format filter\n");
		return -4;
	}

	// 준비된 필터를 연결합니다.
	returnCode = avfilter_graph_config(videoFilterContext.filterGraph, NULL);
	if(returnCode < 0)
	{
		fprintf(stderr, "Failed to configure video filter context\n");
		return -5;
	}

	av_buffersink_set_frame_size(videoFilterContext.sinkContext, avCodecContext->frame_size);

	avfilter_inout_free(&inputs);
	avfilter_inout_free(&outputs);
}

static int initAudioFilter()
{
	AVStream* avStream = inputFile.avFormatContext->streams[inputFile.audioIndex];
	AVCodecContext* avCodecContext = avStream->codec;
	AVFilterInOut *inputs, *outputs;
	AVFilterContext* resampleFilter;
	char args[512];
	int returnCode;

	audioFilterContext.filterGraph = NULL;
	audioFilterContext.srcContext = NULL;
	audioFilterContext.sinkContext = NULL;

	// 필터그래프를 위한 메모리를 할당합니다.
	audioFilterContext.filterGraph = avfilter_graph_alloc();
	if(audioFilterContext.filterGraph == NULL)
	{
		return -1;
	}

	// 필터그래프와 함께 input과 output 필터를 생성
	returnCode = avfilter_graph_parse2(audioFilterContext.filterGraph, "anull", &inputs, &outputs);
	if(returnCode < 0)
	{
		fprintf(stderr, "Failed to parse audio filtergraph\n");
		return -2;
	}

	// Input 필터 생성 -----------------------------------
	// Buffer Source -> input 필터 생성
	snprintf(args, sizeof(args), "time_base=%d/%d:sample_rate=%d:sample_fmt=%s:channel_layout=0x%"PRIx64
		, avStream->time_base.num, avStream->time_base.den
		, avCodecContext->sample_rate
		, av_get_sample_fmt_name(avCodecContext->sample_fmt)
		, avCodecContext->channel_layout);

	// Buffer Source 필터 생성
	returnCode = avfilter_graph_create_filter(
					&audioFilterContext.srcContext
					, avfilter_get_by_name("abuffer")
					, "in", args, NULL, audioFilterContext.filterGraph);
	if(returnCode < 0)
	{
		fprintf(stderr, "Cannot create audio buffer source\n");
		return -3;
	}

	// Buffer Source 필터를 필터그래프의 input으로 연결합니다.
	returnCode = avfilter_link(
					audioFilterContext.srcContext,
					0, inputs->filter_ctx, 0);
	if(returnCode < 0)
	{
		fprintf(stderr, "Cannot link audio buffer source\n");
		return -4;
	}

	// Output 필터 생성 -----------------------------------
	// Buffer Sink 필터 생성
	returnCode = avfilter_graph_create_filter(
					&audioFilterContext.sinkContext
					, avfilter_get_by_name("abuffersink")
					, "out", NULL, NULL, audioFilterContext.filterGraph);
	if(returnCode < 0)
	{
		fprintf(stderr, "Cannot create audio buffer sink\n");
		return -3;
	}

	// 오디오 프레임 포맷 변경을 위한 aformat 필터 생성
	snprintf(args, sizeof(args), "sample_fmts=%s:sample_rates=%d:channel_layouts=0x%"PRIx64
		, av_get_sample_fmt_name(outputFile.avFormatContext->streams[outputFile.audioIndex]->codec->sample_fmt)
		, dstSamplerate
		, dstChannelLayout);

	// aformat 필터를 생성합니다.
	returnCode = avfilter_graph_create_filter(
					&resampleFilter
					, avfilter_get_by_name("aformat")
					, "aformat", args, NULL, audioFilterContext.filterGraph);
	if(returnCode < 0)
	{
		fprintf(stderr, "Cannot create audio format filter\n");
		return -4;
	}

	// 필터그래프의 output을 aformat 필터로 연결합니다.
	returnCode = avfilter_link(outputs->filter_ctx, 0, resampleFilter, 0);
	if(returnCode < 0)
	{
		fprintf(stderr, "Cannot link audio format filter\n");
		return -4;
	}

	// aformat 필터는 Buffer Sink 필터와 연결합니다.
	returnCode = avfilter_link(resampleFilter, 0, audioFilterContext.sinkContext, 0);
	if(returnCode < 0)
	{
		fprintf(stderr, "Cannot link audio format filter\n");
		return -4;
	}

	// 준비된 필터를 연결합니다.
	returnCode = avfilter_graph_config(audioFilterContext.filterGraph, NULL);
	if(returnCode < 0)
	{
		fprintf(stderr, "Failed to configure audio filter context\n");
		return -5;
	}

	av_buffersink_set_frame_size(audioFilterContext.sinkContext, avCodecContext->frame_size);

	avfilter_inout_free(&inputs);
	avfilter_inout_free(&outputs);
}

static void release()
{
	unsigned int index;
	if(inputFile.avFormatContext != NULL)
	{
		for(index = 0; index < inputFile.avFormatContext->nb_streams; index++)
		{
			AVCodecContext* avCodecContext = inputFile.avFormatContext->streams[index]->codec;
			if(index == inputFile.videoIndex || index == inputFile.audioIndex)
			{
				avcodec_close(avCodecContext);
			}
		}

		avformat_close_input(&inputFile.avFormatContext);
	}

	if(outputFile.avFormatContext != NULL)
	{
		for(index = 0; index < outputFile.avFormatContext->nb_streams; index++)
		{
			AVCodecContext* avCodecContext = outputFile.avFormatContext->streams[index]->codec;
			avcodec_close(avCodecContext);
		}

		if(!(outputFile.avFormatContext->oformat->flags & AVFMT_NOFILE))
		{
			avio_closep(&outputFile.avFormatContext->pb);
		}
		avformat_free_context(outputFile.avFormatContext);
	}

	if(audioFilterContext.filterGraph != NULL)
	{
		avfilter_graph_free(&audioFilterContext.filterGraph);
	}

	if(videoFilterContext.filterGraph != NULL)
	{
		avfilter_graph_free(&videoFilterContext.filterGraph);
	}
}

static int decodePacket(AVCodecContext* avCodecContext, AVPacket* packet, AVFrame** frame, int* gotFrame)
{
	int (*decodeFunc)(AVCodecContext *, AVFrame *, int *, const AVPacket *);
	int decodedPacketSize;

	decodeFunc = (avCodecContext->codec_type == AVMEDIA_TYPE_VIDEO) ? avcodec_decode_video2 : avcodec_decode_audio4;
	decodedPacketSize = decodeFunc(avCodecContext, *frame, gotFrame, packet);
	if(*gotFrame)
	{
		(*frame)->pts = av_frame_get_best_effort_timestamp(*frame);
	}

	return decodedPacketSize;
}

static int encodeFrame(AVCodecContext* avCodecContext, AVFrame* frame, AVPacket* packet, int* gotPacket)
{
	int (*encodeFunc)(AVCodecContext*, AVPacket*, const AVFrame*, int *);
	encodeFunc = (avCodecContext->codec_type == AVMEDIA_TYPE_VIDEO) ? avcodec_encode_video2 : avcodec_encode_audio2;
	
	av_init_packet(packet);
	packet->data = NULL;
	packet->size = 0;

	return encodeFunc(avCodecContext, packet, frame, gotPacket);
}

int main(int argc, char* argv[])
{
	int returnCode;

	av_register_all();
	avfilter_register_all();
	av_log_set_level(AV_LOG_DEBUG);

	if(argc < 3)
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

	returnCode = createOutputFile(argv[2]);
	if(returnCode < 0)
	{
		release();
		exit(EXIT_SUCCESS);
	}

	returnCode = initVideoFilter();
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

	AVFrame* decodedFrame = av_frame_alloc();
	AVFrame* filteredFrame = av_frame_alloc();
	if(decodedFrame == NULL || filteredFrame == NULL)
	{
		release();
		exit(EXIT_SUCCESS);
	}
	
	AVPacket packet;
	int gotFrame, gotPacket;

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

		AVStream* avStream = inputFile.avFormatContext->streams[packet.stream_index];
		AVCodecContext* avCodecContext = avStream->codec;
		if(avCodecContext == NULL)
		{
			av_free_packet(&packet);
			continue;
		}
		
		const enum AVMediaType streamType = avCodecContext->codec_type;
		gotFrame = 0;

		returnCode = decodePacket(avCodecContext, &packet, &decodedFrame, &gotFrame);
		if(returnCode >= 0 && gotFrame)
		{
			FilterContext* filterContext = 
							(streamType == AVMEDIA_TYPE_VIDEO) ? 
							&videoFilterContext : &audioFilterContext;

			returnCode = av_buffersrc_add_frame(filterContext->srcContext, decodedFrame);
			if(returnCode < 0)
			{
				fprintf(stderr, "Error occurred when putting frame into filter context\n");
				break;
			}

			while(1)
			{
				int streamIndex = (packet.stream_index == inputFile.videoIndex) ? 
									outputFile.videoIndex : outputFile.audioIndex;
				AVStream* outAVStream = outputFile.avFormatContext->streams[streamIndex];
				AVCodecContext* outCodecContext = outAVStream->codec;

				returnCode = av_buffersink_get_frame(filterContext->sinkContext, filteredFrame);
				if(returnCode < 0)
				{
					break;
				}
				
				AVPacket encodedPacket;

				gotPacket = 0;
				returnCode = encodeFrame(outCodecContext, filteredFrame, &encodedPacket, &gotPacket);
				if(gotPacket)
				{
					av_packet_rescale_ts(&encodedPacket, outCodecContext->time_base, outCodecContext->time_base);
					encodedPacket.stream_index = streamIndex;
					returnCode = av_interleaved_write_frame(outputFile.avFormatContext, &encodedPacket);
					if(returnCode < 0)
					{
						fprintf(stderr, "Error occurred when writing packet into file\n");
						break;
					}

					av_free_packet(&encodedPacket);
				}

				av_frame_unref(filteredFrame);
			} // while
			av_frame_unref(decodedFrame);
		} // if

		av_free_packet(&packet);
	} // while
	
	// 파일을 쓰는 시점에서 마무리하지 못한 정보를 정리하는 시점입니다.
	av_write_trailer(outputFile.avFormatContext);

	av_frame_free(&decodedFrame);
	av_frame_free(&filteredFrame);

	release();

	return 0;
}