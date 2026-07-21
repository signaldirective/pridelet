
EXTRA_DIST = pridelet.1.in
CLEANFILES = pridelet.1

man_MANS = pridelet.1

pridelet.1: pridelet.1.in
	sed -e "s,[@]datarootdir@,$(datarootdir),g" $(srcdir)/$^ > $@

