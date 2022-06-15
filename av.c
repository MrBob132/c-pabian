#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>

#include <SDL.h>
#include <SDL_thread.h>

#ifdef __MINGW32__
#undef main /* Prevents SDL from overriding main() */
#endif

#include <stdio.h>
#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "av.h"

SDL_Surface     *screen;
SDL_mutex       *screen_mutex;
SDL_Thread 		*parse_tid;

/* Since we only have one decoding thread, the Big Struct
   can be global in case we need it. */
VideoState *global_video_state;

AVFrameQueue *AVFrame_queue_init(AVFrameQueue *q) {
	q = av_malloc(sizeof(AVFrameQueue));
	q->mutex = SDL_CreateMutex();
	if (q->mutex == NULL){
		fprintf(stdout, "failed to SDL_CreateMutex\n");
	}
	q->cond = SDL_CreateCond();
	if (q->cond == NULL){
		fprintf(stdout, "failed to SDL_CreateCond\n");
	}
	q->head = NULL;
	q->tail = NULL;
	return q;
}

int AVFrame_queue_put(bool queue, AVFrame *frame, VideoState *is1) {
	AVFrameQueue *q;
	VideoState *is = global_video_state;
	if(is->quit)
    	return -1;
	if (is == NULL){
		printf("is == NULL\n");
		return -1;
	}
	if (is->video_frame_queue == NULL){
		printf("is->video_frame_queue == NULL\n");
		return -1;
	}
  	if (queue) {
		q = is->audio_frame_queue;
	}
	else {
		q = is->video_frame_queue;
	}
	AVFrameNode *avframenode;
	avframenode = av_malloc(sizeof(AVFrameNode));
	if (!avframenode){
		return -1;
	}
	avframenode->frame = *frame;
	avframenode->prev = NULL;
	
	SDL_LockMutex(q->mutex);
	
	if (q->head == NULL){
		q->head = avframenode;
	}
	else if (q->head->prev == NULL){
		q->head->prev = avframenode;
		q->tail = avframenode;
	}
	else if (q->tail != NULL){
		q->tail->prev = avframenode;
		q->tail = avframenode;
	}
	else {
		SDL_CondSignal(q->cond);
  		SDL_UnlockMutex(q->mutex);
		return -1;//if head-prev != NULL tail can't be NULL
	}
	SDL_CondSignal(q->cond);
  	SDL_UnlockMutex(q->mutex);
  	return 0;
}

int AVFrame_queue_get(bool queue, AVFrame *frame, int block, VideoState *is)
{
	if (is == NULL){
		printf("AVFrame_queue_get is == NULL\n");
		return -1;
	}
	if (is->video_frame_queue == NULL){
		printf("AVFrame_queue_get is->video_frame_queue == NULL\n");
		return -1;
	}
	AVFrameQueue *q;
	if(is->quit)
    	return -1;
	if (queue) {
		q = is->audio_frame_queue;
	}
	else {
		q = is->video_frame_queue;
	}
	AVFrameNode *avframenode;
	int ret;
	SDL_LockMutex(q->mutex);
	for(;;) {
		if(is->quit) {
			ret = -1;
			break;
		}
		avframenode = q->head;
		if (avframenode) {
			q->head = avframenode->prev;
			if ((!q->head) || (q->head == q->tail))
				q->tail = NULL;
			*frame = avframenode->frame;
			av_free(avframenode);
			ret = 1;
			break;
		} else if (!block) {
			ret = 0;
			break;
		} else {
			SDL_CondWait(q->cond, q->mutex);
		}
	}
	SDL_UnlockMutex(q->mutex);
	return ret;
}

//not used
int pcm2avframe(int nb_samples, const int16_t *recieve_frame, VideoState *is){
	uint16_t *samples;
	float t, tincr;
	int i, j, k, ret;
	AVFrame *frame;
	
	frame = av_frame_alloc();
	if (!frame) {
		fprintf(stderr, "Could not allocate audio frame\n");
		exit(1);
	}
	frame->nb_samples     = nb_samples;
	frame->format         = AV_SAMPLE_FMT_S16 ;
	frame->channel_layout = AV_CH_LAYOUT_MONO;
	frame->channels = 1;

	/* allocate the data buffers */
	ret = av_frame_get_buffer(frame, 0);
	if (ret < 0) {
		fprintf(stderr, "Could not allocate audio data buffers\n");
		exit(1);
	}

	ret = av_frame_make_writable(frame);
	if (ret < 0)
		exit(1);
	samples = (uint16_t*)frame->data[0];
	memcpy(samples, recieve_frame, nb_samples*1*sizeof(int16_t));
	//adding to queue
	if (AVFrame_queue_put(true, frame, is) == -1){
  		printf("Error in AVFrame_queue_put inside pcm2avframe\n");
  		return -1;
		//add error handling
	}
	return 1;
}

