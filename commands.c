#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <SDL.h>
#include <SDL_thread.h>
#include <stdbool.h>

#include "av.h"
#include "Pabian.h"
#include "commands.h"
#include "util.h"
#include "capture.h"

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
extern struct AsyncREPL *async_repl;

extern Tox *tox;
extern ToxAV *toxav;
extern pthread_t toxav_video_out_thread;

/*******************************************************************************
 *
 * Commands
 *
 ******************************************************************************/

void command_help(int narg, char **args);

void command_guide(int narg, char **args) {
    PRINT("This program is a minimal workable implementation of Tox client.");
    PRINT("As it pursued simplicity at the cost of robustness and efficiency.\n");

    PRINT("Commands are any input lines with leading `/`,");
    PRINT("Command args are seprated by blanks,");
    PRINT("while some special commands may accept any-character string, like `/setname` and `/setstmsg`.\n");

    PRINT("Use `/setname <YOUR NAME>` to set your name");
    PRINT("Use `/info` to see your Name, Tox Id and Network Connection.");
    PRINT("Use `/contacts` to list friends and groups, and use `/go <TARGET>` to talk to one of them.");
    PRINT("Finally, use `/help` to get a list of available commands.\n");

    PRINT("HAVE FUN!\n")
}

void _print_friend_info(struct Friend *f, bool is_self) {
    PRINT("%-15s%s", "Name:", f->name);

    if (is_self) {
        uint8_t tox_id_bin[TOX_ADDRESS_SIZE];
        tox_self_get_address(tox, tox_id_bin);
        char *hex = bin2hex(tox_id_bin, sizeof(tox_id_bin));
        PRINT("%-15s%s","Tox ID:", hex);
        free(hex);
    }

    char *hex = bin2hex(f->pubkey, sizeof(f->pubkey));
    PRINT("%-15s%s","Public Key:", hex);
    free(hex);
    PRINT("%-15s%s", "Status Msg:",f->status_message);
    PRINT("%-15s%s", "Network:",connection_enum2text(f->connection));
}

void command_info(int narg, char **args) {
    if (narg == 0) { // self
        _print_friend_info(&self, true);
        return;
    }

    uint32_t contact_idx;
    if (!str2uint(args[0],&contact_idx)) goto FAIL;

    uint32_t num = INDEX_TO_NUM(contact_idx);
    switch (INDEX_TO_TYPE(contact_idx)) {
        case TALK_TYPE_FRIEND: {
            struct Friend *f = getfriend(num);
            if (f) {
                _print_friend_info(f, false);
                return;
            }
            break;
        }
        case TALK_TYPE_GROUP: {
            struct Group *cf = getgroup(num);
            if (cf) {
                PRINT("GROUP TITLE:\t%s",cf->title);
                PRINT("PEER COUNT:\t%zu", cf->peers_count);
                PRINT("Peers:");
                for (int i=0;i<cf->peers_count;i++){
                    PRINT("\t%s",cf->peers[i].name);
                }
                return;
            }
            break;
        }
    }
FAIL:
    WARN("^ Invalid contact index");
}

void command_setname(int narg, char **args) {
    char *name = args[0];
    size_t len = strlen(name);
    TOX_ERR_SET_INFO err;
    tox_self_set_name(tox, (uint8_t*)name, strlen(name), &err);

    if (err != TOX_ERR_SET_INFO_OK) {
        ERROR("! set name failed, errcode:%d", err);
        return;
    }

    self.name = realloc(self.name, len + 1);
    strcpy(self.name, name);
}

void command_setstmsg(int narg, char **args) {
    char *status = args[0];
    size_t len = strlen(status);
    TOX_ERR_SET_INFO err;
    tox_self_set_status_message(tox, (uint8_t*)status, strlen(status), &err);
    if (err != TOX_ERR_SET_INFO_OK) {
        ERROR("! set status message failed, errcode:%d", err);
        return;
    }

    self.status_message = realloc(self.status_message, len+1);
    strcpy(self.status_message, status);
}

