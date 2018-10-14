
/* **********************************************************************
   * Quotes.C   - July 1993, Fredric L. Rice.                           *
   *            - October 1994, last update.                            *
   *                                                                    *
   * Appends quotes to the end of *.MSG messages.                       *
   *                                                                    *
   * This program reads in all of the messages in your specified echo   *
   * mail directories and searches for the --- tear line.  It will then *
   * append one of your quotes above the tear line.                     *
   *                                                                    *
   ********************************************************************** */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#ifdef __TURBOC__
    #include <alloc.h>
    #include <dir.h>

    #define Bios_Com            bioscom
    #define Far_Malloc          farmalloc
    #define Far_Free            farfree
    #define Find_First(a,b,c)   findfirst(a,b,c)
    #define Find_Next           findnext
    #define File_Block          ffblk
    #define Find_Name           ff_name
#else
    #include <malloc.h>

    #define Bios_Com            _bios_serialcom
    #define Far_Malloc          _fmalloc
    #define Far_Free            _ffree
    #define Find_First(a,b,c)   _dos_findfirst(a,c,b)
    #define Find_Next           _dos_findnext
    #define File_Block          find_t
    #define Find_Name           name
#endif

/* **********************************************************************
   * Define various macros that will be needed.                         *
   *                                                                    *
   ********************************************************************** */

#define skipspace(s)    while (isspace(*s))  ++(s)

/* **********************************************************************
   * Define the global constants that will be used.                     *
   *                                                                    *
   ********************************************************************** */

#define TRUE                    1
#define FALSE                   0
#define BOOL                    unsigned char
#define USHORT                  unsigned short
#define The_Version             "2.01"

/* **********************************************************************
   * ErrorLevel values.                                                 *
   *                                                                    *
   ********************************************************************** */

#define No_Problem              0
#define No_Memory               10
#define Missing_Config          11
#define Bad_Configuration       12
#define Cant_Write_MSG_File     13

/* **********************************************************************
   * The message file format offered here is Fido format which has      *
   * been tested with OPUS and Dutchie. It represents the latest        *
   * format that I know about.                                          *
   *                                                                    *
   ********************************************************************** */

    static struct fido_msg
    {
        char from[36];             /* Who the message is from             */
        char to[36];               /* Who the message to to               */
        char subject[72];          /* The subject of the message.         */
        char date[20];             /* Message createion date/time         */
        USHORT times;              /* Number of time the message was read */
        USHORT destination_node;   /* Intended destination node           */
        USHORT originate_node;     /* The originator node of the message  */
        USHORT cost;               /* Cost to send this message           */
        USHORT originate_net;      /* The originator net of the message   */
        USHORT destination_net;    /* Intended destination net number     */
        USHORT destination_zone;   /* Intended zone for the message       */
        USHORT originate_zone;     /* The zone of the originating system  */
        USHORT destination_point;  /* Is there a point to destination?    */
        USHORT originate_point;    /* The point originated the message    */
        USHORT reply;              /* Thread to previous reply            */
        USHORT attribute;          /* Message type                        */
        USHORT upwards_reply;      /* Thread to next message reply        */
    } message;                     /* Something to store this structure   */

/* **********************************************************************
   * 'Attribute' bit definitions, some of which we will use             *
   *                                                                    *
   ********************************************************************** */

#define Fido_Private            0x0001
#define Fido_Crash              0x0002
#define Fido_Read               0x0004
#define Fido_Sent               0x0008
#define Fido_File_Attach        0x0010
#define Fido_Forward            0x0020
#define Fido_Orphan             0x0040
#define Fido_Kill               0x0080
#define Fido_Local              0x0100
#define Fido_Hold               0x0200
#define Fido_Reserved1          0x0400
#define Fido_File_Request       0x0800
#define Fido_Ret_Rec_Req        0x1000
#define Fido_Ret_Rec            0x2000
#define Fido_Req_Audit_Trail    0x4000
#define Fido_Update_Req         0x8000

/* **********************************************************************
   * Message text is read into a linked list.                           *
   *                                                                    *
   ********************************************************************** */

    static struct Message_Text
    {
        char *value;                    /* Pointer to the text          */
        struct Message_Text *next;      /* A pointer to the next one    */
    } *mt_first, *mt_last, *mt_point;   /* Make three pointers to it.   */

