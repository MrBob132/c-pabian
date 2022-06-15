/******************************************************************************
 *
 * Tox Callbacks
 *
 ******************************************************************************/

void friend_message_cb(Tox *tox, uint32_t friend_num, TOX_MESSAGE_TYPE type, const uint8_t *message, size_t length, void *user_data);

void friend_name_cb(Tox *tox, uint32_t friend_num, const uint8_t *name, size_t length, void *user_data);

void friend_status_message_cb(Tox *tox, uint32_t friend_num, const uint8_t *message, size_t length, void *user_data);

void friend_connection_status_cb(Tox *tox, uint32_t friend_num, TOX_CONNECTION connection_status, void *user_data);

void friend_request_cb(Tox *tox, const uint8_t *public_key, const uint8_t *message, size_t length, void *user_data);

void self_connection_status_cb(Tox *tox, TOX_CONNECTION connection_status, void *user_data);

void group_invite_cb(Tox *tox, uint32_t friend_num, TOX_CONFERENCE_TYPE type, const uint8_t *cookie, size_t length, void *user_data);

void group_title_cb(Tox *tox, uint32_t group_num, uint32_t peer_number, const uint8_t *title, size_t length, void *user_data);

void group_message_cb(Tox *tox, uint32_t group_num, uint32_t peer_number, TOX_MESSAGE_TYPE type, const uint8_t *message, size_t length, void *user_data);

void group_peer_list_changed_cb(Tox *tox, uint32_t group_num, void *user_data);

void group_peer_name_cb(Tox *tox, uint32_t group_num, uint32_t peer_num, const uint8_t *name, size_t length, void *user_data);

//file transfer callbacks
void tox_events_handle_file_chunk_request(Tox *tox, uint32_t friend_number, uint32_t file_number, uint64_t position, size_t length, void *user_data);

void tox_events_handle_file_recv_chunk(Tox *tox, uint32_t friend_number, uint32_t file_number, uint64_t position, const uint8_t *data, size_t length, void *user_data);


void tox_events_handle_file_recv_control(Tox *tox, uint32_t friend_number, uint32_t file_number, Tox_File_Control control, void *user_data);


void tox_events_handle_file_recv(Tox *tox, uint32_t friend_number, uint32_t file_number, uint32_t kind, uint64_t file_size, const uint8_t *filename, size_t filename_length, void *user_data);


//ToxAv audio callback
void call_cb(ToxAV *av, uint32_t friend_number, bool audio_enabled, bool video_enabled, void *user_data);

void call_state_cb(ToxAV *av, uint32_t friend_number, uint32_t state, void *user_data);

void audio_bit_rate_cb(ToxAV *av, uint32_t friend_number, uint32_t audio_bit_rate, void *user_data);

void video_bit_rate_cb(ToxAV *av, uint32_t friend_number, uint32_t video_bit_rate, void *user_data);

void audio_receive_frame_cb(ToxAV *av, uint32_t friend_number, const int16_t *pcm, size_t sample_count, uint8_t channels, uint32_t sampling_rate, void *user_data);

void video_receive_frame_cb(ToxAV *av, uint32_t friend_number, uint16_t width, uint16_t height, const uint8_t *y, const uint8_t *u, const uint8_t *v, int32_t ystride, int32_t ustride, int32_t vstride, void *user_data);
