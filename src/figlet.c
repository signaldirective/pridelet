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
#include "control.h"

#define STD_GLYPHS (127 - 32)
#define EXT_GLYPHS (STD_GLYPHS + 7)

#define CHAR_WIDTH 6

static int feed_figlet(context_t *, uint32_t, uint32_t);
static int flush_figlet(context_t *);
static int end_figlet(context_t *);
static int read_font_header(context_t *, int *, int *);
static char *ww_wrap(context_t *, char *, int);
static int render_vsmushed(context_t *, char *);

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

/* Find and open the current font file, trying the same locations as
 * libcaca does, including the ".tlf" and ".flf" suffixes. */
static FILE *open_font(context_t *cx)
{
    char base[2048];
    char path[2048];
    char const *candidates[3];
    int n, i;

    if(cx->font[0] == '/')
    {
        candidates[0] = "";
        n = 1;
    }
    else
    {
        candidates[0] = cx->dir;
        candidates[1] = ".";
        candidates[2] = SYSTEMFONTDIR;
        n = 3;
    }

    for(i = 0; i < n; i++)
    {
        FILE *f;

        if(candidates[i][0])
            snprintf(base, 2047, "%s/%s", candidates[i], cx->font);
        else
            snprintf(base, 2047, "%s", cx->font);

        f = fopen(base, "r");
        if(f)
            return f;

        snprintf(path, 2047, "%s.tlf", base);
        f = fopen(path, "r");
        if(f)
            return f;

        snprintf(path, 2047, "%s.flf", base);
        f = fopen(path, "r");
        if(f)
            return f;
    }

    return NULL;
}

/* Parse the font file header to extract the print direction and the
 * full layout parameter. Returns 0 on success. */
static int read_font_header(context_t *cx, int *print_direction,
                            int *full_layout)
{
    FILE *f = open_font(cx);
    char buf[2048];
    char hardblank[10];
    unsigned int height, baseline, max_length;
    int old_layout, comment_lines;
    int pd = 0, fl = 0, ct = 0;
    int n;

    if(!f)
        return -1;

    if(!fgets(buf, sizeof(buf), f))
    {
        fclose(f);
        return -1;
    }
    fclose(f);

    n = sscanf(buf, "%*[ft]lf2a%6s %u %u %u %i %u %i %i %i",
               hardblank, &height, &baseline, &max_length,
               &old_layout, &comment_lines, &pd, &fl, &ct);
    if(n < 6)
        return -1;

    if(print_direction)
        *print_direction = pd;
    if(full_layout)
        *full_layout = fl;

    return 0;
}

/* Wrap the buffered text at word boundaries, inserting newlines so
 * that no line is wider than the output width. */
static char *ww_wrap(context_t *cx, char *src, int len)
{
    char *out = malloc(len * 2 + 1);
    int outpos = 0;
    int col = 0;
    int max_cols = cx->term_width / CHAR_WIDTH;
    int i = 0;

    if(max_cols < 1) max_cols = 1;

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

    return out;
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

    /* Use the font's print direction and vertical layout if we can
     * read its header, and if the user did not override them. */
    {
        int pd = 0, fl = 0;

        if(read_font_header(cx, &pd, &fl) == 0)
        {
            if(cx->rtl == -1)
                cx->rtl = pd ? 1 : 0;
            if(!cx->vsmush && (fl & 16384))
                cx->vsmush = 1;
            cx->vsmush_rules = fl & 0x1f00;
        }
        else
        {
            if(cx->rtl == -1)
                cx->rtl = 0;
            cx->vsmush_rules = 0;
        }
    }

    cx->feed = feed_figlet;
    cx->flush = flush_figlet;
    cx->end = end_figlet;

    return 0;
}

static int feed_figlet(context_t *cx, uint32_t ch, uint32_t attr)
{
    ch = control_map(cx, ch);

    if(cx->wordwrap || cx->rtl || cx->vsmush)
    {
        ww_append(cx, (char)ch);
        return 0;
    }
    return caca_put_figchar(cx->cv, ch);
}

/* Reverse a range of characters [start, end) in place. */
static void reverse_range(char *start, char *end)
{
    char *p = start;
    char *q = end - 1;

    while(p < q)
    {
        char t = *p;
        *p = *q;
        *q = t;
        p++;
        q--;
    }
}

/* Reverse the characters of each line in a multi-line string, so that
 * text is rendered right-to-left. */
static void reverse_lines(char *text)
{
    char *line_start = text;
    char *p = text;

    while(*p)
    {
        if(*p == '\n')
        {
            reverse_range(line_start, p);
            line_start = p + 1;
        }
        p++;
    }
    reverse_range(line_start, p);
}

