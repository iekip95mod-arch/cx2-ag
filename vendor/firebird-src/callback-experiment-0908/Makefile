include ../headless/Makefile

FLAGS += -I../core
OBJDIR ?= objects
SUFFIX ?=
EXPERIMENT_SOURCES := $(ASMSOURCES) $(CSOURCES) $(filter-out main.cpp,$(CPPSOURCES)) ../headless/main.cpp
EXPERIMENT_OBJECTS := $(addprefix $(OBJDIR)/,$(addsuffix .o,$(basename $(notdir $(EXPERIMENT_SOURCES)))))

define experiment_rule
$(OBJDIR)/$(basename $(notdir $(1))).o: $(if $(filter armsnippets_loader.c cpu.cpp,$(notdir $(1))),$(notdir $(1)),$(1)) Makefile armsnippets.h
	@mkdir -p $(OBJDIR)
	$(if $(filter %.cpp,$(1)),$(CXX) $(CXXFLAGS),$(CC) $(CFLAGS)) -c $$< -o $$@
endef

$(foreach source,$(EXPERIMENT_SOURCES),$(eval $(call experiment_rule,$(source))))

experiment: $(EXPERIMENT_OBJECTS)
	$(CXX) $(CXXFLAGS) $(LFLAGS) $^ $(LIBS) -o firebird-headless-experiment$(SUFFIX)

$(OBJDIR)/test-headless.o: ../headless/main.cpp Makefile
	@mkdir -p $(OBJDIR)
	$(CXX) $(CXXFLAGS) -Dmain=firebird_app_main -c $< -o $@

cpu-lifecycle-test$(SUFFIX): cpu_lifecycle_test.cpp $(filter-out $(OBJDIR)/main.o,$(EXPERIMENT_OBJECTS)) $(OBJDIR)/test-headless.o
	$(CXX) $(CXXFLAGS) $(LFLAGS) $^ $(LIBS) -o $@
