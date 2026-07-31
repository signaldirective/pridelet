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
 * This file contains control file (character mapping table) handling.
 *
 * Control files (.flc) map input characters onto other characters,
 * similar to the Unix "tr" command. They can contain a number of
 * translation commands, separated by "f" (freeze) commands; each
 * group of translations is a transformation stage. When a character
 * is processed, each stage is applied in turn, and within a stage
 * only the first matching translation is used.
 */

#include "config.h"

#if defined(HAVE_INTTYPES_H)
#   include <inttypes.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <caca.h>

#include "toilet.h"
#include "control.h"

/* A command is either a translation or a freeze. */
struct cf_command
{
    int translate;               /* 1 = translate, 0 = freeze */
    long rangelo, rangehi, offset;
};

static struct cf_command *commands = NULL;
static size_t ncommands = 0;
static size_t capcommands = 0;

static void cmd_add(int translate, long rangelo, long rangehi, long offset)
{
    if(ncommands == capcommands)
    {
        capcommands = capcommands ? capcommands * 2 : 64;
        commands = realloc(commands, capcommands * sizeof(*commands));
    }
    commands[ncommands].translate = translate;
    commands[ncommands].rangelo = rangelo;
    commands[ncommands].rangehi = rangehi;
    commands[ncommands].offset = offset;
    ncommands++;
}

/* Skip whitespace, leaving the first non-whitespace char unread. */
static void skipws(FILE *f)
{
    int c;

    do { c = fgetc(f); } while(c != EOF && isspace(c));
    if(c != EOF)
        ungetc(c, f);
}

/* Skip to the end of the line, handling \r, \n, and \r\n. */
static void skiptoeol(FILE *f)
{
    int c;

    while((c = fgetc(f)) != EOF)
    {
        if(c == '\n')
            return;
        if(c == '\r')
        {
            c = fgetc(f);
            if(c != EOF && c != '\n')
                ungetc(c, f);
            return;
        }
    }
}

/* Read a numeric character code. Accepts a leading sign, a "0"
 * prefix for octal and a "0x" or "0X" prefix for hexadecimal. */
static long read_num(FILE *f)
{
    int c;
    long acc = 0;
    int base = 10;
    int sign = 1;

    skipws(f);
    c = fgetc(f);
    if(c == '-')
        sign = -1;
    else if(c != EOF)
        ungetc(c, f);

    c = fgetc(f);
    if(c == '0')
    {
        c = fgetc(f);
        if(c == 'x' || c == 'X')
            base = 16;
        else if(c != EOF)
        {
            base = 8;
            ungetc(c, f);
        }
    }
    else if(c != EOF)
        ungetc(c, f);

    while((c = fgetc(f)) != EOF)
    {
        int digit;

        c = toupper(c);
        if(c >= '0' && c <= '9')
            digit = c - '0';
        else if(c >= 'A' && c <= 'F')
            digit = c - 'A' + 10;
        else
        {
            ungetc(c, f);
            break;
        }
        acc = acc * base + digit;
    }

    return acc * sign;
}

/* Read a "t" command character specification: a single byte, an
 * escape sequence, or an escaped numeric code. */
static long read_tchar(FILE *f)
{
    int c;

    c = fgetc(f);
    if(c == '\n' || c == '\r')
    {
        ungetc(c, f);
        return 0;
    }
    if(c != '\\')
        return c;

    c = fgetc(f);
    switch(c)
    {
        case 'a': return 7;
        case 'b': return 8;
        case 'e': return 27;
        case 'f': return 12;
        case 'n': return 10;
        case 'r': return 13;
        case 't': return 9;
        case 'v': return 11;
        default:
            break;
    }
    if(c == '-' || c == 'x' || (c >= '0' && c <= '9'))
    {
        if(c != EOF)
            ungetc(c, f);
        return read_num(f);
    }

    return c;
}

static int parse_control_file(const char *path)
{
    FILE *f = fopen(path, "r");
    int command;
    long firstch, lastch, offset;

    if(!f)
    {
        fprintf(stderr, "error: could not open control file %s\n", path);
        return -1;
    }

    /* Begin with a freeze command */
    cmd_add(0, 0, 0, 0);

    while((command = fgetc(f)) != EOF)
    {
        switch(command)
        {
        case 't': /* Translate */
            skipws(f);
            firstch = read_tchar(f);
            command = fgetc(f);
            if(command == '-')
                lastch = read_tchar(f);
            else
            {
                if(command != EOF)
                    ungetc(command, f);
                lastch = firstch;
            }
            skipws(f);
            offset = read_tchar(f) - firstch;
            skiptoeol(f);
            cmd_add(1, firstch, lastch, offset);
            break;
        case '0': case '1': case '2': case '3': case '4':
        case '5': case '6': case '7': case '8': case '9':
        case '-': /* Mapping table entry */
            ungetc(command, f);
            firstch = read_num(f);
            skipws(f);
            lastch = read_num(f);
            offset = lastch - firstch;
            lastch = firstch;
            skiptoeol(f);
            cmd_add(1, firstch, lastch, offset);
            break;
        case 'f': /* Freeze */
            skiptoeol(f);
            cmd_add(0, 0, 0, 0);
            break;
        case 'b': /* DBCS input mode */
        case 'u': /* UTF-8 input mode */
        case 'h': /* HZ input mode */
        case 'j': /* Shift-JIS input mode */
        case 'g': /* ISO 2022 character set choices */
        case 's': /* String command */
            /* Input encoding modes are not needed here, as the input
             * is already decoded to UTF-8 before being fed. */
            skiptoeol(f);
            break;
        case '#': /* Comment */
            skiptoeol(f);
            break;
        default:
            break;
        }
    }

    fclose(f);
    return 0;
}

int control_init(context_t *cx)
{
    control_end(cx);

    for(unsigned int i = 0; i < cx->ncontrolfiles; i++)
    {
        const char *name = cx->controlfiles[i];
        char path[2048];

        if(strchr(name, '/'))
        {
            /* Given as a full pathname */
            snprintf(path, 2047, "%s.flc", name);
        }
        else
        {
            /* Try the font directory, then the current directory */
            snprintf(path, 2047, "%s/%s.flc", cx->dir, name);
            FILE *f = fopen(path, "r");
            if(!f)
            {
                snprintf(path, 2047, "%s.flc", name);
                f = fopen(path, "r");
            }
            if(f)
                fclose(f);
            else
            {
                fprintf(stderr, "error: could not open control file %s\n",
                        name);
                return -1;
            }
        }

        if(parse_control_file(path) < 0)
            return -1;
    }

    return 0;
}

uint32_t control_map(context_t *cx, uint32_t ch)
{
    long c = (long)ch;
    size_t i;

    (void)cx;

    for(i = 0; i < ncommands; i++)
    {
        if(!commands[i].translate)
            continue;
        if(c >= commands[i].rangelo && c <= commands[i].rangehi)
        {
            c += commands[i].offset;
            /* Skip the rest of this stage */
            i++;
            while(i < ncommands && commands[i].translate)
                i++;
        }
    }

    return (uint32_t)c;
}

int control_end(context_t *cx)
{
    (void)cx;
    free(commands);
    commands = NULL;
    ncommands = 0;
    capcommands = 0;
    return 0;
}
