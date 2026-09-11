# Prints Makefile.ki's own object list. Included after it, so OBJS is whatever that file says today
# and the unified link cannot end up holding a stale copy of the list. A wildcard over the directory
# would be simpler and wrong: giacprobe.o sits beside them and has its own main.
print-objs:
	@echo $(OBJS)

# The defines Giac itself was compiled under. giac_typed.o includes Giac's headers and passes gen
# objects to code inside those objects, so it has to read those headers the same way they did. Some
# of these change the layout of gen, so a mismatch would not be a compile error, it would be a
# corrupted value at the boundary. Asked for rather than copied, for the same reason as the objects.
print-defines:
	@echo $(filter -D%,$(GCCFLAGS))

ifneq ($(strip $(OBJDIR)),)
NPS_GIAC_RULES := $(lastword $(MAKEFILE_LIST))
NPS_GIAC_OUTPUTS := $(addprefix $(OBJDIR)/,$(OBJS) luabridge.o)

$(OBJDIR):
	mkdir -p "$@"

$(OBJDIR)/%.o: %.cc Makefile.ki $(NPS_GIAC_RULES) | $(OBJDIR)
	$(CXX) $(GCCFLAGS) -MMD -MP -MF $(@:.o=.d) -c $< -o $@

$(OBJDIR)/%.o: %.c Makefile.ki $(NPS_GIAC_RULES) | $(OBJDIR)
	$(GCC) $(GCCFLAGS) -MMD -MP -MF $(@:.o=.d) -c $< -o $@

-include $(NPS_GIAC_OUTPUTS:.o=.d)
endif