/* **********************************************************************
   * A list of the echo mail directories is stored in a linked list.    *
   *                                                                    *
   ********************************************************************** */

    static struct Directory_List
    {
        char *value;                    /* Pointer to the directory     */
        struct Directory_List *next;    /* A pointer to the next one    */
    } *dl_first, *dl_last, *dl_point;   /* Make three pointers to it.   */

/* **********************************************************************
   * Quotes are read into memory.                                       *
   *                                                                    *
   ********************************************************************** */

    static struct Quote_Entry
    {
        char *value;                    /* Pointer to the text          */
        struct Quote_Entry *next;       /* Pointer to the next line     */
    } ;                                 /* That should do it.           */

    static struct Quote_List
    {
        struct Quote_Entry *qe_first;   /* Pointer to the first one.    */
        struct Quote_Entry *qe_last;    /* Pointer to the last one.     */
        struct Quote_Entry *qe_point;   /* Pointer to the traverse it.  */
        struct Quote_List *next;        /* A pointer to the next one    */
    } *ql_first, *ql_last, *ql_point;   /* Make three pointers to it.   */

/* **********************************************************************
   * Define any local data.                                             *
   *                                                                    *
   ********************************************************************** */

    static USHORT quote_count;
    static USHORT directory_count;
    static USHORT want_diag;
    static char *inbound_data;

/* **********************************************************************
   * We only allow a quote to be used once and then we recycle.         *
   *                                                                    *
   ********************************************************************** */

    unsigned char used_quote[2000];
    static char config_file[101];

/* **********************************************************************
   * Initialize this program.                                           *
   *                                                                    *
   ********************************************************************** */
   
static void initialize(void)
{
    USHORT loop;
    char *env;

/*
 * Get our environment variable to determine the path
 * to our configuration file and error message file
 */

    if (NULL == (env = getenv("QUOTE")))
    {
        (void)strcpy(config_file, "QUOTE.CFG");
    }
    else
    {
        (void)strcpy(config_file, env);

        if (config_file[strlen(config_file) - 1] != '\\')
        {
            (void)strcat(config_file, "\\");
        }

        (void)strcat(config_file, "QUOTE.CFG");
    }                                                      

/*
 * Zero various items
 */

    quote_count = 0;
    directory_count = 0;
    want_diag = FALSE;

    mt_first = mt_last = mt_point = (struct Message_Text *)NULL;
    dl_first = dl_last = dl_point = (struct Directory_List *)NULL;
    ql_first = ql_last = ql_point = (struct Quote_List *)NULL;

/*
 * See our random
 */

    srand((unsigned)time(NULL));

/*
 * Allocate a memory buffer
 */

    inbound_data = (char *)Far_Malloc(1024 * 10);   /* 10K buffer */

    if (inbound_data == (char *)NULL)
    {
        (void)printf("I ran out of memory!\n");
        exit(No_Memory);
    }

/*
 * Zero what's left
 */

    for (loop = 0; loop < 2000; loop++)
        used_quote[loop] = FALSE;
}

/* **********************************************************************
   * Get me a random number to use for my quote.                        *
   *                                                                    *
   ********************************************************************** */

static int get_random_number(void)
{
    USHORT test;
    USHORT loop;
    USHORT THERE_IS_LIFE = TRUE;

    if (quote_count == 1)
        return(0);

    while (THERE_IS_LIFE)
    {
        test = rand();

        if (test <= quote_count)
        {
            if (used_quote[test - 1] == FALSE)
            {
                used_quote[test - 1] = TRUE;
                return(test - 1);
            }
            else
            {
                for (loop = 0; loop < 2000; loop++)
                {
                    if (used_quote[loop] == FALSE)
                    {
                        break;
                    }
                }

                if (loop > 1990)
                {
                    for (loop = 0; loop < 2000; loop++)
                    {
                        used_quote[loop] = FALSE;
                    }
                }
            }
        }
    }

    return(0);
}

/* **********************************************************************
   * Find how many messages there are.                                  *
   *                                                                    *
   ********************************************************************** */

