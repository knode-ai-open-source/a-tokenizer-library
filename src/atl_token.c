// SPDX-FileCopyrightText:  2023 Andy Curtis <contactandyc@gmail.com>
// SPDX-License-Identifier: Apache-2.0
#include "a-tokenizer-library/atl_token.h"
#include "a-memory-library/aml_buffer.h"
#include "the-io-library/io_in.h"
#include "the-macro-library/macro_map.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

struct token_head_s;
typedef struct token_head_s token_head_t;

struct token_head_s {
    aml_pool_t *pool;
    atl_token_t *head;
    atl_token_t *tail;

    atl_token_t *next;
    atl_token_t *global;

    atl_token_set_var_cb cb;
    void *arg;    
};

static
void token_merge(token_head_t *th, const char *p, size_t len, atl_token_type_t type ) {
    size_t tlen = strlen(th->tail->token);

    // Allocate a new token that copies the old tail
    atl_token_t *t = (atl_token_t *)aml_pool_alloc(th->pool,
                        sizeof(atl_token_t) + (tlen + len + 1));
    *t = *(th->tail); // copies pos
    t->token = (char *)(t+1);
    t->type = type;
    memcpy(t->token, th->tail->token, tlen);
    memcpy(t->token + tlen, p, len);
    t->token[tlen + len] = '\0';
    t->len = tlen + len;

    if(t->parent && t->parent->child==th->tail)
        t->parent->child = t;
    if(t->child && t->child->parent==th->tail)
        t->child->parent = t;
    if(t->prev && t->prev->next==th->tail)
        t->prev->next = t;
    if(t->next && t->next->prev==th->tail)
        t->next->prev = t;

    if(th->head == th->tail) {
        th->head = th->tail = t;
    }
    else {
        th->tail = t;
    }
}

