TEST_SANITIZE ?= FALSE
TEST_BUILD_DIR ?= build/tests/$(if $(filter TRUE,$(TEST_SANITIZE)),san,normal)
TEST_SAN_FLAGS :=
ifeq "$(TEST_SANITIZE)" "TRUE"
    TEST_SAN_FLAGS += -O1 -fsanitize=address,undefined
endif
TEST_CFLAGS = $(CFLAGS) -I../core $(TEST_SAN_FLAGS)
TEST_CXXFLAGS = $(CXXFLAGS) -I../core $(TEST_SAN_FLAGS)
TEST_CORE_SOURCES := $(ASMSOURCES) $(CSOURCES) $(filter-out main.cpp,$(CPPSOURCES))
TEST_CORE_OBJECTS := $(addprefix $(TEST_BUILD_DIR)/,$(addsuffix .o,$(basename $(notdir $(TEST_CORE_SOURCES)))))

define test_object_rule
$(TEST_BUILD_DIR)/$(basename $(notdir $(1))).o: $(1) Makefile tests.mk
	@mkdir -p $$(@D)
	$(if $(filter %.cpp,$(1)),$(CXX) $(TEST_CXXFLAGS),$(CC) $(TEST_CFLAGS)) -MMD -MP -c $$< -o $$@
endef

$(foreach source,$(TEST_CORE_SOURCES),$(eval $(call test_object_rule,$(source))))

$(TEST_BUILD_DIR)/main-for-tests.o: main.cpp Makefile tests.mk
	@mkdir -p $(@D)
	$(CXX) $(TEST_CXXFLAGS) -MMD -MP -Dmain=firebird_app_main -c $< -o $@

$(TEST_BUILD_DIR)/main.o: main.cpp Makefile tests.mk
	@mkdir -p $(@D)
	$(CXX) $(TEST_CXXFLAGS) -MMD -MP -c $< -o $@

$(TEST_BUILD_DIR)/armsnippetstest: ../core/tests/armsnippetstest.c Makefile tests.mk
	@mkdir -p $(@D)
	$(CC) $(TEST_CFLAGS) -MMD -MP -MF $@.d -MT $@ $< -o $@

$(TEST_BUILD_DIR)/armsnippetscputest: ../core/tests/armsnippetscputest.cpp $(TEST_CORE_OBJECTS) $(TEST_BUILD_DIR)/main-for-tests.o
	$(CXX) $(TEST_CXXFLAGS) -MMD -MP -MF $@.d -MT $@ $(LFLAGS) $< $(TEST_CORE_OBJECTS) $(TEST_BUILD_DIR)/main-for-tests.o $(LIBS) -o $@

$(TEST_BUILD_DIR)/firebird-headless: $(TEST_CORE_OBJECTS) $(TEST_BUILD_DIR)/main.o
	$(CXX) $(TEST_CXXFLAGS) $(LFLAGS) $^ $(LIBS) -o $@

$(TEST_BUILD_DIR)/usblinktest.o: ../core/tests/usblinktest.c Makefile tests.mk
	@mkdir -p $(@D)
	$(CC) $(TEST_CFLAGS) -MMD -MP -c $< -o $@

$(TEST_BUILD_DIR)/usblinktest: $(TEST_BUILD_DIR)/usblinktest.o $(filter-out $(TEST_BUILD_DIR)/usblink.o,$(TEST_CORE_OBJECTS)) $(TEST_BUILD_DIR)/main-for-tests.o
	$(CXX) $(TEST_CXXFLAGS) $(LFLAGS) $^ $(LIBS) -o $@

$(TEST_BUILD_DIR)/headlessinputtest: ../core/tests/headlessinputtest.cpp main.cpp $(TEST_CORE_OBJECTS)
	$(CXX) $(TEST_CXXFLAGS) -MMD -MP -MF $@.d -MT $@ $(LFLAGS) $< $(TEST_CORE_OBJECTS) $(LIBS) -o $@

$(TEST_BUILD_DIR)/lcdtest.o: ../core/tests/lcdtest.c Makefile tests.mk
	@mkdir -p $(@D)
	$(CC) $(TEST_CFLAGS) -MMD -MP -c $< -o $@

$(TEST_BUILD_DIR)/lcdtest: $(TEST_BUILD_DIR)/lcdtest.o $(filter-out $(TEST_BUILD_DIR)/lcd.o,$(TEST_CORE_OBJECTS)) $(TEST_BUILD_DIR)/main-for-tests.o
	$(CXX) $(TEST_CXXFLAGS) $(LFLAGS) $^ $(LIBS) -o $@

.PHONY: check test-build
$(TEST_BUILD_DIR)/usblinkcx2test: ../core/tests/usblinkcx2test.cpp Makefile tests.mk
	@mkdir -p $(@D)
	$(CXX) $(TEST_CXXFLAGS) -MMD -MP -MF $@.d -MT $@ $< -o $@

check: $(TEST_BUILD_DIR)/armsnippetstest $(TEST_BUILD_DIR)/armsnippetscputest $(TEST_BUILD_DIR)/usblinktest $(TEST_BUILD_DIR)/headlessinputtest $(TEST_BUILD_DIR)/usblinkcx2test $(TEST_BUILD_DIR)/lcdtest
	$(TEST_BUILD_DIR)/armsnippetstest
	$(TEST_BUILD_DIR)/armsnippetscputest
	$(TEST_BUILD_DIR)/usblinktest
	$(TEST_BUILD_DIR)/headlessinputtest
	$(TEST_BUILD_DIR)/usblinkcx2test
	UBSAN_OPTIONS=halt_on_error=1 $(TEST_BUILD_DIR)/lcdtest

test-build: $(TEST_BUILD_DIR)/firebird-headless

-include $(wildcard $(TEST_BUILD_DIR)/*.d)