static int find_message_count(char *directory)
{
    USHORT result;
    USHORT total;
    char directory_search[100];
    struct File_Block file_block;

    total = 0;

/*
 * Build the directory name to search for, include \ if needed
 */

    (void)strcpy(directory_search, directory);

    if (directory[strlen(directory) - 1] != '\\')
        (void)strcat(directory, "\\");

    (void)strcat(directory_search, "*.MSG");

/*
 * See if we have at least one
 */

    result = Find_First(directory_search, &file_block, 0x16);

    if (! result)
        total++;

/*
 * Scan all messages
 */

    while (! result)
    {
        result = Find_Next(&file_block);

        if (! result)
        {
            total++;
        }
    }

/*
 * Return the value
 */

    return(total);
}

/* **********************************************************************
   * Plug the directory.                                                *
   *                                                                    *
   ********************************************************************** */

static void plug_directory(char *atpoint)
{
    atpoint[strlen(atpoint) - 1] = (char)NULL;

/*
 * Append \ if needed
 */

    if (atpoint[strlen(atpoint) - 1] != '\\')
        (void)strcat(atpoint, "\\");

/*
 * Allocate memory for the data structure of the linked list
 */

    dl_point = (struct Directory_List *)
        Far_Malloc(sizeof(struct Directory_List));

    if (dl_point == (struct Directory_List *)NULL)
    {
        (void)printf("I ran out of memory!\n");
        fcloseall();
        exit(No_Memory);
    }

/*
 * Allocate memory for the full path
 */

    dl_point->value = (char *)Far_Malloc(strlen(atpoint) + 6);

    if (dl_point->value == (char *)NULL)
    {
        (void)printf("I ran out of memory!\n");
        fcloseall();
        exit(No_Memory);
    }

/*
 * Copy it over
 */

    (void)strcpy(dl_point->value, atpoint);

/*
 * Append the entry in the linked list.
 */

    dl_point->next = (struct Directory_List *)NULL;

    if (dl_first == (struct Directory_List *)NULL)
    {
        dl_first = dl_point;
    }
    else
    {
        dl_last->next = dl_point;
    }

    dl_last = dl_point;

    directory_count++;
}

/* **********************************************************************
   * Store the quote entry.                                             *
   *                                                                    *
   ********************************************************************** */

static void store_quote_entry(char *atpoint, struct Quote_List *ql_last)
{
/*
 * Allocate memory for the data structure of the linked list
 */

    ql_last->qe_point = (struct Quote_Entry *)
        Far_Malloc(sizeof(struct Quote_Entry));

    if (ql_last->qe_point == (struct Quote_Entry *)NULL)
    {
        (void)printf("I ran out of memory!\n");
        fcloseall();
        exit(No_Memory);
    }

/*
 * Allocate memory for the quote line
 */

    ql_last->qe_point->value = (char *)Far_Malloc(strlen(atpoint) + 2);

    if (ql_last->qe_point->value == (char *)NULL)
    {
        (void)printf("I ran out of memory!\n");
        fcloseall();
        exit(No_Memory);
    }

/*
 * Copy it over.
 */

     (void)strcpy(ql_last->qe_point->value, atpoint);

/*
 * Append the entry in the linked list.
 */

    ql_last->qe_point->next = (struct Quote_Entry *)NULL;

    if (ql_last->qe_first == (struct Quote_Entry *)NULL)
    {
        ql_last->qe_first = ql_last->qe_point;
    }
    else
    {
        ql_last->qe_last->next = ql_last->qe_point;
    }

    ql_last->qe_last = ql_last->qe_point;
}

/* **********************************************************************
   * Store a quote into memory.                                         *
   *                                                                    *
   ********************************************************************** */

static void plug_quote(char *atpoint, FILE *config)
{
    char record[201];

/*
 * Store the first line we were offered.
 */

    atpoint[strlen(atpoint) - 1] = (char)NULL;

/*
 * Allocate memory for the data structure of the linked list
 */

    ql_point = (struct Quote_List *)
        Far_Malloc(sizeof(struct Quote_List));

    if (ql_point == (struct Quote_List *)NULL)
    {
        (void)printf("I ran out of memory!\n");
        fcloseall();
        exit(No_Memory);
    }

/*
 * Append the entry in the linked list.
 */

    ql_point->qe_first = (struct Quote_Entry *)NULL;
    ql_point->qe_last  = (struct Quote_Entry *)NULL;
    ql_point->qe_point = (struct Quote_Entry *)NULL;
    ql_point->next = (struct Quote_List *)NULL;

    if (ql_first == (struct Quote_List *)NULL)
    {
        ql_first = ql_point;
    }
    else
    {
        ql_last->next = ql_point;
    }

    ql_last = ql_point;

    quote_count++;

/*
 * Store the first line of the quote entry
 */

    store_quote_entry(atpoint, ql_last);

/*
 * See if there are more.  A blank line is the end.
 */

    while (! feof(config))
    {
        (void)fgets(record, 200, config);

        if (! feof(config))
        {
            atpoint = record;

            if (strlen(atpoint) < 2)
            {
                return;
            }

            if (! strncmp(atpoint, "      ", 6))
                atpoint += 6;

            atpoint[strlen(atpoint) - 1] = (char)NULL;
            store_quote_entry(atpoint, ql_last);
        }
    }
}

