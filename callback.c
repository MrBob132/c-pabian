#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <SDL.h>
#include <SDL_thread.h>
#include <stdbool.h>

#include "av.h"
#include "Pabian.h"
#include "callback.h"
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
extern struct AsyncREPL *async_repl;

extern Tox *tox;
extern pthread_t toxav_video_out_thread;

/*******************************************************************************
 *
 * Tox Callbacks
 *
 ******************************************************************************/

void friend_message_cb(Tox *tox, uint32_t friend_num, TOX_MESSAGE_TYPE type, const uint8_t *message,
                                   size_t length, void *user_data)
{
    struct Friend *f = getfriend(friend_num);
    if (!f) return;
    if (type != TOX_MESSAGE_TYPE_NORMAL) {
        INFO("* receive MESSAGE ACTION type from %s, no supported", f->name);
        return;
    }

    char *msg = genmsg(&f->hist, GUEST_MSG_PREFIX "%.*s", getftime(), f->name, (int)length, (char*)message);
    if (GEN_INDEX(friend_num, TALK_TYPE_FRIEND) == TalkingTo) {
        PRINT("%s", msg);
    } else {
        INFO("* receive message from %s, use `/go <contact_index>` to talk\n",f->name);
    }
}

void friend_name_cb(Tox *tox, uint32_t friend_num, const uint8_t *name, size_t length, void *user_data) {
    struct Friend *f = getfriend(friend_num);

    if (f) {
        f->name = realloc(f->name, length+1);
        sprintf(f->name, "%.*s", (int)length, (char*)name);
        if (GEN_INDEX(friend_num, TALK_TYPE_FRIEND) == TalkingTo) {
            INFO("* Opposite changed name to %.*s", (int)length, (char*)name)
            sprintf(async_repl->prompt, FRIEND_TALK_PROMPT, f->name);
        }
    }
}

void friend_status_message_cb(Tox *tox, uint32_t friend_num, const uint8_t *message, size_t length, void *user_data) {
    struct Friend *f = getfriend(friend_num);
    if (f) {
        f->status_message = realloc(f->status_message, length + 1);
        sprintf(f->status_message, "%.*s",(int)length, (char*)message);
    }
}

void friend_connection_status_cb(Tox *tox, uint32_t friend_num, TOX_CONNECTION connection_status, void *user_data)
{
    struct Friend *f = getfriend(friend_num);
    if (f) {
        f->connection = connection_status;
        INFO("* %s is %s", f->name, connection_enum2text(connection_status));
    }
}

void friend_request_cb(Tox *tox, const uint8_t *public_key, const uint8_t *message, size_t length, void *user_data) {
    INFO("* receive friend request(use `/accept` to see).");

    struct Request *req = malloc(sizeof(struct Request));

    req->id = 1 + ((requests != NULL) ? requests->id : 0);
    req->is_friend_request = true;
    memcpy(req->userdata.friend.pubkey, public_key, TOX_PUBLIC_KEY_SIZE);
    req->msg = malloc(length + 1);
    sprintf(req->msg, "%.*s", (int)length, (char*)message);

    req->next = requests;
    requests = req;
}

void self_connection_status_cb(Tox *tox, TOX_CONNECTION connection_status, void *user_data)
{
    self.connection = connection_status;
    INFO("* You are %s", connection_enum2text(connection_status));
}

void group_invite_cb(Tox *tox, uint32_t friend_num, TOX_CONFERENCE_TYPE type, const uint8_t *cookie, size_t length, void *user_data) {
    struct Friend *f = getfriend(friend_num);
    if (f) {
        if (type == TOX_CONFERENCE_TYPE_AV) {
            WARN("* %s invites you to an AV group, which has not been supported.", f->name);
            return;
        }
        INFO("* %s invites you to a group(try `/accept` to see)",f->name);
        struct Request *req = malloc(sizeof(struct Request));
        req->id = 1 + ((requests != NULL) ? requests->id : 0);
        req->next = requests;
        requests = req;

        req->is_friend_request = false;
        req->userdata.group.cookie = malloc(length);
        memcpy(req->userdata.group.cookie, cookie, length),
        req->userdata.group.length = length;
        req->userdata.group.friend_num = friend_num;
        int sz = snprintf(NULL, 0, "%s%s", "From ", f->name);
        req->msg = malloc(sz + 1);
        sprintf(req->msg, "%s%s", "From ", f->name);
    }
}

