#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

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

#include "av.h"
#include "Pabian.h"
#include "callback.h"
#include "capture.h"
#include "setup.h"
#include "util.h"
#include "commands.h"

#ifdef __MINGW32__
#undef main /* Prevents SDL from overriding main() */
#endif


/*******************************************************************************
 * Global variables
 ******************************************************************************/
int NEW_STDIN_FILENO = STDIN_FILENO;
struct Request *requests = NULL;
struct File_Transfer *file_transfers = NULL;
struct Friend *friends = NULL;
struct Call *calls = NULL;
struct Friend self;
struct Group *groups = NULL;
uint32_t TalkingTo = TALK_TYPE_NULL;
bool signal_exit = false;

Tox *tox;
ToxAV *toxav;
pthread_t toxav_video_out_thread;


/*******************************************************************************
 *
 * Async REPL
 *
 ******************************************************************************/

struct termios saved_tattr;

struct AsyncREPL *async_repl;

void arepl_exit(void) {
    tcsetattr(NEW_STDIN_FILENO, TCSAFLUSH, &saved_tattr);
}

void setup_arepl(void) {
    if (!(isatty(STDIN_FILENO) && isatty(STDOUT_FILENO))) {
        fputs("! stdout & stdin should be connected to tty", stderr);
        exit(1);
    }
    async_repl = malloc(sizeof(struct AsyncREPL));
    async_repl->nbuf = 0;
    async_repl->nstack = 0;
    async_repl->sz = LINE_MAX_SIZE;
    async_repl->line = malloc(LINE_MAX_SIZE);
    async_repl->prompt = malloc(LINE_MAX_SIZE);

    strcpy(async_repl->prompt, CMD_PROMPT);

    // stdin and stdout may share the same file obj,
    // reopen stdin to avoid accidentally getting stdout modified.

    char stdin_path[4080];  // 4080 is large enough for a path length for *nix system.
#ifdef F_GETPATH   // macosx
    if (fcntl(STDIN_FILENO, F_GETPATH, stdin_path) == -1) {
        fputs("! fcntl get stdin filepath failed", stderr);
        exit(1);
    }
#else  // linux
    if (readlink("/proc/self/fd/0", stdin_path, sizeof(stdin_path)) == -1) {
        fputs("! get stdin filename failed", stderr);
        exit(1);
    }
#endif
	if (readlink("/proc/self/fd/0", stdin_path, sizeof(stdin_path)) == -1) {
        fputs("! get stdin filename failed", stderr);
        exit(1);
    }
	printf("%s", stdin_path);
    NEW_STDIN_FILENO = STDIN_FILENO;
    if (NEW_STDIN_FILENO == -1) {
        fputs("! reopen stdin failed",stderr);
        exit(1);
    }
    //close(STDIN_FILENO);

    // Set stdin to Non-Blocking
    int flags = fcntl(NEW_STDIN_FILENO, F_GETFL, 0);
    fcntl(NEW_STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);

    /* Set stdin to Non-Canonical terminal mode. */
    struct termios tattr;
    tcgetattr(NEW_STDIN_FILENO, &tattr);
    saved_tattr = tattr;  // save it to restore when exit
    tattr.c_lflag &= ~(ICANON|ECHO); /* Clear ICANON. */
    tattr.c_cc[VMIN] = 1;
    tattr.c_cc[VTIME] = 0;
    tcsetattr(NEW_STDIN_FILENO, TCSAFLUSH, &tattr);

    atexit(arepl_exit);
}

void arepl_reprint(struct AsyncREPL *arepl) {
    fputs(CODE_ERASE_LINE, stdout);
    if (arepl->prompt) fputs(arepl->prompt, stdout);
    if (arepl->nbuf > 0) printf("%.*s", arepl->nbuf, arepl->line);
    if (arepl->nstack > 0) {
        printf("%.*s",(int)arepl->nstack, arepl->line + arepl->sz - arepl->nstack);
        printf("\033[%dD",arepl->nstack); // move cursor
    }
    fflush(stdout);
}

