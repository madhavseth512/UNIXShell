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

static int append_redir(command *c, redir ***tail, redir_kind kind,
                        const char *target) {
    redir *r = calloc(1, sizeof *r);
    if (r == NULL) {
        return -1;
    }
    r->kind = kind;
    r->target = strdup(target);
    if (r->target == NULL) {
        free(r);
        return -1;
    }
    **tail = r;
    *tail = &r->next;
    (void)c;
    return 0;
}

static command *parse_command(parser *ps) {
    command *c = calloc(1, sizeof *c);
    if (c == NULL) {
        fprintf(stderr, "msh: out of memory\n");
        return NULL;
    }

    int argcap = 0;
    redir **rtail = &c->redirs;
    int oom = 0;

    for (;;) {
        token *t = peek(ps);

        if (t->type == TOK_WORD) {
            if (append_arg(c, &argcap, t->text) == -1) {
                oom = 1;
                break;
            }
            advance(ps);
            continue;
        }

        if (t->type == TOK_LESS || t->type == TOK_GREAT ||
            t->type == TOK_DGREAT) {
            redir_kind kind = (t->type == TOK_LESS)    ? REDIR_IN
                              : (t->type == TOK_GREAT) ? REDIR_OUT
                                                       : REDIR_APPEND;
            advance(ps);
            /* A redirection operator must be followed by a filename. */
            if (peek(ps)->type != TOK_WORD) {
                syntax_error(peek(ps));
                command_free(c);
                return NULL;
            }
            if (append_redir(c, &rtail, kind, peek(ps)->text) == -1) {
                oom = 1;
                break;
            }
            advance(ps);
            continue;
        }

        break;
    }

    if (oom) {
        fprintf(stderr, "msh: out of memory\n");
        command_free(c);
        return NULL;
    }

    /* Nothing at all: the caller wrote "| foo", "foo &&", or similar. */
    if (c->argc == 0 && c->redirs == NULL) {
        syntax_error(peek(ps));
        command_free(c);
        return NULL;
    }
    return c;
}

static pipeline *parse_pipeline(parser *ps) {
    pipeline *pl = calloc(1, sizeof *pl);
    if (pl == NULL) {
        fprintf(stderr, "msh: out of memory\n");
        return NULL;
    }

    int cap = 0;
    for (;;) {
        command *c = parse_command(ps);
        if (c == NULL) {
            pipeline_free(pl);
            return NULL;
        }

        if (pl->ncmds == cap) {
            int newcap = cap ? cap * 2 : 4;
            command **tmp = realloc(pl->cmds, (size_t)newcap * sizeof *tmp);
            if (tmp == NULL) {
                fprintf(stderr, "msh: out of memory\n");
                command_free(c);
                pipeline_free(pl);
                return NULL;
            }
            pl->cmds = tmp;
            cap = newcap;
        }
        pl->cmds[pl->ncmds++] = c;

        if (peek(ps)->type != TOK_PIPE) {
            break;
        }
        advance(ps);
    }
    return pl;
}

static andor *parse_andor(parser *ps) {
    andor *head = NULL;
    andor **tail = &head;

    for (;;) {
        pipeline *pl = parse_pipeline(ps);
        if (pl == NULL) {
            andor_free(head);
            return NULL;
        }

        andor *node = calloc(1, sizeof *node);
        if (node == NULL) {
            fprintf(stderr, "msh: out of memory\n");
            pipeline_free(pl);
            andor_free(head);
            return NULL;
        }
        node->pl = pl;
        node->op_to_next = OP_NONE;
        *tail = node;
        tail = &node->next;

        token_type t = peek(ps)->type;
        if (t == TOK_AND_IF) {
            node->op_to_next = OP_AND;
        } else if (t == TOK_OR_IF) {
            node->op_to_next = OP_OR;
        } else {
            break;
        }
        advance(ps);
    }
    return head;
}

cmdlist *parse(token_list *tl, int *syntax_err) {
    parser ps = {tl, 0};

    *syntax_err = 0;
    if (peek(&ps)->type == TOK_EOF) {
        return NULL; /* blank line: not an error */
    }

    cmdlist *head = NULL;
    cmdlist **tail = &head;

    for (;;) {
        andor *ao = parse_andor(&ps);
        if (ao == NULL) {
            *syntax_err = 1;
            cmdlist_free(head);
            return NULL;
        }

        cmdlist *node = calloc(1, sizeof *node);
        if (node == NULL) {
            fprintf(stderr, "msh: out of memory\n");
            *syntax_err = 1;
            andor_free(ao);
            cmdlist_free(head);
            return NULL;
        }
        node->ao = ao;
        *tail = node;
        tail = &node->next;

        if (peek(&ps)->type != TOK_SEMI) {
            break;
        }
        advance(&ps);
        if (peek(&ps)->type == TOK_EOF) {
            break; /* a trailing ';' is allowed */
        }
    }

    if (peek(&ps)->type != TOK_EOF) {
        syntax_error(peek(&ps));
        *syntax_err = 1;
        cmdlist_free(head);
        return NULL;
    }
    return head;
}
