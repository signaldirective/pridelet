/*
 *  TOIlet        The Other Implementation’s letters
 *  Copyright (c) 2006 Sam Hocevar <sam@hocevar.net>
 *                All Rights Reserved
 *
 *  This program is free software. It comes without any warranty, to
 *  the extent permitted by applicable law. You can redistribute it
 *  and/or modify it under the terms of the Do What The Fuck You Want
 *  To Public License, Version 2, as published by Sam Hocevar. See
 *  http://sam.zoy.org/wtfpl/COPYING for more details.
 */

/*
 * This file contains functions for handling FIGlet fonts.
 */

#include "config.h"

#if defined(HAVE_INTTYPES_H)
#   include <inttypes.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <caca.h>

#include "toilet.h"
#include "render.h"

#define STD_GLYPHS (127 - 32)
#define EXT_GLYPHS (STD_GLYPHS + 7)

#define CHAR_WIDTH 6

static int feed_figlet(context_t *, uint32_t, uint32_t);
static int flush_figlet(context_t *);
static int end_figlet(context_t *);

/* Word-wrap buffer */
static char *ww_buf = NULL;
static int ww_len = 0;
static int ww_cap = 0;

static void ww_append(context_t *cx, char ch)
{
    if(ww_len + 2 > ww_cap)
    {
        ww_cap = ww_cap ? ww_cap * 2 : 512;
        ww_buf = realloc(ww_buf, ww_cap);
    }
    ww_buf[ww_len++] = ch;
    ww_buf[ww_len] = '\0';
}

static void ww_reset(void)
{
    ww_len = 0;
    if(ww_buf) ww_buf[0] = '\0';
}

/* Feed processed text (with word-wrap newlines) to the FIGlet engine */
static void feed_wrapped(context_t *cx, char *text)
{
    char *p = text;
    while(*p)
    {
        uint32_t ch = (unsigned char)*p;
        caca_put_figchar(cx->cv, ch);
        p++;
    }
}

static void apply_justify(context_t *cx)
{
    int cw = caca_get_canvas_width(cx->torender);
    int ch = caca_get_canvas_height(cx->torender);
    int target = cx->term_width;

    if(!cx->justify || !strcmp(cx->justify, "left"))
        return;

    if(cw >= target)
        return;

    if(!strcmp(cx->justify, "center"))
    {
        int pad = (target - cw) / 2;
        caca_set_canvas_boundaries(cx->torender, -pad, 0,
                                   cw + pad, ch);
    }
    else if(!strcmp(cx->justify, "right"))
    {
        int pad = target - cw;
        caca_set_canvas_boundaries(cx->torender, -pad, 0,
                                   cw + pad, ch);
    }
}

int init_figlet(context_t *cx)
{
    char path[2048];

    /* If the font name is an absolute path, try it directly first */
    if(cx->font[0] == '/')
    {
        snprintf(path, 2047, "%s", cx->font);
        if(!caca_canvas_set_figfont(cx->cv, path))
            goto done;
    }

    /* Try font directory */
    snprintf(path, 2047, "%s/%s", cx->dir, cx->font);
    if(!caca_canvas_set_figfont(cx->cv, path))
        goto done;

    /* Try current directory */
    snprintf(path, 2047, "./%s", cx->font);
    if(!caca_canvas_set_figfont(cx->cv, path))
        goto done;

    /* Try system font directory */
    snprintf(path, 2047, SYSTEMFONTDIR "/%s", cx->font);
    if(!caca_canvas_set_figfont(cx->cv, path))
        goto done;

    fprintf(stderr, "error: could not load font %s\n", cx->font);
    return -1;

done:

    caca_set_figfont_smush(cx->cv, cx->hmode);
    caca_set_figfont_width(cx->cv, cx->term_width);

    cx->feed = feed_figlet;
    cx->flush = flush_figlet;
    cx->end = end_figlet;

    return 0;
}

static int feed_figlet(context_t *cx, uint32_t ch, uint32_t attr)
{
    if(cx->wordwrap)
    {
        ww_append(cx, (char)ch);
        return 0;
    }
    return caca_put_figchar(cx->cv, ch);
}

static int flush_figlet(context_t *cx)
{
    int ret;

    if(cx->wordwrap && ww_len > 0)
    {
        /* Remove trailing newline if present */
        int len = ww_len;
        if(len > 0 && ww_buf[len - 1] == '\n')
            len--;

        /* Word-wrap: split into words, rebuild with newlines at width limit */
        if(len > 0)
        {
            char *src = ww_buf;
            char *out = malloc(len * 2 + 1);
            int outpos = 0;
            int col = 0;
            int max_cols = cx->term_width / CHAR_WIDTH;
            if(max_cols < 1) max_cols = 1;
            int i = 0;

            while(i < len)
            {
                /* Skip leading spaces */
                while(i < len && src[i] == ' ')
                    i++;
                if(i >= len) break;

                /* Find end of word */
                int start = i;
                while(i < len && src[i] != ' ' && src[i] != '\n')
                    i++;
                int word_len = i - start;

                /* Check if word fits (including inter-word space); if not, wrap */
                if(col > 0 && col + 1 + word_len > max_cols)
                {
                    out[outpos++] = '\n';
                    col = 0;
                }

                /* Add space between words on same line */
                if(col > 0)
                {
                    out[outpos++] = ' ';
                    col++;
                }

                /* Copy word */
                int j;
                for(j = 0; j < word_len; j++)
                    out[outpos++] = src[start + j];
                col += word_len;
            }
            out[outpos] = '\0';

            feed_wrapped(cx, out);
            free(out);
        }
        ww_reset();
        ret = caca_flush_figlet(cx->cv);
    }
    else
    {
        ret = caca_flush_figlet(cx->cv);
    }

    cx->torender = caca_create_canvas(caca_get_canvas_width(cx->cv),
                                      caca_get_canvas_height(cx->cv));
    caca_blit(cx->torender, 0, 0, cx->cv, NULL);
    caca_set_canvas_size(cx->cv, 0, 0);

    if(cx->justify)
        apply_justify(cx);

    return ret;
}

static int end_figlet(context_t *cx)
{
    free(ww_buf);
    ww_buf = NULL;
    ww_len = 0;
    ww_cap = 0;
    return caca_canvas_set_figfont(cx->cv, NULL);
}

