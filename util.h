/*******************************************************************************
 *
 * Utils
 *
 ******************************************************************************/
bool str2uint(char *str, uint32_t *num);

char* genmsg(struct ChatHist **pp, const char *fmt, ...);

char* getftime(void);

const char * connection_enum2text(TOX_CONNECTION conn);

struct Friend *getfriend(uint32_t friend_num);

struct Friend *addfriend(uint32_t friend_num);

bool delfriend(uint32_t friend_num);

struct Group *addgroup(uint32_t group_num);

bool delgroup(uint32_t group_num);

struct Group *getgroup(uint32_t group_num);

uint8_t *hex2bin(const char *hex);

struct File_Transfer *gettransfer(uint32_t friend_num, uint32_t file_num);

struct File_Transfer *addtransfer(uint32_t friend_num, uint32_t file_num, uint64_t 
position, size_t filename_length, uint32_t kind, uint64_t file_size, uint8_t *filename, 
FILE *fd, uint8_t *file_id);

bool deltransfer(uint32_t friend_num, uint32_t file_num);

struct Call *getcall(uint32_t friend_num);

bool delcall(uint32_t friend_num);

struct Call *addcall(uint32_t friend_number, bool audio_enabled, bool video_enabled);

char *bin2hex(const uint8_t *bin, size_t length);

struct ChatHist ** get_current_histp(void);
