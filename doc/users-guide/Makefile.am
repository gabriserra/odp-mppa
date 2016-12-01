TARGET = $(top_builddir)/doc/output/users-guide.html

EXTRA_DIST = users-guide.adoc
COMMON_DOC_FLAGS = -a "imagesdir=$(abs_top_builddir)/doc/images" -a data-uri -a icons -a toc2  -a max-width=55em

all-local: $(TARGET)

$(TARGET): users-guide.adoc
	@mkdir -p $(top_builddir)/doc/output
	asciidoc -$(COMMON_DOC_FLAGS) -b html5 --out-file=$@ $< || \
	asciidoc -$(COMMON_DOC_FLAGS) -b html4 --out-file=$@ $<

clean-local:
	rm -f $(TARGET)
