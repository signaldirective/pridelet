
bin_PROGRAMS = toilet

toilet_SOURCES = main.c toilet.h \
                 render.c render.h \
                 filter.c filter.h \
                 export.c export.h \
                 term.c figlet.c

toilet_CPPFLAGS = -DFONTDIR=\"$(datadir)/figlet\" -DCOLORSDIR=\"$(abs_top_srcdir)\"
toilet_CFLAGS = @CACA_CFLAGS@ @JSON_C_CFLAGS@
toilet_LDADD = @CACA_LIBS@ @JSON_C_LIBS@

echo-sources: ; echo $(SOURCES)

