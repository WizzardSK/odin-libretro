/* The game list (odin-games.json at the top of the sources), in the core as
 * its file is, ended by a zero: the core has no data folder of its own to
 * read it from. */

#include "../host/embedded.inc"

	READ_ONLY

	.global C_NAME(odin_games_json) NEXT .balign 16 NEXT C_NAME(odin_games_json): .incbin "odin-games.json" NEXT .byte 0

/* The lists of the mesh programs Odin comes with (shaders/), written to
 * system/odin/shaders for the renderer to read (create_renderer) */
	.global C_NAME(odin_shaders_airtrix) NEXT .balign 16 NEXT C_NAME(odin_shaders_airtrix): .incbin "shaders/airtrix.txt" NEXT .byte 0
	.global C_NAME(odin_shaders_braveff) NEXT .balign 16 NEXT C_NAME(odin_shaders_braveff): .incbin "shaders/braveff.txt" NEXT .byte 0
	.global C_NAME(odin_shaders_pharrier) NEXT .balign 16 NEXT C_NAME(odin_shaders_pharrier): .incbin "shaders/pharrier.txt" NEXT .byte 0
	.global C_NAME(odin_shaders_sgnascar) NEXT .balign 16 NEXT C_NAME(odin_shaders_sgnascar): .incbin "shaders/sgnascar.txt" NEXT .byte 0
	.global C_NAME(odin_shaders_swracer) NEXT .balign 16 NEXT C_NAME(odin_shaders_swracer): .incbin "shaders/swracer.txt" NEXT .byte 0
	.global C_NAME(odin_shaders_von4) NEXT .balign 16 NEXT C_NAME(odin_shaders_von4): .incbin "shaders/von4.txt" NEXT .byte 0
	.global C_NAME(odin_shaders_atvtrack) NEXT .balign 16 NEXT C_NAME(odin_shaders_atvtrack): .incbin "shaders/atvtrack.txt" NEXT .byte 0
	.global C_NAME(odin_shaders_gfootbal) NEXT .balign 16 NEXT C_NAME(odin_shaders_gfootbal): .incbin "shaders/gfootbal.txt" NEXT .byte 0
	.global C_NAME(odin_shaders_smashdrv) NEXT .balign 16 NEXT C_NAME(odin_shaders_smashdrv): .incbin "shaders/smashdrv.txt" NEXT .byte 0

	STACK_NOTE
