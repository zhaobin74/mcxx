#ifdef HAVE_CONFIG_H
   #include "config.h"
#endif


#include "cxx-process.h"
#include "cxx-utils.h"
#include "cxx-diagnostic.h"
#include "fortran03-lexer.h"
#include "fortran03-utils.h"
#include "fortran03-parser-internal.h"

#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>

#include <ctype.h>


#ifdef FORTRAN_NEW_SCANNER
#define new_mf03lex mf03lex
#define new_mf03_open_file_for_scanning mf03_open_file_for_scanning
#define new_mf03_prepare_string_for_scanning mf03_prepare_string_for_scanning
#endif // NEW_FORTRAN_SCANNER

/*
   Include stack.

   The maxim level of nesting is not defined in Fortran 95 standard.
   We have set it to 99.
 */

enum {
    MAX_INCLUDE_DEPTH = 99,
};


typedef
struct token_location_tag
{
    const char* filename;
    int line;
    int column;
} token_location_t;

struct scan_file_descriptor
{
    const char *current_pos; // position in the buffer

    const char *buffer; // scanned buffer
    size_t buffer_size; // number of characters in buffer relevant for scanning

    // Physical filename scanned (may be different in fixed form)
    const char* scanned_filename;

    int fd; // if fd >= 0 this is a mmap

    token_location_t current_location;
};

enum lexer_textual_form
{
    LX_INVALID_FORM = 0,
    LX_FREE_FORM = 1,

    // Not yet implemented
    LX_FIXED_FORM = 2,
};

enum lexing_substate
{
    LEXER_SUBSTATE_NORMAL = 0,

    // !$OMP ...
    LEXER_SUBSTATE_PRAGMA_DIRECTIVE,
    LEXER_SUBSTATE_PRAGMA_FIRST_CLAUSE,
    LEXER_SUBSTATE_PRAGMA_CLAUSE,
    LEXER_SUBSTATE_PRAGMA_VAR_LIST,
};

static
struct new_lexer_state_t
{
    enum lexer_textual_form form;
    enum lexing_substate substate;

    int include_stack_size;
    struct scan_file_descriptor include_stack[MAX_INCLUDE_DEPTH];
    struct scan_file_descriptor *current_file;

    // If not null, we are scanning through a sentinel (i.e. "omp")
    char *sentinel;

    // beginning of line
    char bol:1;
    // last token was end of line (the parser does not like redundant EOS)
    char last_eos:1; 
    // states that we are inside a string-literal (changes the way we handle
    // continuations)
    char character_context:1;
    // we are scanning a format statement
    char in_format_statement:1;
    // we have seen a nonblock do construct
    char in_nonblock_do_construct:1;

    int num_nonblock_labels;
    int size_nonblock_labels_stack;
    int *nonblock_labels_stack;

    int num_pragma_constructs;
    int size_pragma_constructs_stack;
    char** pragma_constructs_stack;
} lexer_state;

static token_location_t get_current_location(void)
{
    return lexer_state.current_file->current_location;
}

int mf03_flex_debug = 1;

static void init_lexer_state(void);

