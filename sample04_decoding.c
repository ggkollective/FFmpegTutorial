#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/common.h>
#include <libavutil/avutil.h>
#include <stdio.h>

AVFormatContext* inAVFormatContext = NULL;
const char* inFileName;

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
		if(avCodecContext->codec_type == AVMEDIA_TYPE_VIDEO || avCodecContext->codec_type == AVMEDIA_TYPE_AUDIO)
		{
			// 코덱 ID를 통해 FFmpeg 라이브러리가 자동으로 코덱을 찾도록 합니다.
			AVCodec* decoder = avcodec_find_decoder(avCodecContext->codec_id);
			if(decoder == NULL)
			{
				fprintf(stderr, "Could not find decoder for stream #%d\n", index);
				break;
			}

			// 찾아낸 디코더를 통해 코덱을 엽니다.
			if(avcodec_open2(avCodecContext, decoder, NULL) < 0)
			{
				fprintf(stderr, "Failed opening codec for stream #%d\n", index);
				break;
			}

			if(avCodecContext->codec_type == AVMEDIA_TYPE_VIDEO)
			{
				videoStreamIndex = index;
			}
			else
			{
				audioStreamIndex = index;
			}
		}
	}

	if(videoStreamIndex < 0 && audioStreamIndex < 0)
	{
		fprintf(stderr, "Failed to retrieve input stream information\n");
		return -3;
	}

	return 0;
}

static void release()
{
	if(inAVFormatContext != NULL)
	{
		avformat_close_input(&inAVFormatContext);
	}
}

static int decodePacket(AVCodecContext* avCodecContext, AVPacket* packet, AVFrame** frame, int* gotFrame)
{
	int (*decodeFunc)(AVCodecContext *, AVFrame *, int *, const AVPacket *);
	int decodedPacketSize;
	
	*gotFrame = 0;

	decodeFunc = (avCodecContext->codec_type == AVMEDIA_TYPE_VIDEO) ? avcodec_decode_video2 : avcodec_decode_audio4;
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

	av_register_all();

	if(argc < 2)
	{
		fprintf(stderr, "usage : %s <input>\n", argv[0]);
		exit(EXIT_SUCCESS);
	}

	inFileName = argv[1];
	returnCode = openInputFile();
	if(returnCode < 0)
	{
		release();
		exit(EXIT_SUCCESS);
	}

	AVFrame* decodedFrame = av_frame_alloc();
	if(decodedFrame == NULL)
	{
		release();
		exit(EXIT_SUCCESS);
	}

	while(1)
	{
		AVPacket packet;
		
		returnCode = av_read_frame(inAVFormatContext, &packet);
		if(returnCode == AVERROR_EOF)
		{
			fprintf(stdout, "End of frame\n");
			break;
		}

		AVCodecContext* avCodecContext = inAVFormatContext->streams[packet.stream_index]->codec;
		if(avCodecContext == NULL) continue;
		
		const enum AVMediaType streamType = avCodecContext->codec_type;
		int gotFrame;

		returnCode = decodePacket(avCodecContext, &packet, &decodedFrame, &gotFrame);
		if(returnCode >= 0 && gotFrame)
		{
			fprintf(stdout, "-----------------------\n");
			if(streamType == AVMEDIA_TYPE_VIDEO)
			{
				fprintf(stdout, "Video : frame->width, height : %dx%d\n", decodedFrame->width, decodedFrame->height);
				fprintf(stdout, "Video : frame->sample_aspect_ratio : %d/%d\n", decodedFrame->sample_aspect_ratio.num, decodedFrame->sample_aspect_ratio.den);
			}
			else
			{
				fprintf(stdout, "Audio : frame->nb_samples : %d\n", decodedFrame->nb_samples);
				fprintf(stdout, "Audio : frame->channels : %d\n", decodedFrame->channels);
			}

			av_frame_unref(decodedFrame);
		}

		av_free_packet(&packet);
	}

	av_frame_free(&decodedFrame);

	release();

	return 0;
}