/* Vertical smushing: compute the sub-character to display when two
 * sub-characters overlap vertically, or 0 if they cannot be smushed.
 * The rule is a set of vertical smushing rule bits. */
static uint32_t vsmush(uint32_t ch1, uint32_t ch2, int rule)
{
    /* Rule 1 (256): equal character smushing */
    if(rule & 256)
    {
        if(ch1 == ch2 && ch1 != ' ')
            return ch1;
    }

    if(ch1 < 0x80 && ch2 < 0x80)
    {
        char const charlist[] = "|/\\[]{}()<>";

        /* Rule 2 (512): underscore smushing */
        if(rule & 512)
        {
            if(ch1 == '_' && strchr(charlist, ch2))
                return ch2;
            if(ch2 == '_' && strchr(charlist, ch1))
                return ch1;
        }

        /* Rule 3 (1024): hierarchy smushing */
        if(rule & 1024)
        {
            char const *p1 = strchr(charlist, ch1);
            char const *p2 = strchr(charlist, ch2);
            if(p1 && p2)
            {
                int c1 = (p1 - charlist) / 2;
                int c2 = (p2 - charlist) / 2;
                if(c1 < c2)
                    return ch2;
                if(c1 > c2)
                    return ch1;
            }
        }

        /* Rule 4 (2048): horizontal line smushing */
        if(rule & 2048)
        {
            if((ch1 == '-' && ch2 == '_') || (ch1 == '_' && ch2 == '-'))
                return '=';
        }
    }

    /* Rule 5 (4096): vertical line supersmushing */
    if(rule & 4096)
    {
        if(ch1 == '|' && ch2 == '|')
            return '|';
    }

    return 0;
}

/* Check whether two line canvases can overlap vertically by the given
 * number of rows, i.e. whether every conflicting sub-character pair in
 * the overlap region can be smushed. */
static int can_vsmush(context_t *cx, caca_canvas_t *top,
                      caca_canvas_t *bottom, int overlap)
{
    int h = caca_get_canvas_height(top);
    int wt = caca_get_canvas_width(top);
    int wb = caca_get_canvas_width(bottom);
    int w = wt > wb ? wt : wb;
    int x, y;

    for(y = 0; y < overlap; y++)
        for(x = 0; x < w; x++)
    {
        uint32_t ch1 = caca_get_char(top, x, h - overlap + y);
        uint32_t ch2 = caca_get_char(bottom, x, y);
        if(ch1 == ' ' || ch2 == ' ')
            continue;
        if(!vsmush(ch1, ch2, cx->vsmush_rules))
            return 0;
    }

    return 1;
}

/* Vertical line supersmushing: check whether the bottom line can slide
 * up one more row, i.e. whether the newly overlapping row only has
 * vertical bars to smush. */
static int can_supersmush(caca_canvas_t *top, caca_canvas_t *bottom,
                          int overlap)
{
    int h = caca_get_canvas_height(top);
    int wt = caca_get_canvas_width(top);
    int wb = caca_get_canvas_width(bottom);
    int w = wt > wb ? wt : wb;
    int yt = h - overlap;
    int yb = overlap - 1;
    int x;

    for(x = 0; x < w; x++)
    {
        uint32_t ch1 = caca_get_char(top, x, yt);
        uint32_t ch2 = caca_get_char(bottom, x, yb);
        if(ch1 == ' ' || ch2 == ' ')
            continue;
        if(ch1 != '|' || ch2 != '|')
            return 0;
    }

    return 1;
}

/* Compute how many rows two rendered lines should overlap, using the
 * vertical fitting and smushing rules. */
static int compute_voverlap(context_t *cx, caca_canvas_t *top,
                            caca_canvas_t *bottom)
{
    int h = caca_get_canvas_height(top);
    int wt = caca_get_canvas_width(top);
    int wb = caca_get_canvas_width(bottom);
    int w = wt > wb ? wt : wb;
    int rowA_bottom = -1, rowB_top = h;
    int overlap, x, y;

    for(y = h - 1; y >= 0 && rowA_bottom < 0; y--)
        for(x = 0; x < w; x++)
            if(caca_get_char(top, x, y) != ' ')
            {
                rowA_bottom = y;
                break;
            }

    for(y = 0; y < h && rowB_top >= h; y++)
        for(x = 0; x < w; x++)
            if(caca_get_char(bottom, x, y) != ' ')
            {
                rowB_top = y;
                break;
            }

    if(rowA_bottom < 0 || rowB_top >= h)
        return 0;

    overlap = h + rowB_top - rowA_bottom - 1;
    if(overlap < 0)
        overlap = 0;
    if(overlap > h - 1)
        overlap = h - 1;

    /* Smushing: overlap one more row when the sub-characters allow it */
    if(cx->vsmush && overlap < h - 1)
    {
        if(cx->vsmush_rules == 0)
            overlap++; /* universal smushing */
        else if(can_vsmush(cx, top, bottom, overlap + 1))
            overlap++;
    }

    /* Vertical line supersmushing */
    if(cx->vsmush && (cx->vsmush_rules & 4096))
    {
        while(overlap < h - 1 && can_supersmush(top, bottom, overlap + 1))
            overlap++;
    }

    return overlap;
}