void command_add(int narg, char **args) {
    char *hex_id = args[0];
    char *msg = "";
    if (narg > 1) msg = args[1];

    uint8_t *bin_id = hex2bin(hex_id);
    TOX_ERR_FRIEND_ADD err;
    uint32_t friend_num = tox_friend_add(tox, bin_id, (uint8_t*)msg, strlen(msg), &err);
    free(bin_id);

    if (err != TOX_ERR_FRIEND_ADD_OK) {
        ERROR("! add friend failed, errcode:%d",err);
        return;
    }

    addfriend(friend_num);
}

void command_del(int narg, char **args) {
    uint32_t contact_idx;
    if (!str2uint(args[0], &contact_idx)) goto FAIL;
    uint32_t num = INDEX_TO_NUM(contact_idx);
    switch (INDEX_TO_TYPE(contact_idx)) {
        case TALK_TYPE_FRIEND:
            if (delfriend(num)) {
                tox_friend_delete(tox, num, NULL);
                return;
            }
            break;
        case TALK_TYPE_GROUP:
            if (delgroup(num)) {
                tox_conference_delete(tox, num, NULL);
                return;
            }
            break;
    }
FAIL:
    WARN("^ Invalid contact index");
}

void command_contacts(int narg, char **args) {
    struct Friend *f = friends;
    PRINT("#Friends(conctact_index|name|connection|status message):\n");
    for (;f != NULL; f = f->next) {
        PRINT("%3d  %15.15s  %12.12s  %s",GEN_INDEX(f->friend_num, TALK_TYPE_FRIEND), f->name, connection_enum2text(f->connection), f->status_message);
    }

    struct Group *cf = groups;
    PRINT("\n#Groups(contact_index|count of peers|name):\n");
    for (;cf != NULL; cf = cf->next) {
        PRINT("%3d  %10d  %s",GEN_INDEX(cf->group_num, TALK_TYPE_GROUP), tox_conference_peer_count(tox, cf->group_num, NULL), cf->title);
    }
}

void command_save(int narg, char **args) {
    update_savedata_file();
}

void command_go(int narg, char **args) {
    if (narg == 0) {
        TalkingTo = TALK_TYPE_NULL;
        strcpy(async_repl->prompt, CMD_PROMPT);
        return;
    }
    uint32_t contact_idx;
    if (!str2uint(args[0], &contact_idx)) goto FAIL;
    uint32_t num = INDEX_TO_NUM(contact_idx);
    switch (INDEX_TO_TYPE(contact_idx)) {
        case TALK_TYPE_FRIEND: {
            struct Friend *f = getfriend(num);
            if (f) {
                TalkingTo = contact_idx;
                sprintf(async_repl->prompt, FRIEND_TALK_PROMPT, f->name);
                return;
            }
            break;
        }
        case TALK_TYPE_GROUP: {
            struct Group *cf = getgroup(num);
            if (cf) {
                TalkingTo = contact_idx;
                sprintf(async_repl->prompt, GROUP_TALK_PROMPT, cf->title);
                return;
            }
            break;
       }
    }

FAIL:
    WARN("^ Invalid contact index");
}

void command_history(int narg, char **args) {
    uint32_t n = DEFAULT_CHAT_HIST_COUNT;
    if (narg > 0 && !str2uint(args[0], &n)) {
        WARN("Invalid args");
    }

    struct ChatHist **hp = get_current_histp();
    if (!hp) {
        WARN("you are not talking to someone");
        return;
    }

    struct ChatHist *hist = *hp;

    while (hist && hist->next) hist = hist->next;
    PRINT("%s", "------------ HISTORY BEGIN ---------------")
    for (int i=0;i<n && hist; i++,hist=hist->prev) {
        printf("%s\n", hist->msg);
    }
    PRINT("%s", "------------ HISTORY   END ---------------")
}