/* **********************************************************************
   * Extract configuration information.                                 *
   *                                                                    *
   ********************************************************************** */

static void extract_configuration(void)
{
    char record[201], *atpoint;
    FILE *config;

/*
 * Open up the configuration file.
 */

    if ((config = fopen(config_file, "rt")) == (FILE *)NULL)
    {
        (void)printf("I was unable to find config file: %s\n", config_file);
        fcloseall();
        exit(Missing_Config);
    }

/*
 * Extract each line and parse it out, calling the function which
 * will do the majority of the work.
 */

    while (! feof(config))
    {
        (void)fgets(record, 200, config);

        if (! feof(config))
        {
            atpoint = record;
            skipspace(atpoint);

            if (! strncmp(atpoint, "look", 4))
            {
                atpoint += 4;
                skipspace(atpoint);
                plug_directory(atpoint);
            }
            else if (! strnicmp(atpoint, "quote", 5))
            {
                atpoint += 6;
                plug_quote(atpoint, config);
            }
        }
    }

    (void)fclose(config);

/*
 * Check conformity!
 */

    if (directory_count == 0)
    {
        (void)printf("Your configuration file has no directories specified.\n");
        fcloseall();
        exit(Bad_Configuration);
    }

    if (quote_count == 0)
    {
        (void)printf("Your configuration file has no quotes specified.\n");
        fcloseall();
        exit(Bad_Configuration);
    }
}

/* **********************************************************************
   * Store the line in the message text linked list.                    *
   *                                                                    *
   ********************************************************************** */

static void store_this_line(void)
{
    char *record;

/*
 * Copy the pointer to our inbound buffer
 */

    record = inbound_data;

/*
 * Allocate memory for the data structure of the linked list
 */

    mt_point = (struct Message_Text *)
        Far_Malloc(sizeof(struct Message_Text));

    if (mt_point == (struct Message_Text *)NULL)
    {
        (void)printf("I ran out of memory!\n");
        fcloseall();
        exit(No_Memory);
    }

/*
 * Allocate memory for the text
 */

    mt_point->value = (char *)Far_Malloc(strlen(record) + 2);

    if (mt_point->value == (char *)NULL)
    {
        (void)printf("I ran out of memory!\n");
        fcloseall();
        exit(No_Memory);
    }

/*
 * Copy it over
 */

	(void)strcpy(mt_point->value, record);

/*
 * Append the entry in the linked list.
 */

    mt_point->next = (struct Message_Text *)NULL;

    if (mt_first == (struct Message_Text *)NULL)
    {
        mt_first = mt_point;
    }
    else
    {
        mt_last->next = mt_point;
    }

    mt_last = mt_point;
}

/* **********************************************************************
   * Read the files text into a linked list.  If the linked list is     *
   * not empty before we start, empty it.                               *
   *                                                                    *
   ********************************************************************** */
   
static void read_message_into_memory(FILE *msg_file)
{
    struct Message_Text *hold;
    char byte;
    char *data_buffer;

/*
 * Start with any existing text in memory.
 */

    mt_point = mt_first;

    while (mt_point)
    {
        hold = mt_point->next;

        Far_Free(mt_point->value);
        Far_Free(mt_point);

        mt_point = hold;
    }

    mt_first = mt_last = mt_point = (struct Message_Text *)NULL;

/*
 * Now read it all in.
 */

    data_buffer = inbound_data;

    while (! feof(msg_file))
    {
        byte = fgetc(msg_file);

        if (! feof(msg_file))
        {
            if (byte != 0x0d && byte != 0x0a)
            {
                *data_buffer++ = byte;
            }

            if (byte == 0x0d)
            {
                *data_buffer++ = (char)NULL;
                store_this_line();
                data_buffer = inbound_data;
            }
        }
        else
        {
            *data_buffer++ = (char)NULL;
            store_this_line();
            return;
        }
    }

    *data_buffer++ = (char)NULL;
    store_this_line();
}

