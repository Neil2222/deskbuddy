# The device side of the PDC Manager integration (ZDMW): availability lease, the click under way, cooldown, the
# sign's lines. The companion does the HTTP (docs/protocol.md). Hardware independent; host tests build the same
# sources: firmware/tests/host.
idf_component_register(SRCS "src/pdc.c"
                       INCLUDE_DIRS "include"
                       REQUIRES link)
