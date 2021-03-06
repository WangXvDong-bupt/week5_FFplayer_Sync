
#include "AudioPlayer.h"
#include <iostream>

#include "OpenAL/include/alc.h"
#include "OpenAL/include/al.h"
#include <chrono>
#include <thread>
#include <string>
#include <iomanip>

#define RATE 48000
#define NUMBUFFERS (12)
#define SERVICE_UPDATE_PERIOD (20)

using std::cout;
using std::endl;
using std::string;
using std::stringstream;
AudioPlayer::AudioPlayer()
{

}


AudioPlayer::~AudioPlayer()
{
	SDL_CloseAudio();//关闭音频设备 
	swr_free(&swrCtx);
}


void AudioPlayer::audioSetting()
{
	//重采样设置选项-----------------------------------------------------------start
	//输入的采样格式
	in_sample_fmt = pCodeCtx->sample_fmt;
	//输出的采样格式 16bit PCM
	out_sample_fmt = AV_SAMPLE_FMT_S16;
	//输入的采样率
	in_sample_rate = pCodeCtx->sample_rate;
	//输出的采样率
	out_sample_rate = pCodeCtx->sample_rate;
	//输入的声道布局
	in_ch_layout = pCodeCtx->channel_layout;
	if (in_ch_layout <= 0)
	{
		in_ch_layout = av_get_default_channel_layout(pCodeCtx->channels);
	}
	//输出的声道布局
	out_ch_layout = AV_CH_LAYOUT_STEREO;			//与SDL播放不同

	//frame->16bit 44100 PCM 统一音频采样格式与采样率
	swrCtx = swr_alloc();
	swr_alloc_set_opts(swrCtx, out_ch_layout, out_sample_fmt, out_sample_rate, in_ch_layout, in_sample_fmt,
		in_sample_rate, 0, NULL);
	swr_init(swrCtx);
	//重采样设置选项-----------------------------------------------------------end

	//获取输出的声道个数
	out_channel_nb = av_get_channel_layout_nb_channels(out_ch_layout);
}

int AudioPlayer::setAudioSDL()
{
	//获取输出的声道个数
	out_channel_nb = av_get_channel_layout_nb_channels(out_ch_layout);
	//SDL_AudioSpec
	wanted_spec.freq = out_sample_rate;
	wanted_spec.format = AUDIO_S16SYS;
	wanted_spec.channels = out_channel_nb;
	wanted_spec.silence = 0;
	wanted_spec.samples = pCodeCtx->frame_size;
	wanted_spec.callback = fill_audio;//回调函数
	wanted_spec.userdata = pCodeCtx;
	if (SDL_OpenAudio(&wanted_spec, NULL) < 0) {
		printf("can't open audio.\n");
		return -1;
	}
}

static Uint8* audio_chunk;
//设置音频数据长度
static Uint32 audio_len;
static Uint8* audio_pos;

void  AudioPlayer::fill_audio(void* udata, Uint8* stream, int len) {
	//SDL 2.0
	SDL_memset(stream, 0, len);
	if (audio_len == 0)		//有数据才播放
		return;
	len = (len > audio_len ? audio_len : len);

	SDL_MixAudio(stream, audio_pos, len, SDL_MIX_MAXVOLUME);
	audio_pos += len;
	audio_len -= len;
}

int AudioPlayer::allocDataBuf(uint8_t** outData, int inputSamples) {
	int bytePerOutSample = -1;
	switch (out_sample_fmt) {
	case AV_SAMPLE_FMT_U8:
		bytePerOutSample = 1;
		break;
	case AV_SAMPLE_FMT_S16P:
	case AV_SAMPLE_FMT_S16:
		bytePerOutSample = 2;
		break;
	case AV_SAMPLE_FMT_S32:
	case AV_SAMPLE_FMT_S32P:
	case AV_SAMPLE_FMT_FLT:
	case AV_SAMPLE_FMT_FLTP:
		bytePerOutSample = 4;
		break;
	case AV_SAMPLE_FMT_DBL:
	case AV_SAMPLE_FMT_DBLP:
	case AV_SAMPLE_FMT_S64:
	case AV_SAMPLE_FMT_S64P:
		bytePerOutSample = 8;
		break;
	default:
		bytePerOutSample = 2;
		break;
	}

	int guessOutSamplesPerChannel =
		av_rescale_rnd(inputSamples, out_sample_rate, in_sample_rate, AV_ROUND_UP);
	int guessOutSize = guessOutSamplesPerChannel * out_sample_rate * bytePerOutSample;

	std::cout << "GuessOutSamplesPerChannel: " << guessOutSamplesPerChannel << std::endl;
	std::cout << "GuessOutSize: " << guessOutSize << std::endl;

	guessOutSize *= 1.2;  // just make sure.

	//*outData = (uint8_t*)av_malloc(sizeof(uint8_t) * guessOutSize);
	*outData = (uint8_t*)av_malloc(2 * out_sample_rate);

	// av_samples_alloc(&outData, NULL, outChannels, guessOutSamplesPerChannel,
	// AV_SAMPLE_FMT_S16, 0);
	return guessOutSize;
}