void group_title_cb(Tox *tox, uint32_t group_num, uint32_t peer_number, const uint8_t *title, size_t length, void *user_data) {
    struct Group *cf = getgroup(group_num);
    if (cf) {
        cf->title = realloc(cf->title, length+1);
        sprintf(cf->title, "%.*s", (int)length, (char*)title);
        if (GEN_INDEX(group_num, TALK_TYPE_GROUP) == TalkingTo) {
            INFO("* Group title changed to %s", cf->title);
            sprintf(async_repl->prompt, GROUP_TALK_PROMPT, cf->title);
        }
    }
}

void group_message_cb(Tox *tox, uint32_t group_num, uint32_t peer_number, TOX_MESSAGE_TYPE type, const uint8_t *message, size_t length, void *user_data) {
    struct Group *cf = getgroup(group_num);
    if (!cf) return;

    if (tox_conference_peer_number_is_ours(tox, group_num, peer_number, NULL))  return;

    if (type != TOX_MESSAGE_TYPE_NORMAL) {
        INFO("* receive MESSAGE ACTION type from group %s, no supported", cf->title);
        return;
    }
    if (peer_number >= cf->peers_count) {
        ERROR("! Unknown peer_number, peer_count:%zu, peer_number:%u", cf->peers_count, peer_number);
        return;
    }

    struct GroupPeer *peer = &cf->peers[peer_number];
    char *msg = genmsg(&cf->hist, GUEST_MSG_PREFIX "%.*s", getftime(), peer->name, (int)length, (char*)message);

    if (GEN_INDEX(group_num, TALK_TYPE_GROUP) == TalkingTo) {
        PRINT("%s", msg);
    } else {
        INFO("* receive group message from %s, in group %s",peer->name, cf->title);
    }
}

void group_peer_list_changed_cb(Tox *tox, uint32_t group_num, void *user_data) {
    struct Group *cf = getgroup(group_num);
    if (!cf) return;

    TOX_ERR_CONFERENCE_PEER_QUERY err;
    uint32_t count = tox_conference_peer_count(tox, group_num, &err);
    if (err != TOX_ERR_CONFERENCE_PEER_QUERY_OK) {
        ERROR("get group peer count failed, errcode:%d",err);
        return;
    }
    if (cf->peers) free(cf->peers);
    cf->peers = calloc(count, sizeof(struct GroupPeer));
    cf->peers_count = count;

    for (int i=0;i<count;i++) {
        struct GroupPeer *p = cf->peers + i;
        tox_conference_peer_get_name(tox, group_num, i, (uint8_t*)p->name, NULL);
        tox_conference_peer_get_public_key(tox, group_num, i, p->pubkey,NULL);
    }
}
void group_peer_name_cb(Tox *tox, uint32_t group_num, uint32_t peer_num, const uint8_t *name, size_t length, void *user_data) {
    struct Group *cf = getgroup(group_num);
    if (!cf || peer_num >= cf->peers_count) {
        ERROR("! Unexpected group_num/peer_num in group_peer_name_cb");
        return;
    }

    struct GroupPeer *p = &cf->peers[peer_num];
    sprintf(p->name, "%.*s", (int)length, (char*)name);
}

//file transfer callbacks
void tox_events_handle_file_chunk_request(Tox *tox, uint32_t friend_number, uint32_t file_number, uint64_t position, size_t length, void *user_data)
{
	struct File_Transfer *t = gettransfer(friend_number, file_number);
	if (t){
		uint8_t *data = malloc(length);
		Tox_Err_File_Send_Chunk err;
		if (length == 0){
			INFO("length == 0")
			tox_file_send_chunk(tox, friend_number, file_number, position, data, length, &err);
			if (err != TOX_ERR_FILE_SEND_CHUNK_OK){
				//ERROR("! Send chunk failed, errcode: %d",err);
				return;
			}
			return;
		}
		fread(data, length, 1, t->fd);
		tox_file_send_chunk(tox, friend_number, file_number, position, data, length, &err);
		if (err != TOX_ERR_FILE_SEND_CHUNK_OK){
				ERROR("! Send chunk failed, errcode: %d",err);
				return;
			}
			return;
	}
FAIL:
	WARN("^ Invalid contact index in tox_events_handle_file_chunk_request()");
}