int arepl_readline(struct AsyncREPL *arepl, char c, char *line, size_t sz){
    static uint32_t escaped = 0;
    if (c == '\033') { // mark escape code
        escaped = 1;
        return 0;
    }

    if (escaped>0) escaped++;

    switch (c) {
        case '\n': {
            int ret = snprintf(line, sz, "%.*s%.*s\n",(int)arepl->nbuf, arepl->line, (int)arepl->nstack, arepl->line + arepl->sz - arepl->nstack);
            arepl->nbuf = 0;
            arepl->nstack = 0;
            return ret;
        }

        case '\010':  // C-h
        case '\177':  // Backspace
            if (arepl->nbuf > 0) arepl->nbuf--;
            break;
        case '\025': // C-u
            arepl->nbuf = 0;
            break;
        case '\013': // C-k Vertical Tab
            arepl->nstack = 0;
            break;
        case '\001': // C-a
            while (arepl->nbuf > 0) _AREPL_CURSOR_LEFT();
            break;
        case '\005': // C-e
            while (arepl->nstack > 0) _AREPL_CURSOR_RIGHT();
            break;
        case '\002': // C-b
            if (arepl->nbuf > 0) _AREPL_CURSOR_LEFT();
            break;
        case '\006': // C-f
            if (arepl->nstack > 0) _AREPL_CURSOR_RIGHT();
            break;
        case '\027': // C-w: backward delete a word
            while (arepl->nbuf>0 && arepl->line[arepl->nbuf-1] == ' ') arepl->nbuf--;
            while (arepl->nbuf>0 && arepl->line[arepl->nbuf-1] != ' ') arepl->nbuf--;
            break;

        case 'D':
        case 'C':
            if (escaped == 3 && arepl->nbuf >= 1 && arepl->line[arepl->nbuf-1] == '[') { // arrow keys
                arepl->nbuf--;
                if (c == 'D' && arepl->nbuf > 0) _AREPL_CURSOR_LEFT(); // left arrow: \033[D
                if (c == 'C' && arepl->nstack > 0) _AREPL_CURSOR_RIGHT(); // right arrow: \033[C
                break;
            }
            // fall through to default case
        default:
            arepl->line[arepl->nbuf++] = c;
    }
    return 0;
}


/*******************************************************************************
 *
 * Main
 *
 ******************************************************************************/

char *poptok(char **strp) {
    static const char *dem = " \t";
    char *save = *strp;
    *strp = strpbrk(*strp, dem);
    if (*strp == NULL) return save;

    *((*strp)++) = '\0';
    *strp += strspn(*strp,dem);
    return save;
}

void repl_iterate(void){
    static char buf[128];
    static char line[LINE_MAX_SIZE];
    while (1) {
        int n = read(NEW_STDIN_FILENO, buf, sizeof(buf));
        if (n <= 0) {
            break;
        }
        for (int i=0;i<n;i++) { // for_1
            char c = buf[i];
            if (c == '\004')          /* C-d */
                exit(0);
            if (!arepl_readline(async_repl, c, line, sizeof(line))) continue; // continue to for_1

            int len = strlen(line);
            line[--len] = '\0'; // remove trailing \n

            if (TalkingTo != TALK_TYPE_NULL && line[0] != '/') {  // if talking to someone, just print the msg out.
                struct ChatHist **hp = get_current_histp();
                if (!hp) {
                    ERROR("! You are not talking to someone. use `/go` to return to cmd mode");
                    continue; // continue to for_1
                }
                char *msg = genmsg(hp, SELF_MSG_PREFIX "%.*s", getftime(), self.name, len, line);
                PRINT("%s", msg);
                switch (INDEX_TO_TYPE(TalkingTo)) {
                    case TALK_TYPE_FRIEND:
                        tox_friend_send_message(tox, INDEX_TO_NUM(TalkingTo), TOX_MESSAGE_TYPE_NORMAL, (uint8_t*)line, strlen(line), NULL);
                        continue; // continue to for_1
                    case TALK_TYPE_GROUP:
                        tox_conference_send_message(tox, INDEX_TO_NUM(TalkingTo), TOX_MESSAGE_TYPE_NORMAL, (uint8_t*)line, strlen(line), NULL);
                        continue;  // continue to for_1
                }
            }

            PRINT(CMD_MSG_PREFIX "%s", line);  // take this input line as a command.
            if (len == 0) continue; // continue to for_1.  ignore empty line

            if (line[0] == '/') {
                char *l = line + 1; // skip leading '/'
                char *cmdname = poptok(&l);
                struct Command *cmd = NULL;
                for (int j=0; j<COMMAND_LENGTH;j++){ // for_2
                    if (strcmp(commands[j].name, cmdname) == 0) {
                        cmd = &commands[j];
                        break; // break for_2
                    }
                }
                if (cmd) {
                    char *tokens[cmd->narg];
                    int ntok = 0;
                    for (; l != NULL && ntok != cmd->narg; ntok++) {
                        // if it's the last arg, then take the rest line.
                        char *tok = (ntok == cmd->narg - 1) ? l : poptok(&l);
                        tokens[ntok] = tok;
                    }
                    if (ntok < cmd->narg - (cmd->narg >= COMMAND_ARGS_REST ? COMMAND_ARGS_REST : 0)) {
                        WARN("Wrong number of cmd args");
                    } else {
                        cmd->handler(ntok, tokens);
                        if (SAVEDATA_AFTER_COMMAND) update_savedata_file();
                    }
                    continue; // continue to for_1
                }
            }

            WARN("! Invalid command, use `/help` to get list of available commands.");
        } // end for_1
    } // end while
    arepl_reprint(async_repl);
}


