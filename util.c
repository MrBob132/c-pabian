#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <SDL.h>
#include <SDL_thread.h>
#include <stdbool.h>

#include "av.h"
#include "Pabian.h"
#include "util.h"

/*******************************************************************************
 * Global variables
 ******************************************************************************/
extern struct Request *requests;
extern struct File_Transfer *file_transfers;
extern struct Friend *friends;
extern struct Call *calls;
extern struct Friend self;
extern struct Group *groups;
extern uint32_t TalkingTo;

extern Tox *tox;



/*******************************************************************************
 *
 * Utils
 *
 ******************************************************************************/
bool str2uint(char *str, uint32_t *num) {
    char *str_end;
    long l = strtol(str,&str_end,10);
    if (str_end == str || l < 0 ) return false;
    *num = (uint32_t)l;
    return true;
}

char* genmsg(struct ChatHist **pp, const char *fmt, ...) {
    va_list va;
    va_start(va, fmt);

    va_list va2;
    va_copy(va2, va);
    size_t len = vsnprintf(NULL, 0, fmt, va2);
    va_end(va2);

    struct ChatHist *h = malloc(sizeof(struct ChatHist));
    h->prev = NULL;
    h->next = (*pp);
    if (*pp) (*pp)->prev = h;
    *pp = h;
    h->msg = malloc(len+1);

    vsnprintf(h->msg, len+1, fmt, va);
    va_end(va);

    return h->msg;
}

char* getftime(void) {
    static char timebuf[64];

    time_t tt = time(NULL);
    struct tm *tm = localtime(&tt);
    strftime(timebuf, sizeof(timebuf), "%H:%M:%S", tm);
    return timebuf;
}

const char * connection_enum2text(TOX_CONNECTION conn) {
    switch (conn) {
        case TOX_CONNECTION_NONE:
            return "Offline";
        case TOX_CONNECTION_TCP:
            return "Online(TCP)";
        case TOX_CONNECTION_UDP:
            return "Online(UDP)";
        default:
            return "UNKNOWN";
    }
}

struct Friend *getfriend(uint32_t friend_num) {
    struct Friend **p = &friends;
    LIST_FIND(p, (*p)->friend_num == friend_num);
    return *p;
}

struct Friend *addfriend(uint32_t friend_num) {
    struct Friend *f = calloc(1, sizeof(struct Friend));
    f->next = friends;
    friends = f;
    f->friend_num = friend_num;
    f->connection = TOX_CONNECTION_NONE;
    tox_friend_get_public_key(tox, friend_num, f->pubkey, NULL);
    return f;
}


bool delfriend(uint32_t friend_num) {
    struct Friend **p = &friends;
    LIST_FIND(p, (*p)->friend_num == friend_num);
    struct Friend *f = *p;
    if (f) {
        *p = f->next;
        if (f->name) free(f->name);
        if (f->status_message) free(f->status_message);
        while (f->hist) {
            struct ChatHist *tmp = f->hist;
            f->hist = f->hist->next;
            free(tmp);
        }
        free(f);
        return 1;
    }
    return 0;
}

struct Group *addgroup(uint32_t group_num) {
    struct Group *cf = calloc(1, sizeof(struct Group));
    cf->next = groups;
    groups = cf;

    cf->group_num = group_num;

    return cf;
}

bool delgroup(uint32_t group_num) {
    struct Group **p = &groups;
    LIST_FIND(p, (*p)->group_num == group_num);
    struct Group *cf = *p;
    if (cf) {
        *p = cf->next;
        if (cf->peers) free(cf->peers);
        if (cf->title) free(cf->title);
        while (cf->hist) {
            struct ChatHist *tmp = cf->hist;
            cf->hist = cf->hist->next;
            free(tmp);
        }
        free(cf);
        return 1;
    }
    return 0;
}

struct Group *getgroup(uint32_t group_num) {
    struct Group **p = &groups;
    LIST_FIND(p, (*p)->group_num == group_num);
    return *p;
}

uint8_t *hex2bin(const char *hex)
{
    size_t len = strlen(hex) / 2;
    uint8_t *bin = malloc(len);

    for (size_t i = 0; i < len; ++i, hex += 2) {
        sscanf(hex, "%2hhx", &bin[i]);
    }

    return bin;
}

struct File_Transfer *gettransfer(uint32_t friend_num, uint32_t file_num) {
    struct File_Transfer **p = &file_transfers;
    LIST_FIND(p, (*p)->friend_num == friend_num && (*p)->file_num == file_num);
    return *p;
}

struct File_Transfer *addtransfer(uint32_t friend_num, uint32_t file_num, uint64_t position, size_t filename_length, uint32_t kind, uint64_t file_size, uint8_t *filename, FILE *fd, uint8_t *file_id) {
    struct File_Transfer *t = calloc(1, sizeof(struct File_Transfer));
    t->next = file_transfers;
    file_transfers = t;
    t->friend_num = friend_num;
    t->file_num = file_num;
    t->position = position;
    t->filename_length = filename_length;
    t->kind = kind;
    t->file_size = file_size;
    t->filename = filename;
    t->fd = fd;
    return t;
}

bool deltransfer(uint32_t friend_num, uint32_t file_num) {
    struct File_Transfer **p = &file_transfers;
    LIST_FIND(p, (*p)->friend_num == friend_num && (*p)->file_num == file_num);
    struct File_Transfer *t = *p;
    if (t) {
        *p = t->next;
        if (t->fd) fclose(t->fd);
        free(t);
        return 1;
    }
    return 0;
}

struct Call *getcall(uint32_t friend_num) {
    struct Call **p = &calls;
    LIST_FIND(p, (*p)->friend_num == friend_num);
    return *p;
}

bool delcall(uint32_t friend_num) {
    struct Call **p = &calls;
    LIST_FIND(p, (*p)->friend_num == friend_num);
    struct Call *c = *p;
    if (c) {
        *p = c->next;
        free(c);
        return 1;
    }
    return 0;
}

struct Call *addcall(uint32_t friend_number, bool audio_enabled, bool video_enabled) {
	if (getcall(friend_number)){
		WARN("a friend calls twise without hanging");
		delcall(friend_number);
		//a friend calls twise without hanging
	}
    struct Call *c = calloc(1, sizeof(struct Call));
    c->next = calls;
    calls = c;
    c->friend_num = friend_number;
    c->audio_enabled = audio_enabled;
    c->video_enabled = video_enabled;
    c->in_call = false;
    c->end = false;
    c->is = NULL;
    if (audio_enabled) c->audio_bit_rate = 48;
    if (video_enabled) c->video_bit_rate = 5000;
    return c;
}

char *bin2hex(const uint8_t *bin, size_t length) {
    char *hex = malloc(2*length + 1);
    char *saved = hex;
    for (int i=0; i<length;i++,hex+=2) {
        sprintf(hex, "%02X",bin[i]);
    }
    return saved;
}

struct ChatHist ** get_current_histp(void) {
    if (TalkingTo == TALK_TYPE_NULL) return NULL;
    uint32_t num = INDEX_TO_NUM(TalkingTo);
    switch (INDEX_TO_TYPE(TalkingTo)) {
        case TALK_TYPE_FRIEND: {
            struct Friend *f = getfriend(num);
            if (f) return &f->hist;
            break;
        }
        case TALK_TYPE_GROUP: {
            struct Group *cf = getgroup(num);
            if (cf) return &cf->hist;
            break;
       }
    }
    return NULL;
}
