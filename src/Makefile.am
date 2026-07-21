
bin_PROGRAMS = pridelet

pridelet_SOURCES = main.c toilet.h \
                 render.c render.h \
                 filter.c filter.h \
                 export.c export.h \
                 term.c figlet.c

pridelet_CPPFLAGS = -DFONTDIR=\"$(datadir)/figlet\" -DCOLORSDIR=\"$(abs_top_srcdir)\" -DSYSTEMFONTDIR=\"/usr/share/figlet\"
pridelet_CFLAGS = @CACA_CFLAGS@ @JSON_C_CFLAGS@
pridelet_LDADD = @CACA_LIBS@ @JSON_C_LIBS@

echo-sources: ; echo $(SOURCES)

