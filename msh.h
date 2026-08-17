#ifndef MSH_H
#define MSH_H

#include <signal.h>
#include <sys/types.h>

/* ------------------------------------------------------------------ *
 * Lexer
 *
 * A hand-written character scanner, not strtok_r. Operators must be
 * recognised without surrounding whitespace ("ls>out" is one command,
 * three tokens), which a delimiter-splitter cannot express.
 * ------------------------------------------------------------------ */

typedef enum {
    TOK_WORD,
    TOK_LESS,    /* <  */
    TOK_GREAT,   /* >  */
    TOK_DGREAT,  /* >> */
    TOK_PIPE,    /* |  */
    TOK_AND_IF,  /* && */
    TOK_OR_IF,   /* || */
    TOK_SEMI,    /* ;  */
    TOK_EOF
} token_type;

typedef struct {
    token_type type;
    char *text; /* owned; NULL for every type except TOK_WORD */
} token;

typedef struct {
    token *items;
    int count;
} token_list;

/* Returns 0 on success, -1 on a lexical error (already reported). */
int lex(const char *line, token_list *out);
void token_list_free(token_list *tl);

/* ------------------------------------------------------------------ *
 * AST
 *
 * The whole grammar is declared here up front, because the shape of the
 * tree is what the parser is for. Milestones 4-6 fill in the productions
 * that are currently stubs: this milestone parses a single command, and
 * anything else is reported as a syntax error.
 *
 *   list     := andor ( ';' andor )* [';']
 *   andor    := pipeline ( ('&&' | '||') pipeline )*
 *   pipeline := command ( '|' command )*
 *   command  := ( WORD | redirect )+
 * ------------------------------------------------------------------ */

typedef enum { REDIR_IN, REDIR_OUT, REDIR_APPEND } redir_kind;

typedef struct redir {
    redir_kind kind;
    char *target;
    struct redir *next;
} redir;

typedef struct {
    char **argv; /* NULL-terminated; may be NULL when the command is only
                    redirections, as in a bare "> file" */
    int argc;
    redir *redirs;
} command;

typedef struct {
    command **cmds;
    int ncmds;
} pipeline;

typedef enum { OP_NONE, OP_AND, OP_OR } andor_op;

typedef struct andor {
    pipeline *pl;
    andor_op op_to_next; /* operator joining this node to ->next */
    struct andor *next;
} andor;

typedef struct cmdlist {
    andor *ao;
    struct cmdlist *next; /* next ';'-separated item */
} cmdlist;

/*
 * Returns the parsed list, or NULL. NULL means an empty line when
 * *syntax_error is 0, and a reported syntax error when it is 1.
 */
cmdlist *parse(token_list *tl, int *syntax_error);
void cmdlist_free(cmdlist *cl);

/* ------------------------------------------------------------------ *
 * Execution
 * ------------------------------------------------------------------ */

int run_cmdlist(cmdlist *cl);

/* ------------------------------------------------------------------ *
 * Builtins
 * ------------------------------------------------------------------ */

int is_builtin(const char *name);
int run_builtin(command *cmd);

/* ------------------------------------------------------------------ *
 * Shell-wide state
 * ------------------------------------------------------------------ */

extern int g_last_status;    /* $? */
extern int g_exit_requested; /* set by the exit builtin */
extern int g_exit_code;

#endif /* MSH_H */