static atl_token_t * __token_init(token_head_t *th, const char *p, size_t len, size_t offs,
								  atl_token_type_t type, atl_token_cb_t attr_type, atl_token_t *alt_token) {
    if(th->tail) {
        if(p[0] == '-' && p[1] == 0 && th->tail->token[0] == '-' && th->tail->token[1] == 0) {
            th->tail->token[0] = '+';
            return NULL;
        }
        if(p[0] == '+' && p[1] == 0 && th->tail->token[0] == '+' && th->tail->token[1] == 0) {
            return NULL;
        }
        if(p[0] == '-' && p[1] == 0 && th->tail->token[0] == '+' && th->tail->token[1] == 0) {
            th->tail->token[0] = '-';
            return NULL;
        }
        if(p[0] == '+' && p[1] == 0 && th->tail->token[0] == '-' && th->tail->token[1] == 0) {
            return NULL;
        }
        if(type == ATL_TOKEN_COMPARISON && th->tail->type == ATL_TOKEN_COMPARISON) {
            token_merge(th, p, len, type);
            return NULL;
        }
    }

    atl_token_type_t tail_type = ATL_TOKEN_OTHER;
    if(th->tail)
        tail_type = th->tail->type;

    if(type == ATL_TOKEN_CLOSE_PAREN) {
        if(th->tail && th->tail->parent && th->tail->parent->type == ATL_TOKEN_OPEN_PAREN)
            th->tail = th->tail->parent;
        else if(tail_type == ATL_TOKEN_OPEN_PAREN && !th->tail->child) {
            atl_token_t *t = (atl_token_t *)aml_pool_zalloc(th->pool, sizeof(atl_token_t) + 1);
            t->token = (char *)(t+1);
            t->type = ATL_TOKEN_NULL;
            t->parent = th->tail;
			t->pos = offs;
			t->len = len;
            th->tail->child = t;
        }
        return NULL;
    }
    else if(type == ATL_TOKEN_CLOSE_BRACE) {
        if(th->tail && th->tail->parent && th->tail->parent->type == ATL_TOKEN_OPEN_BRACE)
            th->tail = th->tail->parent;
        else if(tail_type == ATL_TOKEN_OPEN_BRACE && !th->tail->child) {
            atl_token_t *t = (atl_token_t *)aml_pool_zalloc(th->pool, sizeof(atl_token_t) + 1);
            t->token = (char *)(t+1);
            t->type = ATL_TOKEN_NULL;
            t->parent = th->tail;
			t->pos = offs;
			t->len = len;
            th->tail->child = t;
        }
        return NULL;
    }    
    else if(type == ATL_TOKEN_CLOSE_BRACKET) {
        if(th->tail && th->tail->parent && th->tail->parent->type == ATL_TOKEN_OPEN_BRACKET)
            th->tail = th->tail->parent;
        else if(tail_type == ATL_TOKEN_OPEN_BRACKET && !th->tail->child) {
            atl_token_t *t = (atl_token_t *)aml_pool_zalloc(th->pool, sizeof(atl_token_t) + 1);
            t->token = (char *)(t+1);
            t->type = ATL_TOKEN_NULL;
            t->parent = th->tail;
			t->pos = offs;
			t->len = len;
            th->tail->child = t;
        }
        return NULL;
    }
    else if(type == ATL_TOKEN_DQUOTE) {
        if(th->tail && th->tail->parent && th->tail->parent->type == ATL_TOKEN_DQUOTE) {
            th->tail = th->tail->parent;
            return NULL;        
        }
        else if(tail_type == ATL_TOKEN_DQUOTE && !th->tail->child) {
            return NULL;        
        }
    }
    else if(type == ATL_TOKEN_SPACE) {
        if(th->tail && th->tail->parent && 
           th->tail->parent->type == ATL_TOKEN_COMPARISON && 
           !strcmp(th->tail->parent->token, "="))
            th->tail = th->tail->parent;
        return NULL;
    }
    else if(type == ATL_TOKEN_COMMA) {        
        if(!th->tail || ((!th->tail->parent || (th->tail->parent->type != ATL_TOKEN_OPEN_PAREN && th->tail->parent->type != ATL_TOKEN_OPEN_BRACKET)) && tail_type != ATL_TOKEN_OPEN_PAREN && tail_type != ATL_TOKEN_OPEN_BRACKET)) {            
            return NULL;
        }
    }

    atl_token_t *t = alt_token;
    atl_token_t *resp = t;
    if(!t) {   
        t = (atl_token_t *)aml_pool_zalloc(th->pool, sizeof(atl_token_t) + len+1);
        resp = t;
        t->token = (char *)(t+1);
        memcpy(t->token, p, len);
        t->token[len] = 0;
        t->type = type;
		t->pos = offs;
		t->len = len;
        t->attr_type = attr_type;
    }
    if(attr_type == NEXT) {
        t->next = th->next;
        th->next = t;
        return resp;
    }
    else if(attr_type == GLOBAL) {
        t->next = th->global;
        th->global = t;
        return resp;
    }
    t->attr = th->next;
    th->next = NULL;

    if(!th->head)
        th->head = th->tail = t;
    else {
        if(!th->tail->child && 
            (tail_type == ATL_TOKEN_OPEN_PAREN || 
             th->tail->type == ATL_TOKEN_OPEN_BRACE || 
             th->tail->type == ATL_TOKEN_OPEN_BRACKET ||
             th->tail->type == ATL_TOKEN_DQUOTE
            )) {            
            t->parent = th->tail;
            th->tail->child = t;            
            th->tail = t;
        } 
        else if(tail_type == ATL_TOKEN_COMPARISON && !th->tail->child && !strcmp(th->tail->token, "=")) {
            t->parent = th->tail;
            th->tail->child = t;
            th->tail = t;
        } 
        else {
            t->parent = th->tail->parent;
            t->prev = th->tail;
            th->tail->next = t;        
            th->tail = t;
            if(tail_type == ATL_TOKEN_COLON) {
                if(th->tail->prev && th->tail->prev->prev && th->tail->prev->prev->prev && th->tail->prev->prev->prev->type == ATL_TOKEN_QUESTION  && 
                th->tail->prev->prev->prev->prev && th->tail->prev->prev->prev->prev->prev && th->tail->prev->prev->prev->prev->prev->type == ATL_TOKEN_COMPARISON && th->tail->prev->prev->prev->prev->prev->prev) {
                    t = (atl_token_t *)aml_pool_zalloc(th->pool, sizeof(atl_token_t) + 2);
                    *t = *(th->tail->prev->prev->prev->prev->prev->prev);
                    t->token = (char *)(t+1);
                    t->token[0] = '(';
                    t->token[1] = 0;
                    t->type = ATL_TOKEN_OPEN_PAREN;
                    t->child = th->tail->prev->prev->prev->prev->prev->prev;
                    t->next = NULL;
					t->pos = offs;
					t->len = len;
                    t->child->parent = t;
                    t->child->prev = NULL;
                    th->tail = t;
                    if(t->prev)
                        t->prev->next = t;
                }
            }

        }
    }
    return resp;
}

static atl_token_t * _token_init(token_head_t *th, const char *p, size_t len, size_t offs,
								 atl_token_type_t type, atl_token_cb_t attr_type) {
    atl_token_t *alt = NULL;
    if(th->cb) {
        atl_token_cb_data_t data;
        memset(&data, 0, sizeof(data));
        data.pool = th->pool;
        data.param = aml_pool_strndup(th->pool, p, len);
        if(th->cb(th->arg, &data) == SKIP)
            return NULL;
        alt = data.alt_token;
    }
    return __token_init(th, p, len, offs, type, attr_type, alt );
}

static atl_token_t * token_init(token_head_t *th, const char *p, size_t len, size_t offs, atl_token_type_t type) {
    return _token_init(th, p, len, offs, type, NORMAL );
}

static void indent(int depth) {
    for( int i=0; i<depth; i++ )
        printf( "\t" );    
}

void token_dump(atl_token_t *t, int depth) {
    while(t) {
        indent(depth);
        printf( "%s\n", t->token );
        for( uint32_t i=0; i<t->num_attrs; i++ ) {
            indent(depth);
            printf( " * %s\n", t->attrs[i] );
        }
        if(t->attr) {
            token_dump(t->attr, depth+2);
        }
        if(t->child) {
            token_dump(t->child, depth+1);
        }
        t = t->next;
    }
}

void atl_token_dump(atl_token_t *t) {
    token_dump(t, 0);
}


