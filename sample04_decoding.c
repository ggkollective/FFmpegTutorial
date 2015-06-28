#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/common.h>
#include <libavutil/avutil.h>
#include <stdio.h>

typedef struct _FileContext
{
	AVFormatContext* avFormatContext;
	const char* fileName;
	int videoIndex;
	int audioIndex;
} FileContext;

static FileContext inputFile;

static int openDecoder(AVCodecContext* avCodecContext)
{
	// 코덱 ID를 통해 FFmpeg 라이브러리가 자동으로 코덱을 찾도록 합니다.
	AVCodec* decoder = avcodec_find_decoder(avCodecContext->codec_id);
	if(decoder == NULL)
	{
		fprintf(stderr, "Could not find decoder for stream #%d\n", index);
		return -1;
	}

	// 찾아낸 디코더를 통해 코덱을 엽니다.
	if(avcodec_open2(avCodecContext, decoder, NULL) < 0)
	{
		fprintf(stderr, "Failed opening codec for stream #%d\n", index);
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

static void release()
{
	if(inputFile.avFormatContext != NULL)
	{
		unsigned int index;
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
}

static int decodePacket(AVCodecContext* avCodecContext, AVPacket* packet, AVFrame** frame, int* gotFrame)
{
	int (*decodeFunc)(AVCodecContext *, AVFrame *, int *, const AVPacket *);
	int decodedPacketSize;

	// 비디오인지 오디오인지에 따라 디코딩할 함수를 정합니다.
	decodeFunc = (avCodecContext->codec_type == AVMEDIA_TYPE_VIDEO) ? avcodec_decode_video2 : avcodec_decode_audio4;
	decodedPacketSize = decodeFunc(avCodecContext, *frame, gotFrame, packet);
	if(*gotFrame)
	{
		// packet에 있는 PTS와 DTS를 자동으로 frame으로 넘겨주는 작업입니다.
		(*frame)->pts = av_frame_get_best_effort_timestamp(*frame);
	}

	return decodedPacketSize;
}

int main(int argc, char* argv[])
{
	int returnCode;

	av_register_all();
	av_log_set_level(AV_LOG_DEBUG);

	if(argc < 2)
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

	// AVFrame은 디코딩된, 즉 압축되지 않은 raw 데이터를 담는데 사용합니다.
	AVFrame* decodedFrame = av_frame_alloc();
	if(decodedFrame == NULL)
	{
		release();
		exit(EXIT_SUCCESS);
	}
	
	AVPacket packet;
	int gotFrame;
	
	while(1)
	{	
		returnCode = av_read_frame(inputFile.avFormatContext, &packet);
		if(returnCode == AVERROR_EOF)
		{
			fprintf(stdout, "End of frame\n");
			break;
		}

		AVCodecContext* avCodecContext = inputFile.avFormatContext->streams[packet.stream_index]->codec;
		if(avCodecContext == NULL) continue;
		
		const enum AVMediaType streamType = avCodecContext->codec_type;
		gotFrame = 0;

		returnCode = decodePacket(avCodecContext, &packet, &decodedFrame, &gotFrame);
		if(returnCode >= 0 && gotFrame)
		{
			fprintf(stdout, "-----------------------\n");
			if(streamType == AVMEDIA_TYPE_VIDEO)
			{
				fprintf(stdout, "Video : frame->width, height : %dx%d\n", 
					decodedFrame->width, decodedFrame->height);
				fprintf(stdout, "Video : frame->sample_aspect_ratio : %d/%d\n", 
					decodedFrame->sample_aspect_ratio.num, decodedFrame->sample_aspect_ratio.den);
			}
			else
			{
				fprintf(stdout, "Audio : frame->nb_samples : %d\n", 
					decodedFrame->nb_samples);
				fprintf(stdout, "Audio : frame->channels : %d\n", 
					decodedFrame->channels);
			}

			av_frame_unref(decodedFrame);
		} // if

		av_free_packet(&packet);
	} // while

	av_frame_free(&decodedFrame);

	release();

	return 0;
}