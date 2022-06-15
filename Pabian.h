#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <stdarg.h>
#include <pthread.h>
#include <signal.h>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <tox/toxav.h>
#include <tox/tox.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>

#include <SDL.h>
#include <SDL_thread.h>




/*******************************************************************************
 *
 * Consts & Macros
 *
 ******************************************************************************/

// where to save the tox data.
// if don't want to save, set it to NULL.
static const char *savedata_filename = "./savedata.tox";
static const char *savedata_tmp_filename = "./savedata.tox.tmp";

static const char *DOWNLOAD_DICTIONARY = "./down/";

struct DHT_node {
    const char *ip;
    uint16_t port;
    const char key_hex[TOX_PUBLIC_KEY_SIZE*2 + 1];
};

static const struct DHT_node bootstrap_nodes[] = {

    // Setup tox bootrap nodes

    {"node.tox.biribiri.org",      33445, "F404ABAA1C99A9D37D61AB54898F56793E1DEF8BD46B1038B9D822E8460FAB67"},
    {"128.199.199.197",            33445, "B05C8869DBB4EDDD308F43C1A974A20A725A36EACCA123862FDE9945BF9D3E09"},
    {"2400:6180:0:d0::17a:a001",   33445, "B05C8869DBB4EDDD308F43C1A974A20A725A36EACCA123862FDE9945BF9D3E09"},
    {"141.95.108.234",   33445, "2DEF3156812324B1593A6442C937EAE0A8BD98DE529D2D4A7DD4BA6CB3ECF262"},
};

#define LINE_MAX_SIZE 512  // If input line's length surpassed this value, it will be truncated.

//#define DOWNLOAD_DICTIONARY ""
#define DOWNLOAD_DICTIONARY_LENGTH 2

#define PORT_RANGE_START 33445     // tox listen port range
#define PORT_RANGE_END   34445

#define AREPL_INTERVAL  30  // Async REPL iterate interval. unit: millisecond.

#define DEFAULT_CHAT_HIST_COUNT  20 // how many items of chat history to show by default;

#define SAVEDATA_AFTER_COMMAND true // whether save data after executing any command

/// Macros for terminal display

#define CODE_ERASE_LINE    "\r\033[2K"

#define RESET_COLOR        "\x01b[0m"
#define SELF_TALK_COLOR    "\x01b[35m"  // magenta
#define GUEST_TALK_COLOR   "\x01b[90m" // bright black
#define CMD_PROMPT_COLOR   "\x01b[34m" // blue

#define CMD_PROMPT   CMD_PROMPT_COLOR "> " RESET_COLOR // green
#define FRIEND_TALK_PROMPT  CMD_PROMPT_COLOR "%-.12s << " RESET_COLOR
#define GROUP_TALK_PROMPT  CMD_PROMPT_COLOR "%-.12s <<< " RESET_COLOR

#define GUEST_MSG_PREFIX  GUEST_TALK_COLOR "%s  %12.12s | " RESET_COLOR
#define SELF_MSG_PREFIX  SELF_TALK_COLOR "%s  %12.12s | " RESET_COLOR
#define CMD_MSG_PREFIX  CMD_PROMPT