static atl_token_t *token_parse_init(token_head_t *th,
									 const char *p,
									 size_t len,
									 size_t offs)
{
    // Allocate space for atl_token_t plus the token text
    atl_token_t *t = (atl_token_t *)aml_pool_zalloc(th->pool, sizeof(atl_token_t) + len + 1);
    t->token = (char *)(t + 1);
    memcpy(t->token, p, len);
    t->token[len] = '\0';

    // Set all tokens to ATL_TOKEN_TOKEN
    t->type = ATL_TOKEN_TOKEN;

    // Optionally store offsets, if you need them
    t->pos = offs;
    t->len = len;

    // Link into the tail of the list
    if (!th->head) {
        th->head = th->tail = t;
    } else {
        t->prev = th->tail;
        th->tail->next = t;
        th->tail = t;
    }

    return t;
}

/**
 * Returns a pointer to the beginning of the Nth token in the string, or the
 * end of the string if the number of tokens is fewer than N. If N == 0,
 * simply return the beginning of the string (no skip).
 *
 * Token boundaries are exactly the same as in atl_token_count():
 *   - Single punctuation/spacing characters like : ? & | ' " . , ( ) etc.
 *   - Sequences of whitespace
 *   - A backslash '\' plus its following character (if any)
 */
const char *atl_token_skip(const char *s, size_t n)
{
    if (n == 0) {
        // If n == 0, return the start of the string (no skip).
        return s;
    }

    size_t found = 0;
    const char *token_start = NULL;

    while (*s) {
        int ch = *s;

        switch (ch) {
        // These single characters immediately end any current token
        case ':' :
        case '?' :
        case '&' :
        case '|' :
        case '\'':
        case '\"':
        case '.':
        case ',' :
        case '(':
        case ')':
        case '[':
        case ']':
        case '{':
        case '}':
        case '!':
        case '=':
        case '<':
        case '>':
        case '-':
        case '+':
        case '*':
        case '/':
        case '%':
        case '^':
        case '#':
        case '~':
        case '`':
        case ';':
            if (token_start) {
                // We just finished a token
                found++;
                if (found == n) {
                    // Return the beginning of the Nth token
                    return token_start;
                }
                token_start = NULL;
            }
            s++;
            break;

        // Underscore or any control/whitespace in the range [1..32]
        case '_':
        case 1 ... 32:
            if (token_start) {
                found++;
                if (found == n) {
                    return token_start;
                }
                token_start = NULL;
            }
            s++;
            // Skip consecutive whitespace/control characters
            while (*s > 0 && *s <= 32) {
                s++;
            }
            break;

        // Backslash: if there's a following character, treat it as "consumed"
        case '\\':
            if (token_start) {
                found++;
                if (found == n) {
                    return token_start;
                }
                token_start = NULL;
            }
            if (s[1]) {
                s += 2; // Skip '\' and its next character
            } else {
                s++;
            }
            break;

        default:
            // We are in the middle of a token
            if (!token_start) {
                // This is the start of a new token
                token_start = s;
            }
            s++;
            break;
        }
    }

    // If we ended and there's a token in progress, count it
    if (token_start) {
        found++;
        if (found == n) {
            return token_start;
        }
    }

    // If we didn't reach the Nth token, return end of string
    return s;
}

size_t atl_token_count(const char *s) {
	size_t count = 0;
    char *token_start = NULL;
    while(*s) {
        int ch = *s;
        switch(ch) {
        case ':' :
        case '?' :
        case '&' :
        case '|' :
        case '\'':
        case '\"':
        case '.':
        case ',' :
        case '(':
        case ')':
        case '[':
        case ']':
        case '{':
        case '}':
        case '!':
        case '=':
        case '<':
        case '>':
        case '-':
        case '+':
        case '*':
        case '/':
        case '%':
        case '^':
        case '#':
        case '~':
        case '`':
        case ';':
            if(token_start) {
				count++;
                token_start = NULL;
            }
            s++;
            break;
        case '_':
        case 1 ... 32:
            if(token_start) {
				count++;
                token_start = NULL;
            }
            s++;
            while(*s > 0 && *s <= 32)
                s++;
            break;
        case '\\':
            if(token_start) {
				count++;
                token_start = NULL;
            }
            if(s[1])
                s += 2;
            else
                s++;
            break;
        default:
            if(!token_start)
                token_start = (char *)s;
            s++;
            break;
        };
    }
    if(token_start)
		count++;
    return count;
}

// allintitle:This is a test num_results:10 this-is {1+5}
atl_token_t *atl_token_parse(aml_pool_t *pool, const char *s) {
    token_head_t th;
    memset(&th, 0, sizeof(th));
    th.pool = pool;
    const char *start = s;
    char *token_start = NULL;
    while(*s) {
        int ch = *s;
        switch(ch) {
        case ':' :
        case '?' :
        case '&' :
        case '|' :
        case '\'':
        case '\"':
        case '.':
        case ',' :
        case '(':
        case ')':
        case '[':
        case ']':
        case '{':
        case '}':
        case '!':
        case '=':
        case '<':
        case '>':
        case '-':
        case '+':
        case '*':
        case '/':
        case '%':
        case '^':
        case '#':
        case '~':
        case '`':
        case ';':
            if(token_start) {
                token_parse_init(&th, token_start, s-token_start, token_start-start );
                token_start = NULL;
            }
            s++;
            break;
        case '_':
        case 1 ... 32:
            if(token_start) {
                token_parse_init(&th, token_start, s-token_start, token_start-start );
                token_start = NULL;
            }
            s++;
            while(*s > 0 && *s <= 32)
                s++;
            break;
        case '\\':
            if(token_start) {
                token_parse_init(&th, token_start, s-token_start, token_start-start );
                token_start = NULL;
            }
            if(s[1])
                s += 2;
            else
                s++;
            break;
        default:
            if(!token_start)
                token_start = (char *)s;
            s++;
            break;
        };
    }
    if(token_start)
        token_parse_init(&th, token_start, s-token_start, token_start-start );
    return th.head;
}

