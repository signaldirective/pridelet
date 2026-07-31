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
 * This is the main program entry point.
 */

#include "config.h"

#if defined HAVE_INTTYPES_H
#   include <inttypes.h>
#endif
#include <stdint.h>
#if defined HAVE_SYS_IOCTL_H && defined HAVE_TIOCGWINSZ
#   include <sys/ioctl.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <caca.h>

#include "toilet.h"
#include "render.h"
#include "filter.h"
#include "export.h"
#include "control.h"

#include <json-c/json.h>

struct flag_entry
{
    char *name;
    uint16_t *colors;
    unsigned int ncolors;
};

static struct flag_entry *flag_entries = NULL;
static unsigned int nflag_entries = 0;
static unsigned int flag_entries_cap = 0;

static uint16_t hex_to_argb(const char *s)
{
    unsigned int r, g, b;
    if(*s == '#') s++;
    sscanf(s, "%02x%02x%02x", &r, &g, &b);
    return (uint16_t)(0xF000 | ((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4));
}

static void add_flag(char *name, struct json_object *arr)
{
    unsigned int len = json_object_array_length(arr);
    if(nflag_entries == flag_entries_cap)
    {
        flag_entries_cap = flag_entries_cap ? flag_entries_cap * 2 : 128;
        flag_entries = realloc(flag_entries,
                               flag_entries_cap * sizeof(*flag_entries));
    }
    struct flag_entry *e = &flag_entries[nflag_entries++];
    e->name = name;
    e->ncolors = len;
    e->colors = malloc(len * sizeof(uint16_t));
    for(unsigned int i = 0; i < len; i++)
    {
        const char *hex = json_object_get_string(
                              json_object_array_get_idx(arr, i));
        e->colors[i] = hex_to_argb(hex);
    }
}

static int load_flags(const char *path)
{
    FILE *f = fopen(path, "r");
    if(!f) return -1;

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *buf = malloc(len + 1);
    if(fread(buf, 1, len, f) != (size_t)len)
    {
        free(buf);
        fclose(f);
        return -1;
    }
    buf[len] = '\0';
    fclose(f);

    struct json_object *root = json_tokener_parse(buf);
    free(buf);
    if(!root) return -1;

    json_object_object_foreach(root, key, val)
    {
        enum json_type type = json_object_get_type(val);
        if(type == json_type_array)
        {
            add_flag(strdup(key), val);
        }
        else if(type == json_type_object)
        {
            struct json_object *colors;
            if(json_object_object_get_ex(val, "colors", &colors))
                add_flag(strdup(key), colors);
        }
    }

    json_object_put(root);
    return 0;
}

static int flag_list(void)
{
    if(!nflag_entries)
    {
        printf("no pride flags loaded (colors.json not found)\n");
        return 0;
    }
    printf("Available pride flags:\n");
    for(unsigned int i = 0; i < nflag_entries; i++)
        printf("  %s\n", flag_entries[i].name);
    return 0;
}

static int set_flag(context_t *cx, const char *name)
{
    if(!nflag_entries)
    {
        fprintf(stderr, "no pride flags loaded (colors.json not found)\n");
        return -1;
    }
    for(unsigned int i = 0; i < nflag_entries; i++)
    {
        if(!strcmp(flag_entries[i].name, name))
        {
            cx->pride_palette = flag_entries[i].colors;
            cx->pride_ncolors = flag_entries[i].ncolors;
            return filter_add(cx, "pride");
        }
    }
    fprintf(stderr, "unknown flag `%s' (use --flag list to list all)\n", name);
    return -1;
}

/* Select the rainbow flag palette if no flag palette has been set yet.
 * Used so that the "pride" filter works on its own. */
static void pride_default(context_t *cx)
{
    if(cx->pride_palette)
        return;
    for(unsigned int i = 0; i < nflag_entries; i++)
        if(!strcmp(flag_entries[i].name, "rainbow"))
        {
            cx->pride_palette = flag_entries[i].colors;
            cx->pride_ncolors = flag_entries[i].ncolors;
            return;
        }
}

/* Check whether the "pride" filter appears in a colon-separated list
 * of filter names. */
static int has_pride_filter(char const *filters)
{
    for(;;)
    {
        while(*filters == ':')
            filters++;
        if(*filters == '\0')
            return 0;
        if(!strncmp(filters, "pride", 5) &&
           (filters[5] == ':' || filters[5] == '\0'))
            return 1;
        while(*filters && *filters != ':')
            filters++;
    }
}

static void version(void);
static void usage(void);

static int controlfile_add(context_t *cx, const char *name)
{
    size_t len = strlen(name);
    char *copy;

    /* A trailing ".flc" suffix may be left off */
    if(len > 4 && !strcasecmp(name + len - 4, ".flc"))
        len -= 4;

    copy = strndup(name, len);
    if(!copy)
        return -1;

    cx->controlfiles = realloc(cx->controlfiles,
                               (cx->ncontrolfiles + 1) * sizeof(char *));
    if(!cx->controlfiles)
    {
        free(copy);
        return -1;
    }
    cx->controlfiles[cx->ncontrolfiles++] = copy;

    return 0;
}

static void controlfile_clear(context_t *cx)
{
    for(unsigned int i = 0; i < cx->ncontrolfiles; i++)
        free(cx->controlfiles[i]);
    free(cx->controlfiles);
    cx->controlfiles = NULL;
    cx->ncontrolfiles = 0;
}

int main(int argc, char *argv[])
{
    context_t struct_cx;
    context_t *cx = &struct_cx;

    int infocode = -1;

    cx->export = "utf8";
    cx->font = "ascii9";
    cx->dir = FONTDIR;

    cx->term_width = 80;

    cx->hmode = "default";

    cx->filters = NULL;
    cx->nfilters = 0;

    cx->pride_palette = NULL;
    cx->pride_ncolors = 0;

    cx->wordwrap = 0;
    cx->justify = NULL;
    cx->paragraph = 0;

    cx->controlfiles = NULL;
    cx->ncontrolfiles = 0;

    cx->rtl = -1;
    cx->vsmush = 0;
    cx->vsmush_rules = 0;

    /* Load pride flag colours from colors.json */
    {
        char const *flagpaths[] =
            { "colors.json", COLORSDIR "/colors.json",
              FONTDIR "/colors.json", FONTDIR "/../colors.json", NULL };
        for(int i = 0; flagpaths[i]; i++)
        {
            if(load_flags(flagpaths[i]) == 0)
                break;
        }
    }

    for(;;)
    {
#define MOREINFO "Try `%s --help' for more information.\n"
        int option_index = 0;
        static struct caca_option long_options[] =
        {
            /* Long option, needs arg, flag, short option */
            { "font", 1, NULL, 'f' },
            { "directory", 1, NULL, 'd' },
            { "width", 1, NULL, 'w' },
            { "termwidth", 0, NULL, 't' },
            { "filter", 1, NULL, 'F' },
            { "gay", 0, NULL, 130 },
            { "metal", 0, NULL, 131 },
            { "rainbow", 0, NULL, 132 },
            { "flag", 1, NULL, 133 },
            { "transgender", 0, NULL, 134 },
            { "word-wrap", 0, NULL, 135 },
            { "justify", 1, NULL, 136 },
            { "normal", 0, NULL, 'n' },
            { "paragraph", 0, NULL, 'p' },
            { "layout", 1, NULL, 'm' },
            { "controlfile", 1, NULL, 'C' },
            { "nocontrolfiles", 0, NULL, 'N' },
            { "left-to-right", 0, NULL, 'L' },
            { "right-to-left", 0, NULL, 'R' },
            { "default-direction", 0, NULL, 'X' },
            { "vertical-smush", 0, NULL, 'V' },
            { "export", 1, NULL, 'E' },
            { "irc", 0, NULL, 140 },
            { "html", 0, NULL, 141 },
            { "help", 0, NULL, 'h' },
            { "infocode", 1, NULL, 'I' },
            { "version", 0, NULL, 'v' },
            { NULL, 0, NULL, 0 }
        };

        int c = caca_getopt(argc, argv, "f:d:w:tsSkWoF:E:hI:vm:C:NnpLRXV",
                            long_options, &option_index);
        if(c == -1)
            break;

        switch(c)
        {
        case 'h': /* --help */
            usage();
            return 0;
        case 'I': /* --infocode */
            infocode = atoi(caca_optarg);
            break;
        case 'v': /* --version */
            version();
            return 0;
        case 'f': /* --font */
            cx->font = caca_optarg;
            break;
        case 'd': /* --directory */
            cx->dir = caca_optarg;
            break;
        case 'F': /* --filter */
            if(!strcmp(caca_optarg, "list"))
                return filter_list();
            if(has_pride_filter(caca_optarg))
                pride_default(cx);
            if(filter_add(cx, caca_optarg) < 0)
                return -1;
            break;
        case 130: /* --gay */
            set_flag(cx, "gay-men");
            break;
        case 131: /* --metal */
            filter_add(cx, "metal");
            break;
        case 132: /* --rainbow */
            set_flag(cx, "rainbow");
            break;
        case 133: /* --flag */
            if(!strcmp(caca_optarg, "list"))
                return flag_list();
            if(set_flag(cx, caca_optarg) < 0)
                return -1;
            break;
        case 134: /* --transgender */
            set_flag(cx, "transgender");
            break;
        case 135: /* --word-wrap */
            cx->wordwrap = 1;
            break;
        case 136: /* --justify */
            if(strcmp(caca_optarg, "left") && strcmp(caca_optarg, "center")
               && strcmp(caca_optarg, "right"))
            {
                fprintf(stderr, "unknown justification `%s' "
                        "(use left, center, or right)\n", caca_optarg);
                return -1;
            }
            cx->justify = caca_optarg;
            break;
        case 'w': /* --width */
            cx->term_width = atoi(caca_optarg);
            break;
        case 't': /* --termwidth */
        {
#if defined HAVE_SYS_IOCTL_H && defined HAVE_TIOCGWINSZ
            struct winsize ws;

            if((ioctl(1, TIOCGWINSZ, &ws) != -1 ||
                ioctl(2, TIOCGWINSZ, &ws) != -1 ||
                ioctl(0, TIOCGWINSZ, &ws) != -1) && ws.ws_col != 0)
                cx->term_width = ws.ws_col;
#endif
            break;
        }
        case 's':
            cx->hmode = "default";
            break;
        case 'S':
            cx->hmode = "smush";
            break;
        case 'k':
            cx->hmode = "kern";
            break;
        case 'W':
            cx->hmode = "none";
            break;
        case 'o':
            cx->hmode = "overlap";
            break;
        case 'm': /* --layout */
        {
            char *end = NULL;
            int n = strtol(caca_optarg, &end, 10);
            if(end && *end == '\0')
            {
                switch(n)
                {
                    case -1: cx->hmode = "default"; break;
                    case 0:  cx->hmode = "none"; break;
                    case 1:  cx->hmode = "kern"; break;
                    case 2:
                    case -2: cx->hmode = "smush"; break;
                    default:
                        fprintf(stderr, "unknown layout mode `%d'\n", n);
                        return -1;
                }
            }
            else
            {
                if(!strcmp(caca_optarg, "default"))
                    cx->hmode = "default";
                else if(!strcmp(caca_optarg, "full")
                     || !strcmp(caca_optarg, "fullwidth"))
                    cx->hmode = "none";
                else if(!strcmp(caca_optarg, "kern")
                     || !strcmp(caca_optarg, "kerning"))
                    cx->hmode = "kern";
                else if(!strcmp(caca_optarg, "smush"))
                    cx->hmode = "smush";
                else if(!strcmp(caca_optarg, "overlap"))
                    cx->hmode = "overlap";
                else
                {
                    fprintf(stderr, "unknown layout mode `%s'\n",
                            caca_optarg);
                    return -1;
                }
            }
            break;
        }
        case 'n': /* --normal */
            cx->paragraph = 0;
            break;
        case 'p': /* --paragraph */
            cx->paragraph = 1;
            cx->wordwrap = 1;
            break;
        case 'C': /* --controlfile */
            if(controlfile_add(cx, caca_optarg) < 0)
                return -1;
            break;
        case 'N': /* --nocontrolfiles */
            controlfile_clear(cx);
            break;
        case 'L': /* --left-to-right */
            cx->rtl = 0;
            break;
        case 'R': /* --right-to-left */
            cx->rtl = 1;
            break;
        case 'X': /* --default-direction */
            cx->rtl = -1;
            break;
        case 'V': /* --vertical-smush */
            cx->vsmush = 1;
            break;
        case 'E': /* --export */
            if(!strcmp(caca_optarg, "list"))
                return export_list();
            if(export_set(cx, caca_optarg) < 0)
                return -1;
            break;
        case 140: /* --irc */
            export_set(cx, "irc");
            break;
        case 141: /* --html */
            export_set(cx, "html");
            break;
        case '?':
            printf(MOREINFO, argv[0]);
            return 1;
        default:
            printf("%s: invalid option -- %i\n", argv[0], c);
            printf(MOREINFO, argv[0]);
            return 1;
        }
    }

    switch(infocode)
    {
        case -1:
            break;
        case 0:
            version();
            return 0;
        case 1:
            printf("20201\n");
            return 0;
        case 2:
            printf("%s\n", cx->dir);
            return 0;
        case 3:
            printf("%s\n", cx->font);
            return 0;
        case 4:
            printf("%u\n", cx->term_width);
            return 0;
        default:
            return 0;
    }

    if(render_init(cx) < 0)
        return -1;

    if(control_init(cx) < 0)
        return -1;

    if(caca_optind >= argc)
        render_stdin(cx);
    else
        render_list(cx, argc - caca_optind, argv + caca_optind);

    render_end(cx);
    control_end(cx);
    filter_end(cx);

    return 0;
}

#define USAGE \
    "Usage: pridelet [ -hmnpstvSWXLRV ] [ -d fontdirectory ]\n" \
    "              [ -f fontfile ] [ -F filter ] [ -w outputwidth ]\n" \
    "              [ -C controlfile ] [ -I infocode ] [ -E format ] [ message ]\n"

#define HELP \
    "  -f, --font <name>        select the font\n" \
    "  -d, --directory <dir>    specify font directory\n" \
    "  -s, -S, -k, -W, -o       render mode (default, force smushing,\n" \
    "                           kerning, full width, overlap)\n" \
    "  -m, --layout <mode>      layout mode (number or name: -1 default,\n" \
    "                           0 full, 1 kern, 2 smush, -2 force smush)\n" \
    "  -w, --width <width>      set output width\n" \
    "  -t, --termwidth          adapt to terminal's width\n" \
    "  -F, --filter <filters>   apply one or several filters to the text\n" \
    "  -F, --filter list        list available filters\n" \
    "      --rainbow            use rainbow flag colours\n" \
    "      --gay                use gay men pride flag colours\n" \
    "      --transgender        use transgender pride flag colours\n" \
    "      --flag <name>        use a pride flag from colors.json\n" \
    "  -p, --paragraph          paragraph mode (reflow text)\n" \
    "  -n, --normal             normal mode (default, keep newlines)\n" \
    "      --word-wrap          wrap output at word boundaries\n" \
    "      --justify <mode>     justify text (left, center, right)\n" \
    "  -C, --controlfile <file> add a control file (character mapping)\n" \
    "  -N, --nocontrolfiles     clear the control file list\n" \
    "  -L, --left-to-right      print text left-to-right\n" \
    "  -R, --right-to-left      print text right-to-left\n" \
    "  -X, --default-direction  use the font's print direction\n" \
    "  -V, --vertical-smush     smush successive lines vertically\n" \
    "      --metal              metal filter (same as -F metal)\n" \
    "  -E, --export <format>    select export format\n" \
    "  -E, --export list        list available export formats\n" \
    "      --irc                output IRC colour codes (same as -E irc)\n" \
    "      --html               output an HTML document (same as -E html)\n" \
    "  -h, --help               display this help and exit\n" \
    "  -I, --infocode <code>    print FIGlet-compatible infocode\n" \
    "  -v, --version            output version information and exit\n"

static void version(void)
{
    printf(
    "pridelet -- a fork of TOIlet with pride flag colours\n"
    "Version: %s, date: %s\n"
    "\n"
    "Based on TOIlet Copyright 2006 Sam Hocevar <sam@hocevar.net>\n"
    "\n"
    "%s", VERSION, DATE, USAGE);
}

static void usage(void)
{
    printf("%s%s", HELP, USAGE);
}

