TARGET = $(top_builddir)/doc/output/release-guide.html

EXTRA_DIST = release-guide.adoc
COMMON_DOC_FLAGS = -a "imagesdir=$(abs_top_builddir)/doc/images" -a data-uri -a icons -a toc2  -a max-width=55em

all-local: $(TARGET)

$(TARGET): release-guide.adoc
	@mkdir -p $(top_srcdir)/doc/output
	asciidoc -$(COMMON_DOC_FLAGS) -b html5 --out-file=$@ $< || \
	asciidoc -$(COMMON_DOC_FLAGS) -b html4 --out-file=$@ $<

clean-local:
	rm -f $(TARGET)