/* Merge two rendered lines vertically, applying the vertical smushing
 * rules. The caller must no longer use the two input canvases. */
static caca_canvas_t *merge_vsmush(context_t *cx, caca_canvas_t *top,
                                   caca_canvas_t *bottom)
{
    int h = caca_get_canvas_height(top);
    int bh = caca_get_canvas_height(bottom);
    int wt = caca_get_canvas_width(top);
    int wb = caca_get_canvas_width(bottom);
    int w = wt > wb ? wt : wb;
    int overlap = compute_voverlap(cx, top, bottom);
    caca_canvas_t *result = caca_create_canvas(w, h + bh - overlap);
    int x, y;

    caca_blit(result, 0, 0, top, NULL);

    for(y = 0; y < bh; y++)
        for(x = 0; x < w; x++)
    {
        uint32_t chb = caca_get_char(bottom, x, y);
        uint32_t cha;
        int yr = h - overlap + y;

        if(chb == ' ')
            continue;

        cha = caca_get_char(result, x, yr);
        if(cha == ' ')
            caca_put_char(result, x, yr, chb);
        else if(cx->vsmush_rules == 0)
            caca_put_char(result, x, yr, chb); /* universal smushing */
        else
            caca_put_char(result, x, yr, vsmush(cha, chb, cx->vsmush_rules));
    }

    caca_free_canvas(top);
    caca_free_canvas(bottom);

    return result;
}

/* Render a multi-line text, merging consecutive lines with vertical
 * smushing. The merged result is left in cx->cv. */
static int render_vsmushed(context_t *cx, char *text)
{
    int nlines = 1;
    char *p;
    caca_canvas_t **line;
    caca_canvas_t *result;
    char *line_start;
    char *end;
    int n = 0;
    int i;

    for(p = text; *p; p++)
        if(*p == '\n')
            nlines++;

    line = malloc(nlines * sizeof(*line));

    line_start = text;
    for(;;)
    {
        end = strchr(line_start, '\n');
        if(end)
            *end = '\0';

        feed_wrapped(cx, line_start);
        caca_flush_figlet(cx->cv);

        if(caca_get_canvas_width(cx->cv) > 0)
        {
            line[n] = caca_create_canvas(caca_get_canvas_width(cx->cv),
                                         caca_get_canvas_height(cx->cv));
            caca_blit(line[n], 0, 0, cx->cv, NULL);
            n++;
        }
        caca_set_canvas_size(cx->cv, 0, 0);

        if(!end)
            break;
        *end = '\n';
        line_start = end + 1;
    }

    result = line[0];
    for(i = 1; i < n; i++)
        result = merge_vsmush(cx, result, line[i]);

    caca_set_canvas_size(cx->cv, caca_get_canvas_width(result),
                         caca_get_canvas_height(result));
    caca_blit(cx->cv, 0, 0, result, NULL);
    caca_free_canvas(result);

    free(line);

    return 0;
}

static int flush_figlet(context_t *cx)
{
    int ret;

    if(cx->wordwrap || cx->rtl || cx->vsmush)
    {
        if(ww_len > 0)
        {
            char *text;
            int len = ww_len;

            /* Remove trailing newline if present */
            if(ww_buf[len - 1] == '\n')
                len--;

            if(len > 0)
            {
                if(cx->wordwrap)
                    text = ww_wrap(cx, ww_buf, len);
                else
                {
                    text = malloc(len + 1);
                    memcpy(text, ww_buf, len);
                    text[len] = '\0';
                }

                if(cx->rtl)
                    reverse_lines(text);

                if(cx->vsmush)
                    ret = render_vsmushed(cx, text);
                else
                {
                    feed_wrapped(cx, text);
                    ret = caca_flush_figlet(cx->cv);
                }

                free(text);
            }
            else
            {
                ret = caca_flush_figlet(cx->cv);
            }
        }
        else
        {
            ret = caca_flush_figlet(cx->cv);
        }
        ww_reset();
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

