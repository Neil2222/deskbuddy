# The Claude usage strip: how much of the Claude plan's 5-hour and 7-day windows is used, the prediction for the end
# of each and when it resets, as the companion reports it (a lease, docs/protocol.md); the strip at the bottom of the
# stage. Hardware independent; host tests build the same sources: firmware/tests/host.
idf_component_register(SRCS "src/usage.c" "src/usage_view.c"
                       INCLUDE_DIRS "include"
                       REQUIRES gfx link
                       PRIV_REQUIRES marquee)