void tox_loop(void *arg)
{
	Tox *tox = (Tox *)arg;
	INFO("Starting tox thread\n")
	uint32_t msecs = 0;
    while (!signal_exit) {
        if (msecs >= AREPL_INTERVAL) {
            msecs = 0;
            repl_iterate();
        }
        tox_iterate(tox, NULL);
        uint32_t v = tox_iteration_interval(tox);
        msecs += v;

        struct timespec pause;
        pause.tv_sec = 0;
        pause.tv_nsec = v * 1000 * 1000;
        nanosleep(&pause, NULL);
    }
    
    INFO("Shut down tox thread\n")
}

void toxav_audio_loop(void *arg)
{
	ToxAV *toxav = (ToxAV *)arg;
	INFO("Starting toxav_audio thread\n")
	
	while (!signal_exit) {
		toxav_audio_iterate(toxav);

		long long time = toxav_audio_iteration_interval(toxav) * 1000000L;
		nanosleep((const struct timespec[]){{0, time}}, NULL);
	}
	
	INFO("Shut down toxav_audio thread\n")
}

void toxav_video_loop(void *arg)
{
	ToxAV *toxav = (ToxAV *)arg;
	INFO("Starting toxav_video thread\n")
	
	while (!signal_exit) {
		toxav_video_iterate(toxav);

		long long time = toxav_video_iteration_interval(toxav) * 1000000L;
		nanosleep((const struct timespec[]){{0, time}}, NULL);
	}
	
	INFO("Shut down toxav_video thread\n")
}

int main(int argc, char **argv) {
	int error;
    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        fputs("Usage: Pabian\n", stdout);
        fputs("\n", stdout);
        fputs("Minitox does not take any arguments.\n", stdout);
        return 0;
    }

    fputs("Type `/guide` to print the guide.\n", stdout);
    fputs("Type `/help` to print command list.\n\n",stdout);
	
    setup_arepl();
    setup_tox();
    setup_toxav();

    INFO("* Waiting to be online ...");
	
	sigset_t sig_set;
	sigemptyset(&sig_set);
	sigaddset(&sig_set, SIGTERM);
	sigaddset(&sig_set, SIGINT);
	
	pthread_t tox_thread, toxav_audio_thread, toxav_video_thread;
	pthread_sigmask(SIG_BLOCK, &sig_set, NULL);
	error = pthread_create(&tox_thread, NULL, &tox_loop, tox);
	if (error != 0){
		ERROR("Thread can't be created : %d", error);
		return;
	}
	pthread_setname_np(tox_thread, "toxcore_thread");
	error = pthread_create(&toxav_audio_thread, NULL, &toxav_audio_loop, toxav);
	if (error != 0){
		ERROR("Thread can't be created : %d", error);
		return;
	}
	pthread_setname_np(toxav_audio_thread, "toxav_audio_thread");
	error = pthread_create(&toxav_video_thread, NULL, &toxav_video_loop, toxav);
	if (error != 0){
		ERROR("Thread can't be created : %d", error);
		return;
	}
	pthread_setname_np(toxav_video_thread, "toxav_video_thread");
	
	int sig;
	sigwait(&sig_set, &sig);
	INFO("Shutdown signal received\n")
	signal_exit = true;
	struct Call *c = getcall(2);
	if (c && c->in_call){
		INFO("Shutdown call\n")
		c->is->quit = 1;
	}
	INFO("Waiting for tox and toxav threads to finish\n")
	pthread_join(tox_thread, NULL);
	pthread_join(toxav_audio_thread, NULL);
	pthread_join(toxav_video_thread, NULL);
	toxav_kill(toxav);
	tox_kill(tox);
	
    

    return 0;
}