extern int new_mf03_open_file_for_scanning(const char* scanned_filename, const char* input_filename)
{
    int fd = open(scanned_filename, O_RDONLY);
    if (fd < 0)
    {
        running_error("error: cannot open file '%s' (%s)", scanned_filename, strerror(errno));
    }

    // Get size of file because we need it for the mmap
    struct stat s;
    int status = fstat (fd, &s);
    if (status < 0)
    {
        running_error("error: cannot get status of file '%s' (%s)", scanned_filename, strerror(errno));
    }

    const char *mmapped_addr = mmap(0, s.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (mmapped_addr == MAP_FAILED)
    {
        running_error("error: cannot map file '%s' in memory (%s)", scanned_filename, strerror(errno));
    }

    lexer_state.include_stack_size = 0;
    lexer_state.current_file = &lexer_state.include_stack[lexer_state.include_stack_size];

    lexer_state.current_file->scanned_filename = scanned_filename;

    lexer_state.current_file->fd = fd;
    lexer_state.current_file->buffer_size = s.st_size;
    lexer_state.current_file->current_pos
        = lexer_state.current_file->buffer = mmapped_addr;

    lexer_state.current_file->current_location.filename = input_filename;
    lexer_state.current_file->current_location.line = 1;
    lexer_state.current_file->current_location.column = 0;

    init_lexer_state();

    return 0;
}

// KEEP THESE TABLES SORTED
static
struct keyword_table_tag
{
    const char* keyword;
    int token_id;
} keyword_table[] =
{
    {"abstract", TOKEN_ABSTRACT},
    {"access", TOKEN_ACCESS},
    {"acquired", TOKEN_ACQUIRED},
    {"action", TOKEN_ACTION},
    {"advance", TOKEN_ADVANCE},
    {"all", TOKEN_ALL},
    {"allocatable", TOKEN_ALLOCATABLE},
    {"allocate", TOKEN_ALLOCATE},
    {"allstop", TOKEN_ALLSTOP},
    {"assign", TOKEN_ASSIGN},
    {"assignment", TOKEN_ASSIGNMENT},
    {"associate", TOKEN_ASSOCIATE},
    {"asynchronous", TOKEN_ASYNCHRONOUS},
    {"backspace", TOKEN_BACKSPACE},
    {"bind", TOKEN_BIND},
    {"blank", TOKEN_BLANK},
    {"block", TOKEN_BLOCK},
    {"blockdata", TOKEN_BLOCKDATA}, 
    {"c", TOKEN_C},
    {"call", TOKEN_CALL},
    {"case", TOKEN_CASE},
    {"character", TOKEN_CHARACTER},
    {"class", TOKEN_CLASS},
    {"close", TOKEN_CLOSE},
    {"codimension", TOKEN_CODIMENSION},
    {"common", TOKEN_COMMON},
    {"complex", TOKEN_COMPLEX},
    {"concurrent", TOKEN_CONCURRENT},
    {"contains", TOKEN_CONTAINS},
    {"contiguous", TOKEN_CONTIGUOUS},
    {"continue", TOKEN_CONTINUE},
    {"convert", TOKEN_CONVERT},
    {"critical", TOKEN_CRITICAL},
    {"cycle", TOKEN_CYCLE},
    {"data", TOKEN_DATA},
    {"deallocate", TOKEN_DEALLOCATE},
    {"decimal", TOKEN_DECIMAL},
    {"default", TOKEN_DEFAULT},
    {"deferred", TOKEN_DEFERRED},
    {"delim", TOKEN_DELIM},
    {"dimension", TOKEN_DIMENSION},
    {"direct", TOKEN_DIRECT},
    {"do", TOKEN_DO},
    {"double", TOKEN_DOUBLE},
    {"doublecomplex", TOKEN_DOUBLECOMPLEX},
    {"doubleprecision", TOKEN_DOUBLEPRECISION}, 
    {"elemental", TOKEN_ELEMENTAL},
    {"else", TOKEN_ELSE},
    {"elseif", TOKEN_ELSEIF}, 
    {"elsewhere", TOKEN_ELSEWHERE},
    {"encoding", TOKEN_ENCODING},
    {"end", TOKEN_END},
    {"endassociate", TOKEN_ENDASSOCIATE},
    {"endblock", TOKEN_ENDBLOCK},
    {"endblockdata", TOKEN_ENDBLOCKDATA},
    {"endcritical", TOKEN_ENDCRITICAL},
    {"enddo", TOKEN_ENDDO},
    {"endfile", TOKEN_ENDFILE},
    {"endfunction", TOKEN_ENDFUNCTION},
    {"endif", TOKEN_ENDIF},
    {"endinterface", TOKEN_ENDINTERFACE},
    {"endmodule", TOKEN_ENDMODULE},
    {"endprocedure", TOKEN_ENDPROCEDURE},
    {"endprogram", TOKEN_ENDPROGRAM},
    {"endselect", TOKEN_ENDSELECT},
    {"endsubmodule", TOKEN_ENDSUBMODULE},
    {"endsubroutine", TOKEN_ENDSUBROUTINE},
    {"endtype", TOKEN_ENDTYPE},
    {"endwhere", TOKEN_ENDWHERE},
    {"entry", TOKEN_ENTRY},
    {"enum", TOKEN_ENUM},
    {"enumerator", TOKEN_ENUMERATOR},
    {"eor", TOKEN_EOR},
    {"equivalence", TOKEN_EQUIVALENCE},
    {"err", TOKEN_ERR},
    {"errmsg", TOKEN_ERRMSG},
    {"exist", TOKEN_EXIST},
    {"exit", TOKEN_EXIT},
    {"extends", TOKEN_EXTENDS},
    {"external", TOKEN_EXTERNAL},
    {"file", TOKEN_FILE},
    {"final", TOKEN_FINAL},
    {"flush", TOKEN_FLUSH},
    {"fmt", TOKEN_FMT},
    {"forall", TOKEN_FORALL},
    {"form", TOKEN_FORM},
    {"format", TOKEN_FORMAT},
    {"formatted", TOKEN_FORMATTED},
    {"function", TOKEN_FUNCTION},
    {"generic", TOKEN_GENERIC},
    {"go", TOKEN_GO},
    {"goto", TOKEN_GOTO},
    {"id", TOKEN_ID},
    {"if", TOKEN_IF},
    {"images", TOKEN_IMAGES},
    {"implicit", TOKEN_IMPLICIT},
    {"import", TOKEN_IMPORT},
    {"impure", TOKEN_IMPURE},
    {"in", TOKEN_IN},
    {"inout", TOKEN_INOUT},
    {"inquire", TOKEN_INQUIRE},
    {"integer", TOKEN_INTEGER},
    {"intent", TOKEN_INTENT},
    {"interface", TOKEN_INTERFACE},
    {"intrinsic", TOKEN_INTRINSIC},
    {"iolength", TOKEN_IOLENGTH},
    {"iomsg", TOKEN_IOMSG},
    {"iostat", TOKEN_IOSTAT},
    {"is", TOKEN_IS},
    {"kind", TOKEN_KIND},
    {"len", TOKEN_LEN},
    {"lock", TOKEN_LOCK},
    {"logical", TOKEN_LOGICAL},
    {"memory", TOKEN_MEMORY},
    {"module", TOKEN_MODULE},
    {"mold", TOKEN_MOLD},
    {"name", TOKEN_NAME},
    {"named", TOKEN_NAMED},
    {"namelist", TOKEN_NAMELIST},
    {"newunit", TOKEN_NEWUNIT},
    {"nextrec", TOKEN_NEXTREC},
    {"nml", TOKEN_NML},
    {"none", TOKEN_NONE},
    {"nopass", TOKEN_NOPASS},
    {"nullify", TOKEN_NULLIFY},
    {"number", TOKEN_NUMBER},
    {"only", TOKEN_ONLY},
    {"open", TOKEN_OPEN},
    {"opencl", TOKEN_OPENCL},
    {"opened", TOKEN_OPENED},
    {"operator", TOKEN_OPERATOR},
    {"optional", TOKEN_OPTIONAL},
    {"out", TOKEN_OUT},
    {"overridable", TOKEN_OVERRIDABLE},
    {"pad", TOKEN_PAD},
    {"parameter", TOKEN_PARAMETER},
    {"pass", TOKEN_PASS},
    {"pause", TOKEN_PAUSE},
    {"pending", TOKEN_PENDING},
    {"pixel", TOKEN_PIXEL},
    {"pointer", TOKEN_POINTER},
    {"pos", TOKEN_POS},
    {"position", TOKEN_POSITION},
    {"precision", TOKEN_PRECISION},
    {"print", TOKEN_PRINT},
    {"private", TOKEN_PRIVATE},
    {"procedure", TOKEN_PROCEDURE},
    {"program", TOKEN_PROGRAM},
    {"protected", TOKEN_PROTECTED},
    {"public", TOKEN_PUBLIC},
    {"pure", TOKEN_PURE},
    {"read", TOKEN_READ},
    {"readwrite", TOKEN_READWRITE},
    {"real", TOKEN_REAL},
    {"rec", TOKEN_REC},
    {"recl", TOKEN_RECL},
    {"recursive", TOKEN_RECURSIVE},
    {"result", TOKEN_RESULT},
    {"return", TOKEN_RETURN},
    {"rewind", TOKEN_REWIND},
    {"round", TOKEN_ROUND},
    {"save", TOKEN_SAVE},
    {"select", TOKEN_SELECT},
    {"selectcase", TOKEN_SELECTCASE},
    {"sequence", TOKEN_SEQUENCE},
    {"sequential", TOKEN_SEQUENTIAL},
    {"sign", TOKEN_SIGN},
    {"size", TOKEN_SIZE},
    {"source", TOKEN_SOURCE},
    {"stat", TOKEN_STAT},
    {"status", TOKEN_STATUS},
    {"stop", TOKEN_STOP},
    {"stream", TOKEN_STREAM},
    {"submodule", TOKEN_SUBMODULE},
    {"subroutine", TOKEN_SUBROUTINE},
    {"sync", TOKEN_SYNC},
    {"target", TOKEN_TARGET},
    {"then", TOKEN_THEN},
    {"to", TOKEN_TO},
    {"type", TOKEN_TYPE},
    {"unformatted", TOKEN_UNFORMATTED},
    {"unit", TOKEN_UNIT},
    {"unlock", TOKEN_UNLOCK},
    {"use", TOKEN_USE},
    {"value", TOKEN_VALUE},
    {"vector", TOKEN_VECTOR},
    {"volatile", TOKEN_VOLATILE},
    {"wait", TOKEN_WAIT},
    {"where", TOKEN_WHERE},
    {"while", TOKEN_WHILE},
    {"write", TOKEN_WRITE}
};

static
struct special_token_table_tag
{
    const char* keyword;
    int token_id;
    char preserve_eos;
} special_tokens[] =
{
    {"@END GLOBAL@", END_GLOBAL, 0 },
    {"@EXPRESSION@", SUBPARSE_EXPRESSION, 0 },
    {"@GLOBAL@", GLOBAL, 0 },
    {"@IS_VARIABLE@", TOKEN_IS_VARIABLE, 0 },
    {"@NODECL-LITERAL-EXPR@",  NODECL_LITERAL_EXPR, 0 },
    {"@NODECL-LITERAL-STMT@", NODECL_LITERAL_STMT, 0 },
    {"@OMP-DECLARE-REDUCTION@", SUBPARSE_OPENMP_DECLARE_REDUCTION, 1 },
    {"@OMP-DEPEND-ITEM@", SUBPARSE_OPENMP_DEPEND_ITEM, 1 },
    {"@OMPSS-DEPENDENCY-EXPR@", SUBPARSE_OMPSS_DEPENDENCY_EXPRESSION, 1 },
    {"@PROGRAM-UNIT@", SUBPARSE_PROGRAM_UNIT, 0 },
    {"@STATEMENT@", SUBPARSE_STATEMENT, 0 },
    {"@SYMBOL-LITERAL-REF@", SYMBOL_LITERAL_REF, 0 },
    {"@TYPEDEF@", TYPEDEF, 0 },
    {"@TYPE-LITERAL-REF@", TYPE_LITERAL_REF, 0 },
};


static int keyword_table_comp(
        const void* p1,
        const void* p2)
{
    const struct keyword_table_tag* v1 = (const struct keyword_table_tag*)p1;
    const struct keyword_table_tag* v2 = (const struct keyword_table_tag*)p2;

    return strcasecmp(v1->keyword, v2->keyword);
}

static int special_token_table_comp(
        const void* p1,
        const void* p2)
{
    const struct special_token_table_tag* v1 = (const struct special_token_table_tag*)p1;
    const struct special_token_table_tag* v2 = (const struct special_token_table_tag*)p2;

    return strcasecmp(v1->keyword, v2->keyword);
}

static void peek_init(void);
static void init_lexer_state(void)
{
    lexer_state.substate = LEXER_SUBSTATE_NORMAL;
    lexer_state.bol = 1;
    lexer_state.last_eos = 1;
    lexer_state.in_nonblock_do_construct = 0;
    lexer_state.num_nonblock_labels = 0;
    lexer_state.num_pragma_constructs = 0;

    peek_init();
}

static const char * const TL_SOURCE_STRING = "MERCURIUM_INTERNAL_SOURCE";

extern int new_mf03_prepare_string_for_scanning(const char* str)
{
    static int num_string = 0;

    DEBUG_CODE()
    {
        fprintf(stderr, "* Going to parse string in Fortran\n");
        fprintf(stderr, "%s\n", str);
        fprintf(stderr, "* End of parsed string\n");
    }

    lexer_state.include_stack_size = 0;
    lexer_state.current_file = &(lexer_state.include_stack[lexer_state.include_stack_size]);
  
    const char* filename = NULL;
    uniquestr_sprintf(&filename, "%s-%s-%d", TL_SOURCE_STRING, CURRENT_COMPILED_FILE->input_filename, num_string);
    num_string++;

    lexer_state.current_file->fd = -1; // not an mmap
    lexer_state.current_file->buffer_size = strlen(str);
    lexer_state.current_file->current_pos
        = lexer_state.current_file->buffer = str;

    lexer_state.current_file->current_location.filename = filename;
    lexer_state.current_file->current_location.line = 1;
    lexer_state.current_file->current_location.column = 0;
    
    init_lexer_state();

    return 0;
}

static inline void close_current_file(void)
{
    if (lexer_state.current_file->fd >= 0)
    {
        int res = munmap((void*)lexer_state.current_file->buffer, lexer_state.current_file->buffer_size);
        if (res < 0)
        {
            running_error("error: unmaping of file '%s' failed (%s)\n", lexer_state.current_file->current_location.filename, strerror(errno));
        }
        res = close(lexer_state.current_file->fd);
        if (res < 0)
        {
            running_error("error: closing file '%s' failed (%s)\n", lexer_state.current_file->current_location.filename, strerror(errno));
        }
        lexer_state.current_file->fd = -1;
    }
}

static inline char process_end_of_file(void)
{
    // Are we in the last file?
    if (lexer_state.include_stack_size == 0)
    {
        close_current_file();
        return 1;
    }
    else
    {
        DEBUG_CODE() DEBUG_MESSAGE("End of included file %s switching back to %s", 
                lexer_state.current_file->current_location.filename, lexer_state.include_stack[lexer_state.include_stack_size-1].current_location.filename);

        close_current_file();

        lexer_state.include_stack_size--;
        lexer_state.current_file = &(lexer_state.include_stack[lexer_state.include_stack_size]);
        lexer_state.last_eos = 1;
        lexer_state.bol = 1;
        lexer_state.in_nonblock_do_construct = 0;

        return 0;
    }
}


static inline char is_blank(int c)
{
    return (c == ' ' || c == '\t');
}

static inline char is_newline(int c)
{
    return c == '\n'
        || c == '\r';
}

static inline char is_letter(int c)
{
    return ('a' <= c && c <= 'z')
        || ('A' <= c && c <= 'Z');
}

static inline char is_decimal_digit(int c)
{
    return ('0' <= c && c <= '9');
}

static inline char is_binary_digit(int c)
{
    return c == '0' || c == '1';
}

static inline char is_octal_digit(int c)
{
    return '0' <= c && c <= '7';
}

static inline char is_hex_digit(int c)
{
    return ('0' <= c && c <= '9')
        || ('a' <= c && c <= 'f')
        || ('A' <= c && c <= 'F');
}

static inline char past_eof(void)
{
    return (lexer_state.current_file->current_pos >= ((lexer_state.current_file->buffer + lexer_state.current_file->buffer_size)));
}

static char handle_preprocessor_line(void)
{
    const char* keep = lexer_state.current_file->current_pos;
    int keep_column = lexer_state.current_file->current_location.column;

#define ROLLBACK \
    { \
        lexer_state.current_file->current_pos = keep; \
        lexer_state.current_file->current_location.column = keep_column; \
        return 0; \
    }

    // We allow the following syntax
    // ^([ ]*line)?[ ]+[0-9]+[ ]*("[^"]*"[ ]+([1234][ ]+)*$
    while (!past_eof()
            && is_blank(lexer_state.current_file->current_pos[0]))
    {
        lexer_state.current_file->current_pos++;
        lexer_state.current_file->current_location.column++;
    }

    if (past_eof())
        ROLLBACK;

    if (lexer_state.current_file->current_pos[0] == 'l')
    {
        // Attempt to match 'line'
        const char c[] = "line";
        int i = 1; // we already know it starts by 'l'
        lexer_state.current_file->current_pos++;
        while (c[i] != '\0'
                && !past_eof()
                && c[i] == lexer_state.current_file->current_pos[0])
        {
            lexer_state.current_file->current_pos++;
            lexer_state.current_file->current_location.column++;

            i++;
        }

        if (past_eof()
                || c[i] != '\0')
            ROLLBACK;

        if (!is_blank(lexer_state.current_file->current_pos[0]))
            ROLLBACK;

        while (!past_eof()
                && is_blank(lexer_state.current_file->current_pos[0]))
        {
            lexer_state.current_file->current_pos++;
            lexer_state.current_file->current_location.column++;
        }
    }

    // linenum
    if (!is_decimal_digit(lexer_state.current_file->current_pos[0]))
        ROLLBACK;

    int linenum = 0;

    while (!past_eof()
            && is_decimal_digit(lexer_state.current_file->current_pos[0]))
    {
        linenum = 10*linenum + (lexer_state.current_file->current_pos[0] - '0');
        lexer_state.current_file->current_pos++;
        lexer_state.current_file->current_location.column++;
    }

    // This is not possible, fix it to 1
    if (linenum == 0)
        linenum = 1;

    token_location_t filename_loc = lexer_state.current_file->current_location;

    while (!past_eof()
            && is_blank(lexer_state.current_file->current_pos[0]))
    {
        lexer_state.current_file->current_pos++;
        lexer_state.current_file->current_location.column++;
    }

    if (past_eof())
    {
        lexer_state.current_file->current_location.line = linenum;
        lexer_state.current_file->current_location.column = 0;

        return 1;
    }
    else if (is_newline(lexer_state.current_file->current_pos[0]))
    {
        if (lexer_state.current_file->current_pos[0] == '\r')
        {
            lexer_state.current_file->current_pos++;
            if (!past_eof()
                    && lexer_state.current_file->current_pos[0] == '\n')
                lexer_state.current_file->current_pos++;
        }
        else // '\n'
        {
            lexer_state.current_file->current_pos++;
        }

        lexer_state.current_file->current_location.line = linenum;
        lexer_state.current_file->current_location.column = 0;

        return 1;
    }
    else if (lexer_state.current_file->current_pos[0] == '"')
    {
        lexer_state.current_file->current_pos++;
        lexer_state.current_file->current_location.column++;

        const char *start = lexer_state.current_file->current_pos;

        // FIXME - escaped characters
        while (!past_eof()
                && lexer_state.current_file->current_pos[0] != '"'
                && !is_newline(lexer_state.current_file->current_pos[0]))
        {
            lexer_state.current_file->current_pos++;
            lexer_state.current_file->current_location.column++;
        }

        if (lexer_state.current_file->current_pos[0] != '"')
            ROLLBACK;

        const char* final = lexer_state.current_file->current_pos - 1;

        if (final - start == 0)
            ROLLBACK;

        int num_chars = final - start + 1;
        int num_bytes_buffer = num_chars + 1;
        char filename[num_bytes_buffer];
        memcpy(filename, start, num_chars);
        filename[num_bytes_buffer - 1] = '\0';

        // advance delim
        lexer_state.current_file->current_pos++;
        lexer_state.current_file->current_location.column++;

        for (;;)
        {
            while (!past_eof()
                    && is_blank(lexer_state.current_file->current_pos[0]))
            {
                lexer_state.current_file->current_pos++;
                lexer_state.current_file->current_location.column++;
            }

            if (past_eof())
                break;

            if (is_newline(lexer_state.current_file->current_pos[0]))
                break;

            token_location_t flag_loc = lexer_state.current_file->current_location;

            int flag = 0;
            if (is_decimal_digit(lexer_state.current_file->current_pos[0]))
            {
                while (!past_eof()
                        && is_decimal_digit(lexer_state.current_file->current_pos[0]))
                {
                    flag = 10*flag + (lexer_state.current_file->current_pos[0] - '0');

                    lexer_state.current_file->current_pos++;
                    lexer_state.current_file->current_location.column++;
                }

                if (1 <= flag
                        && flag <= 4)
                {
                    // We could do something with these
                }
                else
                {
                    warn_printf("%s:%d:%d: invalid flag %d\n",
                            flag_loc.filename,
                            flag_loc.line,
                            flag_loc.column,
                            flag);
                }
            }
            else
            {
                warn_printf("%s:%d:%d: unexpected tokens at end of line-marker\n",
                        lexer_state.current_file->current_location.filename,
                        lexer_state.current_file->current_location.line,
                        lexer_state.current_file->current_location.column);
                break;
            }
        }

        // Go to end of the line
        while (!past_eof()
                && is_newline(lexer_state.current_file->current_pos[0]))
        {
            lexer_state.current_file->current_pos++;
            lexer_state.current_file->current_location.column++;
        }

        if (past_eof())
        {
        }
        else if (is_newline(lexer_state.current_file->current_pos[0]))
        {
            lexer_state.current_file->current_location.line++;
            lexer_state.current_file->current_location.column = 0;
            if (lexer_state.current_file->current_pos[0] == '\n')
            {
                lexer_state.current_file->current_pos++;
            }
            else // if (lexer_state.current_file->current_pos[0] == '\r')
            {
                lexer_state.current_file->current_pos++;
                if (!past_eof()
                        && lexer_state.current_file->current_pos[0] == '\n')
                {
                    lexer_state.current_file->current_pos++;
                }
            }
        }

        lexer_state.current_file->current_location.filename = uniquestr(filename);
        lexer_state.current_file->current_location.line = linenum;
        lexer_state.current_file->current_location.column = 0;

        return 1;
    }
    else
    {
        warn_printf("%s:%d:%d: invalid filename, ignoring line-marker\n",
                filename_loc.filename,
                filename_loc.line,
                filename_loc.column);

        // Go to end of the line
        while (!past_eof()
                && !is_newline(lexer_state.current_file->current_pos[0]))
        {
            lexer_state.current_file->current_pos++;
            lexer_state.current_file->current_location.column++;
        }

        if (!past_eof())
        {
            lexer_state.current_file->current_location.line++;
            lexer_state.current_file->current_location.column = 0;
            if (lexer_state.current_file->current_pos[0] != '\n')
            {
                lexer_state.current_file->current_pos++;
            }
            else // if (lexer_state.current_file->current_pos[0] != '\r')
            {
                lexer_state.current_file->current_pos++;
                if (!past_eof()
                        && lexer_state.current_file->current_pos[0] != '\n')
                {
                    lexer_state.current_file->current_pos++;
                }
            }
        }

        return 1;
    }
#undef ROLLBACK
}

static inline int free_form_get(void)
{
    if (past_eof())
        return EOF;

    int result = lexer_state.current_file->current_pos[0];

    while (result == '&')
    {
        const char* const keep = lexer_state.current_file->current_pos;
        const char keep_bol = lexer_state.bol;
        // int keep_line = lexer_state.current_file->current_location->line;
        const int keep_column = lexer_state.current_file->current_location.column;

        lexer_state.current_file->current_pos++;
        lexer_state.current_file->current_location.column++;
        lexer_state.bol = 0;

        // blanks
        while (!past_eof()
                && is_blank(lexer_state.current_file->current_pos[0]))
        {
            lexer_state.current_file->current_location.column++;

            lexer_state.current_file->current_pos++;
        }

#define ROLLBACK \
        { \
                lexer_state.current_file->current_location.column = keep_column; \
                lexer_state.bol = keep_bol; \
                lexer_state.current_file->current_pos = keep; \
                break; \
        }

        if (past_eof())
            ROLLBACK;

        if (!lexer_state.character_context)
        {
            // When we are not in character context we allow a comment here
            if (lexer_state.current_file->current_pos[0] == '!')
            {
                while (!past_eof()
                        && !is_newline(lexer_state.current_file->current_pos[0]))
                {
                    lexer_state.current_file->current_location.column++;

                    lexer_state.current_file->current_pos++;
                }
                if (past_eof())
                    ROLLBACK;
            }
            else if (is_newline(lexer_state.current_file->current_pos[0]))
            {
                // handled below
            }
            else
                ROLLBACK;
        }

        if (lexer_state.current_file->current_pos[0] == '\n')
        {
            lexer_state.current_file->current_location.line++;
            lexer_state.current_file->current_location.column = 0;

            lexer_state.current_file->current_pos++;
            if (past_eof())
                ROLLBACK;
        }
        else if (lexer_state.current_file->current_pos[0] == '\r')
        {
            lexer_state.current_file->current_location.line++;
            lexer_state.current_file->current_location.column = 0;

            lexer_state.current_file->current_pos++;
            if (!past_eof()
                    && lexer_state.current_file->current_pos[0] == '\n')
            {
                lexer_state.current_file->current_pos++;
                if (past_eof())
                    ROLLBACK;
            }
        }
        else
            ROLLBACK;

        // Now we need to peek if there is another &, so we have to do
        // token pasting, otherwise we will return a blank
        //
        // Note that the following part is complicated because we
        // need to skip lines with only blanks or only a comment
        char do_rollback = 0;
        for (;;)
        {
            // Skip blanks
            while (!past_eof()
                    && is_blank(lexer_state.current_file->current_pos[0]))
            {
                lexer_state.current_file->current_location.column++;

                lexer_state.current_file->current_pos++;
            }
            if (past_eof())
                break;

            if (is_newline(lexer_state.current_file->current_pos[0]))
            {
                // This is a line of only blanks (i.e. an empty line)
                if (lexer_state.current_file->current_pos[0] == '\n')
                {
                    lexer_state.current_file->current_location.line++;
                    lexer_state.current_file->current_location.column = 0;

                    lexer_state.current_file->current_pos++;
                    if (past_eof())
                        break;
                }
                else // '\r'
                {
                    lexer_state.current_file->current_location.line++;
                    lexer_state.current_file->current_location.column = 0;

                    lexer_state.current_file->current_pos++;
                    if (!past_eof()
                            && lexer_state.current_file->current_pos[0] == '\n')
                    {
                        lexer_state.current_file->current_pos++;
                        if (past_eof())
                            break;
                    }
                }

                // This line was empty, continue to the next
                continue;
            }
            else if (lexer_state.current_file->current_pos[0] == '!')
            {
                if (lexer_state.sentinel != NULL)
                {
                    // This line is just a comment
                    lexer_state.current_file->current_pos++;
                    lexer_state.current_file->current_location.column++;
                    if (past_eof())
                        break;

                    if (lexer_state.current_file->current_pos[0] != '$')
                    {
                        do_rollback = 1;
                        break;
                    }

                    lexer_state.current_file->current_pos++;
                    lexer_state.current_file->current_location.column++;
                    if (past_eof())
                        break;

                    int i = 0;
                    while (!past_eof()
                            && lexer_state.sentinel[i] != '\0'
                            && (tolower(lexer_state.current_file->current_pos[0])
                                == tolower(lexer_state.sentinel[i])))
                    {
                        lexer_state.current_file->current_pos++;
                        lexer_state.current_file->current_location.column++;
                        i++;
                    }

                    if (past_eof())
                        ROLLBACK;

                    if (lexer_state.sentinel[i] != '\0')
                    {
                        do_rollback = 1;
                        break;
                    }

                    // Sentinel done, skip blanks
                    while (!past_eof()
                            && is_blank(lexer_state.current_file->current_pos[0]))
                    {
                        lexer_state.current_file->current_location.column++;

                        lexer_state.current_file->current_pos++;
                    }
                    if (past_eof())
                        break;

                    // We are done
                    break;
                }
                else
                {
                    // This line is just a comment
                    while (!past_eof()
                            && !is_newline(lexer_state.current_file->current_pos[0]))
                    {
                        lexer_state.current_file->current_location.column++;
                        lexer_state.current_file->current_pos++;
                    }

                    if (past_eof())
                        break;

                    // Comment done (we have not yet handled the newline, it will
                    // be handled in the next iteration)
                    continue;
                }
            }
            else if (lexer_state.current_file->current_pos[0] == '#')
            {
                lexer_state.current_file->current_location.column++;
                lexer_state.current_file->current_pos++;
                if (past_eof())
                    break;

                if (handle_preprocessor_line())
                    continue;

                // We are done
                break;
            }
            else if (lexer_state.sentinel != NULL)
            {
                // We expected a sentinel
                do_rollback = 1;
                break;
            }
            else
            {
                // We are done
                break;
            }
        }
        if (past_eof())
            ROLLBACK;
        if (do_rollback)
            ROLLBACK;

        if (lexer_state.current_file->current_pos[0] == '&')
        {
            lexer_state.current_file->current_location.column++;

            lexer_state.current_file->current_pos++;
            if (past_eof())
                ROLLBACK;
            result = lexer_state.current_file->current_pos[0];
        }
        else
        {
            if (lexer_state.character_context)
            {
                // in character context the first nonblank is the next character
                result = lexer_state.current_file->current_pos[0];
            }
            else
            {
                result = ' ';
                // Compensate this artificial character
                lexer_state.current_file->current_pos--;
                lexer_state.current_file->current_location.column--;
            }
        }
#undef ROLLBACK
    }

    if (!is_newline(result))
    {
        lexer_state.current_file->current_location.column++;
        lexer_state.current_file->current_pos++;
    }
    else
    {
        lexer_state.current_file->current_location.line++;
        lexer_state.current_file->current_location.column = 0;

        lexer_state.current_file->current_pos++;
        if (result == '\r'
                && !past_eof()
                && lexer_state.current_file->current_pos[0] == '\n')
        {
            // DOS: \r\n will act like a single '\r'
            lexer_state.current_file->current_pos++;
        }
    }

    return result;
}

typedef
struct peek_token_info_tag
{
    int letter;
    token_location_t loc;
} peek_token_info_t;

static struct peek_queue_tag
{
    peek_token_info_t* buffer;
    int size;

    // Negative offsets from (size - 1)
    int front; // where we take
    int back;  // where we add
} _peek_queue;
enum { PEEK_INITIAL_SIZE = 16 };

static inline void peek_init(void)
{
    if (_peek_queue.buffer == NULL)
    {
        _peek_queue.buffer = xmalloc(PEEK_INITIAL_SIZE * sizeof(*_peek_queue.buffer));
        _peek_queue.size = PEEK_INITIAL_SIZE;
    }
    _peek_queue.back = 0;
    _peek_queue.front = 0;
}

static inline char peek_empty(void)
{
    return (_peek_queue.front == _peek_queue.back);
}

static inline int peek_size(void)
{
    return (_peek_queue.front - _peek_queue.back);
}

#if 0
static void peek_print(void)
{
    fprintf(stderr, "-- PEEK SIZE %d\n", peek_size());
    int i;
    for (i = _peek_queue.front; i > _peek_queue.back; i--)
    {
        fprintf(stderr, "PEEK AT [%d] : [%d] => |%c|\n",
                i,
                (_peek_queue.size - 1) + i,
                _peek_queue.buffer[(_peek_queue.size - 1) + i].letter);
    }
    fprintf(stderr, "--\n");
}
#endif

static inline void peek_add(int c, token_location_t loc)
{
    if ((_peek_queue.size - 1) + _peek_queue.back < 0)
    {
        int new_size = _peek_queue.size * 2;
        peek_token_info_t *new_buffer = xmalloc(new_size * sizeof(*new_buffer));

        memcpy(&new_buffer[(new_size - 1) + _peek_queue.back + 1],
                _peek_queue.buffer,
                _peek_queue.size * sizeof(*new_buffer));

        xfree(_peek_queue.buffer);
        _peek_queue.buffer = new_buffer;
        _peek_queue.size = new_size;
    }
    _peek_queue.buffer[(_peek_queue.size - 1) + _peek_queue.back].letter = c;
    _peek_queue.buffer[(_peek_queue.size - 1) + _peek_queue.back].loc = loc;
    _peek_queue.back--;
}

static inline void peek_take(void)
{
    ERROR_CONDITION(_peek_queue.back == _peek_queue.front, "empty peek queue", 0);

    _peek_queue.front--;
    if (_peek_queue.back == _peek_queue.front)
    {
        _peek_queue.back
            = _peek_queue.front
            = 0;
    }
}

static inline peek_token_info_t peek_get(int n)
{
    ERROR_CONDITION(((_peek_queue.size - 1) + _peek_queue.front) - n < 0, "invalid peek index %d", n);

    return _peek_queue.buffer[((_peek_queue.size - 1) + _peek_queue.front) - n];
}

static inline int get_loc(token_location_t *loc)
{
    token_location_t tmp_loc;

    int c;
    if (!peek_empty())
    {
        peek_token_info_t p = peek_get(0);
        peek_take();

        if (mf03_flex_debug)
        {
            fprintf(stderr, "[PEEK] ");
        }

        c = p.letter;
        tmp_loc = p.loc;
    }
    else
    {
        if (mf03_flex_debug)
        {
            fprintf(stderr, "[FILE] ");
        }
        c = free_form_get();
        tmp_loc = get_current_location();
    }

    if (mf03_flex_debug)
    {
        if (isprint(c))
        {
            fprintf(stderr, "GET LETTER '%c' AND LOCUS |%s:%d:%d|\n",
                    c,
                    tmp_loc.filename,
                    tmp_loc.line,
                    tmp_loc.column);
        }
        else
        {
            fprintf(stderr, "GET LETTER '0x%X' AND LOCUS |%s:%d:%d|\n",
                    c,
                    tmp_loc.filename,
                    tmp_loc.line,
                    tmp_loc.column);
        }
    }
    if (loc != NULL)
        *loc = tmp_loc;

    return c;
}

static inline int get(void)
{
    return get_loc(NULL);
}

static inline int peek_loc(int n, token_location_t *loc)
{
    int s = peek_size();
    if (n >= s)
    {
        int d = n - s + 1;

        int i;
        for (i = 0; i < d; i++)
        {
            int c = free_form_get();
            token_location_t loc2 = get_current_location();
            peek_add(c, loc2);

            if (mf03_flex_debug)
            {
                if (isprint(c))
                {
                    fprintf(stderr, "PEEK LETTER %d of %d '%c' AND LOCUS |%s:%d:%d|\n",
                            i, d - 1,
                            c,
                            loc2.filename,
                            loc2.line,
                            loc2.column);
                }
                else
                {
                    fprintf(stderr, "PEEK LETTER %d of %d '0x%X' AND LOCUS |%s:%d:%d|\n",
                            i, d - 1,
                            c,
                            loc2.filename,
                            loc2.line,
                            loc2.column);
                }
            }
        }
    }
    
    peek_token_info_t p = peek_get(n);

    if (loc != NULL)
        *loc = p.loc;

    return p.letter;
}

static inline int peek(int n)
{
    return peek_loc(n, NULL);
}

typedef
struct tiny_dyncharbuf_tag
{
    int size;
    int next;
    char *buf;
} tiny_dyncharbuf_t;

static inline void tiny_dyncharbuf_new(tiny_dyncharbuf_t* t, int initial_size)
{
    t->size = initial_size;
    t->buf = xmalloc(sizeof(*t->buf) * t->size);
    t->next = 0;
}

static inline void tiny_dyncharbuf_add(tiny_dyncharbuf_t* t, char c)
{
    if (t->next >= t->size)
    {
        t->size *= 2;
        t->buf = xrealloc(t->buf, sizeof(*(t->buf)) * (t->size + 1));
    }
    t->buf[t->next] = c;
    t->next++;
}

static inline void tiny_dyncharbuf_add_str(tiny_dyncharbuf_t* t, const char* str)
{
    if (str == NULL)
        return;

    unsigned int i;
    for (i = 0; i < strlen(str); i++)
    {
        tiny_dyncharbuf_add(t, str[i]);
    }
}

static char* scan_kind(void)
{
    tiny_dyncharbuf_t str;
    tiny_dyncharbuf_new(&str, 32);

    int c = get();
    ERROR_CONDITION(c != '_', "input stream is incorrectly located (c=%c)", c);

    token_location_t loc;
    c = peek_loc(0, &loc);
    if (is_decimal_digit(c)
            || is_letter(c))
    {
        tiny_dyncharbuf_add(&str, '_');

        if (is_decimal_digit(c))
        {
            while (is_decimal_digit(c))
            {
                tiny_dyncharbuf_add(&str, c);
                get();
                c = peek(0);
            }
        }
        else if (is_letter(c))
        {
            while (is_letter(c)
                    || is_decimal_digit(c)
                    || c == '_')
            {
                tiny_dyncharbuf_add(&str, c);
                get();
                c = peek(0);
            }
        }
        else
        {
            internal_error("Code unreachable", 0);
        }
    }
    else
    {
        error_printf("%s:%d:%d: invalid kind-specifier\n",
                loc.filename,
                loc.line,
                loc.column);
    }

    tiny_dyncharbuf_add(&str, '\0');

    return str.buf;
}

static int commit_text(int token_id, const char* str,
        token_location_t loc)
{
    if (mf03_flex_debug)
    {
        fprintf(stderr, "COMMITTING TOKEN %02d WITH TEXT |%s| AND LOCUS |%s:%d:%d|\n\n",
                token_id, str,
                loc.filename,
                loc.line,
                loc.column);
    }
    lexer_state.last_eos = (token_id == EOS);
    lexer_state.bol = 0;

    mf03lval.token_atrib.token_text = uniquestr(str);
    mf03lloc.first_filename = loc.filename;
    mf03lloc.first_line = loc.line;
    mf03lloc.first_column = loc.column;

    return token_id;
}

static int commit_text_and_free(int token_id, char* str,
        token_location_t loc)
{
    token_id = commit_text(token_id, str, loc);
    xfree(str);
    return token_id;
}

static inline void scan_character_literal(
        const char* prefix,
        char delim,
        char allow_suffix_boz,
        token_location_t loc,
        // out
        int* token_id,
        char** text)
{
    ERROR_CONDITION(prefix == NULL, "Invalid prefix", 0);

    tiny_dyncharbuf_t str;
    tiny_dyncharbuf_new(&str, strlen(prefix) + 32);

    tiny_dyncharbuf_add_str(&str, prefix);

    *token_id = CHAR_LITERAL;

    char can_be_binary = allow_suffix_boz;
    char can_be_octal = allow_suffix_boz;
    char can_be_hexa = allow_suffix_boz;

    lexer_state.character_context = 1;

    char unended_literal = 0;
    int c = peek(0);
    token_location_t loc2 = loc;

    for (;;)
    {
        if (c != delim
                && !is_newline(c))
        {
            get_loc(&loc2);
            tiny_dyncharbuf_add(&str, c);

            can_be_binary = 
                can_be_binary &&
                is_binary_digit(c);

            can_be_octal = 
                can_be_octal &&
                is_octal_digit(c);

            can_be_hexa = 
                can_be_hexa &&
                is_hex_digit(c);

            c = peek_loc(0, &loc2);
        }
        else if (c == delim)
        {
            get_loc(&loc2);
            tiny_dyncharbuf_add(&str, c);
            c = peek_loc(0, &loc2);

            if (c != delim)
                break;

            can_be_binary
                = can_be_hexa
                = can_be_octal
                = 0;

            get_loc(&loc2);
            tiny_dyncharbuf_add(&str, c);
            c = peek_loc(0, &loc2);
        }
        else // c == '\n' || c == '\r'
        {
            error_printf("%s:%d:%d: error: unended character literal\n",
                    loc2.filename,
                    loc2.line,
                    loc2.column);
            unended_literal = 1;
            break;
        }
    }

    lexer_state.character_context = 0;

    if (unended_literal)
    {
        tiny_dyncharbuf_add(&str, delim);
        can_be_binary =
            can_be_octal =
            can_be_hexa = 0;
    }

    if (can_be_binary
            || can_be_octal
            || can_be_hexa)
    {
        c = peek(0);
        if (can_be_binary
                && (tolower(c) == 'b'))
        {
            get();
            tiny_dyncharbuf_add(&str, c);
            *token_id = BINARY_LITERAL;
        }
        else if (can_be_octal
                && (tolower(c) == 'o'))
        {
            get();
            tiny_dyncharbuf_add(&str, c);
            *token_id = OCTAL_LITERAL;
        }
        else if (can_be_hexa
                && (tolower(c) == 'x'
                    || tolower(c) == 'z'))
        {
            get();
            tiny_dyncharbuf_add(&str, c);
            *token_id = HEX_LITERAL;
        }
    }

    tiny_dyncharbuf_add(&str, '\0');

    *text = str.buf;
}

static char* scan_fractional_part_of_real_literal(void)
{
    // We assume that we are right after the '.'

    // a valid real literal at this point must be [.][0-9]*([edq][+-]?[0-9]+)_kind

    tiny_dyncharbuf_t str;
    tiny_dyncharbuf_new(&str, 32);

    tiny_dyncharbuf_add(&str, '.');

    int c = peek(0);

    while (is_decimal_digit(c))
    {
        get();
        tiny_dyncharbuf_add(&str, c);
        c = peek(0);
    }

    if (tolower(c) == 'e'
            || tolower(c) == 'd'
            || tolower(c) == 'q')
    {
        char e = c;
        token_location_t exp_loc;
        c = peek_loc(1, &exp_loc);

        if (is_decimal_digit(c))
        {
            tiny_dyncharbuf_add(&str, e);
            get();
        }
        else if (c == '+' || c == '-')
        {
            char s = c;
            c = peek_loc(2, &exp_loc);
            if (!is_decimal_digit(c))
            {
                error_printf("%s:%d:%d: error: missing exponent in real literal\n",
                        exp_loc.filename,
                        exp_loc.line,
                        exp_loc.column);
                // 1.23e+a
                // 1.23e-a
                tiny_dyncharbuf_add(&str, '\0');
                return str.buf;
            }

            tiny_dyncharbuf_add(&str, e);
            get();
            tiny_dyncharbuf_add(&str, s);
            get();
        }
        else
        {
            // 1.23ea
            error_printf("%s:%d:%d: error: missing exponent in real literal\n",
                    exp_loc.filename,
                    exp_loc.line,
                    exp_loc.column);
            tiny_dyncharbuf_add(&str, '\0');
            return str.buf;
        }

        while (is_decimal_digit(c))
        {
            get();
            tiny_dyncharbuf_add(&str, c);
            c = peek(0);
        }
    }
    if (c == '_')
    {
        char *kind_str = scan_kind();
        tiny_dyncharbuf_add_str(&str, kind_str);
        xfree(kind_str);
    }

    tiny_dyncharbuf_add(&str, '\0');
    return str.buf;
}

static char is_include_line(void)
{
    const char* keep = lexer_state.current_file->current_pos;
    int keep_column = lexer_state.current_file->current_location.column;

#define ROLLBACK \
    { \
        lexer_state.current_file->current_pos = keep; \
        lexer_state.current_file->current_location.column = keep_column; \
        return 0; \
    }

    const char c[] = "include";

    int i = 1; // letter 'i' has already been matched
    while (!past_eof()
            && c[i] != '\0'
            && c[i] == tolower(lexer_state.current_file->current_pos[0]))
    {
        lexer_state.current_file->current_location.column++;
        lexer_state.current_file->current_pos++;

        i++;
    }

    if (past_eof() ||
            c[i] != '\0')
        ROLLBACK;

    while (!past_eof()
            && is_blank(lexer_state.current_file->current_pos[0]))
    {
        lexer_state.current_file->current_location.column++;
        lexer_state.current_file->current_pos++;
    }

    if (past_eof())
        ROLLBACK;

    char delim = lexer_state.current_file->current_pos[0];
    if (delim != '\''
            && delim != '\"')
        ROLLBACK;

    lexer_state.current_file->current_location.column++;
    lexer_state.current_file->current_pos++;

    token_location_t loc = lexer_state.current_file->current_location;

    const char* start = lexer_state.current_file->current_pos;

    while (!past_eof()
            && !is_newline(lexer_state.current_file->current_pos[0])
            && lexer_state.current_file->current_pos[0] != delim)
    {
        lexer_state.current_file->current_location.column++;
        lexer_state.current_file->current_pos++;
    }

    if (past_eof()
            || is_newline(lexer_state.current_file->current_pos[0]))
        ROLLBACK;

    const char* final = lexer_state.current_file->current_pos - 1;

    if (final - start == 0)
        ROLLBACK;

    // Jump delim
    lexer_state.current_file->current_location.column++;
    lexer_state.current_file->current_pos++;

    // Blanks until the end of the line
    while (!past_eof()
            && is_blank(lexer_state.current_file->current_pos[0]))
    {
        lexer_state.current_file->current_location.column++;
        lexer_state.current_file->current_pos++;
    }

    if (past_eof())
        ROLLBACK;

    if (!is_newline(lexer_state.current_file->current_pos[0]))
        ROLLBACK;

#undef ROLLBACK

    if (lexer_state.current_file->current_pos[0] == '\n')
    {
        lexer_state.current_file->current_pos++;
    }
    else if (lexer_state.current_file->current_pos[0] == '\r')
    {
        lexer_state.current_file->current_pos++;
        if (!past_eof())
        {
            if (lexer_state.current_file->current_pos[0] == '\n')
                lexer_state.current_file->current_pos++;
        }
    }
    else
    {
        internal_error("Code unreachable", 0);
    }

    lexer_state.current_file->current_location.column = 0;
    lexer_state.current_file->current_location.line++;

    // Now get the filename
    int num_chars = final - start + 1;
    int num_bytes_buffer = num_chars + 1; // account final NULL
    char include_name[num_bytes_buffer];

    memcpy(include_name, start, num_chars);
    include_name[num_bytes_buffer - 1] = '\0';

    const char* include_filename = find_file_in_directories(
            CURRENT_CONFIGURATION->num_include_dirs,
            CURRENT_CONFIGURATION->include_dirs, 
            include_name,
            /* origin */ loc.filename);
    

    int fd = open(include_filename, O_RDONLY);
    if (fd < 0)
    {
        running_error("%s:%d:%d: error: cannot open included file '%s' (%s)\n",
                loc.filename,
                loc.line,
                loc.column,
                include_filename,
                strerror(errno));
    }

    // Get size of file because we need it for the mmap
    struct stat s;
    int status = fstat (fd, &s);
    if (status < 0)
    {
        running_error("%s:%d:%d: error: cannot get status of included file '%s' (%s)\n",
                loc.filename,
                loc.line,
                loc.column,
                include_filename, strerror(errno));
    }

    const char *mmapped_addr = mmap(0, s.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (mmapped_addr == MAP_FAILED)
    {
        running_error("%s:%d:%d: error: cannot map included file '%s' in memory (%s)",
                loc.filename,
                loc.line,
                loc.column,
                include_filename,
                strerror(errno));
    }

    lexer_state.include_stack_size++;
    if (lexer_state.include_stack_size == MAX_INCLUDE_DEPTH)
    {
        running_error("%s:%d:%d: error: too many nested included files",
                loc.filename,
                loc.line,
                loc.column);
    }

    lexer_state.current_file = &lexer_state.include_stack[lexer_state.include_stack_size];

    lexer_state.current_file->scanned_filename = include_filename;

    lexer_state.current_file->fd = fd;
    lexer_state.current_file->buffer_size = s.st_size;
    lexer_state.current_file->current_pos
        = lexer_state.current_file->buffer = mmapped_addr;

    lexer_state.current_file->current_location.filename = include_filename;
    lexer_state.current_file->current_location.line = 1;
    lexer_state.current_file->current_location.column = 0;

    lexer_state.bol = 1;
    lexer_state.last_eos = 1;
    lexer_state.in_nonblock_do_construct = 0;
    lexer_state.num_nonblock_labels = 0;
    lexer_state.num_pragma_constructs = 0;

    return 1;
}

static inline char is_format_statement(void)
{
    int peek_idx = 0;
    int p = peek(peek_idx);

    // Skip blanks after label
    while (is_blank(p))
    {
        peek_idx++;
        p = peek(peek_idx);
    }

    int i = 0;
    const char c[] = "format";
    while (c[i] != '\0'
            && tolower(p) == c[i])
    {
        peek_idx++;
        p = peek(peek_idx);

        i++;
    }

    if (c[i] != '\0')
        return 0;

    // Skip blanks after FORMAT keyword
    while (is_blank(p))
    {
        peek_idx++;
        p = peek(peek_idx);
    }

    if (p != '(')
        return 0;

    // Skip opening parenthesis
    peek_idx++;
    p = peek(peek_idx);

    char delim;
    int level = 1;
    char in_string = 0;

    // Now find the matching closing parenthesis
    while (p != EOF
            && !is_newline(p)
            && (level > 0))
    {
        if (!in_string)
        {
            if (p == '(')
            {
                level++;
            }
            else if (p == ')')
            {
                level--;
            }
            else if (p == '\'' || p == '"')
            {
                delim = p;
                in_string = 1;
            }
        }
        else
        {
            if (p == delim)
            {
                int p1 = peek(peek_idx + 1);
                if (p1 != delim)
                {
                    in_string = 0;
                }
                else // p1 == delim
                {
                    // Skip the delimiter as we do not want
                    // to see again
                    peek_idx++;
                }
            }
        }
        peek_idx++;
        p = peek(peek_idx);
    }

    // Unbalanced parentheses or opened string
    if ((level > 0) || (in_string == 1)) 
        return 0;

    // Skip blanks after closing parenthesis
    while (is_blank(p))
    {
        peek_idx++;
        p = peek(peek_idx);
    }

    // Expect a newline here
    if (!is_newline(p))
        return 0;

    // This FORMAT seems fine
    return 1;
}

static inline char is_known_sentinel(char** sentinel)
{
    int c;
    // c = peek(0); // $
    get();

    tiny_dyncharbuf_t tmp_sentinel;
    tiny_dyncharbuf_new(&tmp_sentinel, 4);

    c = peek(0);
    while ((is_letter(c)
                || is_decimal_digit(c)
                || c == '_'))
    {
        tiny_dyncharbuf_add(&tmp_sentinel, c);
        get();
        c = peek(0);
    }
    tiny_dyncharbuf_add(&tmp_sentinel, '\0');

    int i;
    char found = 0;
    for (i = 0; i < CURRENT_CONFIGURATION->num_pragma_custom_prefix; i++)
    {
        if (strcasecmp(tmp_sentinel.buf, CURRENT_CONFIGURATION->pragma_custom_prefix[i]) == 0)
        {
            found = 1;
            break;
        }
    }

    if (!found)
    {
        *sentinel = tmp_sentinel.buf;
        return 0;
    }
    else
    {
        xfree(tmp_sentinel.buf);
        *sentinel = xstrdup(CURRENT_CONFIGURATION->pragma_custom_prefix[i]);
        return 1;
    }
}

static const char* format_pragma_string(const char* c)
{
    char *tmp = xstrdup(c);

    char* p = tmp;

    while (*p != '\0')
    {
        if (*p == '|')
            *p = ' ';
        p++;
    }

    return tmp;
}

static int compute_length_match(const char* lexed_directive,
        const char* available_directive,
        const char **discard_source)
{
    *discard_source = NULL;
    const char *p = lexed_directive;
    const char *q = available_directive;

    while (*p != '\0'
            && *q != '\0')
    {
        if (*q == '|')
        {
            // A '|' should match like [[:blank:]]* 
            while (*p == ' ' || *p == '\t')
                p++;
            // We advanced too much
            p--;
        }
        else if (*q != tolower(*p))
        {
            return 0;
        }
        q++;
        p++;
    }

    *discard_source = p;
    return (q - available_directive);
}

static const char* return_pragma_prefix_longest_match_inner(pragma_directive_set_t* pragma_directive_set,
        const char* lexed_directive,
        const char **discard_source,
        pragma_directive_kind_t* directive_kind)
{
    const char* longest_match_so_far = NULL;
    int length_match = 0;

    int j;
    char exact_match = 0;
    int size_lexed_directive = strlen(lexed_directive);
    for (j = 0; j < pragma_directive_set->num_directives && !exact_match; j++)
    {
        const char * current_discard_source = NULL;

        int current_match = compute_length_match(lexed_directive, pragma_directive_set->directive_names[j], 
                &current_discard_source);
        
        if (current_match >= length_match && current_match != 0)
        {
            int size_directive = strlen(pragma_directive_set->directive_names[j]);
            if (current_match == size_lexed_directive && size_lexed_directive == size_directive)
            {
               exact_match = 1;    
            }
            length_match = current_match;
            longest_match_so_far = pragma_directive_set->directive_names[j];
            *discard_source = current_discard_source;
            *directive_kind = pragma_directive_set->directive_kinds[j];
        }
    }

    return longest_match_so_far;
}

static const char* return_pragma_prefix_longest_match(
        const char* prefix, 
        char is_end_directive,
        const char* lexed_directive,
        pragma_directive_kind_t* kind)
{
    const char* longest_match = NULL;
    const char* discard_source = NULL;

    const char* looked_up_directive = lexed_directive;
    if (is_end_directive)
    {
        // skip 'end'
        looked_up_directive += 3;
        while (is_blank(*looked_up_directive))
            looked_up_directive++;
    }

    int i;
    for (i = 0; i < CURRENT_CONFIGURATION->num_pragma_custom_prefix; i++)
    {
        if (strcmp(CURRENT_CONFIGURATION->pragma_custom_prefix[i], prefix) == 0)
        {
            pragma_directive_set_t* pragma_directive_set = CURRENT_CONFIGURATION->pragma_custom_prefix_info[i];
            longest_match = return_pragma_prefix_longest_match_inner(pragma_directive_set, 
                    looked_up_directive, &discard_source, kind);
        }
    }

    // Now advance the token stream
    const char *start = lexed_directive;
    const char *end = discard_source;

    // The first letter is always already consumed
    start++;

    while (start != end)
    {
        start++;
        get();
    }

    return longest_match;
}

// means: no more letters available for this file
static inline char is_end_of_file(void)
{
    return peek_empty()
        && past_eof();
}

// This is the lexer <-> parser interface from yacc/bison
// this function just returns the next token from the current
// input stream
extern int new_mf03lex(void)
{
    for (;;)
    {
        int c0;
        token_location_t loc;

        // We are forced to peek because we have to return
        // an artificial token for nonblock labels
        if (lexer_state.substate == LEXER_SUBSTATE_NORMAL
                && lexer_state.last_eos
                && lexer_state.num_nonblock_labels > 0
                // peek here
                && is_decimal_digit(c0 = peek_loc(0, &loc)))
        {
            char label_str[6];
            label_str[0] = c0;

            int i = 1;
            int peek_idx = 1;
            int c = peek(peek_idx);
            while (i < 5
                    && is_decimal_digit(c))
            {
                label_str[i] = c;
                i++;
                peek_idx++;
                c = peek(peek_idx);
            }

            label_str[i] = '\0';

            if (!is_decimal_digit(c))
            {
                int label = atoi(label_str);
                if (lexer_state.nonblock_labels_stack[lexer_state.num_nonblock_labels - 1] == label)
                {
                    lexer_state.num_nonblock_labels--;
                    return commit_text(TOKEN_END_NONBLOCK_DO, label_str, loc);
                }
            }
        }

        c0 = get_loc(&loc);
        if (lexer_state.substate == LEXER_SUBSTATE_NORMAL)
        {
            switch (c0)
            {
                case EOF:
                    {
                        char end_of_scan = process_end_of_file();
                        if (end_of_scan)
                        {
                            if (!lexer_state.last_eos)
                            {
                                // Make sure we force a final EOS
                                return commit_text(EOS, NULL, get_current_location());
                            }
                            return 0;
                        }
                        else
                            continue;
                    }
                case '#':
                    {
                        if (!lexer_state.bol)
                            break;

                        if (handle_preprocessor_line())
                        {
                            lexer_state.bol = 1;
                            continue;
                        }

                        break;
                    }
                case ' ':
                case '\t':
                    {
                        // Whitespace ignored
                        continue;
                    }
                case '\n':
                case '\r':
                    {
                        // Regarding \r\n in DOS, the get function will always skip \n if it finds it right after \r
                        if (!lexer_state.last_eos)
                        {
                            int n = commit_text(EOS, NULL, loc);
                            lexer_state.sentinel = NULL;
                            lexer_state.bol = 1;
                            return n;
                        }
                        else
                        {
                            if (mf03_flex_debug)
                            {
                                fprintf(stderr, "SKIPPING NEWLINE %s:%d:%d\n",
                                        loc.filename,
                                        loc.line,
                                        loc.column);
                            }
                            lexer_state.sentinel = NULL;
                            lexer_state.bol = 1;
                        }
                        continue;
                    }
                case '!':
                    {
                        // comment
                        c0 = peek(0);
                        if (c0 == '$')
                        {
                            int c1 = peek(1);
                            if (is_blank(c1))
                            {
                                // Conditional compilation
                                if (!CURRENT_CONFIGURATION->disable_empty_sentinels)
                                {
                                    get(); // $
                                    get(); // <<blank>>
                                    lexer_state.sentinel = "";
                                    continue;
                                }
                                // otherwise handle it as if it were a comment
                            }
                            else if (is_letter(c1)
                                    || is_decimal_digit(c1)
                                    || c1 == '_')
                            {
                                char* sentinel = NULL;
                                if (is_known_sentinel(&sentinel))
                                {
                                    lexer_state.substate = LEXER_SUBSTATE_PRAGMA_DIRECTIVE;
                                    lexer_state.sentinel = xstrdup(sentinel);
                                    return commit_text_and_free(PRAGMA_CUSTOM, sentinel, loc);
                                }
                                else
                                {
                                    warn_printf("%s:%d:%d: warning: ignoring unknown '!$%s' directive\n",
                                            loc.filename,
                                            loc.line,
                                            loc.column,
                                            sentinel);

                                    // Return an UNKNOWN_PRAGMA
                                    int sentinel_length = strlen(sentinel);
                                    int prefix_length = /* !$ */ 2 + sentinel_length;

                                    tiny_dyncharbuf_t str;
                                    tiny_dyncharbuf_new(&str, prefix_length + 32);

                                    tiny_dyncharbuf_add_str(&str, "!$");
                                    tiny_dyncharbuf_add_str(&str, sentinel);

                                    xfree(sentinel);

                                    // Everything will be scanned as a single token
                                    int c = peek(0);
                                    while (!is_newline(c))
                                    {
                                        tiny_dyncharbuf_add(&str, c);
                                        get();
                                        c = peek(0);
                                    }
                                    tiny_dyncharbuf_add(&str, '\0');
                                    return commit_text_and_free(UNKNOWN_PRAGMA, str.buf, loc);
                                }
                            }
                        }

                        while (!is_newline(c0))
                        {
                            get();
                            c0 = peek(0);
                        }

                        if (c0 == '\r')
                        {
                            if (peek(1) == '\n')
                                get();
                        }

                        // we are now right before \n (or \r)
                        continue;
                    }
                case ';':
                    {
                        if (!lexer_state.last_eos)
                        {
                            return commit_text(EOS, NULL, loc);
                        }
                        break;
                    }
                case '(' :
                    {
                        if (lexer_state.in_format_statement)
                        {
                            tiny_dyncharbuf_t str;
                            tiny_dyncharbuf_new(&str, 32);

                            tiny_dyncharbuf_add(&str, c0);
                            // Everything will be scanned as a single token
                            int c = peek(0);
                            while (!is_newline(c))
                            {
                                tiny_dyncharbuf_add(&str, c);
                                get();
                                c = peek(0);
                            }
                            tiny_dyncharbuf_add(&str, '\0');

                            lexer_state.in_format_statement = 0;
                            return commit_text_and_free(FORMAT_SPEC, str.buf, loc);
                        }

                        int c1 = peek(0);
                        if (c1 == '/')
                        {
                            get();
                            return commit_text(TOKEN_LPARENT_SLASH, "(/", loc);
                        }
                        else
                        {
                            return commit_text('(', "(", loc);
                        }
                    }
                case '/' :
                    {
                        int c1 = peek(0);
                        if (c1 == '/')
                        {
                            get();
                            return commit_text(TOKEN_DOUBLE_SLASH, "//", loc);
                        }
                        else if (c1 == '=')
                        {
                            get();
                            return commit_text(TOKEN_NOT_EQUAL, "/=", loc);
                        }
                        else if (c1 == ')')
                        {
                            get();
                            return commit_text(TOKEN_SLASH_RPARENT, "/)", loc);
                        }
                        else
                            return commit_text('/', "/", loc);
                    }
                case ')' :
                case '[' :
                case ']' :
                case ',' :
                case '%' :
                case '+' :
                case '-' :
                case ':' :
                    {
                        const char s[] = {c0, '\0'};
                        return commit_text(c0, s, loc);
                    }
                case '*' :
                    {
                        int c1 = peek(0);
                        if (c1 == '*')
                        {
                            get();
                            return commit_text(TOKEN_RAISE, "**", loc);
                        }
                        else
                        {
                            return commit_text('*', "*", loc);
                        }
                    }
                case '<' :
                    {
                        int c1 = peek(0);
                        if (c1 == '=')
                        {
                            get();
                            return commit_text(TOKEN_LOWER_OR_EQUAL_THAN, "<=", loc);
                        }
                        else
                        {
                            return commit_text(TOKEN_LOWER_THAN, "<", loc);
                        }
                    }
                case '>' :
                    {
                        int c1 = peek(0);
                        if (c1 == '=')
                        {
                            get();
                            return commit_text(TOKEN_GREATER_OR_EQUAL_THAN, ">=", loc);
                        }
                        else
                        {
                            return commit_text(TOKEN_GREATER_THAN, ">", loc);
                        }
                    }
                case '=':
                    {
                        int c1 = peek(0);
                        if (c1 == '=')
                        {
                            get();
                            return commit_text(TOKEN_EQUAL, "==", loc);
                        }
                        else if (c1 == '>')
                        {
                            get();
                            return commit_text(TOKEN_POINTER_ACCESS, "=>", loc);
                        }
                        else
                        {
                            return commit_text('=', "=", loc);
                        }
                    }
                case '.':
                    {
                        int c1 = peek(0);
                        int c2 = peek(1);
                        if (tolower(c1) == 'e'
                                && tolower(c2) == 'q')
                        {
                            int c3 = peek(2);
                            int c4 = peek(3);
                            if (c3 == '.')
                            {
                                get(); // e
                                get(); // q
                                get(); // .
                                return commit_text(TOKEN_EQUAL, "==", loc);
                            }
                            else if (tolower(c3) == 'v'
                                    && c4 == '.')
                            {
                                char str[sizeof(".eqv.")];
                                str[0] = c0;    // .
                                str[1] = get(); // e
                                str[2] = get(); // q
                                str[3] = get(); // v
                                str[4] = get(); // .
                                str[5] = '\0';
                                return commit_text(TOKEN_LOGICAL_EQUIVALENT, str, loc);
                            }
                        }
                        else if (tolower(c1) == 'n')
                        {
                            if (tolower(c2) == 'e')
                            {
                                int c3 = peek(2);
                                int c4 = peek(3);
                                int c5 = peek(4);
                                if (c3 == '.')
                                {
                                    get(); // n
                                    get(); // e
                                    get(); // .
                                    return commit_text(TOKEN_NOT_EQUAL, "/=", loc);
                                }
                                else if (tolower(c3) == 'q'
                                        && tolower(c4) == 'v'
                                        && c5 == '.')
                                {
                                    char str[sizeof(".neqv.")];
                                    str[0] = c0;    // .
                                    str[1] = get(); // n
                                    str[2] = get(); // e
                                    str[3] = get(); // q
                                    str[4] = get(); // v
                                    str[5] = get(); // .
                                    str[6] = '\0';
                                    return commit_text(TOKEN_LOGICAL_NOT_EQUIVALENT, str, loc);
                                }
                            }
                            else if (tolower(c2) == 'o')
                            {
                                int c3 = peek(2);
                                int c4 = peek(3);
                                if (tolower(c3) == 't'
                                        && c4 == '.')
                                {
                                    char str[sizeof(".not.")];
                                    str[0] = c0;    // .
                                    str[1] = get(); // n
                                    str[2] = get(); // o
                                    str[3] = get(); // t
                                    str[4] = get(); // .
                                    str[5] = '\0';
                                    return commit_text(TOKEN_LOGICAL_NOT, str, loc);
                                }
                            }
                        }
                        else if (tolower(c1) == 'l')
                        {
                            int c3 = peek(2);
                            if (tolower(c2) == 'e'
                                    && c3 == '.')
                            {
                                get(); // l
                                get(); // e
                                get(); // .
                                return commit_text(TOKEN_LOWER_OR_EQUAL_THAN, "<=", loc);
                            }
                            else if (tolower(c2) == 't'
                                    && c3 == '.')
                            {
                                get(); // l
                                get(); // t
                                get(); // .
                                return commit_text(TOKEN_LOWER_THAN, "<", loc);
                            }
                        }
                        else if (tolower(c1) == 'g')
                        {
                            int c3 = peek(2);
                            if (tolower(c2) == 'e'
                                    && c3 == '.')
                            {
                                get(); // g
                                get(); // e
                                get(); // .
                                return commit_text(TOKEN_GREATER_OR_EQUAL_THAN, ">=", loc);
                            }
                            else if (tolower(c2) == 't'
                                    && c3 == '.')
                            {
                                get(); // g
                                get(); // t
                                get(); // .
                                return commit_text(TOKEN_GREATER_THAN, ">", loc);
                            }
                        }
                        else if (tolower(c1) == 'o'
                                && tolower(c2) == 'r')
                        {
                            int c3 = peek(2);
                            if (c3 == '.')
                            {
                                char str[sizeof(".or.")];
                                str[0] = c0;    // .
                                str[1] = get(); // o
                                str[2] = get(); // r
                                str[3] = get(); // .
                                str[4] = '\0';
                                return commit_text(TOKEN_LOGICAL_OR, str, loc);
                            }
                        }
                        else if (tolower(c1) == 'a'
                                && tolower(c2) == 'n')
                        {
                            int c3 = peek(2);
                            int c4 = peek(3);
                            if (tolower(c3) == 'd'
                                    && c4 == '.')
                            {
                                char str[sizeof(".and.")];
                                str[0] = c0;    // .
                                str[1] = get(); // a
                                str[2] = get(); // n
                                str[3] = get(); // d
                                str[4] = get(); // .
                                str[5] = '\0';
                                return commit_text(TOKEN_LOGICAL_AND, str, loc);
                            }
                        }
                        else if (tolower(c1) == 't'
                                && tolower(c2) == 'r'
                                && tolower(peek(2)) == 'u'
                                && tolower(peek(3)) == 'e'
                                && tolower(peek(4)) == '.')
                        {
                            char str[6 + 1];
                            str[0] = '.';
                            str[1] = get(); // t
                            str[2] = get(); // r
                            str[3] = get(); // u
                            str[4] = get(); // e
                            str[5] = get(); // .
                            str[6] = '\0';

                            int c = peek(0);
                            if (c == '_')
                            {
                                char* kind_str = scan_kind();

                                tiny_dyncharbuf_t t_str;
                                tiny_dyncharbuf_new(&t_str, 32);

                                tiny_dyncharbuf_add_str(&t_str, str);
                                tiny_dyncharbuf_add_str(&t_str, kind_str);
                                xfree(kind_str);
                                tiny_dyncharbuf_add_str(&t_str, '\0');

                                return commit_text_and_free(TOKEN_TRUE, t_str.buf, loc);
                            }
                            else
                            {
                                return commit_text(TOKEN_TRUE, str, loc);
                            }
                        }
                        else if (tolower(c1) == 'f'
                                && tolower(c2) == 'a'
                                && tolower(peek(2)) == 'l'
                                && tolower(peek(3)) == 's'
                                && tolower(peek(4)) == 'e'
                                && tolower(peek(5)) == '.')
                        {
                            char str[7 + 1];
                            str[0] = '.';
                            str[1] = get(); // f
                            str[2] = get(); // a
                            str[3] = get(); // l
                            str[4] = get(); // s
                            str[5] = get(); // e
                            str[6] = get(); // .
                            str[7] = '\0';

                            int c = peek(0);
                            if (c == '_')
                            {
                                char* kind_str = scan_kind();

                                tiny_dyncharbuf_t t_str;
                                tiny_dyncharbuf_new(&t_str, 32);

                                tiny_dyncharbuf_add_str(&t_str, str);
                                tiny_dyncharbuf_add_str(&t_str, kind_str);
                                xfree(kind_str);
                                tiny_dyncharbuf_add_str(&t_str, '\0');

                                return commit_text_and_free(TOKEN_FALSE, t_str.buf, loc);
                            }
                            else
                            {
                                return commit_text(TOKEN_FALSE, str, loc);
                            }
                        }
                        else if (is_decimal_digit(c1))
                        {
                            char* fractional_part = scan_fractional_part_of_real_literal();
                            return commit_text_and_free(REAL_LITERAL, fractional_part, loc);
                        }
                        else if (is_letter(c1))
                        {
                            tiny_dyncharbuf_t user_def_op;
                            tiny_dyncharbuf_new(&user_def_op, 32);

                            tiny_dyncharbuf_add(&user_def_op, c0);
                            tiny_dyncharbuf_add(&user_def_op, c1);
                            get();

                            int c = c2;
                            while (c != '.'
                                    && is_letter(c))
                            {
                                tiny_dyncharbuf_add(&user_def_op, c);
                                get();
                                c = peek(0);
                            }

                            if (c != '.')
                            {
                                error_printf("%s:%d:%d: error: unended user-defined operator name\n",
                                        loc.filename,
                                        loc.line,
                                        loc.column);
                            }
                            else
                            {
                                get(); // .
                            }

                            tiny_dyncharbuf_add(&user_def_op, '.');
                            tiny_dyncharbuf_add(&user_def_op, '\0');
                            return commit_text_and_free(USER_DEFINED_OPERATOR, user_def_op.buf, loc); // '.[a-z].'
                        }
                    }
                    // string literals
                case '"':
                case '\'':
                    {
                        char prefix[2] = {c0, '\0'};

                        int token_id = 0;
                        char *text = NULL;

                        scan_character_literal(prefix, /* delim */ c0, /* allow_suffix_boz */ 1, loc, &token_id, &text);

                        return commit_text_and_free(token_id, text, loc);
                        break;
                    }
                    // letters
                case 'A': case 'B': case 'C': case 'D': case 'E': case 'F': case 'G': case 'H':
                case 'I': case 'J': case 'K': case 'L': case 'M': case 'N': case 'O': case 'P':
                case 'Q': case 'R': case 'S': case 'T': case 'U': case 'V': case 'W': case 'X':
                case 'Y': case 'Z':
                case 'a': case 'b': case 'c': case 'd': case 'e': case 'f': case 'g': case 'h':
                case 'i': case 'j': case 'k': case 'l': case 'm': case 'n': case 'o': case 'p':
                case 'q': case 'r': case 's': case 't': case 'u': case 'v': case 'w': case 'x':
                case 'y': case 'z':
                    {
                        if (lexer_state.bol
                                && (tolower(c0) == 'i'))
                        {
                            // Maybe this is an include line
                            if (is_include_line())
                                continue;
                        }

                        if (tolower(c0) == 'b'
                                || tolower(c0) == 'o'
                                || tolower(c0) == 'z'
                                || tolower(c0) == 'x')
                        {
                            int c1 = peek(0);

                            if (c1 == '\'' || c1 == '"')
                            {
                                tiny_dyncharbuf_t str;
                                tiny_dyncharbuf_new(&str, 32);

                                tiny_dyncharbuf_add(&str, c0);
                                tiny_dyncharbuf_add(&str, c1);
                                get();

                                int token_id = 0;
                                token_location_t loc2 = loc;
                                int c;
                                int length = 0;
                                switch (c0)
                                {
                                    case 'b': case 'B':
                                        {
                                            token_id = BINARY_LITERAL;
                                            c = peek_loc(0, &loc2);
                                            while (c != c1
                                                    && is_binary_digit(c))
                                            {
                                                tiny_dyncharbuf_add(&str, c);
                                                get();
                                                c = peek_loc(0, &loc2);
                                                length++;
                                            }

                                            if (c != c1
                                                    && !is_newline(c))
                                            {
                                                error_printf("%s:%d:%d: error: invalid binary digit\n",
                                                        loc2.filename,
                                                        loc2.line,
                                                        loc2.column);
                                            }
                                            break;
                                        }
                                    case 'o' : case 'O':
                                        {
                                            token_id = OCTAL_LITERAL;
                                            c = peek_loc(0, &loc2);
                                            while (c != c1
                                                    && is_octal_digit(c))
                                            {
                                                tiny_dyncharbuf_add(&str, c);
                                                get();
                                                c = peek_loc(0, &loc2);
                                                length++;
                                            }

                                            if (c != c1
                                                    && !is_newline(c))
                                            {
                                                error_printf("%s:%d:%d: error: invalid octal digit\n",
                                                        loc2.filename,
                                                        loc2.line,
                                                        loc2.column);
                                            }
                                            break;
                                        }
                                    case 'x': case 'X':
                                    case 'z': case 'Z':
                                        {
                                            token_id = HEX_LITERAL;
                                            c = peek_loc(0, &loc2);
                                            while (c != c1
                                                    && is_hex_digit(c))
                                            {
                                                tiny_dyncharbuf_add(&str, c);
                                                get();
                                                c = peek_loc(0, &loc2);
                                                length++;
                                            }


                                            if (c != c1
                                                    && !is_newline(c))
                                            {
                                                error_printf("%s:%d:%d: error: invalid hexadecimal digit\n",
                                                        loc2.filename,
                                                        loc2.line,
                                                        loc2.column);
                                            }
                                            break;
                                        }
                                    default:
                                        internal_error("Code unreachable", 0);
                                }

                                if (c == c1)
                                {
                                    tiny_dyncharbuf_add(&str, c1);
                                    get();

                                    if (length == 0)
                                    {
                                        error_printf("%s:%d:%d: error: empty integer literal\n",
                                                loc2.filename,
                                                loc2.line,
                                                loc2.column);

                                        tiny_dyncharbuf_add(&str, 0);
                                        tiny_dyncharbuf_add(&str, c1);
                                    }

                                    c = peek(0);
                                    if (c == '_')
                                    {
                                        char *kind_str = scan_kind();
                                        tiny_dyncharbuf_add_str(&str, kind_str);
                                    }
                                }
                                else
                                {
                                    error_printf("%s:%d:%d: error: unended integer literal\n",
                                            loc2.filename,
                                            loc2.line,
                                            loc2.column);
                                    tiny_dyncharbuf_add(&str, c1);
                                }

                                tiny_dyncharbuf_add(&str, '\0');
                                return commit_text(token_id, str.buf, loc);
                            }
                        }

                        // peek as many letters as possible
                        tiny_dyncharbuf_t identifier;
                        tiny_dyncharbuf_new(&identifier, 32);

                        tiny_dyncharbuf_add(&identifier, c0);

                        int c = peek(0);
                        while (is_letter(c)
                                || is_decimal_digit(c)
                                || (c == '_'
                                    && peek(1) != '\''
                                    && peek(1) != '"'))
                        {
                            tiny_dyncharbuf_add(&identifier, c);
                            get();
                            c = peek(0);
                        }
                        tiny_dyncharbuf_add(&identifier, '\0');

                        int c2 = peek(1);
                        if (c == '_'
                                && (c2 == '\''
                                    || c2 == '"'))
                        {
                            int c1 = c;
                            get();
                            get();

                            tiny_dyncharbuf_t t_str;
                            tiny_dyncharbuf_new(&t_str, strlen(identifier.buf) + 32 + 1);

                            tiny_dyncharbuf_add_str(&t_str, identifier.buf);
                            xfree(identifier.buf);

                            tiny_dyncharbuf_add(&t_str, c1); // _
                            tiny_dyncharbuf_add(&t_str, c2); // " or '
                            tiny_dyncharbuf_add(&t_str, '\0');

                            int token_id;
                            char *text = NULL;
                            scan_character_literal(t_str.buf, /* delim */ c2, /* allow_suffix_boz */ 0, loc,
                                    &token_id, &text);
                            xfree(t_str.buf);

                            return commit_text_and_free(token_id, text, loc);
                        }

                        struct keyword_table_tag k;
                        k.keyword = identifier.buf;

                        struct keyword_table_tag *result =
                            (struct keyword_table_tag*)
                            bsearch(&k, keyword_table,
                                    sizeof(keyword_table) / sizeof(keyword_table[0]),
                                    sizeof(keyword_table[0]),
                                    keyword_table_comp);

                        ERROR_CONDITION(lexer_state.in_format_statement
                                && (result == NULL
                                    || result->token_id != TOKEN_FORMAT),
                                "Invalid token for format statement", 0);

                        int token_id = IDENTIFIER;
                        if (result != NULL)
                        {
                            token_id = result->token_id;
                        }

                        if (token_id == TOKEN_DO)
                        {
                            // Special treatment required for nonblock DO constructs
                            // so the label is properly matched
                            int peek_idx = 0;

                            c = peek(peek_idx);
                            while (is_blank(c))
                            {
                                peek_idx++;
                                c = peek(peek_idx);
                            }

                            if (is_decimal_digit(c))
                            {
                                lexer_state.in_nonblock_do_construct = 1;
                            }
                        }

                        return commit_text_and_free(token_id, identifier.buf, loc);
                    }
                    // Numbers
                case '0': case '1': case '2': case '3': case '4':
                case '5': case '6': case '7': case '8': case '9':
                    {
                        tiny_dyncharbuf_t digits;
                        tiny_dyncharbuf_new(&digits, 32);

                        tiny_dyncharbuf_add(&digits, c0);

                        int c = peek(0);
                        while (is_decimal_digit(c))
                        {
                            tiny_dyncharbuf_add(&digits, c);
                            get();
                            c = peek(0);
                        }
                        tiny_dyncharbuf_add(&digits, '\0');

                        if (tolower(c) == 'e'
                                || tolower(c) == 'd'
                                || tolower(c) == 'q')
                        {
                            char* fractional_part = scan_fractional_part_of_real_literal();

                            tiny_dyncharbuf_t t_str;
                            tiny_dyncharbuf_new(&t_str, strlen(digits.buf) + strlen(fractional_part) + 1);
                            tiny_dyncharbuf_add_str(&t_str, digits.buf);
                            xfree(digits.buf);
                            tiny_dyncharbuf_add_str(&t_str, fractional_part);
                            xfree(fractional_part);

                            tiny_dyncharbuf_add(&t_str, '\0');

                            return commit_text_and_free(REAL_LITERAL, t_str.buf, loc);
                        }
                        else if (c == '.')
                        {
                            // There are two cases here
                            //   1.op. must be tokenized as DECIMAL_LITERAL USER_DEFINED_OPERATOR
                            //   1.2   must be tokenized as REAL_LITERAL
                            // note that
                            //     1.e.2 is the first case
                            //     1.e+2 is the second case

                            // Check for 1.op.
                            int peek_idx = 1;
                            char d = peek(peek_idx);
                            while (is_letter(d)
                                    && peek_idx <= 32) // operator-names are limited to 32
                            {
                                peek_idx++;
                                d = peek(peek_idx);
                            }

                            if (d == '.'
                                    && peek_idx > 1)
                            {
                                // This is case 1.op.
                                return commit_text_and_free(DECIMAL_LITERAL, digits.buf, loc);
                            }
                            else
                            {
                                // scan the fractional part of a real
                                get(); // we must be past the '.' for scan_fractional_part_of_real_literal
                                char* fractional_part = scan_fractional_part_of_real_literal();

                                tiny_dyncharbuf_t t_str;
                                tiny_dyncharbuf_new(&t_str, strlen(digits.buf) + strlen(fractional_part) + 1);
                                tiny_dyncharbuf_add_str(&t_str, digits.buf);
                                xfree(digits.buf);
                                tiny_dyncharbuf_add_str(&t_str, fractional_part);
                                xfree(fractional_part);

                                tiny_dyncharbuf_add(&t_str, '\0');

                                return commit_text_and_free(REAL_LITERAL, t_str.buf, loc);
                            }
                        }
                        else if (c == '_')
                        {
                            // 1_"HELLO"
                            int c2 = peek(1);
                            if (c2 == '\''
                                    || c2 == '"')
                            {
                                int c1 = peek(0);
                                get();
                                c2 = peek(0);
                                get();

                                tiny_dyncharbuf_t t_str;
                                tiny_dyncharbuf_new(&t_str, strlen(digits.buf) + 32 + 1);

                                tiny_dyncharbuf_add_str(&t_str, digits.buf);
                                xfree(digits.buf);

                                tiny_dyncharbuf_add(&t_str, c1); // _
                                tiny_dyncharbuf_add(&t_str, c2); // " or '
                                tiny_dyncharbuf_add(&t_str, '\0');

                                int token_id;
                                char* text;
                                scan_character_literal(/*prefix */ t_str.buf, /* delim */ c2, /* allow_suffix_boz */ 0, loc,
                                        &token_id, &text);
                                xfree(t_str.buf);

                                return commit_text_and_free(token_id, text, loc);
                            }
                            else
                            {
                                char *kind_str = scan_kind();

                                tiny_dyncharbuf_t t_str;
                                tiny_dyncharbuf_new(&t_str, strlen(digits.buf) + strlen(kind_str) + 1);

                                tiny_dyncharbuf_add_str(&t_str, digits.buf);
                                xfree(digits.buf);

                                tiny_dyncharbuf_add_str(&t_str, kind_str);
                                xfree(kind_str);
                                tiny_dyncharbuf_add(&t_str, '\0');

                                return commit_text_and_free(DECIMAL_LITERAL, t_str.buf, loc);
                            }
                        }
                        else if (tolower(c) == 'h')
                        {
                            get();
                            // This is a Holleritz constant
                            int length = atoi(digits.buf);
                            xfree(digits.buf);

                            if (length == 0)
                            {
                                error_printf("%s:%d:%d: error: ignoring invalid Hollerith constant of length 0\n",
                                        loc.filename,
                                        loc.line,
                                        loc.column);
                                continue;
                            }
                            else
                            {
                                lexer_state.character_context = 1;
                                tiny_dyncharbuf_t holl;
                                tiny_dyncharbuf_new(&holl, length + 1);
                                int ok = 1;
                                for (int i = 0; i < length; i++)
                                {
                                    c = peek(0);
                                    if (is_newline(c)
                                            || c == EOF)
                                    {
                                        error_printf("%s:%d:%d: error: unended Hollerith constant\n",
                                                loc.filename,
                                                loc.line,
                                                loc.column);
                                        ok = 0;
                                        break;
                                    }
                                    tiny_dyncharbuf_add(&holl, c);
                                    get();
                                }
                                tiny_dyncharbuf_add(&holl, '\0');
                                lexer_state.character_context = 0;

                                if (!ok)
                                    continue;

                                return commit_text_and_free(TOKEN_HOLLERITH_CONSTANT, holl.buf, loc);
                            }
                        }
                        else
                        {
                            if (lexer_state.last_eos
                                    && strlen(digits.buf) <= 6 /* maximum length of a label */
                                    && is_format_statement())
                            {
                                lexer_state.in_format_statement = 1;
                            }
                            else if (lexer_state.in_nonblock_do_construct)
                            {
                                lexer_state.in_nonblock_do_construct = 0;

                                int label = atoi(digits.buf);
                                if (lexer_state.num_nonblock_labels > 0
                                        && lexer_state.nonblock_labels_stack[lexer_state.num_nonblock_labels - 1] == label)
                                {
                                    // DO 50 I = 1, 10
                                    //   DO 50 J = 1, 20  ! This second 50 is a TOKEN_SHARED_LABEL
                                    return commit_text_and_free(TOKEN_SHARED_LABEL, digits.buf, loc);
                                }
                                else
                                {
                                    if (lexer_state.num_nonblock_labels == lexer_state.size_nonblock_labels_stack)
                                    {
                                        lexer_state.size_nonblock_labels_stack = 2*lexer_state.size_nonblock_labels_stack + 1;
                                        lexer_state.nonblock_labels_stack = xrealloc(
                                                lexer_state.nonblock_labels_stack,
                                                lexer_state.size_nonblock_labels_stack
                                                * sizeof(*lexer_state.nonblock_labels_stack));
                                    }
                                    lexer_state.nonblock_labels_stack[lexer_state.num_nonblock_labels] = label;
                                    lexer_state.num_nonblock_labels++;
                                }
                            }

                            return commit_text_and_free(DECIMAL_LITERAL, digits.buf, loc);
                        }
                    }
                case '@':
                    {
                        tiny_dyncharbuf_t str;
                        tiny_dyncharbuf_new(&str, 32);

                        tiny_dyncharbuf_add(&str, c0);
                        char c = peek(0);
                        while (c != '@'
                                && !is_newline(c)
                                && c != EOF)
                        {
                            tiny_dyncharbuf_add(&str, c);
                            get();
                            c = peek(0);
                        }
                        if (c == '@')
                        {
                            tiny_dyncharbuf_add(&str, c);
                            get();
                        }
                        tiny_dyncharbuf_add(&str, '\0');

                        int token_id = 0;
                        char preserve_eos = 0;
                        if (strncmp(str.buf, "@STATEMENT-PH", strlen("@STATEMENT-PH")) == 0)
                        {
                            // FIXME - make the check above a bit more robust
                            token_id = STATEMENT_PLACEHOLDER;
                        }
                        else
                        {
                            struct special_token_table_tag k;
                            k.keyword = str.buf;

                            struct special_token_table_tag *result =
                                (struct special_token_table_tag*)
                                bsearch(&k, special_tokens,
                                        sizeof(special_tokens) / sizeof(special_tokens[0]),
                                        sizeof(special_tokens[0]),
                                        special_token_table_comp);

                            if (result == NULL)
                            {
                                error_printf("%s:%d:%d: invalid special token '%s', ignoring\n",
                                        loc.filename,
                                        loc.line,
                                        loc.column,
                                        str.buf);
                                continue;
                            }
                            else
                            {
                                token_id = result->token_id;
                                preserve_eos = result->preserve_eos;
                            }
                        }

                        char last_eos = lexer_state.last_eos;
                        int n = commit_text_and_free(token_id, str.buf, loc);
                        if (preserve_eos)
                            lexer_state.last_eos = last_eos;
                        return n;
                    }
                default: { /* do nothing */ }
            }
        } // LEXER_SUBSTATE_NORMAL
        else if (lexer_state.substate == LEXER_SUBSTATE_PRAGMA_DIRECTIVE
                || lexer_state.substate == LEXER_SUBSTATE_PRAGMA_FIRST_CLAUSE
                || lexer_state.substate == LEXER_SUBSTATE_PRAGMA_CLAUSE
                || lexer_state.substate == LEXER_SUBSTATE_PRAGMA_VAR_LIST)

        {
            switch (c0)
            {
                case EOF:
                    {
                        lexer_state.substate = LEXER_SUBSTATE_NORMAL;
                        lexer_state.sentinel = NULL;
                        error_printf("%s:%d:%d: error: unexpected end-of-file in directive\n",
                                loc.filename,
                                loc.line,
                                loc.column);
                        continue;
                    }
                case '\n':
                case '\r':
                    {
                        lexer_state.substate = LEXER_SUBSTATE_NORMAL;
                        lexer_state.sentinel = NULL;
                        if (!lexer_state.last_eos)
                        {
                            int n = commit_text(PRAGMA_CUSTOM_NEWLINE, NULL, loc);
                            lexer_state.bol = 1;
                            lexer_state.last_eos = 1;
                            return n;
                        }
                        lexer_state.bol = 1;
                        continue;
                    }
                case '!':
                    {
                        // A comment, skip until the end
                        while (!is_newline(c0))
                        {
                            get();
                            c0 = peek(0);
                        }

                        if (c0 == '\r')
                        {
                            if (peek(1) == '\n')
                                get();
                        }

                        continue;
                    }
                case ' ':
                case '\t':
                    {
                        // skip blanks
                        continue;
                    }
                default : { /* do nothing */ }
            }

            if (lexer_state.substate == LEXER_SUBSTATE_PRAGMA_DIRECTIVE)
            {
                // !$OMP xxx
                //       ^
                switch (c0)
                {
                    case 'A': case 'B': case 'C': case 'D': case 'E': case 'F': case 'G': case 'H':
                    case 'I': case 'J': case 'K': case 'L': case 'M': case 'N': case 'O': case 'P':
                    case 'Q': case 'R': case 'S': case 'T': case 'U': case 'V': case 'W': case 'X':
                    case 'Y': case 'Z':
                    case 'a': case 'b': case 'c': case 'd': case 'e': case 'f': case 'g': case 'h':
                    case 'i': case 'j': case 'k': case 'l': case 'm': case 'n': case 'o': case 'p':
                    case 'q': case 'r': case 's': case 't': case 'u': case 'v': case 'w': case 'x':
                    case 'y': case 'z':
                        {
                            // !$OMP xxx yyy zzz
                            //       ^
                            // identifier(\s\+{identifier})*
                            // and then we are told how much we have to back up
                            tiny_dyncharbuf_t str;
                            tiny_dyncharbuf_new(&str, 32);

                            tiny_dyncharbuf_add(&str, c0);
                            int peek_idx = 0;

                            char c = peek(peek_idx);
                            while (is_letter(c)
                                    || is_decimal_digit(c)
                                    || c == '_')
                            {
                                tiny_dyncharbuf_add(&str, c);
                                peek_idx++;
                                c = peek(peek_idx);
                            }

                            while (is_blank(c))
                            {
                                tiny_dyncharbuf_t t_str;
                                tiny_dyncharbuf_new(&t_str, 32);

                                while (is_blank(c))
                                {
                                    tiny_dyncharbuf_add(&t_str, c);
                                    peek_idx++;
                                    c = peek(peek_idx);
                                }

                                if (is_letter(c))
                                {
                                    while (is_letter(c)
                                            || is_decimal_digit(c)
                                            || c == '_')
                                    {
                                        tiny_dyncharbuf_add(&t_str, c);
                                        peek_idx++;
                                        c = peek(peek_idx);
                                    }
                                    tiny_dyncharbuf_add(&t_str, '\0');

                                    tiny_dyncharbuf_add_str(&str, t_str.buf);
                                    xfree(t_str.buf);
                                }
                                else
                                {
                                    xfree(t_str.buf);
                                    break;
                                }
                            }
                            tiny_dyncharbuf_add(&str, '\0');

                            char is_end_directive = (strlen(str.buf) > 3
                                    && strncasecmp(str.buf, "end", 3) == 0);

                            pragma_directive_kind_t directive_kind = PDK_NONE; 
                            const char* longest_match = return_pragma_prefix_longest_match(
                                    lexer_state.sentinel, is_end_directive, str.buf, &directive_kind);

                            int token_id = 0;
                            switch (directive_kind)
                            {
                                case PDK_DIRECTIVE:
                                    {
                                        if (!is_end_directive)
                                        {
                                            token_id = PRAGMA_CUSTOM_DIRECTIVE;
                                        }
                                        else
                                        {
                                            running_error("%s:%d:%d: error: invalid directive '!$%s END %s'\n", 
                                                    loc.filename,
                                                    loc.line,
                                                    loc.column,
                                                    strtoupper(lexer_state.sentinel),
                                                    strtoupper(longest_match));
                                        }
                                        break;
                                    }
                                case PDK_CONSTRUCT_NOEND :
                                    {
                                        if (!is_end_directive)
                                        {
                                            token_id = PRAGMA_CUSTOM_CONSTRUCT_NOEND;
                                        }
                                        else
                                        {
                                            token_id = PRAGMA_CUSTOM_END_CONSTRUCT_NOEND;
                                        }
                                        break;
                                    }
                                case PDK_CONSTRUCT :
                                    {
                                        if (!is_end_directive)
                                        {
                                            if (lexer_state.num_pragma_constructs == lexer_state.size_pragma_constructs_stack)
                                            {
                                                lexer_state.size_pragma_constructs_stack = 2*lexer_state.size_pragma_constructs_stack + 1;
                                                lexer_state.pragma_constructs_stack = xrealloc(
                                                        lexer_state.pragma_constructs_stack,
                                                        lexer_state.size_pragma_constructs_stack
                                                        *sizeof(*lexer_state.pragma_constructs_stack));
                                            }
                                            lexer_state.pragma_constructs_stack[lexer_state.num_pragma_constructs] = xstrdup(longest_match);
                                            lexer_state.num_pragma_constructs++;
                                            token_id = PRAGMA_CUSTOM_CONSTRUCT;
                                        }
                                        else
                                        {
                                            if (lexer_state.num_pragma_constructs > 0)
                                            {
                                                char* top = lexer_state.pragma_constructs_stack[lexer_state.num_pragma_constructs-1];
                                                if (strcmp(top, longest_match) != 0)
                                                {
                                                    running_error("%s:%d:%d: error: invalid nesting for '!$%s %s', expecting '!$%s END %s'\n", 
                                                            loc.filename,
                                                            loc.line,
                                                            loc.column,
                                                            strtoupper(lexer_state.sentinel), 
                                                            strtoupper(longest_match),
                                                            strtoupper(lexer_state.sentinel), 
                                                            strtoupper(format_pragma_string(top)));
                                                }
                                                else
                                                {
                                                    xfree(top);
                                                    lexer_state.num_pragma_constructs--;
                                                    token_id = PRAGMA_CUSTOM_END_CONSTRUCT;
                                                }
                                            }
                                            else
                                            {
                                                running_error("%s:%d:%d: error: bad nesting for '!$%s %s'\n",
                                                        loc.filename,
                                                        loc.line,
                                                        loc.column,
                                                        strtoupper(lexer_state.sentinel), 
                                                        strtoupper(longest_match));
                                            }
                                        }
                                        break;
                                    }
                                case PDK_NONE :
                                    {
                                        running_error("%s:%d:%d: error: unknown directive '!$%s %s'",
                                                loc.filename,
                                                loc.line,
                                                loc.column,
                                                lexer_state.sentinel,
                                                str.buf);
                                        break;
                                    }
                                default: internal_error("Invalid pragma directive kind kind=%d", directive_kind);
                            }

                            lexer_state.substate = LEXER_SUBSTATE_PRAGMA_FIRST_CLAUSE;
                            int n = commit_text(token_id, longest_match, loc);
                            xfree(str.buf);
                            return n;
                            break;
                        }
                    default : { /* do nothing */ }
                }
            }
            else if (lexer_state.substate == LEXER_SUBSTATE_PRAGMA_FIRST_CLAUSE)
            {
                // !$OMP PARALLEL xxx
                //                ^
                switch (c0)
                {
                    case '(':
                        {
                            lexer_state.substate = LEXER_SUBSTATE_PRAGMA_VAR_LIST;
                            return commit_text('(', "(", loc);
                            break;
                        }
                    case 'A': case 'B': case 'C': case 'D': case 'E': case 'F': case 'G': case 'H':
                    case 'I': case 'J': case 'K': case 'L': case 'M': case 'N': case 'O': case 'P':
                    case 'Q': case 'R': case 'S': case 'T': case 'U': case 'V': case 'W': case 'X':
                    case 'Y': case 'Z':
                    case 'a': case 'b': case 'c': case 'd': case 'e': case 'f': case 'g': case 'h':
                    case 'i': case 'j': case 'k': case 'l': case 'm': case 'n': case 'o': case 'p':
                    case 'q': case 'r': case 's': case 't': case 'u': case 'v': case 'w': case 'x':
                    case 'y': case 'z':
                        {
                            tiny_dyncharbuf_t str;
                            tiny_dyncharbuf_new(&str, 32);

                            tiny_dyncharbuf_add(&str, c0);

                            char c = peek(0);
                            while (is_letter(c)
                                    || c == '_'
                                    || is_decimal_digit(c))
                            {
                                tiny_dyncharbuf_add(&str, c);
                                get();
                                c = peek(0);
                            }
                            tiny_dyncharbuf_add(&str, '\0');

                            // advance blanks now because we want to move onto
                            // the next '(' if any
                            while (is_blank(c))
                            {
                                get();
                                c = peek(0);
                            }

                            if (c == '(')
                            {
                                lexer_state.substate = LEXER_SUBSTATE_PRAGMA_VAR_LIST;
                            }
                            else
                            {
                                lexer_state.substate = LEXER_SUBSTATE_PRAGMA_CLAUSE;
                            }

                            return commit_text_and_free(PRAGMA_CUSTOM_CLAUSE, str.buf, loc);
                            break;
                        }
                    default: { /* do nothing */ }
                }
            }
            else if (lexer_state.substate == LEXER_SUBSTATE_PRAGMA_CLAUSE)
            {
                // !$OMP PARALLEL FOO(X) xxx ...
                //                       ^
                switch (c0)
                {
                    case ',':
                        {
                            return commit_text(',', ",", loc);
                            continue;
                        }
                    case 'A': case 'B': case 'C': case 'D': case 'E': case 'F': case 'G': case 'H':
                    case 'I': case 'J': case 'K': case 'L': case 'M': case 'N': case 'O': case 'P':
                    case 'Q': case 'R': case 'S': case 'T': case 'U': case 'V': case 'W': case 'X':
                    case 'Y': case 'Z':
                    case 'a': case 'b': case 'c': case 'd': case 'e': case 'f': case 'g': case 'h':
                    case 'i': case 'j': case 'k': case 'l': case 'm': case 'n': case 'o': case 'p':
                    case 'q': case 'r': case 's': case 't': case 'u': case 'v': case 'w': case 'x':
                    case 'y': case 'z':
                        {
                            tiny_dyncharbuf_t str;
                            tiny_dyncharbuf_new(&str, 32);

                            tiny_dyncharbuf_add(&str, c0);

                            char c = peek(0);
                            while (is_letter(c)
                                    || c == '_'
                                    || is_decimal_digit(c))
                            {
                                tiny_dyncharbuf_add(&str, c);
                                get();
                                c = peek(0);
                            }
                            tiny_dyncharbuf_add(&str, '\0');

                            // advance blanks now because we want to move onto
                            // the next '(' if any
                            while (is_blank(c))
                            {
                                get();
                                c = peek(0);
                            }

                            if (c == '(')
                            {
                                lexer_state.substate = LEXER_SUBSTATE_PRAGMA_VAR_LIST;
                            }

                            return commit_text_and_free(PRAGMA_CUSTOM_CLAUSE, str.buf, loc);
                            break;
                        }
                    default: { /* do nothing */ }
                }
            }
            else if (lexer_state.substate == LEXER_SUBSTATE_PRAGMA_VAR_LIST)
            {
                // !$OMP PARALLEL FOO(xxx)
                //                    ^
                if (c0 == '(')
                {
                    return commit_text('(', "(", loc);
                }
                else if (c0 == ')')
                {
                    lexer_state.substate = LEXER_SUBSTATE_PRAGMA_CLAUSE;
                    return commit_text(')', ")", loc);
                }
                else
                {
                    tiny_dyncharbuf_t str;
                    tiny_dyncharbuf_new(&str, 32);

                    tiny_dyncharbuf_add(&str, c0);

                    int parentheses = 0;
                    char c = peek(0);
                    while ((c != ')'
                                || parentheses > 0)
                            && !is_newline(c)
                            && c != EOF)
                    {
                        tiny_dyncharbuf_add(&str, c);

                        if (c == '(')
                            parentheses++;
                        else if (c == ')')
                            parentheses--;

                        get();
                        c = peek(0);
                    }
                    tiny_dyncharbuf_add(&str, '\0');

                    if (c != ')')
                    {
                        error_printf("%s:%d:%d: error: unended clause\n",
                                loc.filename,
                                loc.line,
                                loc.column);
                    }

                    return commit_text_and_free(PRAGMA_CLAUSE_ARG_TEXT, str.buf, loc);
                }
            }
        }
        else
        {
            internal_error("invalid lexer substate", 0);
        }

        // Default case, unclassifiable token
        if (isprint(c0))
        {
            error_printf("%s:%d:%d: error: unexpected character: `%c' (0x%X)\n", 
                    loc.filename,
                    loc.line,
                    loc.column,
                    c0, c0);
        }
        else
        {
            error_printf("%s:%d:%d: error: unexpected character: 0x%X\n\n", 
                    loc.filename,
                    loc.line,
                    loc.column,
                    c0);
        }
    }
    internal_error("Code unreachable", 0);
}