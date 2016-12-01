TARGET = $(top_builddir)/doc/output/implementers-guide.html

EXTRA_DIST = implementers-guide.adoc
COMMON_DOC_FLAGS = -a "imagesdir=$(abs_top_builddir)/doc/images" -a data-uri -a icons -a toc2  -a max-width=55em

all-local: $(TARGET)

$(TARGET): implementers-guide.adoc
	@mkdir -p $(top_builddir)/doc/output
	asciidoc -$(COMMON_DOC_FLAGS) -b html5 --out-file=$@ $< || \
	asciidoc -$(COMMON_DOC_FLAGS) -b html4 --out-file=$@ $<

clean-local:
	rm -f $(TARGET)
