#include <errno.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <syslog.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <SDL.h>
#include <SDL_thread.h>

#include "av.h"
#include "Pabian.h"

#include "capture.h"


extern bool signal_exit;

uint8_t *buffer;
size_t buffer_length;
 
int xioctl(int fd, int request, void *arg)
{
        int r;
 
        do r = ioctl (fd, request, arg);
        while (-1 == r && EINTR == errno);
 
        return r;
}

int print_caps(int fd)
{
        struct v4l2_capability caps = {};
        if (-1 == xioctl(fd, VIDIOC_QUERYCAP, &caps))
        {
                perror("Querying Capabilities");
                return 1;
        }
 
        printf( "Driver Caps:\n"
                "  Driver: \"%s\"\n"
                "  Card: \"%s\"\n"
                "  Bus: \"%s\"\n"
                "  Version: %d.%d\n"
                "  Capabilities: %08x\n",
                caps.driver,
                caps.card,
                caps.bus_info,
                (caps.version>>16)&&0xff,
                (caps.version>>24)&&0xff,
                caps.capabilities);
 
 
        struct v4l2_cropcap cropcap = {0};
        cropcap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (-1 == xioctl (fd, VIDIOC_CROPCAP, &cropcap))
        {
                perror("Querying Cropping Capabilities");
                return 1;
        }
 
        printf( "Camera Cropping:\n"
                "  Bounds: %dx%d+%d+%d\n"
                "  Default: %dx%d+%d+%d\n"
                "  Aspect: %d/%d\n",
                cropcap.bounds.width, cropcap.bounds.height, cropcap.bounds.left, cropcap.bounds.top,
                cropcap.defrect.width, cropcap.defrect.height, cropcap.defrect.left, cropcap.defrect.top,
                cropcap.pixelaspect.numerator, cropcap.pixelaspect.denominator);
 
        int support_grbg10 = 0;
 
        struct v4l2_fmtdesc fmtdesc = {0};
        fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        char fourcc[5] = {0};
        char c, e;
        printf("  FMT : CE Desc\n--------------------\n");
        while (0 == xioctl(fd, VIDIOC_ENUM_FMT, &fmtdesc))
        {
                strncpy(fourcc, (char *)&fmtdesc.pixelformat, 4);
                if (fmtdesc.pixelformat == V4L2_PIX_FMT_SGRBG10)
                    support_grbg10 = 1;
                c = fmtdesc.flags & 1? 'C' : ' ';
                e = fmtdesc.flags & 2? 'E' : ' ';
                printf("  %s: %c%c %s\n", fourcc, c, e, fmtdesc.description);
                fmtdesc.index++;
        }
 
        struct v4l2_format fmt = {0};
        fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        fmt.fmt.pix.width = 640;
        fmt.fmt.pix.height = 480;
        fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;//V4L2_PIX_FMT_YUV422P
        fmt.fmt.pix.field = V4L2_FIELD_NONE;//V4L2_FIELD_INTERLACED
        
        if (-1 == xioctl(fd, VIDIOC_S_FMT, &fmt))
        {
            perror("Setting Pixel Format");
            return 1;
        }
 
        strncpy(fourcc, (char *)&fmt.fmt.pix.pixelformat, 4);
        printf( "Selected Camera Mode:\n"
                "  Width: %d\n"
                "  Height: %d\n"
                "  PixFmt: %s\n"
                "  Field: %d\n",
                fmt.fmt.pix.width,
                fmt.fmt.pix.height,
                fourcc,
                fmt.fmt.pix.field);
        return 0;
}
 
int init_mmap(int fd)
{
    struct v4l2_requestbuffers req = {0};
    req.count = 1;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
 
    if (-1 == xioctl(fd, VIDIOC_REQBUFS, &req))
    {
        perror("Requesting Buffer");
        return 1;
    }
 
    struct v4l2_buffer buf = {0};
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = 0;
    if(-1 == xioctl(fd, VIDIOC_QUERYBUF, &buf))
    {
        perror("Querying Buffer");
        return 1;
    }
 	buffer_length = buf.length;
    buffer = mmap (NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, buf.m.offset);
 
    return 0;
}
 
int capture_image(int fd, videoFrame *vf)
{
    struct v4l2_buffer buf = {0};
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = 0;
    if(-1 == xioctl(fd, VIDIOC_QBUF, &buf))
    {
        perror("Query Buffer");
        return 1;
    }
 
    if(-1 == xioctl(fd, VIDIOC_STREAMON, &buf.type))
    {
        perror("Start Capture");
        return 1;
    }
 
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(fd, &fds);
    struct timeval tv = {0};
    tv.tv_sec = 2;
    int r = select(fd+1, &fds, NULL, NULL, &tv);
    if(-1 == r)
    {
        perror("Waiting for Frame");
        return 1;
    }
 
    if(-1 == xioctl(fd, VIDIOC_DQBUF, &buf))
    {
        perror("Retrieving Frame");
        return 1;
    }
    yuv422to420(vf->y, vf->u, vf->v, buffer, 640, 480);
    vf->width = 640;
    vf->height = 480;
    return 0;
}


void yuv422to420(uint8_t *plane_y, uint8_t *plane_u, uint8_t *plane_v,
                        uint8_t *input, uint16_t width, uint16_t height)
{
    uint8_t *end = input + width * height * 2;

    while (input != end) {
        uint8_t *line_end = input + width * 2;

        while (input != line_end) {
            *plane_y++ = *input++;
            *plane_u++ = *input++;
            *plane_y++ = *input++;
            *plane_v++ = *input++;
        }

        line_end = input + width * 2;

        while (input != line_end) {
            *plane_y++ = *input++;
            input++;//u
            *plane_y++ = *input++;
            input++;//v
        }
    }
}


int uninit_device(){
	if (-1 == munmap(buffer, buffer_length))
		return -1;
	return 1;
}

int get_frame(videoFrame *vf, int initio, int fd)
{
	if (!initio){
		if(print_caps(fd)){
			printf("failed print_caps\n");
			return -1;
		}
		if(init_mmap(fd)){
			printf("failed init_mmap\n");
			return -1;
		}
		capture_image(fd, vf);
		return 1;
	}
	else{
		capture_image(fd, vf);
	}
	return 1;
}

void toxav_video_out_loop(void *arg){
	struct AVargs *argss = (AVargs *)arg;
	ToxAV *toxav = argss->av;
	struct Call *c = argss->c;
	int initio = 0;
	int fd;
	fd = open("/dev/video0", O_RDWR);
	if (fd == -1)
	{
		    perror("Opening video device");
		    return;
	}
	while (!c->end) {
		while (c->in_call) {
			videoFrame *vf = malloc(sizeof(videoFrame));
			vf->y = malloc(640*480);
			vf->u = malloc(640*480/4);
			vf->v = malloc(640*480/4);
			if (initio == 1)
				get_frame(vf, 1, fd);
			else
				initio = get_frame(vf, 0, fd);
			Toxav_Err_Send_Frame err;
			if (c->in_call)
				toxav_video_send_frame(toxav, c->friend_num, vf->width, vf->height, vf->y, vf->u, vf->v, &err);
			
			if (err != TOXAV_ERR_SEND_FRAME_OK){
				ERROR("! sending video frame failed error: %d", err);
				continue;
			}
			INFO("sending video frame success\n")
			free(vf);
		}
	}
	if (argss)
		free(argss);
	if (-1 == uninit_device()){
		ERROR("! failed uninit_device()");
	}
	if (-1 == close(fd))
		ERROR("! failed to close camera");
}