static
bool has_parent(token_head_t *th, atl_token_type_t type) {
    atl_token_t *t = th->tail;
    if(t && t->type == type)
        return true;
    while(t && t->parent && t->parent->type != type)
        t = t->parent;
    if(t && t->parent)
        return true;
    return false;
}

static
bool is_quoted(token_head_t *th) {
    atl_token_t *t = th->tail;
    if(t && (t->type == ATL_TOKEN_DQUOTE))
        return true;
    while(t && t->parent && t->parent->type != ATL_TOKEN_DQUOTE)
        t = t->parent;
    if(t && t->parent)
        return true;
    return false; 
}

atl_token_t *fix_ors(aml_pool_t *pool, atl_token_t *t) {
    atl_token_t *st = t;
    atl_token_t *resp = t;
    aml_buffer_t *bh = aml_buffer_pool_init(pool, 32);
    while(t) {
        atl_token_t *next = t->next;
        if(t->child && t->type != ATL_TOKEN_DQUOTE)
           t->child = fix_ors(pool, t->child);
        if(t->type == ATL_TOKEN_OR || (t->type == ATL_TOKEN_TOKEN && !strcasecmp(t->token, "or"))) {
            if(next && st != t)            
                aml_buffer_append(bh, &st, sizeof(st));
            if(t->prev)
                t->prev->next = NULL;
            if(next)
                next->prev = NULL;            
            st = next;
        }
        t = next;
    }
    if(aml_buffer_length(bh)) {
        if(st)
            aml_buffer_append(bh, &st, sizeof(st));
        atl_token_t **starts = (atl_token_t **)aml_buffer_data(bh);
        uint32_t num_starts = aml_buffer_length(bh) / sizeof(atl_token_t *);

        atl_token_t *or_parent = (atl_token_t *)aml_pool_alloc(pool, sizeof(atl_token_t) + 4);
        memset(or_parent, 0, sizeof(*or_parent));
        or_parent->token = (char *)(or_parent+1);
        strcpy(or_parent->token, "or");
        or_parent->type = ATL_TOKEN_OR;
        or_parent->parent = starts[0]->parent;
        if(or_parent->parent && or_parent->parent->child == resp)
            or_parent->parent->child = or_parent;
        for( uint32_t i=0; i<num_starts; i++ ) {
            if(starts[i]->next) {
                // (
                atl_token_t *parent = (atl_token_t *)aml_pool_alloc(pool, sizeof(atl_token_t) + 2);
                memset(parent, 0, sizeof(*parent));
                parent->token = (char *)(parent+1);
                parent->token[0] = '(';
                parent->token[1] = 0;
                parent->type = ATL_TOKEN_OPEN_PAREN;
                parent->parent = or_parent;
                parent->child = starts[i];
                atl_token_t *n = starts[i];
                while(n) {
                    n->parent = parent;
                    n = n->next;
                }
                if(!or_parent->child)
                    or_parent->child = parent;
                else {
                    n = or_parent->child;
                    while(n->next)
                        n = n->next;
                    parent->prev = n;
                    n->next = parent;
                }
            }
            else {
                // add single token
                starts[i]->parent = or_parent;
                if(!or_parent->child)
                    or_parent->child = starts[i];
                else {
                    atl_token_t *n = or_parent->child;
                    while(n->next)
                        n = n->next;
                    starts[i]->prev = n;
                    n->next = starts[i];
                }
            }
        }
        return or_parent;
    }
    return resp;
}