void tox_events_handle_file_recv_chunk(Tox *tox, uint32_t friend_number, uint32_t file_number, uint64_t position, const uint8_t *data, size_t length, void *user_data)
{
	//INFO("position: %u", position)
	struct File_Transfer *t = gettransfer(friend_number, file_number);
	if (t){
		if (length == 0 || (t->file_size != UINT64_MAX && position == t->file_size)){
			deltransfer(friend_number, file_number);
			return;
		}
		fwrite(data, 1, length, t->fd);
	}
}


void tox_events_handle_file_recv_control(Tox *tox, uint32_t friend_number, uint32_t file_number, Tox_File_Control control, void *user_data)
{
	struct File_Transfer *t = gettransfer(friend_number, file_number);
	if (t){
		if (control == TOX_FILE_CONTROL_CANCEL) deltransfer(friend_number, file_number); 
	}
}


void tox_events_handle_file_recv(Tox *tox, uint32_t friend_number, uint32_t file_number, uint32_t kind, uint64_t file_size, const uint8_t *filename, size_t filename_length, void *user_data)
{
	struct Friend *f = getfriend(friend_number);
	uint8_t *file_id = malloc(TOX_FILE_ID_LENGTH);
	Tox_Err_File_Get get_error;
	Tox_Err_File_Control control_error;
    if (f) {
    	char path[DOWNLOAD_DICTIONARY_LENGTH+filename_length+1];
    	strcpy(path, DOWNLOAD_DICTIONARY);
    	strncat(path, (char *)filename, filename_length);
    	FILE *fd = fopen(path, "wb");
    	if (fd == NULL){
    		ERROR("failed open file: %s", path);
    	}
    	tox_file_get_file_id(tox, friend_number, file_number, file_id, &get_error);
    	if (get_error != TOX_ERR_FILE_GET_OK){
    		ERROR("! Get id of file failed, errcode: %d",get_error);
    		return;
    	}
    	addtransfer(friend_number, file_number, 0, filename_length, kind, file_size, (char *)filename, fd, *file_id);
    	tox_file_control(tox, friend_number, file_number, TOX_FILE_CONTROL_RESUME, &control_error);
    	if (control_error != TOX_ERR_FILE_CONTROL_OK){
    		ERROR("! set control of file failed, errcode: %d",control_error);
    		return;
    	}
    	return;
    }
}


//ToxAv audio callback
void call_cb(ToxAV *av, uint32_t friend_number, bool audio_enabled, bool video_enabled, void *user_data)
{
	struct Friend *f = getfriend(friend_number);
	if (f){
		INFO("call recieved from: %s, %d\tuse '/answer' to answer", f->name, friend_number);
		addcall(friend_number, audio_enabled, video_enabled);
	}
}

