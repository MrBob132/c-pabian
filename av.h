#include <libavutil/channel_layout.h>
#include <libavutil/common.h>
#include <libavutil/frame.h>
#include <libavutil/samplefmt.h>
// compatibility with newer API
#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(55,28,1)
#define av_frame_alloc avcodec_alloc_frame
#define av_frame_free avcodec_free_frame
#endif

#define SDL_AUDIO_BUFFER_SIZE 1024
#define MAX_AUDIO_FRAME_SIZE 192000

#define MAX_AUDIOQ_SIZE (5 * 16 * 1024)
#define MAX_VIDEOQ_SIZE (5 * 256 * 1024)

#define FF_REFRESH_EVENT (SDL_USEREVENT)
#define FF_QUIT_EVENT (SDL_USEREVENT + 1)

#define VIDEO_PICTURE_QUEUE_SIZE 1


typedef struct VideoPicture {
  SDL_Overlay 	*bmp;
  int 			width, height; /* source height & width */
  int 			allocated;
} VideoPicture;

typedef struct AVFrameNode {//video
	AVFrame 	frame;
	struct AVFrameNode *prev;
} AVFrameNode;

typedef struct AVFrameQueue {//video
	AVFrameNode 	*head;
	AVFrameNode 	*tail;
	SDL_mutex 		*mutex;
  	SDL_cond 		*cond;
} AVFrameQueue;

typedef struct VideoState {
	uint16_t 		width, height;
	uint8_t         audio_buf[(MAX_AUDIO_FRAME_SIZE * 3) / 2];
	unsigned int    audio_buf_size;
	unsigned int    audio_buf_index;
	AVFrame         audio_frame;
	AVStream        *video_st;
	struct SwsContext *sws_ctx;

	VideoPicture    pictq[VIDEO_PICTURE_QUEUE_SIZE];
	int             pictq_size, pictq_rindex, pictq_windex;
	SDL_mutex       *pictq_mutex;
	SDL_cond        *pictq_cond;

	SDL_Thread      *parse_tid;
	SDL_Thread      *video_tid;

	AVFrameQueue 	*audio_frame_queue;
	AVFrameQueue 	*video_frame_queue;
	
	int             quit;
} VideoState;


AVFrameQueue *AVFrame_queue_init(AVFrameQueue *q);

int AVFrame_queue_put(bool queue, AVFrame *frame, VideoState *is);

int AVFrame_queue_get(bool queue, AVFrame *frame, int block, VideoState *is);


int pcm2avframe(int nb_samples, const int16_t *recieve_frame, VideoState *is);//not used

int audio_frame_to_buffer(VideoState *is, uint8_t *audio_buf, int buf_size);//not used

void audio_callback(void *userdata, Uint8 *stream, int len);//not used


//check agian for mistakes!!!!!!!!!!
int video_frame_to_queue(uint16_t width, uint16_t height, const uint8_t *y, const uint8_t *u, const uint8_t *v, int32_t ystride, int32_t ustride, int32_t vstride, VideoState *is);


static Uint32 sdl_refresh_timer_cb(Uint32 interval, void *opaque);

/* schedule a video refresh in 'delay' ms */
static void schedule_refresh(VideoState *is, int delay);

void video_display(VideoState *is);

void video_refresh_timer(void *userdata);
      
void alloc_picture(void *userdata);

int queue_picture(VideoState *is, AVFrame *pFrame);

int video_thread(void *arg);

int stream_component_open(VideoState *is, int sample_rate, int type, uint16_t width, uint16_t height);

int event_loop(void *arg);

int destroy_av(VideoState *is);

VideoState *initialize_av_out(uint32_t sampling_rate, uint16_t width, uint16_t height);