atl_token_t *fix_nots(aml_pool_t *pool, atl_token_t *t) {
    atl_token_t *st = t;
    atl_token_t *resp = t;
    int num_nots = 0, num_others = 0;
    while(t) {
        atl_token_t *next = t->next;
        if(t->child && t->type != ATL_TOKEN_DQUOTE)
           t->child = fix_nots(pool, t->child);
        if(t->type == ATL_TOKEN_NOT || (t->type == ATL_TOKEN_TOKEN && !strcasecmp(t->token, "not"))) {
            if(next) {
                num_nots++;
                next = next->next;
            }
        }
        else
            num_others++;
        t = next;
    }
    if(num_nots > 0 && num_others > 0) {
        atl_token_t *root_parent = resp->parent;
        atl_token_t **nots = (atl_token_t **)aml_pool_zalloc(pool, sizeof(atl_token_t *)*(num_nots+1));
        atl_token_t **others = (atl_token_t **)aml_pool_zalloc(pool, sizeof(atl_token_t *)*(num_others+1));
        num_nots = 0;
        num_others = 0;
        t = resp;
        while(t) {
            atl_token_t *next = t->next;
            if(t->type == ATL_TOKEN_NOT || (t->type == ATL_TOKEN_TOKEN && !strcasecmp(t->token, "not"))) {
                if(t->prev)
                    t->prev->next = NULL;
                if(next) {
                    next->prev = NULL;
                    if(next->next)
                        next->next->prev = NULL;
                }
                if(next) {
                    nots[num_nots] = next;
                    num_nots++;
                    atl_token_t *next2 = next->next;
                    next->next = NULL;                    
                    next = next2;                  
                }
            }
            else {
                others[num_others] = t;
                num_others++;
                if(t->prev)
                    t->prev->next = NULL;
                if(t->next)
                    t->next->prev = NULL;
                t->next = NULL;
                t->prev = NULL;
            }
            t = next;
        }
        atl_token_t *not_root = NULL;
        if(num_nots > 1) {
            atl_token_t *or_parent = (atl_token_t *)aml_pool_alloc(pool, sizeof(atl_token_t) + 4);
            memset(or_parent, 0, sizeof(*or_parent));
            or_parent->token = (char *)(or_parent+1);
            strcpy(or_parent->token, "or");
            or_parent->type = ATL_TOKEN_OR;
            or_parent->child = nots[0];
            nots[0]->parent = or_parent;
            nots[0]->prev = NULL;
            nots[num_nots-1]->next = NULL;
            for( int i=1; i<num_nots; i++ ) {
                nots[i]->parent = or_parent;
                nots[i]->prev = nots[i-1];
                nots[i-1]->next = nots[i];
            }
            not_root = or_parent;
        }
        else
            not_root = nots[0];
        atl_token_t *other_root = NULL;
        if(num_others > 1) {
            atl_token_t *and_parent = (atl_token_t *)aml_pool_alloc(pool, sizeof(atl_token_t) + 2);
            memset(and_parent, 0, sizeof(*and_parent));
            and_parent->token = (char *)(and_parent+1);
            strcpy(and_parent->token, "(");
            and_parent->type = ATL_TOKEN_OPEN_PAREN;
            and_parent->child = others[0];
            others[0]->parent = and_parent;
            others[0]->prev = NULL;
            others[num_others-1]->next = NULL;
            for( int i=1; i<num_others; i++ ) {
                others[i]->parent = and_parent;
                others[i]->prev = others[i-1];
                others[i-1]->next = others[i];
            }
            other_root = and_parent;
        }
        else if(num_others == 1)
            other_root = others[0];

        atl_token_t *not_parent = (atl_token_t *)aml_pool_alloc(pool, sizeof(atl_token_t) + 4);
        memset(not_parent, 0, sizeof(*not_parent));
        not_parent->token = (char *)(not_parent+1);
        strcpy(not_parent->token, "not");
        not_parent->parent = root_parent;
        if(not_parent->parent && not_parent->parent->child == resp)
            not_parent->parent->child = not_parent;
        not_parent->type = ATL_TOKEN_NOT;
        not_parent->child = not_root;
        not_root->parent = not_parent;
        not_root->next = other_root;
        not_root->prev = NULL;
        if(other_root) {
            other_root->prev = not_root;
            other_root->parent = not_parent;
            other_root->next = NULL;
        }
        return not_parent;
    }
    return resp;
}


atl_token_t *group_and(aml_pool_t *pool, atl_token_t *t) {
    if(!t->next)
        return t;
    atl_token_t *and_parent = (atl_token_t *)aml_pool_alloc(pool, sizeof(atl_token_t) + 2);
    memset(and_parent, 0, sizeof(*and_parent));
    and_parent->token = (char *)(and_parent+1);
    strcpy(and_parent->token, "(");
    and_parent->type = ATL_TOKEN_OPEN_PAREN;
    and_parent->child = t;
    t->parent = and_parent;
    return and_parent;
}

atl_token_t *fix_open_parens(aml_pool_t *pool, atl_token_t *t) {
    return t;
}

char *get_next_token(aml_pool_t *pool, char **s, char *eot) {
    char *p = *s;
    char *sp = p;    
    if(*p == '\'' || *p == '\"') {
        char quote = *p++;    
        sp++;
        while(*p && *p != quote) {
            if(*p == '\\' && p[1])
                p += 2;
            else
                p++;
        }
        *s = *p == quote ? p+1 : p;
        return aml_pool_strndup(pool, sp, p-sp);
    }

    while(*p) {
        if(*p <= ' ')
            break;
        if(eot[0] && strchr(eot, *p))
            break;
        p++;
    }
    *s = p;
    return aml_pool_strndup(pool, sp, p-sp);
}

char *token_parse_attr(token_head_t *th, char *p, char *ep) {
    char *s = ep;
    aml_buffer_t *bh = aml_buffer_pool_init(th->pool, 16);
    if(*s == '[') {
        s++;
        while(*s && *s != ']') {
            char *token = get_next_token(th->pool, &s, "],");
            if(token[0])
                aml_buffer_append(bh, &token, sizeof(token));
            if(*s && *s != ']')
                s++;
        }
        if(*s == ']')
            s++;
    }
    char *token = get_next_token(th->pool, &s, "");
    if(token[0])
        aml_buffer_append(bh, &token, sizeof(token));

    atl_token_cb_data_t data;
    memset(&data, 0, sizeof(data));
    data.pool = th->pool;
    data.param = aml_pool_strndup(th->pool, p, ep-p);
    data.attrs = (char **)aml_buffer_data(bh);
    data.num_attrs = aml_buffer_length(bh) / sizeof(char *);
    atl_token_cb_t v=NORMAL;
    if(th->cb)
        v=th->cb(th->arg, &data);
    if(data.no_params) {
        data.num_attrs = 0;
        s = ep;
    }

    if(v == SKIP)
        return s;

    atl_token_t *tok = __token_init(th, data.param, strlen(data.param), 0, ATL_TOKEN_TOKEN, v, data.alt_token);
    if(data.num_attrs) {
        tok->attrs = data.attrs;
        tok->num_attrs = data.num_attrs;
    }
    return s;
}