bool fileGotToEnd = false;
int AudioPlayer::feedAudioData(ALuint uiSource, ALuint alBufferId) {
	int ret = -1;

	uint8_t* outBuffer = (uint8_t*)av_malloc(2 * out_sample_rate);		//
	static int outBufferSize = 0;
	AVPacket* packet = (AVPacket*)av_malloc(sizeof(AVPacket));
	static AVFrame* aFrame = av_frame_alloc();

	while (true) {
		while (!fileGotToEnd) {
			if (av_read_frame(pFormatCtx, packet) >= 0) {
				ret = avcodec_send_packet(pCodeCtx, packet);
				if (ret == 0) {
					av_packet_unref(packet);
					break;
				}
			}
			else {
				fileGotToEnd = true;
				if (pCodeCtx != nullptr) avcodec_send_packet(pCodeCtx, nullptr);
			}
		}

		ret = -1;
		ret = avcodec_receive_frame(pCodeCtx, aFrame);

		if (ret == 0) {
			ret = 2;
			//ptsAudio = av_q2d(pFormatCtx->streams[index]->time_base) * packet->pts * 1000;
			ptsAudio = av_q2d(pFormatCtx->streams[index]->time_base) * aFrame->pts * 1000;
			cout << "ptsAudio:" << ptsAudio << "  time_base:" << av_q2d(pFormatCtx->streams[index]->time_base)
				<< "  aFrame->pts" << aFrame->pts << endl;
			break;
		}
		else if (ret == AVERROR_EOF) {
			cout << "no more output frames." << endl;
			ret = 0;
			break;
		}
		else if (ret == AVERROR(EAGAIN)) {
			continue;
		}
		else continue;
	}

	if (ret == 2) {
		if (outBuffer == nullptr) {
			outBufferSize = allocDataBuf(&outBuffer, aFrame->nb_samples);
		}
		else {
			memset(outBuffer, 0, outBufferSize);
		}

		int outSamples;
		int outDataSize;

		outSamples = swr_convert(swrCtx, &outBuffer, 2 * out_sample_rate,
			(const uint8_t**)aFrame->data, aFrame->nb_samples);
		if (outSamples <= 0) {
			//throw std::runtime_error("error: outSamples=" + outSamples);
		}

		outDataSize = av_samples_get_buffer_size(NULL, out_channel_nb, outSamples, out_sample_fmt, 1);

		if (outDataSize <= 0) {
			//throw std::runtime_error("error: outDataSize=" + outDataSize);
		}

		unsigned long ulFormat = 0;

		if (aFrame->channels == 1) {
			ulFormat = AL_FORMAT_MONO16;
		}
		else {
			ulFormat = AL_FORMAT_STEREO16;
		}
		alBufferData(alBufferId, ulFormat, outBuffer, outDataSize, out_sample_rate);
		alSourceQueueBuffers(uiSource, 1, &alBufferId);
	}

	return 0;
}

uint64_t AudioPlayer::getPts() {
	return ptsAudio;
}


