/* The game list (odin-games.json at the top of the sources), in the core as
 * its file is, ended by a zero: the core has no data folder of its own to
 * read it from. */

#include "../host/embedded.inc"

	READ_ONLY

	.global C_NAME(odin_games_json) NEXT .balign 16 NEXT C_NAME(odin_games_json): .incbin "odin-games.json" NEXT .byte 0

	STACK_NOTE