//not used
int audio_frame_to_buffer(VideoState *is, uint8_t *audio_buf, int buf_size) {
	int data_size = 0;
	AVFrame *frame = NULL;
	if(is->quit)
    	return -1;
	frame = av_frame_alloc();
	if (!frame) {
		fprintf(stderr, "Could not allocate audio frame\n");
		exit(1);
	}
	
	for(;;) {
		if(is->quit) {
      		return -1;
    	}
    	if (AVFrame_queue_get(true, frame, 1, is) < 0)
				return -1;
		if (frame){
			data_size = av_samples_get_buffer_size(NULL, 
					       frame->channels,
					       frame->nb_samples,
					       frame->format,
					       1);
			assert(data_size >= 0);
			assert(data_size <= buf_size);
			memcpy(audio_buf, frame->data[0], data_size);
			return data_size;
		}
	}
}

//not used
void audio_callback(void *userdata, Uint8 *stream, int len) {

  VideoState *is = (VideoState *)userdata;
  if(is->quit)
    return -1;
  int len1, audio_size;
  while(len > 0) {
    if(is->audio_buf_index >= is->audio_buf_size) {
      /* We have already sent all our data; get more */
      audio_size = audio_frame_to_buffer(is, is->audio_buf, sizeof(is->audio_buf));
      if(audio_size < 0) {
	/* If error, output silence */
	is->audio_buf_size = 1024;
	assert(is->audio_buf_size <= len);
	memset(is->audio_buf, 0, is->audio_buf_size);
      } else {
	is->audio_buf_size = audio_size;
      }
      is->audio_buf_index = 0;
    }
    len1 = is->audio_buf_size - is->audio_buf_index;
    if(len1 > len)
      len1 = len;
    memcpy(stream, (uint8_t *)is->audio_buf + is->audio_buf_index, len1);
    len -= len1;
    stream += len1;
    is->audio_buf_index += len1;
  }
}

/*
	called from the recieve frame callback and is used to store the frame recieved
*/
int video_frame_to_queue(uint16_t width, uint16_t height, const uint8_t *y, const uint8_t *u, const uint8_t *v, int32_t ystride, int32_t ustride, int32_t vstride, VideoState *is) {
	AVFrame* frame = av_frame_alloc();
	frame->width = width;
	frame->height = height;
	frame->format = AV_PIX_FMT_YUV420P;
	
	// Allocate a buffer large enough for all data
	int ret = av_image_alloc(frame->data, frame->linesize, frame->width, frame->height, frame->format, 32);
	if(ret < 0){
 		printf("Alloc Fail\n");
 		return -1;
	}
	// Copy data from the 3 input buffers
	frame->linesize[0] = abs(ystride);
	frame->linesize[1] = abs(ustride);
	frame->linesize[2] = abs(vstride);
	
	memcpy(frame->data[0], y, frame->width * frame->height);
	memcpy(frame->data[1], u, (frame->width / 2) * frame->height / 2);
	memcpy(frame->data[2], v, (frame->width / 2) * frame->height / 2);
	
	if (AVFrame_queue_put(false, frame, is) == -1){
		printf("AVFrame_queue_put Fail\n");
		return -1;
	}
	return 0;
}


static Uint32 sdl_refresh_timer_cb(Uint32 interval, void *opaque) {
  SDL_Event event;
  event.type = FF_REFRESH_EVENT;
  event.user.data1 = opaque;
  SDL_PushEvent(&event);
  return 0; /* 0 means stop timer */
}

/* schedule a video refresh in 'delay' ms */
static void schedule_refresh(VideoState *is, int delay) {
  SDL_AddTimer(delay, sdl_refresh_timer_cb, is);
}

/*
	displaying the frame
*/
void video_display(VideoState *is) {

	SDL_Rect rect;
	VideoPicture *vp;
	float aspect_ratio;
	int w, h, x, y;
	int i;
	if(is->quit)
    	return;
	vp = &is->pictq[is->pictq_rindex];
	if(vp->bmp) {
		aspect_ratio = 0.0;
		aspect_ratio = (float)is->width / (float)is->height;
		h = screen->h;
		w = ((int)rint(h * aspect_ratio)) & -3;
		if(w > screen->w) {
			w = screen->w;
			h = ((int)rint(w / aspect_ratio)) & -3;
		}
		x = (screen->w - w) / 2;
		y = (screen->h - h) / 2;

		rect.x = x;
		rect.y = y;
		rect.w = w;
		rect.h = h;
		SDL_LockMutex(screen_mutex);
		SDL_DisplayYUVOverlay(vp->bmp, &rect);
		SDL_UnlockMutex(screen_mutex);

	}
}

