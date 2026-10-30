# The strips at the bottom of the stage, as the companion reports them (leases, docs/protocol.md): Claude usage (how
# much of the Claude plan's 5-hour and 7-day windows is used, the prediction for the end of each and when it resets)
# and, stacked above it, the PC's load (processor, memory, the C: disk). Hardware independent; host tests build the
# same sources: firmware/tests/host.
idf_component_register(SRCS "src/usage.c" "src/usage_view.c" "src/sysinfo.c" "src/sysinfo_view.c" "src/strip.c"
                       INCLUDE_DIRS "include"
                       REQUIRES gfx link
                       PRIV_REQUIRES marquee)
