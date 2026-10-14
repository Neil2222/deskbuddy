# Serial protocol to the Windows companion: frame parsing and formatting (docs/protocol.md).
# Hardware independent; host tests build the same sources: firmware/tests/host.
idf_component_register(SRCS "src/link.c"
                       INCLUDE_DIRS "include")
