TESTS_ENVIRONMENT =
AUTOMAKE_OPTIONS = parallel-tests

include $(top_srcdir)/platform/@with_platform@/Makefile.inc
LIB   = $(top_builddir)/lib

#in the following line, the libs using the symbols should come before
#the libs containing them! The includer is given a chance to add things
#before libodp by setting PRE_LDADD before the inclusion.
LDADD = $(PRE_LDADD) $(LIB)/libodphelper.la $(LIB)/libodp.la

INCFLAGS = -I$(top_srcdir)/test \
	-I$(top_srcdir)/platform/@with_platform@/include \
	-I$(top_srcdir)/include \
	-I$(top_srcdir)/helper/include
AM_CFLAGS += $(INCFLAGS)
AM_CXXFLAGS = $(INCFLAGS)

AM_LDFLAGS += -L$(LIB)

@VALGRIND_CHECK_RULES@
valgrind_tools = memcheck drd sgcheck

TESTS_ENVIRONMENT+= ODP_PLATFORM=${with_platform} EXEEXT=${EXEEXT} ODP_TOOLCHAIN_DIR=${top_srcdir}/install/local/k1tools
bindir      = $(datarootdir)/$(subst $(top_srcdir)/,,$(srcdir))

install-exec-local:
	mkdir -p $(DESTDIR)/$(bindir)
	( echo "#!/bin/bash -ex"; \
	  echo "export $(TESTS_ENVIRONMENT)";\
	  echo 'cd $$(readlink -e $$(dirname $$0))';\
	  for test in $(TESTS); do \
		echo "$$(echo $(subst $(top_srcdir)/,,$(srcdir)) | sed -e 's/[a-zA-Z0-9]*/../g')/../../ktest-wrapper.sh $${test}"; \
	  done) > $(DESTDIR)/$(bindir)/runtests.sh
	chmod +x $(DESTDIR)/$(bindir)/runtests.sh
	( \
	  echo '# Generated test file'; \
	  echo 'SET(ENV{LD_LIBRARY_PATH} ../lib/host/:$$ENV{LD_LIBRARY_PATH})'; \
	  echo 'SET(ENV{PATH} ./:$$ENV{PATH})'; \
	  for env in $(TESTS_ENVIRONMENT); do \
	  	  var=$$(echo $$env | sed -e 's/=.*$$//'); \
	  	  value=$$(echo $$env | sed -e 's/^[A-Z0-0_a-z]*=//'); \
	  	  echo "SET(ENV{$$var} $$value)"; \
	  done; \
	  echo 'SET(ENV{ODP_TOOLCHAIN_DIR} $$ENV{K1_TOOLCHAIN_DIR})'; \
	  for test in $(TESTS); do \
	  	  test_name=$$(basename $$test); \
		  echo "ADD_TEST($$test_name $$(echo $(subst $(top_srcdir)/,,$(srcdir)) | sed -e 's/[a-zA-Z0-9]*/../g')/../../ktest-wrapper.sh $${test})"; \
		  echo 'SET_TESTS_PROPERTIES($$test_name PROPERTIES TIMEOUT "400")'; \
		  echo 'SET_TESTS_PROPERTIES($$test_name PROPERTIES COST "0")';\
	  done \
	 ) > $(DESTDIR)/$(bindir)/CTestTestfile.cmake
	 ( \
	   echo 'Name: odp'; \
	   echo 'BuildName: odp'; \
	   echo "Site: $$(hostname)" \
	 ) > $(DESTDIR)/$(bindir)/DartConfiguration.tcl