/* **********************************************************************
   * See if this message is already quoted.                             *
   *                                                                    *
   * If not, insert the quote in the linked list and then return with   *
   * a TRUE, else if it already exists return FALSE.                    *
   *                                                                    *
   ********************************************************************** */

static int check_message_text(struct Quote_List **which_ql,
    struct Message_Text **which_mt)
{
    char *to_test;
    struct Message_Text *mt_tear;
    struct Quote_List *ql_test;
    USHORT random_number;
    USHORT keep_searching = TRUE;

    mt_point = mt_first;

    while (mt_point)
    {
        to_test = mt_point->value;
        skipspace(to_test);

        if (! strncmp(to_test, "~*~", 3))
            return(FALSE);

        if (! strncmp(to_test, "---", 3))
        {
            keep_searching = FALSE;
        }
        else
        {
            if (keep_searching)
            {
                mt_tear = mt_point;
            }
        }

        mt_point = mt_point->next;
    }

/*
 * It needs a quote.
 * We select a quote 'randomly.'
 */

    random_number = get_random_number();
    ql_test = ql_first;

    while (random_number > 0)
    {
        ql_test = ql_test->next;
        random_number--;
    }

    *which_ql = ql_test;
    *which_mt = mt_tear;

    return(TRUE);
}

/* **********************************************************************
   * Include the address information.                                   *
   *                                                                    *
   ********************************************************************** */

static void include_address(FILE *msg_file)
{
    unsigned char report[101];

    (void)sprintf(report,
        "           http://www.skeptictank.org/%c%c",
        0x0d, 0x0a);

    (void)fputs(report, msg_file);

    (void)sprintf(report,
        "           frice@skeptictank.org   <DARWIN><%c%c%c%c",
        0x0d, 0x0a, 0x0d, 0x0a);

    (void)fputs(report, msg_file);
}

/* **********************************************************************
   * Open the file and see if it's local.  If it is, read it into       *
   * memory and then check to see if it's already tagged with our       *
   * quote facility.  If not, we want to quote it.                      *
   *                                                                    *
   ********************************************************************** */

static void check_message(char *directory, char *fname)
{
    char directory_search[201];
    FILE *msg_file;
    USHORT result;
    struct Quote_List *ql_which;
    struct Message_Text *mt_which;
    USHORT done_tag = FALSE;

    (void)strcpy(directory_search, directory);

    if (directory[strlen(directory) - 1] != '\\')
        (void)strcat(directory, "\\");

    (void)strcat(directory_search, fname);

/*
 * Open it for a binary read
 */

    if ((msg_file = fopen(directory_search, "r+b")) == (FILE *)NULL)
    {
        (void)printf("---> I was unable to open message file %s!\n",
            directory_search);

        return;
    }

    if (want_diag)
        (void)printf("File %s\n", directory_search);

/*
 * Get the header off of the message file
 */

    if (fread(&message, sizeof(struct fido_msg), 1, msg_file) != 1)
    {
        (void)printf("---> I was unable to read message file: %s!\n",
            directory_search);

        (void)fclose(msg_file);
        return;
    }

/*
 * See if it's local.  If not, return and ignore the puppy.
 */

    if ((message.attribute & Fido_Local) != Fido_Local)
    {
        (void)fclose(msg_file);
        return;
    }

/*
 * Seems to be local.  Read it into memory.
 */

    read_message_into_memory(msg_file);

/*
 * Scan it for our quote mark.  If it's already quoted then
 * ignore the message.  Don't worry about the memory which was
 * allocated for the message.  When we read a message into
 * memory we automatically free up anything which may be left.
 */

    result = check_message_text(&ql_which, &mt_which);

    if (! result)
    {
        (void)fclose(msg_file);
        return;
    }

    (void)printf("      Adding quote to message: %s\n", directory_search);

/*
 * It's needing our quote.  Write the header first.
 */

    (void)rewind(msg_file);

    if (fwrite(&message, sizeof(struct fido_msg), 1, msg_file) != 1)
    {
        (void)printf("I was unable to write message file!\n");
        fcloseall();
        exit(Cant_Write_MSG_File);
    }

/*
 * We've updated the header...  Let's do the next now.
 */

    mt_point = mt_first;

    while (mt_point)
    {
        if (mt_point == mt_which)
        {
            ql_which->qe_point = ql_which->qe_first;

            (void)fputc(0x0d, msg_file);
            (void)fputc(0x0a, msg_file);

            while (ql_which->qe_point)
            {
                if (! done_tag)
                {
                    include_address(msg_file);

                    (void)fputs("~*~", msg_file);
                    done_tag = TRUE;
                }
                else
                {
                    (void)fputs("   ", msg_file);
                }

                (void)fputs(ql_which->qe_point->value, msg_file);
                (void)fputc(0x0d, msg_file);
                (void)fputc(0x0a, msg_file);

                ql_which->qe_point = ql_which->qe_point->next;
            }
        }

        (void)fputs(mt_point->value, msg_file);
        (void)fputc(0x0d, msg_file);
        (void)fputc(0x0a, msg_file);

        mt_point = mt_point->next;
    }

    (void)fclose(msg_file);
}

