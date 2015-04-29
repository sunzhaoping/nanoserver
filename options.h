#ifndef SN_OPTIONS_HEADER
#define SN_OPTIONS_HEADER

enum sn_option_type {
    SN_OPT_HELP,
    SN_OPT_INT,
    SN_OPT_INCREMENT,
    SN_OPT_DECREMENT,
    SN_OPT_ENUM,
    SN_OPT_SET_ENUM,
    SN_OPT_STRING,
    SN_OPT_BLOB,
    SN_OPT_FLOAT,
    SN_OPT_LIST_APPEND,
    SN_OPT_LIST_APPEND_FMT,
    SN_OPT_READ_FILE
};

struct sn_option {
    /*  Option names  */
    char *longname;
    char shortname;
    char *arg0name;

    /*  Parsing specification  */
    enum sn_option_type type;
    int offset;  /*  offsetof() where to store the value  */
    const void *pointer;  /*  type specific pointer  */

    /*  Conflict mask for options  */
    unsigned long mask_set;
    unsigned long conflicts_mask;
    unsigned long requires_mask;

    /*  Group and description for --help  */
    char *group;
    char *metavar;
    char *description;
};

struct sn_commandline {
    char *short_description;
    char *long_description;
    struct sn_option *options;
    int required_options;
};

struct sn_enum_item {
    char *name;
    int value;
};

struct sn_string_list {
    char **items;
    char **to_free;
    int num;
    int to_free_num;
};

struct sn_blob {
    char *data;
    int length;
    int need_free;
};


void sn_parse_options (struct sn_commandline *cline,
                      void *target, int argc, char **argv);
void sn_free_options (struct sn_commandline *cline, void *target);


#endif