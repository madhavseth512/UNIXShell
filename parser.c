#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "msh.h"

typedef struct {
    token_list *tl;
    int pos;
} parser;

static token *peek(parser *ps) { return &ps->tl->items[ps->pos]; }

static void advance(parser *ps) {
    if (peek(ps)->type != TOK_EOF) {
        ps->pos++;
    }
}

/* Human-readable name for an error message. */
static const char *tok_name(const token *t) {
    switch (t->type) {
    case TOK_WORD:   return t->text;
    case TOK_LESS:   return "<";
    case TOK_GREAT:  return ">";
    case TOK_DGREAT: return ">>";
    case TOK_PIPE:   return "|";
    case TOK_AND_IF: return "&&";
    case TOK_OR_IF:  return "||";
    case TOK_SEMI:   return ";";
    case TOK_EOF:    return "newline";
    }
    return "?";
}

static void syntax_error(const token *t) {
    if (t->type == TOK_EOF) {
        fprintf(stderr, "msh: syntax error: unexpected end of input\n");
    } else {
        fprintf(stderr, "msh: syntax error near unexpected token `%s'\n",
                tok_name(t));
    }
}

/* ---------------- destructors ---------------- */

static void redir_free(redir *r) {
    while (r != NULL) {
        redir *next = r->next;
        free(r->target);
        free(r);
        r = next;
    }
}

static void command_free(command *c) {
    if (c == NULL) {
        return;
    }
    for (int i = 0; i < c->argc; i++) {
        free(c->argv[i]);
    }
    free(c->argv);
    redir_free(c->redirs);
    free(c);
}

static void pipeline_free(pipeline *pl) {
    if (pl == NULL) {
        return;
    }
    for (int i = 0; i < pl->ncmds; i++) {
        command_free(pl->cmds[i]);
    }
    free(pl->cmds);
    free(pl);
}

static void andor_free(andor *ao) {
    while (ao != NULL) {
        andor *next = ao->next;
        pipeline_free(ao->pl);
        free(ao);
        ao = next;
    }
}

void cmdlist_free(cmdlist *cl) {
    while (cl != NULL) {
        cmdlist *next = cl->next;
        andor_free(cl->ao);
        free(cl);
        cl = next;
    }
}

/* ---------------- productions ---------------- */

static int append_arg(command *c, int *cap, const char *word) {
    /* Keep one slot free at all times so argv can always be NULL-terminated;
       execvp finds the end of the list by that NULL and nothing else. */
    if (c->argc + 1 >= *cap) {
        int newcap = *cap ? *cap * 2 : 8;
        char **tmp = realloc(c->argv, (size_t)newcap * sizeof *tmp);
        if (tmp == NULL) {
            return -1;
        }
        c->argv = tmp;
        *cap = newcap;
    }
    c->argv[c->argc] = strdup(word);
    if (c->argv[c->argc] == NULL) {
        return -1;
    }
    c->argc++;
    c->argv[c->argc] = NULL;
    return 0;
}

static command *parse_command(parser *ps) {
    command *c = calloc(1, sizeof *c);
    if (c == NULL) {
        fprintf(stderr, "msh: out of memory\n");
        return NULL;
    }

    int argcap = 0;
    int oom = 0;

    while (peek(ps)->type == TOK_WORD) {
        if (append_arg(c, &argcap, peek(ps)->text) == -1) {
            oom = 1;
            break;
        }
        advance(ps);
    }

    if (oom) {
        fprintf(stderr, "msh: out of memory\n");
        command_free(c);
        return NULL;
    }

    if (c->argc == 0) {
        syntax_error(peek(ps));
        command_free(c);
        return NULL;
    }
    return c;
}

/*
 * Milestone 5 turns this into a loop over '|'. For now a pipeline is
 * exactly one command.
 */
static pipeline *parse_pipeline(parser *ps) {
    pipeline *pl = calloc(1, sizeof *pl);
    if (pl == NULL) {
        fprintf(stderr, "msh: out of memory\n");
        return NULL;
    }

    command *c = parse_command(ps);
    if (c == NULL) {
        pipeline_free(pl);
        return NULL;
    }

    pl->cmds = malloc(sizeof *pl->cmds);
    if (pl->cmds == NULL) {
        fprintf(stderr, "msh: out of memory\n");
        command_free(c);
        pipeline_free(pl);
        return NULL;
    }
    pl->cmds[pl->ncmds++] = c;
    return pl;
}

/*
 * Milestone 6 turns this into a loop over '&&' and '||'.
 */
static andor *parse_andor(parser *ps) {
    pipeline *pl = parse_pipeline(ps);
    if (pl == NULL) {
        return NULL;
    }

    andor *node = calloc(1, sizeof *node);
    if (node == NULL) {
        fprintf(stderr, "msh: out of memory\n");
        pipeline_free(pl);
        return NULL;
    }
    node->pl = pl;
    node->op_to_next = OP_NONE;
    return node;
}

cmdlist *parse(token_list *tl, int *syntax_err) {
    parser ps = {tl, 0};

    *syntax_err = 0;
    if (peek(&ps)->type == TOK_EOF) {
        return NULL; /* blank line: not an error */
    }

    andor *ao = parse_andor(&ps);
    if (ao == NULL) {
        *syntax_err = 1;
        return NULL;
    }

    cmdlist *head = calloc(1, sizeof *head);
    if (head == NULL) {
        fprintf(stderr, "msh: out of memory\n");
        *syntax_err = 1;
        andor_free(ao);
        return NULL;
    }
    head->ao = ao;

    /* Milestone 6 turns this into a loop over ';'. Until then anything
       left over is an operator this parser cannot yet handle. */
    if (peek(&ps)->type != TOK_EOF) {
        syntax_error(peek(&ps));
        *syntax_err = 1;
        cmdlist_free(head);
        return NULL;
    }
    return head;
}