atl_token_t *atl_token_parse_expression(aml_pool_t *pool, const char *s,
                                      atl_token_set_var_cb cb, void *arg) {
    token_head_t th;
    memset(&th, 0, sizeof(th));
    th.pool = pool;
    th.cb = cb;
    th.arg = arg;
    char *token_start = NULL;
    while(*s) {
        int ch = *s;
        switch(ch) {
        /*
        case '0' ... '9':
        {
            if(token_start) {
                s++;
                continue;
            }
            else {
                const char *sp = s;
                s++;
                while(*s >= '0' && *s <= '9')
                    s++;
                if(*s == '.' && s[1] >= '0' && s[1] <= '9') {
                    s++;
                    while(*s >= '0' && *s <= '9')
                        s++;
                }
                _token_init(&th, sp, s-sp, ATL_TOKEN_NUMBER, NUMBER );
            }
            break;
        }
        */
        case '*':
            if(token_start) {
                if(has_parent(&th, ATL_TOKEN_OPEN_BRACE)) {
                    if(token_start) {
                        token_init(&th, token_start, s-token_start, 0, ATL_TOKEN_MODIFIER );
                        token_start = NULL;
                    }                
                    token_init(&th, s, 1, 0, ATL_TOKEN_OPERATOR);
                    s++;
                }
                else {
                    s++;
                    token_init(&th, token_start, s-token_start, 0, ATL_TOKEN_MODIFIER );
                    token_start = NULL;
                }
            }
            else {
                token_init(&th, s, 1, 0, ATL_TOKEN_OPERATOR);
                s++;
            }
            break;
        case ':' :
            if(token_start) {                
                s++;
                s = token_parse_attr( &th, token_start, (char *)s);
                token_start = NULL;
            }
            else {
                token_init(&th, s, 1, 0, ATL_TOKEN_COLON);
                s++;
            }
            break;
        case '?' :
            if(token_start) {
                token_init(&th, token_start, s-token_start, 0, ATL_TOKEN_MODIFIER );
                token_start = NULL;
            }
            token_init(&th, s, 1, 0, ATL_TOKEN_QUESTION);
            s++;
            break;
        case '&' :
            if(token_start) {
                token_init(&th, token_start, s-token_start, 0, ATL_TOKEN_TOKEN );
                token_start = NULL;
            }
            if(s[1] == '&')
                s++;
            token_init(&th, s, 1, 0, ATL_TOKEN_AND );
            s++;
            break;
        case '|' :
            if(token_start) {
                token_init(&th, token_start, s-token_start, 0, ATL_TOKEN_TOKEN );
                token_start = NULL;
            }
            if(s[1] == '|')
                s++;
            token_init(&th, s, 1, 0, ATL_TOKEN_OR );
            s++;
            break;
        case '\"':
        {
            if(token_start) {
                token_init(&th, token_start, s-token_start, 0, ATL_TOKEN_TOKEN );
                token_start = NULL;
            }
            token_init(&th, s, 1, 0, ATL_TOKEN_DQUOTE );
            s++;
            break;
        }
        case '.':
            if(token_start) {
                token_init(&th, token_start, s-token_start, 0, ATL_TOKEN_TOKEN );
                token_start = NULL;
            }
            if(s[1] == '.' && s[2] != 0) {
                token_init(&th, s, 2, 0, ATL_TOKEN_DASH);
                s += 2;
            }
            else {
                token_init(&th, s, 1, 0, ATL_TOKEN_OTHER);
                s++;
            }
            break;     
        case ',' :
            if(!is_quoted(&th)) {
                if(token_start) {
                    token_init(&th, token_start, s-token_start, 0, ATL_TOKEN_TOKEN );
                    token_start = NULL;
                }
                token_init(&th, s, 1, 0, ATL_TOKEN_COMMA);
                s++;
                break;
            }
        case '(':
            if(!is_quoted(&th)) {
                if(token_start) {
                    token_init(&th, token_start, s-token_start, 0, ATL_TOKEN_TOKEN );
                    token_start = NULL;
                }
                token_init(&th, s, 1, 0, ATL_TOKEN_OPEN_PAREN);
                s++;
                break;
            }
        case ')':
            if(!is_quoted(&th)) {
                if(token_start) {
                    token_init(&th, token_start, s-token_start, 0, ATL_TOKEN_TOKEN );
                    token_start = NULL;
                }
                token_init(&th, s, 1, 0, ATL_TOKEN_CLOSE_PAREN);
                s++;
                break;
            }
        case '{':
            if(!is_quoted(&th)) {
                if(token_start) {
                    token_init(&th, token_start, s-token_start, 0, ATL_TOKEN_TOKEN );
                    token_start = NULL;
                }
                token_init(&th, s, 1, 0, ATL_TOKEN_OPEN_BRACE);
                s++;
                break;
            }
        case '}':
            if(!is_quoted(&th)) {
                if(token_start) {
                    token_init(&th, token_start, s-token_start, 0, ATL_TOKEN_TOKEN );
                    token_start = NULL;
                }
                token_init(&th, s, 1, 0, ATL_TOKEN_CLOSE_BRACE);
                s++;
                break;
            }
        case '[':
            if(!is_quoted(&th)) {
                if(token_start) {
                    token_init(&th, token_start, s-token_start, 0, ATL_TOKEN_TOKEN );
                    token_start = NULL;
                }
                token_init(&th, s, 1, 0, ATL_TOKEN_OPEN_BRACKET);
                s++;
                break;
            }
        case ']':
            if(!is_quoted(&th)) {
                if(token_start) {
                    token_init(&th, token_start, s-token_start, 0, ATL_TOKEN_TOKEN );
                    token_start = NULL;
                }
                token_init(&th, s, 1, 0, ATL_TOKEN_CLOSE_BRACKET);
                s++;
                break;
            }
        case '!':
        case '=':
        case '<':
        case '>':
            if(!is_quoted(&th)) {
                if(token_start) {
                    token_init(&th, token_start, s-token_start, 0, ATL_TOKEN_TOKEN );
                    token_start = NULL;
                }
                token_init(&th, s, 1, 0, ATL_TOKEN_COMPARISON);
                s++;
                break;
            }
        case '-':
        case '+':
        case '/':
        case '%':
        case '^':
            if(!is_quoted(&th)) {
                if(token_start) {
                    token_init(&th, token_start, s-token_start, 0, ATL_TOKEN_TOKEN );
                    token_start = NULL;
                }
                token_init(&th, s, 1, 0, ATL_TOKEN_OPERATOR);
                s++;
                break;
            }
        case '#':
        case '~':
        case '`':
        case ';':
            if(token_start) {
                token_init(&th, token_start, s-token_start, 0, ATL_TOKEN_TOKEN );
                token_start = NULL;
            }
            token_init(&th, s, 1, 0, ATL_TOKEN_OTHER);
            s++;
            break;
        case '\\':
            if(token_start) {
                token_init(&th, token_start, s-token_start, 0, ATL_TOKEN_TOKEN );
                token_start = NULL;
            }
            if(s[1])
                s += 2;
            else
                s++;
            break;
        case '\'':
        case 1 ... 32:
            if(token_start) {
                token_init(&th, token_start, s-token_start, 0, ATL_TOKEN_TOKEN );
                token_start = NULL;
            }
            s++;
            while(*s > 0 && *s <= 32)
                s++;
            token_init(&th, " ", 1, 0, ATL_TOKEN_SPACE);
            break;
        default:
            if(!token_start)
                token_start = (char *)s;
            s++;
            break;
        };
    }
    if(token_start)
        token_init(&th, token_start, s-token_start, 0, ATL_TOKEN_TOKEN );

    /*
        reorganize OR and NOT
        1. if an OR token is found then lift it up
        2. not only applies to next thing (so a bunch of nots could be or'ed together) 
           AND everything that isn't a NOT and then OR everything that is NOT
   
        NOT should be applied after the initial OR, then all NOT should be OR'ed if more than one

        If a ( only has one child, it should be swapped for it's child
    */
    if(!th.head)
        return NULL;
    th.head = fix_ors(pool, th.head);
    th.head = fix_nots(pool, th.head);
    th.head = group_and(pool, th.head);
    th.head = fix_open_parens(pool, th.head);
    return th.head;
}