int AudioPlayer::playByOpenAL(uint64_t* pts_audio)
{
	//--------初始化openAL--------
	ALCdevice* pDevice = alcOpenDevice(NULL);
	ALCcontext* pContext = alcCreateContext(pDevice, NULL);
	alcMakeContextCurrent(pContext);
	if (alcGetError(pDevice) != ALC_NO_ERROR)
		return AL_FALSE;
	ALuint source;
	alGenSources(1, &source);
	if (alGetError() != AL_NO_ERROR)
	{
		printf("Couldn't generate audio source\n");
		return -1;
	}
	ALfloat SourcePos[] = { 0.0,0.0,0.0 };
	ALfloat SourceVel[] = { 0.0,0.0,0.0 };
	ALfloat ListenerPos[] = { 0.0,0.0 };
	ALfloat ListenerVel[] = { 0.0,0.0,0.0 };
	ALfloat ListenerOri[] = { 0.0,0.0,-1.0,0.0,1.0,0.0 };
	alSourcef(source, AL_PITCH, 1.0);
	alSourcef(source, AL_GAIN, 1.0);
	alSourcefv(source, AL_POSITION, SourcePos);
	alSourcefv(source, AL_VELOCITY, SourceVel);
	alSourcef(source, AL_REFERENCE_DISTANCE, 50.0f);
	alSourcei(source, AL_LOOPING, AL_FALSE);
	//--------初始化openAL--------
	alDistanceModel(AL_LINEAR_DISTANCE_CLAMPED);
	alListener3f(AL_POSITION, 0, 0, 0);

	ALuint alBufferArray[NUMBUFFERS];

	alGenBuffers(NUMBUFFERS, alBufferArray);

	// feed audio buffer first time.
	for (int i = 0; i < NUMBUFFERS; i++) {
		feedAudioData(source, alBufferArray[i]);
		//p.set_value(std::ref(ptsAudio));
		*pts_audio = ptsAudio;
	}

	// Start playing source
	alSourcePlay(source);


	ALint iTotalBuffersProcessed = 0;
	ALint iBuffersProcessed;
	ALint iState;
	ALuint bufferId;
	ALint iQueuedBuffers;
	while (true) {
		std::this_thread::sleep_for(std::chrono::milliseconds(SERVICE_UPDATE_PERIOD));

		// Request the number of OpenAL Buffers have been processed (played) on
		// the Source
		iBuffersProcessed = 0;
		alGetSourcei(source, AL_BUFFERS_PROCESSED, &iBuffersProcessed);
		iTotalBuffersProcessed += iBuffersProcessed;

		// Keep a running count of number of buffers processed (for logging
		// purposes only)
		iTotalBuffersProcessed += iBuffersProcessed;
		// cout << "Total Buffers Processed: " << iTotalBuffersProcessed << endl;
		// cout << "Processed: " << iBuffersProcessed << endl;

		// For each processed buffer, remove it from the Source Queue, read next
		// chunk of audio data from disk, fill buffer with new data, and add it
		// to the Source Queue
		while (iBuffersProcessed > 0) {
			bufferId = 0;
			alSourceUnqueueBuffers(source, 1, &bufferId);
			feedAudioData(source, bufferId);
			//p.set_value(std::ref(ptsAudio));
			*pts_audio = ptsAudio;
			iBuffersProcessed -= 1;
		}

		// Check the status of the Source.  If it is not playing, then playback
		// was completed, or the Source was starved of audio data, and needs to
		// be restarted.
		alGetSourcei(source, AL_SOURCE_STATE, &iState);

		if (iState != AL_PLAYING) {
			// If there are Buffers in the Source Queue then the Source was
			// starved of audio data, so needs to be restarted (because there is
			// more audio data to play)
			alGetSourcei(source, AL_BUFFERS_QUEUED, &iQueuedBuffers);
			if (iQueuedBuffers) {
				alSourcePlay(source);
			}
			else {
				// Finished playing
				break;
			}
		}
	}


	return 0;
}



int AudioPlayer::play()
{
	return 0;
}
/*int AudioPlayer::play()
{
	//编码数据
	AVPacket* packet = (AVPacket*)av_malloc(sizeof(AVPacket));
	//av_init_packet(packet);		//初始化
	//解压缩数据
	AVFrame* frame = av_frame_alloc();
	//存储pcm数据
	uint8_t* out_buffer = (uint8_t*)av_malloc(2 * RATE);

	int ret, got_frame, framecount = 0;
	//一帧一帧读取压缩的音频数据AVPacket

	while (av_read_frame(pFormatCtx, packet) >= 0) {
		if (packet->stream_index == index) {
				//解码AVPacket->AVFrame
			ret = avcodec_decode_audio4(pCodeCtx, frame, &got_frame, packet);

			//ret = avcodec_send_packet(pCodeCtx, packet);
			//if (ret != 0) { printf("%s/n", "error"); }
			//got_frame = avcodec_receive_frame(pCodeCtx, frame);			//output=0》success, a frame was returned
			//while ((got_frame = avcodec_receive_frame(pCodeCtx, frame)) == 0) {
			//		//读取到一帧音频或者视频
			//		//处理解码后音视频 frame
			//		swr_convert(swrCtx, &out_buffer, 2 * 44100, (const uint8_t**)frame->data, frame->nb_samples);
			//}


			if (ret < 0) {
				 printf("%s", "解码完成");
			}

			//非0，正在解码
			int out_buffer_size;
			if (got_frame) {
				//printf("解码%d帧", framecount++);
				swr_convert(swrCtx, &out_buffer, 2 * 44100, (const uint8_t**)frame->data, frame->nb_samples);
				//获取sample的size
				out_buffer_size = av_samples_get_buffer_size(NULL, out_channel_nb, frame->nb_samples,
					out_sample_fmt, 1);
				//设置音频数据缓冲,PCM数据
				audio_chunk = (Uint8*)out_buffer;
				//设置音频数据长度
				audio_len = out_buffer_size;
				audio_pos = audio_chunk;
				//回放音频数据
				SDL_PauseAudio(0);
				while (audio_len > 0)//等待直到音频数据播放完毕!
					SDL_Delay(10);
				packet->data += ret;
				packet->size -= ret;
			}
		}
		//av_packet_unref(packet);
	}

	av_free(out_buffer);
	av_frame_free(&frame);
	//av_free_packet(packet);
	av_packet_unref(packet);
	SDL_CloseAudio();//关闭音频设备
	swr_free(&swrCtx);
	return 0;
}*/