#define PRINT(_fmt, ...) \
    fputs(CODE_ERASE_LINE,stdout);\
    printf(_fmt "\n", ##__VA_ARGS__);

#define COLOR_PRINT(_color, _fmt,...) PRINT(_color _fmt RESET_COLOR, ##__VA_ARGS__)

#define INFO(_fmt,...) COLOR_PRINT("\x01b[36m", _fmt, ##__VA_ARGS__)  // cyran
#define WARN(_fmt,...) COLOR_PRINT("\x01b[33m", _fmt, ##__VA_ARGS__) // yellow
#define ERROR(_fmt,...) COLOR_PRINT("\x01b[31m", _fmt, ##__VA_ARGS__) // red

/*******************************************************************************
 * Utils
 ******************************************************************************/
#define RESIZE(key, size_key, length) \
    if ((size_key) < (length + 1)) { \
        size_key = (length+1);\
        key = calloc(1, size_key);\
    }

#define LIST_FIND(_p, _condition) \
    for (;*(_p) != NULL;_p = &((*_p)->next)) { \
        if (_condition) { \
            break;\
        }\
    }\

#define INDEX_TO_TYPE(idx) (idx % TALK_TYPE_COUNT)
#define INDEX_TO_NUM(idx)  (idx / TALK_TYPE_COUNT)
#define GEN_INDEX(num,type) (num * TALK_TYPE_COUNT + type)

/*******************************************************************************
 * Async REPL
 ******************************************************************************/
#define _AREPL_CURSOR_LEFT() arepl->line[arepl->sz - (++arepl->nstack)] = arepl->line[--arepl->nbuf]
#define _AREPL_CURSOR_RIGHT() arepl->line[arepl->nbuf++] = arepl->line[arepl->sz - (arepl->nstack--)]


/*******************************************************************************
 *
 * Headers
 *
 ******************************************************************************/

struct AsyncREPL {
    char *line;
    char *prompt;
    size_t sz;
    int  nbuf;
    int nstack;
};

typedef void CommandHandler(int narg, char **args);

struct Command {
    char* name;
    char* desc;
    int   narg;
    CommandHandler *handler;
};

struct GroupUserData {
    uint32_t friend_num;
    uint8_t *cookie;
    size_t length;
};

struct FriendUserData {
    uint8_t pubkey[TOX_PUBLIC_KEY_SIZE];
};

union RequestUserData {
    struct GroupUserData group;
    struct FriendUserData friend;
};

struct Request {
    char *msg;
    uint32_t id;
    bool is_friend_request;
    union RequestUserData userdata;
    struct Request *next;
};

struct ChatHist {
    char *msg;
    struct ChatHist *next;
    struct ChatHist *prev;
};

struct GroupPeer {
    uint8_t pubkey[TOX_PUBLIC_KEY_SIZE];
    char name[TOX_MAX_NAME_LENGTH + 1];
};

struct Group {
    uint32_t group_num;
    char *title;
    struct GroupPeer *peers;
    size_t peers_count;

    struct ChatHist *hist;

    struct Group *next;
};

struct Friend {
    uint32_t friend_num;
    char *name;
    char *status_message;
    uint8_t pubkey[TOX_PUBLIC_KEY_SIZE];
    TOX_CONNECTION connection;

    struct ChatHist *hist;

    struct Friend *next;
};


struct File_Transfer {//add attitude about read or write
	uint32_t friend_num;
	uint32_t file_num;
	uint64_t position;
	uint64_t file_size;
	uint8_t *filename;
	size_t filename_length;
	uint32_t kind;
	FILE *fd;
	
	struct File_Transfer *next;
};

struct Call {
	uint32_t friend_num;
	bool audio_enabled;
	bool video_enabled;
	uint32_t audio_bit_rate;
	uint32_t video_bit_rate;
	bool in_call;
	bool end;
	
	VideoState *is;
	
	struct Call *next;
};


typedef struct AVargs {
	ToxAV *av;
	struct Call *c;
} AVargs;




enum TALK_TYPE { TALK_TYPE_FRIEND, TALK_TYPE_GROUP, TALK_TYPE_COUNT, TALK_TYPE_NULL = UINT32_MAX };



/*******************************************************************************
 *
 * Prodotype
 *
 ******************************************************************************/
/*******************************************************************************
 * Async REPL
 ******************************************************************************/
void arepl_exit(void);

void setup_arepl(void);

void arepl_reprint(struct AsyncREPL *arepl);

int arepl_readline(struct AsyncREPL *arepl, char c, char *line, size_t sz);

/*******************************************************************************
 * Main
 ******************************************************************************/

char *poptok(char **strp);

void repl_iterate(void);

void tox_loop(void *arg);

void toxav_audio_loop(void *arg);

void toxav_video_loop(void *arg);

int main(int argc, char **argv);
