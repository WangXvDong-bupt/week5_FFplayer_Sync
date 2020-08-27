#pragma once
#ifndef PLAYER_H
#define PLAYER_H

#define SFM_REFRESH_EVENT  (SDL_USEREVENT + 1)
#define SFM_BREAK_EVENT  (SDL_USEREVENT + 2)

extern "C"
{
	//��װ��ʽ
#include "libavformat/avformat.h"
	//����
#include "libavcodec/avcodec.h"
	//����
#include "libswscale/swscale.h"
#include "libswresample/swresample.h"
#include "libavutil/imgutils.h"
	//����
#include "SDL2/SDL.h"
};

class Player
{
public:
	Player();
	int Player_Quit();
	int openFile(char* filepath, int type);
	//virtual int play() = 0;
protected:
	AVFormatContext* pFormatCtx;
	AVCodecContext* pCodeCtx;
	AVCodec* pCodec;
	int index;
};

#endif