void video_refresh_timer(void *userdata) {

  VideoState *is = (VideoState *)userdata;
  VideoPicture *vp;
  if(is->quit)
    return -1;
  if(is->video_tid) {
    if(is->pictq_size == 0) {
      schedule_refresh(is, 1);
    } else {
      vp = &is->pictq[is->pictq_rindex];
      /* Now, normally here goes a ton of code
	 about timing, etc. we're just going to
	 guess at a delay for now.
      */
      schedule_refresh(is, 50);//40
      
      /* show the picture! */
      video_display(is);
      
      /* update queue for next picture! */
      if(++is->pictq_rindex == VIDEO_PICTURE_QUEUE_SIZE) {
	is->pictq_rindex = 0;
      }
      SDL_LockMutex(is->pictq_mutex);
      is->pictq_size--;
      SDL_CondSignal(is->pictq_cond);
      SDL_UnlockMutex(is->pictq_mutex);
    }
  } else {
    schedule_refresh(is, 100);
  }
}
      
void alloc_picture(void *userdata) {

  VideoState *is = (VideoState *)userdata;
  VideoPicture *vp;

  if(is->quit)
    return;

  vp = &is->pictq[is->pictq_windex];
  if(vp->bmp) {
    // we already have one make another, bigger/smaller
    SDL_FreeYUVOverlay(vp->bmp);
  }
  // Allocate a place to put our YUV image on that screen
  SDL_LockMutex(screen_mutex);
  vp->bmp = SDL_CreateYUVOverlay(is->width,
				 is->height,
				 SDL_YV12_OVERLAY,
				 screen);
  SDL_UnlockMutex(screen_mutex);

  vp->width = is->width;
  vp->height = is->height;
  vp->allocated = 1;

}

int queue_picture(VideoState *is, AVFrame *pFrame) {
	VideoPicture *vp;
	int dst_pix_fmt;
	AVPicture pict;
	
	if(is->quit)
		return -1;
	/* wait until we have space for a new pic */
	SDL_LockMutex(is->pictq_mutex);
	while(is->pictq_size >= VIDEO_PICTURE_QUEUE_SIZE &&
	!is->quit) {
		SDL_CondWait(is->pictq_cond, is->pictq_mutex);
	}
	SDL_UnlockMutex(is->pictq_mutex);


	// windex is set to 0 initially
	vp = &is->pictq[is->pictq_windex];
	/* allocate or resize the buffer! */
	if(!vp->bmp ||
	vp->width != is->width ||
	vp->height != is->height) {
		SDL_Event event;

		vp->allocated = 0;
		alloc_picture(is);
		if(is->quit) {
			return -1;
		}
	}
	/* We have a place to put our picture on the queue */

	if(vp->bmp) {

		SDL_LockYUVOverlay(vp->bmp);

		dst_pix_fmt = AV_PIX_FMT_YUV420P;
		/* point pict at the queue */

		pict.data[0] = vp->bmp->pixels[0];
		pict.data[1] = vp->bmp->pixels[2];
		pict.data[2] = vp->bmp->pixels[1];

		pict.linesize[0] = vp->bmp->pitches[0];
		pict.linesize[1] = vp->bmp->pitches[2];
		pict.linesize[2] = vp->bmp->pitches[1];

		if (is->sws_ctx == NULL){
			printf("is->sws_ctx == NULL\n");
		}
		if (pFrame->data == NULL){
			printf("pFrame->data == NULL\n");
		}
		if (pFrame->linesize == NULL){
			printf("pFrame->linesize == NULL\n");
		}
		// Convert the image into YUV format that SDL uses
		sws_scale(is->sws_ctx, (uint8_t const * const *)pFrame->data,
		pFrame->linesize, 0, 440,
		pict.data, pict.linesize);
		SDL_UnlockYUVOverlay(vp->bmp);
		/* now we inform our display thread that we have a pic ready */
		if(++is->pictq_windex == VIDEO_PICTURE_QUEUE_SIZE) {
			is->pictq_windex = 0;
		}
		SDL_LockMutex(is->pictq_mutex);
		is->pictq_size++;
		SDL_UnlockMutex(is->pictq_mutex);
	}
	return 0;
}