void _command_accept(int narg, char **args, bool is_accept) {
    if (narg == 0) {
        struct Request * req = requests;
        for (;req != NULL;req=req->next) {
            PRINT("%-9u%-12s%s", req->id, (req->is_friend_request ? "FRIEND" : "GROUP"), req->msg);
        }
        return;
    }

    uint32_t request_idx;
    if (!str2uint(args[0], &request_idx)) goto FAIL;
    struct Request **p = &requests;
    LIST_FIND(p, (*p)->id == request_idx);
    struct Request *req = *p;
    if (req) {
        *p = req->next;
        if (is_accept) {
            if (req->is_friend_request) {
                TOX_ERR_FRIEND_ADD err;
                uint32_t friend_num = tox_friend_add_norequest(tox, req->userdata.friend.pubkey, &err);
                if (err != TOX_ERR_FRIEND_ADD_OK) {
                    ERROR("! accept friend request failed, errcode:%d", err);
                } else {
                    addfriend(friend_num);
                }
            } else { // group invite
                struct GroupUserData *data = &req->userdata.group;
                TOX_ERR_CONFERENCE_JOIN err;
                uint32_t group_num = tox_conference_join(tox, data->friend_num, data->cookie, data->length, &err);
                if (err != TOX_ERR_CONFERENCE_JOIN_OK) {
                    ERROR("! join group failed, errcode: %d", err);
                } else {
                    addgroup(group_num);
                }
            }
        }
        free(req->msg);
        free(req);
        return;
    }
FAIL:
    WARN("Invalid request index");
}

void command_accept(int narg, char **args) {
    _command_accept(narg, args, true);
}

void command_deny(int narg, char **args) {
    _command_accept(narg, args, false);
}

void command_invite(int narg, char **args) {
    uint32_t friend_contact_idx;
    if (!str2uint(args[0], &friend_contact_idx) || INDEX_TO_TYPE(friend_contact_idx) != TALK_TYPE_FRIEND) {
        WARN("Invalid friend contact index");
        return;
    }
    int err;
    uint32_t group_num;
    if (narg == 1) {
        group_num = tox_conference_new(tox, (TOX_ERR_CONFERENCE_NEW*)&err);
        if (err != TOX_ERR_CONFERENCE_NEW_OK) {
            ERROR("! Create group failed, errcode:%d", err);
            return;
        }
        addgroup(group_num);
    } else {
        uint32_t group_contact_idx;
        if (!str2uint(args[1], &group_contact_idx) || INDEX_TO_TYPE(group_contact_idx) != TALK_TYPE_GROUP) {
            ERROR("! Invalid group contact index");
            return;
        }
        group_num = INDEX_TO_NUM(group_contact_idx);
    }

    uint32_t friend_num = INDEX_TO_NUM(friend_contact_idx);
    tox_conference_invite(tox, friend_num, group_num, (TOX_ERR_CONFERENCE_INVITE*)&err);
    if (err != TOX_ERR_CONFERENCE_INVITE_OK) {
        ERROR("! Group invite failed, errcode:%d", err);
        return;
    }
}

void command_settitle(int narg, char **args) {
    uint32_t group_contact_idx;
    if (!str2uint(args[0], &group_contact_idx) || INDEX_TO_TYPE(group_contact_idx) != TALK_TYPE_GROUP){
        ERROR("! Invalid group contact index");
        return;
    }
    uint32_t group_num = INDEX_TO_NUM(group_contact_idx);
    struct Group *cf = getgroup(group_num);
    if (!cf) {
        ERROR("! Invalid group contact index");
        return;
    }

    char *title = args[1];
    size_t len = strlen(title);
    TOX_ERR_CONFERENCE_TITLE  err;
    tox_conference_set_title(tox, group_num, (uint8_t*)title, len, &err);
    if (err != TOX_ERR_CONFERENCE_TITLE_OK) {
        ERROR("! Set group title failed, errcode: %d",err);
        return;
    }

    cf->title = realloc(cf->title, len+1);
    sprintf(cf->title, "%.*s",(int)len,title);
}

