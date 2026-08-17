#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "msh.h"

/*
 * Characters that end a word. A word runs until one of these, so
 * "echo hi>out" lexes as WORD(echo) WORD(hi) GREAT WORD(out) with no
 * whitespace required anywhere.
 *
 * '&' is here so that a stray "&" is caught as an error rather than
 * swallowed into a word; background jobs are milestone 9.
 */
static int is_meta(char c) {
    return c == '\0' || c == ' ' || c == '\t' || c == '<' || c == '>' ||
           c == '|' || c == ';' || c == '&';
}

/* Append a token, growing the array geometrically. Takes ownership of text. */
static int push(token_list *tl, int *cap, token_type type, char *text) {
    if (tl->count == *cap) {
        int newcap = *cap ? *cap * 2 : 16;
        token *tmp = realloc(tl->items, (size_t)newcap * sizeof *tmp);
        if (tmp == NULL) {
            free(text);
            return -1;
        }
        tl->items = tmp;
        *cap = newcap;
    }
    tl->items[tl->count].type = type;
    tl->items[tl->count].text = text;
    tl->count++;
    return 0;
}

int lex(const char *line, token_list *tl) {
    int cap = 0;

    tl->items = NULL;
    tl->count = 0;

    for (const char *p = line; *p != '\0';) {
        int rc;

        if (*p == ' ' || *p == '\t') {
            p++;
            continue;
        }

        switch (*p) {
        case '<':
            rc = push(tl, &cap, TOK_LESS, NULL);
            p += 1;
            break;
        case '>':
            if (p[1] == '>') {
                rc = push(tl, &cap, TOK_DGREAT, NULL);
                p += 2;
            } else {
                rc = push(tl, &cap, TOK_GREAT, NULL);
                p += 1;
            }
            break;
        case '|':
            if (p[1] == '|') {
                rc = push(tl, &cap, TOK_OR_IF, NULL);
                p += 2;
            } else {
                rc = push(tl, &cap, TOK_PIPE, NULL);
                p += 1;
            }
            break;
        case '&':
            if (p[1] == '&') {
                rc = push(tl, &cap, TOK_AND_IF, NULL);
                p += 2;
                break;
            }
            fprintf(stderr, "msh: syntax error near unexpected token `&'\n");
            token_list_free(tl);
            return -1;
        case ';':
            rc = push(tl, &cap, TOK_SEMI, NULL);
            p += 1;
            break;
        default: {
            const char *start = p;
            while (!is_meta(*p)) {
                p++;
            }
            char *text = strndup(start, (size_t)(p - start));
            if (text == NULL) {
                fprintf(stderr, "msh: out of memory\n");
                token_list_free(tl);
                return -1;
            }
            rc = push(tl, &cap, TOK_WORD, text);
            break;
        }
        }

        if (rc == -1) {
            fprintf(stderr, "msh: out of memory\n");
            token_list_free(tl);
            return -1;
        }
    }

    if (push(tl, &cap, TOK_EOF, NULL) == -1) {
        fprintf(stderr, "msh: out of memory\n");
        token_list_free(tl);
        return -1;
    }
    return 0;
}

void token_list_free(token_list *tl) {
    for (int i = 0; i < tl->count; i++) {
        free(tl->items[i].text);
    }
    free(tl->items);
    tl->items = NULL;
    tl->count = 0;
}