/*
	thread to wueue frames to the buffer
*/
int video_thread(void *arg) {
  VideoState *is = (VideoState *)arg;
  AVFrame *pFrame;

  pFrame = av_frame_alloc();

  for(;;) {
  	if(is->quit){
    	break;
    }
    if (AVFrame_queue_get(false, pFrame, 1, is) < 0)
		return -1;
    // Did we get a video frame?
    if(queue_picture(is, pFrame) < 0) {
		break;
    }      
  }
  av_frame_free(&pFrame);
  return 0;
}

/*
	setting up the audio and video and their thread
*/
int stream_component_open(VideoState *is, int sample_rate, int type, uint16_t width, uint16_t height) {
	SDL_AudioSpec wanted_spec, spec;

	if(type == AVMEDIA_TYPE_AUDIO) {
		// Set audio settings from codec info
		wanted_spec.freq = sample_rate;
		wanted_spec.format = AUDIO_S16SYS;
		wanted_spec.channels = 2;//stereo
		wanted_spec.silence = 0;
		wanted_spec.samples = SDL_AUDIO_BUFFER_SIZE;
		wanted_spec.callback = audio_callback;
		wanted_spec.userdata = is;

		if(SDL_OpenAudio(&wanted_spec, &spec) < 0) {
			fprintf(stderr, "SDL_OpenAudio: %s\n", SDL_GetError());
			return -1;
		}
	}

	switch(type) {
		case AVMEDIA_TYPE_AUDIO:
			is->audio_buf_size = 0;
			is->audio_buf_index = 0;
			SDL_PauseAudio(0);
			break;
		case AVMEDIA_TYPE_VIDEO:
			is->video_tid = SDL_CreateThread(video_thread, is);
			is->sws_ctx = sws_getContext(width, height, AV_PIX_FMT_YUV420P, width, height, AV_PIX_FMT_YUV420P, SWS_BILINEAR, NULL, NULL, NULL);
			break;
		default:
			break;
	}
	return 0;//success
}

/*
	loop to hander events
*/
int event_loop(void *arg) {
	VideoState *is = (VideoState *)arg;
	SDL_Event       event;

	for(;;) {
		SDL_WaitEvent(&event);
		switch(event.type) {
			case FF_QUIT_EVENT:
			case SDL_QUIT:
				return 0;
				break;
			case FF_REFRESH_EVENT:
				video_refresh_timer(event.user.data1);
				break;
			default:
				break;
		}
	}
	return 0;
}

/*
	destoying the video process
*/
int destroy_av(VideoState *is){
	atexit(SDL_Quit);
	SDL_Event event;
  	event.type = SDL_QUIT;
  	SDL_PushEvent(&event);
  	SDL_WaitThread(parse_tid, NULL);
  	is->quit = 1;
  	SDL_Quit();
  	
  	return 1;//success
}

/*
	initializing the audio and video
*/
VideoState *initialize_av_out(uint32_t sampling_rate, uint16_t width, uint16_t height) {
	VideoState *is;
	is = av_mallocz(sizeof(VideoState));
	global_video_state = is;
	
	// Register all formats and codecs
	av_register_all();

	if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) {
		fprintf(stderr, "Could not initialize SDL - %s\n", SDL_GetError());
		return NULL;
	}
	// Make a screen to put our video
#ifndef __DARWIN__
	screen = SDL_SetVideoMode(640, 480, 0, 0);
#else
	screen = SDL_SetVideoMode(640, 480, 30, 0);//24
#endif
	if(!screen) {
		fprintf(stderr, "SDL: could not set video mode - exiting\n");
		return NULL;
	}
	screen_mutex = SDL_CreateMutex();
	is->pictq_size = 0;
	is->audio_frame_queue = AVFrame_queue_init(is->audio_frame_queue);
	is->video_frame_queue = AVFrame_queue_init(is->video_frame_queue);
	is->pictq_mutex = SDL_CreateMutex();
	is->pictq_cond = SDL_CreateCond();
	
	is->height = height;
	is->width = width;

	schedule_refresh(is, 33);//40
	if(sampling_rate != 0) {
    	stream_component_open(is, sampling_rate, AVMEDIA_TYPE_AUDIO, 0, 0);
  	}
  	if(width != 0 && height != 0) {
    	stream_component_open(is, 0, AVMEDIA_TYPE_VIDEO, width, height);
  	}
	
	parse_tid = SDL_CreateThread(event_loop, is);
	if(!parse_tid) {
		fprintf(stderr, "SDL_CreateThread failed\n");
		av_free(is);
		return NULL;
	}
	return is;
}