struct atl_token_dict_s {
    aml_pool_t *pool;
    macro_map_t *root;
};

struct atl_token_node_s {
    macro_map_t node;
    atl_token_t *token;
};

typedef struct atl_token_node_s atl_token_node_t;

static inline
int compare_token_node_for_find(const char *key, const atl_token_node_t *node) {
    return strcmp(key, (const char *)(node+1));
}

static inline
int compare_token_node_for_insert(const atl_token_node_t *a, const atl_token_node_t *b) {
    return strcmp((const char *)(a+1), (const char *)(b+1));
}

static macro_map_find_kv(_token_node_find, char, atl_token_node_t, compare_token_node_for_find);
static macro_map_insert(_token_node_insert, atl_token_node_t, compare_token_node_for_insert);


atl_token_cb_t atl_token_dict_cb(void *arg, atl_token_cb_data_t *d) {
    atl_token_dict_t *h = (atl_token_dict_t *)arg;
    atl_token_node_t *n = _token_node_find(h->root, d->param);
    if(!n) {
        if(d->param[0] == '@')
            return NUMBER;
        d->no_params = true;
        if(d->param[strlen(d->param)-1] == ':')
            d->param[strlen(d->param)-1] = 0;
        return NORMAL;
    }
    if(n->token->no_params)
        d->no_params = true;
    d->alt_token = atl_token_clone(d->pool, n->token);
    /*    if(d->alt_token) {
        atl_token_dump(d->alt_token);
    }*/
    return n->token->attr_type;
}

