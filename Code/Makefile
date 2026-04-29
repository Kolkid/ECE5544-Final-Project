CXX          = clang++
CXXFLAGS     = -rdynamic $(shell llvm-config --cxxflags) -fPIC -g -std=c++20
LDFLAGS      = $(shell llvm-config --ldflags | tr '\n' ' ') -Wl,--exclude-libs,ALL
BUILDDIR     = build
DEPDIR       = $(BUILDDIR)/.deps
DEPFLAGS     = -MT $@ -MMD -MP -MF $(DEPDIR)/$*.d

TESTS             = loads1 loads2 loads3 loads4
OPTIMIZER_SOURCES = unifiedpass.cpp
OPTIMIZER_LIBS    = $(OPTIMIZER_SOURCES:%.cpp=$(BUILDDIR)/%.so)
TESTS_PRE         = $(TESTS:%=$(BUILDDIR)/tests/%-m2r.ll)
TESTS_OUT         = $(TESTS:%=$(BUILDDIR)/tests/%-opt.ll)
DEPFILES          = $(OPTIMIZER_SOURCES:%.cpp=$(DEPDIR)/%.d)

.PHONY: all clean tests
.SECONDARY:

all: $(OPTIMIZER_LIBS)
tests: $(TESTS_PRE) $(TESTS_OUT)
clean:
	rm -rf $(BUILDDIR)

$(BUILDDIR)/%.o: %.cpp $(DEPDIR)/%.d | $(DEPDIR) $(BUILDDIR)
	$(CXX) $(DEPFLAGS) $(CXXFLAGS) -c $< -o $@

$(BUILDDIR)/%.so: $(BUILDDIR)/%.o
	$(CXX) -shared $^ -o $@ $(LDFLAGS)

$(BUILDDIR)/tests/%-opt.bc: tests/%-test-m2r.bc $(OPTIMIZER_LIBS) | $(BUILDDIR)/tests
	@if echo "$*" | grep -q "loads"; then \
		echo "----------------------------------------------------------------------------"; \
		echo "----------------Running LICM with Andersen's Method test: $*----------------"; \
		echo "----------------------------------------------------------------------------"; \
		opt -bugpoint-enable-legacy-pm=1 $(OPTIMIZER_LIBS:%=-load-pass-plugin=%) \
			-passes='loop-simplify,loop(licm-andersen)' $< -o $@; \
		echo "--- BEFORE LICM stats for $*"; \
		lli -stats -force-interpreter $< 2>&1; \
		echo "--- AFTER LICM stats for $*"; \
		lli -stats -force-interpreter $@ 2>&1; \
	elif echo "$*" | grep -q "dce"; then \
		echo "----------------------------------------------------------"; \
		echo "----------------Running DCE test: $*----------------"; \
		echo "----------------------------------------------------------"; \
		opt -bugpoint-enable-legacy-pm=1 $(OPTIMIZER_LIBS:%=-load-pass-plugin=%) \
			-passes='mem2reg,dead-code-elimination' $< -o $@; \
		echo "--- BEFORE DCE runtime stats (may be empty) for $*"; \
		lli -stats -force-interpreter $< 2>&1 || true; \
		echo "--- AFTER DCE runtime stats (may be empty) for $*"; \
		lli -stats -force-interpreter $@ 2>&1 || true; \
		echo "--- BEFORE DCE Approximate IR size by line count $*"; \
		llvm-dis $< -o - | grep -v '^;' | wc -l; \
		echo "--- AFTER DCE Approximate IR size by line count $*"; \
		llvm-dis $@ -o - | grep -v '^;' | wc -l; \
	else \
		opt -bugpoint-enable-legacy-pm=1 $(OPTIMIZER_LIBS:%=-load-pass-plugin=%) \
			-passes='$*' $< -o $@; \
	fi
$(BUILDDIR)/tests/%-opt.ll: $(BUILDDIR)/tests/%-opt.bc
	llvm-dis $< -o $@

$(BUILDDIR)/tests/%-m2r.ll: tests/%-test-m2r.bc | $(BUILDDIR)/tests
	llvm-dis $< -o $@
$(DEPDIR) $(BUILDDIR) $(BUILDDIR)/tests:
	@mkdir -p $@

$(DEPFILES):
-include $(wildcard $(DEPFILES))
