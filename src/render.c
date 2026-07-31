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
 * This file contains text to canvas rendering functions.
 */

#include "config.h"

#if defined(HAVE_INTTYPES_H)
#   include <inttypes.h>
#endif
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <caca.h>

#include "toilet.h"
#include "render.h"
#include "filter.h"

static int render_flush(context_t *);

int render_init(context_t *cx)
{
    cx->x = cx->y = 0;
    cx->w = cx->h = 0;
    cx->lines = 0;
    cx->cv = caca_create_canvas(0, 0);

    if(!strcasecmp(cx->font, "term"))
        return init_tiny(cx);

    return init_figlet(cx);
}

int render_stdin(context_t *cx)
{
    caca_canvas_t *cv;
    char *line;
    int i, len;
    int paragraph_has_text = 0;
    int is_blank_line;

    /* FIXME: we can't read longer lines */
    len = 1024;
    line = malloc(len);
    cv = caca_create_canvas(0, 0);

    /* Read from stdin */
    while(!feof(stdin))
    {
        if(!fgets(line, len, stdin))
            break;

        /* Strip trailing newline */
        size_t l = strlen(line);
        if(l > 0 && line[l - 1] == '\n')
            line[l - 1] = '\0';

        /* Check if the line is blank (empty after stripping newline) */
        is_blank_line = (line[0] == '\0');

        if(cx->paragraph)
        {
            if(is_blank_line)
            {
                /* Blank line = paragraph break */
                if(paragraph_has_text)
                {
                    render_flush(cx);
                    paragraph_has_text = 0;
                }
            }
            else
            {
                /* Separate words with a space */
                if(paragraph_has_text)
                    cx->feed(cx, ' ', 0);

                caca_set_canvas_size(cv, 0, 0);
                caca_import_canvas_from_memory(cv, line, strlen(line), "utf8");
                for(i = 0; i < caca_get_canvas_width(cv); i++)
                {
                    uint32_t ch = caca_get_char(cv, i, 0);
                    uint32_t at = caca_get_attr(cv, i, 0);
                    cx->feed(cx, ch, at);
                    if(caca_utf32_is_fullwidth(ch)) i++;
                }
                paragraph_has_text = 1;
            }
        }
        else
        {
            /* Normal mode (original behavior) */
            caca_set_canvas_size(cv, 0, 0);
            caca_import_canvas_from_memory(cv, line, strlen(line), "utf8");
            for(i = 0; i < caca_get_canvas_width(cv); i++)
            {
                uint32_t ch = caca_get_char(cv, i, 0);
                uint32_t at = caca_get_attr(cv, i, 0);
                cx->feed(cx, ch, at);
                if(caca_utf32_is_fullwidth(ch)) i++;
            }
            render_flush(cx);
        }
    }

    /* Flush remaining paragraph on EOF */
    if(cx->paragraph && paragraph_has_text)
        render_flush(cx);

    free(line);

    return 0;
}

int render_list(context_t *cx, int argc, char *argv[])
{
    caca_canvas_t *cv;
    int i, j, len;
    char *parser = NULL;

    cv = caca_create_canvas(0, 0);

    if(cx->paragraph)
    {
        /* Paragraph mode: join all argv with spaces,
         * replace newlines with spaces, feed as one paragraph */
        size_t total = 0;
        for(j = 0; j < argc; j++)
            total += strlen(argv[j]) + 1;
        char *full = malloc(total + 1);
        full[0] = '\0';
        for(j = 0; j < argc; j++)
        {
            if(j > 0) strcat(full, " ");
            strcat(full, argv[j]);
        }
        for(char *p = full; *p; p++)
            if(*p == '\n') *p = ' ';

        caca_set_canvas_size(cv, 0, 0);
        caca_import_canvas_from_memory(cv, full, strlen(full), "utf8");
        for(i = 0; i < caca_get_canvas_width(cv); i++)
        {
            uint32_t ch = caca_get_char(cv, i, 0);
            uint32_t at = caca_get_attr(cv, i, 0);
            cx->feed(cx, ch, at);
            if(caca_utf32_is_fullwidth(ch)) i++;
        }
        free(full);
        render_flush(cx);
    }
    else
    {
        for(j = 0; j < argc; )
        {
            char *cr;

            if(!parser)
            {
                if(j)
                    cx->feed(cx, ' ', 0);
                parser = argv[j];
            }

            cr = strchr(parser, '\n');
            if(cr)
                len = (cr - parser) + 1;
            else
                len = strlen(parser);

            caca_set_canvas_size(cv, 0, 0);
            caca_import_canvas_from_memory(cv, parser, len, "utf8");
            for(i = 0; i < caca_get_canvas_width(cv); i++)
            {
                uint32_t ch = caca_get_char(cv, i, 0);
                uint32_t at = caca_get_attr(cv, i, 0);
                cx->feed(cx, ch, at);
                if(caca_utf32_is_fullwidth(ch)) i++;
            }

            if(cr)
            {
                parser += len;
                render_flush(cx);
            }
            else
            {
                parser = NULL;
                j++;
            }
        }

        render_flush(cx);
    }

    caca_free_canvas(cv);

    return 0;
}

int render_end(context_t *cx)
{
    cx->end(cx);
    caca_free_canvas(cx->cv);

    return 0;
}

/* XXX: Following functions are local */

static int render_flush(context_t *cx)
{
    size_t len;
    void *buffer;

    /* Flush current line */
    cx->flush(cx);

    /* Apply optional effects to our string */
    filter_do(cx);

    /* Output line */
    buffer = caca_export_canvas_to_memory(cx->torender, cx->export, &len);
    if(!buffer)
        return -1;
    fwrite(buffer, len, 1, stdout);
    free(buffer);
    caca_free_canvas(cx->torender);

    return 0;
}