void call_state_cb(ToxAV *av, uint32_t friend_number, uint32_t state, void *user_data)
{
	struct Friend *f = getfriend(friend_number);
	struct Call *c = getcall(friend_number);
	if (f && c){
		if ((state & TOXAV_FRIEND_CALL_STATE_ERROR)){
			ERROR("! error in toxav_call_state_cb() ERROR: TOXAV_FRIEND_CALL_STATE_ERROR");
			c->in_call = false;
			c->end = true;
			pthread_join(toxav_video_out_thread, NULL);
			destroy_av(c->is);
			delcall(friend_number);
			return;
		}
		else if ((state & TOXAV_FRIEND_CALL_STATE_FINISHED)){
			INFO("call with: %s %d has ended", f->name, f->friend_num)
			c->in_call = false;
			c->end = true;
			pthread_join(toxav_video_out_thread, NULL);
			destroy_av(c->is);
			delcall(friend_number);
			return;
		}
		if ((state & TOXAV_FRIEND_CALL_STATE_SENDING_A)){
			INFO("in call_cb() state & TOXAV_FRIEND_CALL_STATE_SENDING_A")
			c->in_call = true;
			c->audio_enabled = true;
			c->audio_bit_rate = 24;// add const
		}
		else if (!(state & TOXAV_FRIEND_CALL_STATE_SENDING_A)){
			INFO("in call_cb() state & TOXAV_FRIEND_CALL_STATE_SENDING_A!!!!!!!!!!!")
			c->in_call = true;
			c->audio_enabled = false;
			c->audio_bit_rate = 0;
		}
		if ((state & TOXAV_FRIEND_CALL_STATE_SENDING_V)){
			INFO("in call_cb() state & TOXAV_FRIEND_CALL_STATE_SENDING_V")
			c->in_call = true;
			c->video_enabled = true;
			c->video_bit_rate = 5000;// add const
		}
		else if (!(state & TOXAV_FRIEND_CALL_STATE_SENDING_V)){
			INFO("in call_cb() state & TOXAV_FRIEND_CALL_STATE_SENDING_V!!!!!!!!!!!")
			c->in_call = true;
			c->video_enabled = false;
			c->video_bit_rate = 0;
		}
		if ((state & TOXAV_FRIEND_CALL_STATE_ACCEPTING_A)){
			INFO("in call_cb() state & TOXAV_FRIEND_CALL_STATE_ACCEPTING_A")
			c->in_call = true;
			c->audio_enabled = true;
			c->audio_bit_rate = 24;// add const
		}
		else if (!(state & TOXAV_FRIEND_CALL_STATE_ACCEPTING_A)){
			INFO("in call_cb() state & TOXAV_FRIEND_CALL_STATE_ACCEPTING_A!!!!!!!!!!!")
			c->in_call = true;
			c->audio_enabled = false;
			c->audio_bit_rate = 0;
		}
		if ((state & TOXAV_FRIEND_CALL_STATE_ACCEPTING_V)){
			INFO("in call_cb() state & TOXAV_FRIEND_CALL_STATE_ACCEPTING_V")
			c->in_call = true;
			c->video_enabled = true;
			c->video_bit_rate = 5000;// add const
		}
		else if (!(state & TOXAV_FRIEND_CALL_STATE_ACCEPTING_V)){
			INFO("in call_cb() state & TOXAV_FRIEND_CALL_STATE_ACCEPTING_V!!!!!!!!!!!")
			c->in_call = true;
			c->video_enabled = false;
			c->video_bit_rate = 0;
		}
	}
}

void audio_bit_rate_cb(ToxAV *av, uint32_t friend_number, uint32_t audio_bit_rate, void *user_data)
{
	struct Friend *f = getfriend(friend_number);
	struct Call *c = getcall(friend_number);
	Toxav_Err_Bit_Rate_Set err;
	if (f && c){
		toxav_audio_set_bit_rate(av, friend_number, audio_bit_rate, &err);
		if (err != TOXAV_ERR_BIT_RATE_SET_OK){
			ERROR("! set bit rate failed, error code: %d", err);
			return;
		}
		c->audio_bit_rate = audio_bit_rate;
		INFO("network is too saturated for current bit rates, audio bit rate set to: %d", audio_bit_rate)
	}
}

void video_bit_rate_cb(ToxAV *av, uint32_t friend_number, uint32_t video_bit_rate, void *user_data)
{
	struct Friend *f = getfriend(friend_number);
	struct Call *c = getcall(friend_number);
	Toxav_Err_Bit_Rate_Set err;
	if (f && c){
		toxav_video_set_bit_rate(av, friend_number, video_bit_rate, &err);
		if (err != TOXAV_ERR_BIT_RATE_SET_OK){
			ERROR("! set bit rate failed, error code: %d", err);
			return;
		}
		c->video_bit_rate = video_bit_rate;
		INFO("network is too saturated for current bit rates, video bit rate set to: %d", video_bit_rate)
	}
}

void audio_receive_frame_cb(ToxAV *av, uint32_t friend_number, const int16_t *pcm, size_t sample_count, uint8_t channels, uint32_t sampling_rate, void *user_data)
{
	return;//no audio support
}

void video_receive_frame_cb(ToxAV *av, uint32_t friend_number, uint16_t width, uint16_t height, const uint8_t *y, const uint8_t *u, const uint8_t *v, int32_t ystride, int32_t ustride, int32_t vstride, void *user_data)
{
	struct Friend *f = getfriend(friend_number);
	struct Call *c = getcall(friend_number);
	if (f && c && c->in_call){
		if (!c->is){
			WARN("is not initialized2");
			return;
		}
		
		int ret = video_frame_to_queue(width, height, y, u, v, ystride, ustride, vstride, c->is);
		if (ret == -1){
			WARN("can't send video frame to queue");
		}
		return;
	}
	WARN("if(false) in toxav_video_receive_frame_cb");
}