/*
atl_token_cb_t _atl_token_dict_cb(void *arg, atl_token_cb_data_t *d) {
    atl_token_dict_t *h = (atl_token_dict_t *)arg;
    atl_token_node_t *n = _token_node_find(d->param, h->root);
    if(!n) {
        return d->param[0] == '@' ? NUMBER : NORMAL;
    }
    if(n->token->no_params)
        d->no_params = true;
    d->alt_token = atl_token_clone(d->pool, n->token);
    return NORMAL;
}
*/

atl_token_dict_t *atl_token_dict_init() {
    aml_pool_t *pool = aml_pool_init(16384);
    atl_token_dict_t *h = (atl_token_dict_t *)aml_pool_zalloc(pool, sizeof(*h));
    h->pool = pool;
    return h;
}

atl_token_dict_t *atl_token_dict_load(const char *filename) {
    io_in_options_t opts;
    io_in_options_init(&opts);
    io_in_options_format(&opts, io_delimiter('\n'));
    io_in_t *in = io_in_init(filename, &opts);
    io_record_t *r;
    atl_token_dict_t *dict = atl_token_dict_init();    
    while((r=io_in_advance(in)) != NULL)
        atl_token_dict_add(dict, r->record);
    io_in_destroy(in);
    return dict;
}

void atl_token_dict_destroy(atl_token_dict_t *h) {
    aml_pool_t *pool = h->pool;
    aml_pool_destroy(pool);
}

/* name: global|next|skip|normal|number[,no_params] string */
bool atl_token_dict_add(atl_token_dict_t *h, const char *config_line) {
    char *line = aml_pool_strdup(h->pool, config_line);
    char *p = line;
    while(*p && *p <= ' ')
        p++;
    char *name = p;
    if(!(*name))
        return false;

    while(*p < 0 || *p > ' ')
        p++;
    if(!(*p))
        return false;
    *p++ = 0;

    while(*p && *p <= ' ')
        p++;

    char *type = p;
    if(!(*type))
        return false;
    
    while(*p < 0 || *p > ' ')
        p++;
    if(!(*p))
        return false;
    *p++ = 0;

    bool no_params = false;
    char *param = strchr(type, ',');
    if(param) {
        *param++ = 0;
        if(!strcmp(param, "no_params"))
            no_params = true;
    }

    atl_token_cb_t cb_type = NORMAL;
    if(!strcmp(type, "skip"))
        cb_type = SKIP;
    else if(!strcmp(type, "normal"))
        cb_type = NORMAL;
    else if(!strcmp(type, "next"))
        cb_type = NEXT;
    else if(!strcmp(type, "global"))
        cb_type = GLOBAL;
    else if(!strcmp(type, "number"))
        cb_type = NUMBER;
    else
        return false;
    
    atl_token_t *t = atl_token_parse_expression(h->pool, p, NULL, NULL); //_atl_token_dict_cb, h);
    if(!t)
        return false;
    
    t->attr_type = cb_type;
    t->no_params = no_params;

    atl_token_node_t *n = _token_node_find(h->root, name);
    if(!n) {
        n = (atl_token_node_t *)aml_pool_zalloc(h->pool, sizeof(*n) + strlen(name) + 1);
        strcpy((char *)(n+1), name);
        _token_node_insert(&h->root, n);
        // printf( "inserting %s (%s) %s\n", name, type, p );
    }
    n->token = t;
    // atl_token_dump(t);
    return true;
}

char **atl_token_dict_values(atl_token_dict_t *h, uint32_t *num_values, const char *param) {
    *num_values = 0;
    atl_token_node_t *n = _token_node_find(h->root, param);
    if(!n) return NULL;

    if(n->token) {
        *num_values = n->token->num_attrs;
        return n->token->attrs;
    }
    return NULL;
}

const char *atl_token_dict_value(atl_token_dict_t *h, const char *param) {
    uint32_t num_attrs = 0;
    char **attrs = atl_token_dict_values(h, &num_attrs, param);
    if(num_attrs >= 1)
        return attrs[0];
    return NULL;
}

atl_token_t *_atl_token_clone(aml_pool_t *pool, atl_token_t *t, atl_token_t *parent) {
    atl_token_t *clone = (atl_token_t *)aml_pool_dup(pool, t, sizeof(*t) + strlen(t->token)+1);
    clone->token = (char *)(clone+1);
    strcpy(clone->token, t->token);
    clone->parent = parent;
    clone->child = NULL;
    clone->next = NULL;
    clone->prev = NULL;
    if(parent && t->parent && t->parent->child && t->parent->child == t)
        parent->child = clone;

    strcpy(clone->token, t->token);
    if(t->num_attrs) {
        clone->attrs = (char **)aml_pool_zalloc(pool, sizeof(char *) * (t->num_attrs+1));
        for( uint32_t i=0; i<t->num_attrs; i++ )
            clone->attrs[i] = aml_pool_strdup(pool, t->attrs[i]);
        clone->num_attrs = t->num_attrs;
    }
    if(t->attr)
        clone->attr = atl_token_clone(pool, t->attr);

    if(t->next) {
        clone->next = _atl_token_clone(pool, t->next, parent);
        clone->next->prev = clone;        
    }    
    return clone;    
}

atl_token_t *atl_token_clone(aml_pool_t *pool, atl_token_t *t) {
    return _atl_token_clone(pool, t, NULL);
}