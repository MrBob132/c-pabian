typedef struct videoFrame {
	uint8_t *y, *u, *v;
	uint16_t width, height;
} videoFrame;

int xioctl(int fd, int request, void *arg);

int print_caps(int fd);

int init_mmap(int fd);

int capture_image(int fd, videoFrame *vf);

void yuv422to420(uint8_t *plane_y, uint8_t *plane_u, uint8_t *plane_v, uint8_t *input, uint16_t width, uint16_t height);

int uninit_device();

int get_frame(videoFrame *vf, int initio, int fd);

void toxav_video_out_loop(void *arg);
