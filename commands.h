#define COMMAND_ARGS_REST 10
#define COMMAND_LENGTH (sizeof(commands)/sizeof(struct Command))

/*******************************************************************************
 *
 * Commands
 *
 ******************************************************************************/
void command_help(int narg, char **args);

void command_guide(int narg, char **args);

void _print_friend_info(struct Friend *f, bool is_self);

void command_info(int narg, char **args);

void command_setname(int narg, char **args);

void command_setstmsg(int narg, char **args);

void command_add(int narg, char **args);

void command_del(int narg, char **args);

void command_contacts(int narg, char **args);

void command_save(int narg, char **args);

void command_go(int narg, char **args);

void command_history(int narg, char **args);

void _command_accept(int narg, char **args, bool is_accept);

void command_accept(int narg, char **args);

void command_deny(int narg, char **args);

void command_invite(int narg, char **args);

void command_settitle(int narg, char **args);

void command_transfer(int narg, char **args);

void command_answer(int narg, char **args);

void command_call(int narg, char **args);

static struct Command commands[] = {
    {
        "guide",
        "- print the guide",
        0,
        command_guide,
    },
    {
        "help",
        "- print this message.",
        0,
        command_help,
    },
    {
        "save",
        "- save your data.",
        0,
        command_save,
    },
    {
        "info",
        "[<contact_index>] - show one contact's info, or yourself's info if <contact_index> is empty. ",
        0 + COMMAND_ARGS_REST,
        command_info,
    },
    {
        "setname",
        "<name> - set your name",
        1,
        command_setname,
    },
    {
        "setstmsg",
        "<status_message> - set your status message.",
        1,
        command_setstmsg,
    },
    {
        "add",
        "<toxid> <msg> - add friend",
        2,
        command_add,
    },
    {
        "del",
        "<contact_index> - del a contact.",
        1,
        command_del,
    },
    {
        "contacts",
        "- list your contacts(friends and groups).",
        0,
        command_contacts,
    },
    {
        "go",
        "[<contact_index>] - goto talk to a contact, or goto cmd mode if <contact_index> is empty.",
        0 + COMMAND_ARGS_REST,
        command_go,
    },
    {
        "history",
        "[<n>] - show previous <n> items(default:10) of current chat history",
        0 + COMMAND_ARGS_REST,
        command_history,
    },
    {
        "accept",
        "[<request_index>] - accept or list(if no <request_index> was provided) friend/group requests.",
        0 + COMMAND_ARGS_REST,
        command_accept,
    },
    {
        "deny",
        "[<request_index>] - deny or list(if no <request_index> was provided) friend/group requests.",
        0 + COMMAND_ARGS_REST,
        command_deny,
    },
    {
        "invite",
        "<friend_contact_index> [<group_contact_index>] - invite a friend to a group chat. default: create a group.",
        1 + COMMAND_ARGS_REST,
        command_invite,
    },
    {
        "settitle",
        "<group_contact_index> <title> - set group title.",
        2,
        command_settitle,
    },
    {
        "transfer",
        "<friend_contact_index> <file_path> - send file to friend.",
        2,
        command_transfer,
    },
    {
        "answer",
        "<friend_contact_index> - answer a call.",
        1,
        command_answer,
    },
    {
        "call",
        "<friend_contact_index> - call a friend.",
        1,
        command_call,
    },
};
