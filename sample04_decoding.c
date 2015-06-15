#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/common.h>
#include <libavutil/avutil.h>
#include <stdio.h>

AVFormatContext* inAVFormatContext = NULL;
const char* inFileName;

int videoStreamIndex = -1;
int audioStreamIndex = -1;

static const char* AVDisplayCodecName(AVCodecContext* avCodecContext)
{
	// AVCodecDescriptor Context를 통해 코덱 정보를 쉽게 가져올 수 있습니다.
	const AVCodecDescriptor *codecDescriptor = avcodec_descriptor_get(avCodecContext->codec_id);
	if(codecDescriptor != NULL)
	{
		return codecDescriptor->name;
	}

	return "";
}

static int AVOpenDecoder(AVCodecContext* avCodecContext)
{
	if(avCodecContext == NULL)
	{
		printf("올바르지 않은 AVCodecContext 입니다.\n");
		return -1;
	}

	// 코덱 ID를 통해 FFmpeg 라이브러리가 자동으로 코덱을 찾도록 합니다.
	AVCodec* decoder = avcodec_find_decoder(avCodecContext->codec_id);
	if(decoder == NULL)
	{
		printf("%s 코덱의 디코더를 찾을 수 없습니다.\n", AVDisplayCodecName(avCodecContext));
		return -2;
	}

	// 찾아낸 디코더를 통해 코덱을 엽니다.
	if(avcodec_open2(avCodecContext, decoder, NULL) < 0)
	{
		printf("%s 코덱을 여는데 실패하였습니다.\n", AVDisplayCodecName(avCodecContext));
		return -3;
	}

	printf("%s 코덱을 여는데 성공하였습니다.\n", AVDisplayCodecName(avCodecContext));
	return 0;
}

static int AVOpenInput(const char* fileName)
{
	unsigned int index;
	int returnCode;

	returnCode = avformat_open_input(&inAVFormatContext, fileName, NULL, NULL);
	if(returnCode < 0)
	{
		printf("알려지지 않았거나 잘못된 파일 형식입니다.\n");
		return -1;
	}

	//주어진 AVFormatContext로부터 유효한 스트림이 있는지 찾습니다.
	returnCode = avformat_find_stream_info(inAVFormatContext, NULL);
	if(returnCode < 0)
	{
		printf("유료한 스트림 정보가 없습니다.\n");
		return -2;
	}

	for(index = 0; index < inAVFormatContext->nb_streams; index++)
	{
		AVCodecContext* avCodecContext = inAVFormatContext->streams[index]->codec;
		if(avCodecContext->codec_type == AVMEDIA_TYPE_VIDEO)
		{
			returnCode = AVOpenDecoder(avCodecContext);
			if(returnCode < 0) break;
			videoStreamIndex = index;
		}
		else if(avCodecContext->codec_type == AVMEDIA_TYPE_AUDIO)
		{
			returnCode = AVOpenDecoder(avCodecContext);
			if(returnCode < 0) break;
			audioStreamIndex = index;
		}
	}

	if(videoStreamIndex < 0 && audioStreamIndex < 0)
	{
		printf("이 컨테이너에는 비디오/오디오 스트림 정보가 없습니다.\n");
		return -3;
	}

	return 0;
}

static void AVRelease()
{
	if(inAVFormatContext != NULL)
	{
		avformat_close_input(&inAVFormatContext);
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

	av_register_all();

	if(argc < 2)
	{
		printf("파일 이름을 입력하세요.\n");
		exit(EXIT_SUCCESS);
	}

	inFileName = argv[1];

	returnCode = AVOpenInput(inFileName);
	if(returnCode < 0)
	{
		AVRelease();
		exit(EXIT_SUCCESS);
	}

	//디코딩된 프레임이 저장되는 곳입니다.
	AVFrame* decodedFrame = av_frame_alloc();
	if(decodedFrame == NULL)
	{
		printf("시스템 자원이 부족합니다.\n");
		AVRelease();
		exit(EXIT_SUCCESS);
	}

	while(1)
	{
		AVPacket packet;
		
		returnCode = av_read_frame(inAVFormatContext, &packet);
		if(returnCode == AVERROR_EOF)
		{
			break;
		}

		AVCodecContext* avCodecContext = inAVFormatContext->streams[packet.stream_index]->codec;
		if(avCodecContext == NULL) continue;
		
		const enum AVMediaType streamType = avCodecContext->codec_type;

		int gotFrame;

		returnCode = decodePacket(avCodecContext, streamType, &packet, &decodedFrame, &gotFrame);
		if(gotFrame)
		{
			printf("-----------------------\n");
			if(streamType == AVMEDIA_TYPE_VIDEO)
			{
				printf("[VIDEO] frame->width, height : %dx%d\n", decodedFrame->width, decodedFrame->height);
				printf("[VIDEO] frame->sample_aspect_ratio : %d/%d\n", decodedFrame->sample_aspect_ratio.num, decodedFrame->sample_aspect_ratio.den);
			}
			else
			{
				printf("[AUDIO] frame->nb_samples : %d\n", decodedFrame->nb_samples);
				printf("[AUDIO] frame->channels : %d\n", decodedFrame->channels);
			}

			// AVFrame의 내부 버퍼만 해제합니다.
			av_frame_unref(decodedFrame);
		}

		av_free_packet(&packet);
	}

	//av_frame_alloc으로 할당된 AVFrame을 해제
	av_frame_free(&decodedFrame);

	AVRelease();

	return 0;
}