void command_transfer(int narg, char **args){
	uint32_t contact_idx;
	if (narg != 2){
		ERROR("! Incorrect number of arguments");
	}
    if (!str2uint(args[0], &contact_idx)) goto FAIL;
    uint32_t num = INDEX_TO_NUM(contact_idx);
	struct Friend *f = getfriend(num);
	if (f){
		if (access(args[1], F_OK) != -1){
			FILE *fd = fopen(args[1], "rb");
			fseek(fd, 0L, SEEK_END);
			uint64_t file_size = ftell(fd);
			rewind(fd);
			uint8_t *filename = args[1];
			INFO("%s" , filename)
			Tox_Err_File_Send err1;
			uint32_t file_number = tox_file_send(tox, num, TOX_FILE_KIND_DATA, file_size, NULL, filename, strlen(filename), &err1);
			if (err1 != TOX_ERR_FILE_SEND_OK) {
        		ERROR("! Send file failed, errcode: %d",err1);
        		return;
    		}
    		uint8_t *file_id = malloc(TOX_FILE_ID_LENGTH);
    		Tox_Err_File_Get err2;
    		tox_file_get_file_id(tox, num, file_number, file_id, &err2);
    		if (err2 != TOX_ERR_FILE_SEND_OK) {
        		ERROR("! Get id of file failed, errcode: %d",err2);
        		return;
    		}
    		addtransfer(num, file_number, 0, strlen(filename), TOX_FILE_KIND_DATA, file_size, filename, fd, *file_id);
    		return;
		}
	}
FAIL:
    WARN("^ Invalid contact index");
}

void command_answer(int narg, char **args) {
	uint32_t contact_idx;
	if (narg != 1){
		ERROR("! Incorrect number of arguments");
	}
    if (!str2uint(args[0], &contact_idx)) goto FAIL;
    uint32_t num = INDEX_TO_NUM(contact_idx);
	struct Friend *f = getfriend(num);
	struct Call *c = getcall(num);
	if (f && c){
		Toxav_Err_Answer err;
		c->is = initialize_av_out((uint32_t)0, 640, 480);
		toxav_answer(toxav, num, c->audio_bit_rate, c->video_bit_rate, &err);
		c->in_call = true;
		if (err != TOXAV_ERR_ANSWER_OK){
			ERROR("! failed to answer call, errcode: %d",err);
			return;
		}
		sigset_t sig_set;
		sigemptyset(&sig_set);
		sigaddset(&sig_set, SIGTERM);
		sigaddset(&sig_set, SIGINT);
		
		
		pthread_sigmask(SIG_BLOCK, &sig_set, NULL);
		struct AVargs *argss = malloc(sizeof(AVargs));
		argss->c = c;
		argss->av = toxav;
		int error;
		if (c->video_enabled){
			error = pthread_create(&toxav_video_out_thread, NULL, &toxav_video_out_loop, argss);
			if (error != 0){
				ERROR("Thread can't be created : %d", error);
				return;
			}
		}
		if (c->audio_enabled){
			//no audio support
		}
		
		return;
	}
	
FAIL:
    WARN("^ Invalid contact index");
}


void command_call(int narg, char **args) {
	uint32_t contact_idx;
	if (narg != 1){
		ERROR("! Incorrect number of arguments");
	}
    if (!str2uint(args[0], &contact_idx)) goto FAIL;
    uint32_t num = INDEX_TO_NUM(contact_idx);
	struct Friend *f = getfriend(num);
	if (f){
		Toxav_Err_Call error;
		toxav_call(toxav, num, 0, 5000, &error);
		if (error != TOXAV_ERR_CALL_OK){
				ERROR("! failed toxav_call error: %d", error);
				return;
			}
		struct Call *c = addcall(num, false, true);
		c->is = initialize_av_out((uint32_t)0, 640, 480);
		sigset_t sig_set;
		sigemptyset(&sig_set);
		sigaddset(&sig_set, SIGTERM);
		sigaddset(&sig_set, SIGINT);
		
		
		pthread_sigmask(SIG_BLOCK, &sig_set, NULL);
		struct AVargs *argss = malloc(sizeof(AVargs));
		argss->c = c;
		argss->av = toxav;
		int error1;
		if (c->video_enabled){
			error1 = pthread_create(&toxav_video_out_thread, NULL, &toxav_video_out_loop, argss);
			if (error1 != 0){
				ERROR("Thread can't be created : %d", error);
				return;
			}
		}
	}
FAIL:
    WARN("^ Invalid contact index");
}

void command_help(int narg, char **args){
    for (int i=1;i<COMMAND_LENGTH;i++) {
        printf("%-16s%s\n", commands[i].name, commands[i].desc);
    }
}