/* **********************************************************************
   * Scan through the specified directory for messages which do not     *
   * have a quote on it.  If there is one detected, call another        *
   * routine which will take care of it.                                *
   *                                                                    *
   ********************************************************************** */

static void process_this_directory(struct Directory_List *dl_point)
{
    char directory_search[101];
    struct File_Block file_block;
    USHORT mail_count;
    USHORT result;

    mail_count = find_message_count(dl_point->value);

    (void)printf("            Look: %s (message count: %d)\n",
        dl_point->value, mail_count);

/*
 * Build the directory name to search for, include \ if needed
 */

    (void)strcpy(directory_search, dl_point->value);

    if (directory_search[strlen(directory_search) - 1] != '\\')
        (void)strcat(directory_search, "\\");

    (void)strcat(directory_search, "*.MSG");

/*
 * See if we have at least one
 */

    result = Find_First(directory_search, &file_block, 0x16);

    if (! result)
        check_message(dl_point->value, file_block.Find_Name);

/*
 * Scan all messages
 */

    while (! result)
    {
        result = Find_Next(&file_block);

        if (! result)
        {
            check_message(dl_point->value, file_block.Find_Name);
        }
    }
}

/* **********************************************************************
   * Process each directory one at a time.                              *
   *                                                                    *
   ********************************************************************** */

static void process_directories(void)
{
    dl_point = dl_first;

    while (dl_point)
    {
        process_this_directory(dl_point);
        dl_point = dl_point->next;
    }
}

/* **********************************************************************
   * Dump the quotes to the screen.                                     *
   *                                                                    *
   ********************************************************************** */

static void dump_quotes(void)
{
    ql_point = ql_first;

    while (ql_point)
    {
        (void)printf("Quote:\n");

        ql_point->qe_point = ql_point->qe_first;

        while (ql_point->qe_point)
        {
            (void)printf("[%s]\n", ql_point->qe_point->value);

            ql_point->qe_point = ql_point->qe_point->next;
        }

        (void)printf("\n");

        ql_point = ql_point->next;
    }
}

/* **********************************************************************
   * Tell the world we're running.                                      *
   *                                                                    *
   ********************************************************************** */

static void say_hello(void)
{
    (void)printf("\n\n    Quote version %s\n", The_Version);

    (void)printf("        I see %d %s and %d quote%s to work with...\n",
        directory_count,
        directory_count == 1 ? "directory" : "directories",
        quote_count,
        quote_count == 1 ? "" : "s");
}

/* **********************************************************************
   * The main entry point.                                              *
   *                                                                    *
   ********************************************************************** */

void main(USHORT argc, char *argv[])
{
    USHORT loop;

/*
 * Initialize this program
 */

    initialize();

/*
 * Get our configuration
 */

    extract_configuration();

/*
 * Say hello to everyone
 */

    say_hello();

/*
 * See what command-line switches there may be
 */

    for (loop = 1; loop < argc; loop++)
    {
        if (! strncmp(argv[loop], "/diag", 5))
        {
            want_diag = TRUE;
        }
    }

/*
 * If we are in diagnostic testing, dump the quotes
 */

    if (want_diag)
        dump_quotes();

/*
 * Process all directories
 */

    process_directories();
    exit(No_Problem);
}


