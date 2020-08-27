
#include "VideoPlayer.h"
#include <thread>
#include <string>
#include <iostream>

using std::cout;
using std::endl;
using std::string;
using std::stringstream;

VideoPlayer::VideoPlayer()
{

}

VideoPlayer::~VideoPlayer()
{

}

void VideoPlayer::showInfo()
{
	printf("---------------- File Information ---------------\n");
	av_dump_format(pFormatCtx, 0, "TBBT.mp4", 0);
	printf("-------------------------------------------------\n");
}


int VideoPlayer::setWindow()
{
	//SDL 2.0 Support for multiple windows
	/*screen_w = pCodeCtx->width;
	screen_h = pCodeCtx->height;*/
	int screen_w = pCodeCtx->width;
	int screen_h = pCodeCtx->height;
	screen = SDL_CreateWindow("Simplest ffmpeg player's Window", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
		screen_w, screen_h, SDL_WINDOW_OPENGL);
	if (!screen) {
		printf("SDL: could not create window - exiting:%s\n", SDL_GetError());
		return -1;
	}
	sdlRenderer = SDL_CreateRenderer(screen, -1, 0);
	sdlTexture = SDL_CreateTexture(sdlRenderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, pCodeCtx->width, pCodeCtx->height);
}


int VideoPlayer::sfp_refresh_thread(int timeInterval, bool& exitRefresh, bool& faster) {
	while (!exitRefresh)
	{
		SDL_Event event;
		event.type = SFM_REFRESH_EVENT;
		SDL_PushEvent(&event);
		//SDL_Delay(60);
		if (faster) {
			//std::this_thread::sleep_for(std::chrono::milliseconds(timeInterval / 2));
			std::this_thread::sleep_for(std::chrono::milliseconds(timeInterval)/2);
		}
		else {
			std::this_thread::sleep_for(std::chrono::milliseconds(timeInterval));
		}
	}
	return 0;
}

/*double VideoPlayer::getFrameRate() const{
	if (pCodeCtx != nullptr) {
		auto frameRate = pCodeCtx->framerate;
		//double fr = frameRate.num && frameRate.den ? av_q2d(frameRate) : 0.0;
		double fr = av_q2d(frameRate);
		return fr;
	}
	else {
		//throw std::runtime_error("can not getFrameRate.");
		return 1.0;
	}
}*/
double VideoPlayer::getFrameRate() const {
	if (pFormatCtx != nullptr) {
		AVStream* stream = pFormatCtx->streams[index];
		double frameRate = (double)stream->avg_frame_rate.num / stream->avg_frame_rate.den;
		//double fr = frameRate.num && frameRate.den ? av_q2d(frameRate) : 0.0;
		//double fr = av_q2d(frameRate);
		return frameRate;
	}
	else {
		//throw std::runtime_error("can not getFrameRate.");
		return 0.0;
	}
}


int VideoPlayer::play(uint64_t* pts_audio)
{
	AVPacket* packet = (AVPacket*)av_malloc(sizeof(AVPacket));
	//内存分配
	AVFrame* pFrame = av_frame_alloc();
	AVFrame* pFrameYUV = av_frame_alloc();
	//缓冲区分配内存
	unsigned char* out_buffer = (unsigned char*)av_malloc(av_image_get_buffer_size(AV_PIX_FMT_YUV420P, pCodeCtx->width, pCodeCtx->height, 1));
	//初始化缓冲区
	av_image_fill_arrays(pFrameYUV->data, pFrameYUV->linesize, out_buffer,
		AV_PIX_FMT_YUV420P, pCodeCtx->width, pCodeCtx->height, 1);
	img_convert_ctx = sws_getContext(pCodeCtx->width, pCodeCtx->height, pCodeCtx->pix_fmt,
		pCodeCtx->width, pCodeCtx->height, AV_PIX_FMT_YUV420P, SWS_BICUBIC, NULL, NULL, NULL);

	int ret = -1, got_picture = -1;		//


	auto frameRate = getFrameRate();
	bool exitRefresh = false;
	bool faster = false;
	std::thread refreshThread{ sfp_refresh_thread, (int)(1000 / frameRate), std::ref(exitRefresh),
							  std::ref(faster) };



	//SDL_CreateThread(sfp_refresh_thread, NULL, NULL);
	while (av_read_frame(pFormatCtx, packet) >= 0)
	{
		if (packet->stream_index == index)
		{
			//ret = avcodec_decode_video2(pCodeCtx, pFrame, &got_picture, packet);
			ret = avcodec_send_packet(pCodeCtx, packet);
			got_picture = avcodec_receive_frame(pCodeCtx, pFrame);

			if (ret < 0) {
				printf("Decode Error.\n");
			}
			if (!got_picture)		//got_picture为0说明成功从packet解码出fream
			{
				sws_scale(img_convert_ctx, (const unsigned char* const*)pFrame->data, pFrame->linesize, 0, pCodeCtx->height, pFrameYUV->data, pFrameYUV->linesize);
			}

			while (1)
			{
				SDL_WaitEvent(&event);
				if (event.type == SFM_REFRESH_EVENT)
				{

					//if (ptsAudio != nullptr) {
					if (true) {
						uint64_t vTs = av_frame_get_best_effort_timestamp(pFrame) * av_q2d(pFormatCtx->streams[index]->time_base) * 1000;

						uint64_t aTs = *pts_audio;
						if (vTs > aTs && vTs - aTs > 30) {
							cout << "VIDEO FASTER ================= vTs - aTs [" << (vTs - aTs)
								<< "]ms, SKIP A EVENT" << aTs << "---" << vTs << endl;
							// skip a REFRESH_EVENT
							faster = false;
							//continue;
						}
						else if (vTs < aTs && aTs - vTs > 30) {
							cout << "VIDEO SLOWER ================= aTs - vTs =[" << (aTs - vTs) << "]ms, Faster" << aTs << "---" << vTs
								<< endl;
							faster = true;
						}
						else {
							faster = false;
						}
					}

					//SDL---------------------------
					SDL_UpdateTexture(sdlTexture, NULL, pFrameYUV->data[0], pFrameYUV->linesize[0]);
					SDL_RenderClear(sdlRenderer);
					SDL_RenderCopy(sdlRenderer, sdlTexture, NULL, NULL);
					SDL_RenderPresent(sdlRenderer);
					//SDL End-----------------------
					break;

				}
			}
		}
	}
	sws_freeContext(img_convert_ctx);
	av_free(out_buffer);
	av_packet_unref(packet);
	//av_free_packet(packet);
	av_frame_free(&pFrameYUV);
	av_frame_free(&pFrame);
	SDL_DestroyTexture(sdlTexture);
	SDL_DestroyRenderer(sdlRenderer);
	SDL_DestroyWindow(screen);
	return 0;
}


