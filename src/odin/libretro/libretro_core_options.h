/*
 * Odin (Sega Hikaru) libretro core - Core Options v2
 *
 * The English texts are edited here; the translations in
 * libretro_core_options_intl.h come from Crowdin (intl/).
 */

#ifndef LIBRETRO_CORE_OPTIONS_H__
#define LIBRETRO_CORE_OPTIONS_H__

#include <stdlib.h>
#include <string.h>

#include "libretro.h"

#ifndef HAVE_NO_LANGEXTRA
#include "libretro_core_options_intl.h"
#endif

/*
 ********************************
 * VERSION: 2.0
 ********************************
 *
 * - 2.0: Add support for core options v2 interface
 * - 1.3: Move translations to libretro_core_options_intl.h
 *        - libretro_core_options_intl.h includes BOM and utf-8
 *          fix for MSVC 2010-2013
 *        - Added HAVE_NO_LANGEXTRA flag to disable translations
 *          on platforms/compilers without BOM support
 * - 1.2: Use core options v1 interface when
 *        RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION is >= 1
 *        (previously required RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION == 1)
 * - 1.1: Support generation of core options v0 retro_core_option_value
 *        arrays containing options with a single value
 * - 1.0: First commit
*/

#ifdef __cplusplus
extern "C" {
#endif

/*
 ********************************
 * Core Option Definitions
 ********************************
*/

/* RETRO_LANGUAGE_ENGLISH */

/* Default language:
 * - All other languages must include the same keys and values
 * - Will be used as a fallback in the event that frontend language
 *   is not available
 * - Will be used as a fallback for any missing entries in
 *   frontend language definition
 * - Translations live in libretro_core_options_intl.h, which is
 *   generated from Crowdin - edit only the English texts here
 */

struct retro_core_option_v2_category option_cats_us[] = {
   { NULL, NULL, NULL },
};

struct retro_core_option_v2_definition option_defs_us[] = {
   {
      "odin_renderer",
      "Renderer (Restart)",
      NULL,
      "OpenGL or Vulkan. Automatic takes the one of RetroArch's video driver (glcore or vulkan).",
      NULL,
      NULL,
      { { "auto", "Automatic" }, { "opengl", "OpenGL" }, { "vulkan", "Vulkan" }, { NULL, NULL } },
      "auto"
   },
   {
      "odin_scale",
      "Internal Resolution",
      NULL,
      "The 3D drawn that many times finer than the board's 640x480 screen; the 2D layers stay as they are.",
      NULL,
      NULL,
      { { "1", "1x (640x480)" }, { "2", "2x (1280x960)" }, { "3", "3x (1920x1440)" }, { "4", "4x (2560x1920)" },
	    { "5", "5x (3200x2400)" }, { "6", "6x (3840x2880)" }, { NULL, NULL } },
      "1"
   },
   {
      "odin_upscale_mode",
      "HUD at Higher Resolutions",
      NULL,
      "How the meshes flat to the screen (the HUD, its text) are drawn above 1x: Snap, their corners on the board's pixels (default); Native, over the pixels they cover there; Raw, as fine as the rest.",
      NULL,
      NULL,
      { { "snap", "Snap" }, { "native", "Native" }, { "raw", "Raw" }, { NULL, NULL } },
      "snap"
   },
   {
      "odin_threaded",
      "Threaded Board (Restart)",
      NULL,
      "The board runs the next frame on a thread of its own while the frame before is drawn, as the desktop Odin does: faster on most machines, one frame more of latency. Off: each frame is run and drawn in turn.",
      NULL,
      NULL,
      { { "enabled", NULL }, { "disabled", NULL }, { NULL, NULL } },
      "enabled"
   },
   {
      "odin_rumble",
      "Rumble",
      NULL,
      "The pads shake as the cabinet does: its rumble outputs, the vibration device the optional sound board drives (Star Wars Racer Arcade) and the shocks of the steering wheel (NASCAR Arcade), as the desktop Odin shakes a gamepad. Large Motor Only leaves the small motor still, for a pad where the shocks of the wheel are too much on it.",
      NULL,
      NULL,
      { { "enabled", NULL }, { "large", "Large Motor Only" }, { "disabled", NULL }, { NULL, NULL } },
      "enabled"
   },
   {
      "odin_pharrier_sound_mode",
      "Sound Mode",
      NULL,
      "Planet Harriers has an optional sound board: Main, the main board alone; Both, the two mixed, the optional one at Optional Board Level; 4.0, as the desktop's Quad: four channels, the main board in the front speakers, the optional one in the rear at Optional Board Level (RetroArch folds them for stereo; a RetroArch without multi-channel sound gets Both); Optional Only, the optional board alone.",
      NULL,
      NULL,
      { { "main", "Main" }, { "mix", "Both" }, { "quad", "4.0" }, { "option", "Optional Only" }, { NULL, NULL } },
      "mix"
   },
   {
      "odin_pharrier_option_level",
      "Optional Board Level",
      NULL,
      "The optional sound board of Planet Harriers against the main one, with Sound Mode Both.",
      NULL,
      NULL,
      { { "-12", "-12 dB" }, { "-11", "-11 dB" }, { "-10", "-10 dB" }, { "-9", "-9 dB" }, { "-8", "-8 dB" }, { "-7", "-7 dB" }, { "-6", "-6 dB" }, { "-5", "-5 dB" }, { "-4", "-4 dB" }, { "-3", "-3 dB" }, { "-2", "-2 dB" }, { "-1", "-1 dB" }, { "0", "0 dB" }, { "1", "+1 dB" }, { "2", "+2 dB" }, { "3", "+3 dB" }, { "4", "+4 dB" }, { "5", "+5 dB" }, { "6", "+6 dB" }, { "7", "+7 dB" }, { "8", "+8 dB" }, { "9", "+9 dB" }, { "10", "+10 dB" }, { "11", "+11 dB" }, { "12", "+12 dB" }, { NULL, NULL } },
      "0"
   },
   {
      "odin_swracer_sound_mode",
      "Sound Mode",
      NULL,
      "Star Wars Racer Arcade has an optional sound board: Main, the main board alone; Both, the two mixed, the optional one at Optional Board Level; 4.0, as the desktop's Quad: four channels, the main board in the front speakers, the optional one in the rear at Optional Board Level (RetroArch folds them for stereo; a RetroArch without multi-channel sound gets Both); Optional Only, the optional board alone.",
      NULL,
      NULL,
      { { "main", "Main" }, { "mix", "Both" }, { "quad", "4.0" }, { "option", "Optional Only" }, { NULL, NULL } },
      "mix"
   },
   {
      "odin_swracer_option_level",
      "Optional Board Level",
      NULL,
      "The optional sound board of Star Wars Racer Arcade against the main one, with Sound Mode Both.",
      NULL,
      NULL,
      { { "-12", "-12 dB" }, { "-11", "-11 dB" }, { "-10", "-10 dB" }, { "-9", "-9 dB" }, { "-8", "-8 dB" }, { "-7", "-7 dB" }, { "-6", "-6 dB" }, { "-5", "-5 dB" }, { "-4", "-4 dB" }, { "-3", "-3 dB" }, { "-2", "-2 dB" }, { "-1", "-1 dB" }, { "0", "0 dB" }, { "1", "+1 dB" }, { "2", "+2 dB" }, { "3", "+3 dB" }, { "4", "+4 dB" }, { "5", "+5 dB" }, { "6", "+6 dB" }, { "7", "+7 dB" }, { "8", "+8 dB" }, { "9", "+9 dB" }, { "10", "+10 dB" }, { "11", "+11 dB" }, { "12", "+12 dB" }, { NULL, NULL } },
      "0"
   },
   {
      "odin_sgnascar_sound_mode",
      "Sound Mode",
      NULL,
      "NASCAR Arcade has an optional sound board: Main, the main board alone; Both, the two mixed, the optional one at Optional Board Level; 4.0, as the desktop's Quad: four channels, the main board in the front speakers, the optional one in the rear at Optional Board Level (RetroArch folds them for stereo; a RetroArch without multi-channel sound gets Both); Optional Only, the optional board alone.",
      NULL,
      NULL,
      { { "main", "Main" }, { "mix", "Both" }, { "quad", "4.0" }, { "option", "Optional Only" }, { NULL, NULL } },
      "mix"
   },
   {
      "odin_sgnascar_option_level",
      "Optional Board Level",
      NULL,
      "The optional sound board of NASCAR Arcade against the main one, with Sound Mode Both or 4.0: -24 dB to 0 dB (its engines are loud).",
      NULL,
      NULL,
      { { "-24", "-24 dB" }, { "-23", "-23 dB" }, { "-22", "-22 dB" }, { "-21", "-21 dB" }, { "-20", "-20 dB" }, { "-19", "-19 dB" }, { "-18", "-18 dB" }, { "-17", "-17 dB" }, { "-16", "-16 dB" }, { "-15", "-15 dB" }, { "-14", "-14 dB" }, { "-13", "-13 dB" }, { "-12", "-12 dB" }, { "-11", "-11 dB" }, { "-10", "-10 dB" }, { "-9", "-9 dB" }, { "-8", "-8 dB" }, { "-7", "-7 dB" }, { "-6", "-6 dB" }, { "-5", "-5 dB" }, { "-4", "-4 dB" }, { "-3", "-3 dB" }, { "-2", "-2 dB" }, { "-1", "-1 dB" }, { "0", "0 dB" }, { NULL, NULL } },
      "-12"
   },
   {
      "odin_atvtrack_sound_mode",
      "Sound Mode",
      NULL,
      "ATV Track has a woofer, fed by the board's second pair of channels: Main, the speakers alone; Both, the woofer's lows mixed in at Woofer Level, as the desktop plays it; 4.0, four channels, the speakers in front and the woofer's channel in the rear (RetroArch folds them for stereo; a RetroArch without multi-channel sound gets Both); Woofer Only, the woofer alone: its channel's lows below Woofer Cutoff.",
      NULL,
      NULL,
      { { "main", "Main" }, { "mix", "Both" }, { "quad", "4.0" }, { "option", "Woofer Only" }, { NULL, NULL } },
      "mix"
   },
   {
      "odin_atvtrack_option_level",
      "Woofer Level",
      NULL,
      "The woofer of ATV Track against the speakers, with Sound Mode Both, 4.0 or Woofer Only.",
      NULL,
      NULL,
      { { "-12", "-12 dB" }, { "-11", "-11 dB" }, { "-10", "-10 dB" }, { "-9", "-9 dB" }, { "-8", "-8 dB" }, { "-7", "-7 dB" }, { "-6", "-6 dB" }, { "-5", "-5 dB" }, { "-4", "-4 dB" }, { "-3", "-3 dB" }, { "-2", "-2 dB" }, { "-1", "-1 dB" }, { "0", "0 dB" }, { "1", "+1 dB" }, { "2", "+2 dB" }, { "3", "+3 dB" }, { "4", "+4 dB" }, { "5", "+5 dB" }, { "6", "+6 dB" }, { "7", "+7 dB" }, { "8", "+8 dB" }, { "9", "+9 dB" }, { "10", "+10 dB" }, { "11", "+11 dB" }, { "12", "+12 dB" }, { NULL, NULL } },
      "0"
   },
   {
      "odin_atvtrack_woofer_cutoff",
      "Woofer Cutoff",
      NULL,
      "The lows of the woofer's channel played with Sound Mode Both and Woofer Only: below that many Hz, or all of it (Off).",
      NULL,
      NULL,
      { { "80", "80 Hz" }, { "120", "120 Hz" }, { "160", "160 Hz" }, { "200", "200 Hz" }, { "off", "Off" }, { NULL, NULL } },
      "120"
   },
   {
      "odin_gfootbal_sound_mode",
      "Sound Mode",
      NULL,
      "Gaelco Football has a woofer, fed by the board's second pair of channels: Main, the speakers alone; Both, the woofer's lows mixed in at Woofer Level, as the desktop plays it; 4.0, four channels, the speakers in front and the woofer's channel in the rear (RetroArch folds them for stereo; a RetroArch without multi-channel sound gets Both); Woofer Only, the woofer alone: its channel's lows below Woofer Cutoff.",
      NULL,
      NULL,
      { { "main", "Main" }, { "mix", "Both" }, { "quad", "4.0" }, { "option", "Woofer Only" }, { NULL, NULL } },
      "mix"
   },
   {
      "odin_gfootbal_option_level",
      "Woofer Level",
      NULL,
      "The woofer of Gaelco Football against the speakers, with Sound Mode Both, 4.0 or Woofer Only.",
      NULL,
      NULL,
      { { "-12", "-12 dB" }, { "-11", "-11 dB" }, { "-10", "-10 dB" }, { "-9", "-9 dB" }, { "-8", "-8 dB" }, { "-7", "-7 dB" }, { "-6", "-6 dB" }, { "-5", "-5 dB" }, { "-4", "-4 dB" }, { "-3", "-3 dB" }, { "-2", "-2 dB" }, { "-1", "-1 dB" }, { "0", "0 dB" }, { "1", "+1 dB" }, { "2", "+2 dB" }, { "3", "+3 dB" }, { "4", "+4 dB" }, { "5", "+5 dB" }, { "6", "+6 dB" }, { "7", "+7 dB" }, { "8", "+8 dB" }, { "9", "+9 dB" }, { "10", "+10 dB" }, { "11", "+11 dB" }, { "12", "+12 dB" }, { NULL, NULL } },
      "0"
   },
   {
      "odin_gfootbal_woofer_cutoff",
      "Woofer Cutoff",
      NULL,
      "The lows of the woofer's channel played with Sound Mode Both and Woofer Only: below that many Hz, or all of it (Off).",
      NULL,
      NULL,
      { { "80", "80 Hz" }, { "120", "120 Hz" }, { "160", "160 Hz" }, { "200", "200 Hz" }, { "off", "Off" }, { NULL, NULL } },
      "120"
   },
   {
      "odin_smashdrv_sound_mode",
      "Sound Mode",
      NULL,
      "Smashing Drive has a woofer, fed by the board's second pair of channels: Main, the speakers alone; Both, the woofer's lows mixed in at Woofer Level, as the desktop plays it; 4.0, four channels, the speakers in front and the woofer's channel in the rear (RetroArch folds them for stereo; a RetroArch without multi-channel sound gets Both); Woofer Only, the woofer alone: its channel's lows below Woofer Cutoff.",
      NULL,
      NULL,
      { { "main", "Main" }, { "mix", "Both" }, { "quad", "4.0" }, { "option", "Woofer Only" }, { NULL, NULL } },
      "mix"
   },
   {
      "odin_smashdrv_option_level",
      "Woofer Level",
      NULL,
      "The woofer of Smashing Drive against the speakers, with Sound Mode Both, 4.0 or Woofer Only.",
      NULL,
      NULL,
      { { "-12", "-12 dB" }, { "-11", "-11 dB" }, { "-10", "-10 dB" }, { "-9", "-9 dB" }, { "-8", "-8 dB" }, { "-7", "-7 dB" }, { "-6", "-6 dB" }, { "-5", "-5 dB" }, { "-4", "-4 dB" }, { "-3", "-3 dB" }, { "-2", "-2 dB" }, { "-1", "-1 dB" }, { "0", "0 dB" }, { "1", "+1 dB" }, { "2", "+2 dB" }, { "3", "+3 dB" }, { "4", "+4 dB" }, { "5", "+5 dB" }, { "6", "+6 dB" }, { "7", "+7 dB" }, { "8", "+8 dB" }, { "9", "+9 dB" }, { "10", "+10 dB" }, { "11", "+11 dB" }, { "12", "+12 dB" }, { NULL, NULL } },
      "0"
   },
   {
      "odin_smashdrv_woofer_cutoff",
      "Woofer Cutoff",
      NULL,
      "The lows of the woofer's channel played with Sound Mode Both and Woofer Only: below that many Hz, or all of it (Off).",
      NULL,
      NULL,
      { { "80", "80 Hz" }, { "120", "120 Hz" }, { "160", "160 Hz" }, { "200", "200 Hz" }, { "off", "Off" }, { NULL, NULL } },
      "120"
   },
   {
      "odin_smashdrv_start_volume",
      "Start Volume",
      NULL,
      "The volume Smashing Drive is set to at each power-on, as an operator would with VOLUME - and VOLUME + (the board keeps none of its own at first).",
      NULL,
      NULL,
      { { "0", "0" }, { "25", "25" }, { "50", "50" }, { "75", "75" }, { "96", "96" }, { NULL, NULL } },
      "75"
   },
   {
      "odin_anisotropy",
      "Anisotropic Filtering",
      NULL,
      "Of the texture filter; Off as on the board.",
      NULL,
      NULL,
      { { "1", "Off" }, { "2", "2x" }, { "4", "4x" }, { "8", "8x" }, { "16", "16x" }, { NULL, NULL } },
      "1"
   },
   {
      "odin_audio_thread",
      "Audio Thread (Restart)",
      NULL,
      "The sound handed to RetroArch's audio thread as the board makes it, instead of in each frame: RetroArch then plays what is buffered while a frame takes long (a shader compiled), where the frames' own sound would stutter. Off by default.",
      NULL,
      NULL,
      { { "disabled", NULL }, { "enabled", NULL }, { NULL, NULL } },
      "disabled"
   },
   { NULL, NULL, NULL, NULL, NULL, NULL, {{0}}, NULL },
};
struct retro_core_options_v2 options_us = {
   option_cats_us,
   option_defs_us
};

/*
 ********************************
 * Language Mapping
 ********************************
*/

#ifndef HAVE_NO_LANGEXTRA
struct retro_core_options_v2 *options_intl[RETRO_LANGUAGE_LAST] = {
   &options_us,       /* RETRO_LANGUAGE_ENGLISH */
   &options_ja,       /* RETRO_LANGUAGE_JAPANESE */
   &options_fr,       /* RETRO_LANGUAGE_FRENCH */
   &options_es,       /* RETRO_LANGUAGE_SPANISH */
   &options_de,       /* RETRO_LANGUAGE_GERMAN */
   &options_it,       /* RETRO_LANGUAGE_ITALIAN */
   &options_nl,       /* RETRO_LANGUAGE_DUTCH */
   &options_pt_br,    /* RETRO_LANGUAGE_PORTUGUESE_BRAZIL */
   &options_pt_pt,    /* RETRO_LANGUAGE_PORTUGUESE_PORTUGAL */
   &options_ru,       /* RETRO_LANGUAGE_RUSSIAN */
   &options_ko,       /* RETRO_LANGUAGE_KOREAN */
   &options_cht,      /* RETRO_LANGUAGE_CHINESE_TRADITIONAL */
   &options_chs,      /* RETRO_LANGUAGE_CHINESE_SIMPLIFIED */
   &options_eo,       /* RETRO_LANGUAGE_ESPERANTO */
   &options_pl,       /* RETRO_LANGUAGE_POLISH */
   &options_vn,       /* RETRO_LANGUAGE_VIETNAMESE */
   &options_ar,       /* RETRO_LANGUAGE_ARABIC */
   &options_el,       /* RETRO_LANGUAGE_GREEK */
   &options_tr,       /* RETRO_LANGUAGE_TURKISH */
   &options_sk,       /* RETRO_LANGUAGE_SLOVAK */
   &options_fa,       /* RETRO_LANGUAGE_PERSIAN */
   &options_he,       /* RETRO_LANGUAGE_HEBREW */
   &options_ast,      /* RETRO_LANGUAGE_ASTURIAN */
   &options_fi,       /* RETRO_LANGUAGE_FINNISH */
   &options_id,       /* RETRO_LANGUAGE_INDONESIAN */
   &options_sv,       /* RETRO_LANGUAGE_SWEDISH */
   &options_uk,       /* RETRO_LANGUAGE_UKRAINIAN */
   &options_cs,       /* RETRO_LANGUAGE_CZECH */
   &options_val,      /* RETRO_LANGUAGE_CATALAN_VALENCIA */
   &options_ca,       /* RETRO_LANGUAGE_CATALAN */
   &options_en,       /* RETRO_LANGUAGE_BRITISH_ENGLISH */
   &options_hu,       /* RETRO_LANGUAGE_HUNGARIAN */
   &options_be,       /* RETRO_LANGUAGE_BELARUSIAN */
   &options_gl,       /* RETRO_LANGUAGE_GALICIAN */
   &options_no,       /* RETRO_LANGUAGE_NORWEGIAN */
   &options_ga,       /* RETRO_LANGUAGE_IRISH */
};
#endif

/*
 ********************************
 * Functions
 ********************************
*/

/* Handles configuration/setting of core options.
 * Should be called as early as possible - ideally inside
 * retro_set_environment(), and no later than retro_load_game()
 * > We place the function body in the header to avoid the
 *   necessity of adding more .c files (i.e. want this to
 *   be as painless as possible for core devs)
 */

static inline void libretro_set_core_options(retro_environment_t environ_cb,
      bool *categories_supported)
{
   unsigned version  = 0;
#ifndef HAVE_NO_LANGEXTRA
   unsigned language = 0;
#endif

   if (!environ_cb || !categories_supported)
      return;

   *categories_supported = false;

   if (!environ_cb(RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION, &version))
      version = 0;

   if (version >= 2)
   {
#ifndef HAVE_NO_LANGEXTRA
      struct retro_core_options_v2_intl core_options_intl;

      core_options_intl.us    = &options_us;
      core_options_intl.local = NULL;

      if (environ_cb(RETRO_ENVIRONMENT_GET_LANGUAGE, &language) &&
          (language < RETRO_LANGUAGE_LAST) && (language != RETRO_LANGUAGE_ENGLISH))
         core_options_intl.local = options_intl[language];

      *categories_supported = environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2_INTL,
            &core_options_intl);
#else
      *categories_supported = environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2,
            &options_us);
#endif
   }
   else
   {
      size_t i, j;
      size_t option_index              = 0;
      size_t num_options               = 0;
      struct retro_core_option_definition
            *option_v1_defs_us         = NULL;
#ifndef HAVE_NO_LANGEXTRA
      size_t num_options_intl          = 0;
      struct retro_core_option_v2_definition
            *option_defs_intl          = NULL;
      struct retro_core_option_definition
            *option_v1_defs_intl       = NULL;
      struct retro_core_options_intl
            core_options_v1_intl;
#endif
      struct retro_variable *variables = NULL;
      char **values_buf                = NULL;

      /* Determine total number of options */
      while (true)
      {
         if (option_defs_us[num_options].key)
            num_options++;
         else
            break;
      }

      if (version >= 1)
      {
         /* Allocate US array */
         option_v1_defs_us = (struct retro_core_option_definition *)
               calloc(num_options + 1, sizeof(struct retro_core_option_definition));

         /* Copy parameters from option_defs_us array */
         for (i = 0; i < num_options; i++)
         {
            struct retro_core_option_v2_definition *option_def_us = &option_defs_us[i];
            struct retro_core_option_value *option_values         = option_def_us->values;
            struct retro_core_option_definition *option_v1_def_us = &option_v1_defs_us[i];
            struct retro_core_option_value *option_v1_values      = option_v1_def_us->values;

            option_v1_def_us->key           = option_def_us->key;
            option_v1_def_us->desc          = option_def_us->desc;
            option_v1_def_us->info          = option_def_us->info;
            option_v1_def_us->default_value = option_def_us->default_value;

            /* Values must be copied individually... */
            while (option_values->value)
            {
               option_v1_values->value = option_values->value;
               option_v1_values->label = option_values->label;

               option_values++;
               option_v1_values++;
            }
         }

#ifndef HAVE_NO_LANGEXTRA
         if (environ_cb(RETRO_ENVIRONMENT_GET_LANGUAGE, &language) &&
             (language < RETRO_LANGUAGE_LAST) && (language != RETRO_LANGUAGE_ENGLISH) &&
             options_intl[language])
            option_defs_intl = options_intl[language]->definitions;

         if (option_defs_intl)
         {
            /* Determine number of intl options */
            while (true)
            {
               if (option_defs_intl[num_options_intl].key)
                  num_options_intl++;
               else
                  break;
            }

            /* Allocate intl array */
            option_v1_defs_intl = (struct retro_core_option_definition *)
                  calloc(num_options_intl + 1, sizeof(struct retro_core_option_definition));

            /* Copy parameters from option_defs_intl array */
            for (i = 0; i < num_options_intl; i++)
            {
               struct retro_core_option_v2_definition *option_def_intl = &option_defs_intl[i];
               struct retro_core_option_value *option_values           = option_def_intl->values;
               struct retro_core_option_definition *option_v1_def_intl = &option_v1_defs_intl[i];
               struct retro_core_option_value *option_v1_values        = option_v1_def_intl->values;

               option_v1_def_intl->key           = option_def_intl->key;
               option_v1_def_intl->desc          = option_def_intl->desc;
               option_v1_def_intl->info          = option_def_intl->info;
               option_v1_def_intl->default_value = option_def_intl->default_value;

               /* Values must be copied individually... */
               while (option_values->value)
               {
                  option_v1_values->value = option_values->value;
                  option_v1_values->label = option_values->label;

                  option_values++;
                  option_v1_values++;
               }
            }
         }

         core_options_v1_intl.us    = option_v1_defs_us;
         core_options_v1_intl.local = option_v1_defs_intl;

         environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_INTL, &core_options_v1_intl);
#else
         environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS, option_v1_defs_us);
#endif
      }
      else
      {
         /* Allocate arrays */
         variables  = (struct retro_variable *)calloc(num_options + 1,
               sizeof(struct retro_variable));
         values_buf = (char **)calloc(num_options, sizeof(char *));

         if (!variables || !values_buf)
            goto error;

         /* Copy parameters from option_defs_us array */
         for (i = 0; i < num_options; i++)
         {
            const char *key                        = option_defs_us[i].key;
            const char *desc                       = option_defs_us[i].desc;
            const char *default_value              = option_defs_us[i].default_value;
            struct retro_core_option_value *values = option_defs_us[i].values;
            size_t buf_len                         = 3;
            size_t default_index                   = 0;

            values_buf[i] = NULL;

            if (desc)
            {
               size_t num_values = 0;

               /* Determine number of values */
               while (true)
               {
                  if (values[num_values].value)
                  {
                     /* Check if this is the default value */
                     if (default_value)
                        if (strcmp(values[num_values].value, default_value) == 0)
                           default_index = num_values;

                     buf_len += strlen(values[num_values].value);
                     num_values++;
                  }
                  else
                     break;
               }

               /* Build values string */
               if (num_values > 0)
               {
                  buf_len += num_values - 1;
                  buf_len += strlen(desc);

                  values_buf[i] = (char *)calloc(buf_len, sizeof(char));
                  if (!values_buf[i])
                     goto error;

                  strcpy(values_buf[i], desc);
                  strcat(values_buf[i], "; ");

                  /* Default value goes first */
                  strcat(values_buf[i], values[default_index].value);

                  /* Add remaining values */
                  for (j = 0; j < num_values; j++)
                  {
                     if (j != default_index)
                     {
                        strcat(values_buf[i], "|");
                        strcat(values_buf[i], values[j].value);
                     }
                  }
               }
            }

            variables[option_index].key   = key;
            variables[option_index].value = values_buf[i];
            option_index++;
         }

         /* Set variables */
         environ_cb(RETRO_ENVIRONMENT_SET_VARIABLES, variables);
      }

error:
      /* Clean up */

      if (option_v1_defs_us)
      {
         free(option_v1_defs_us);
         option_v1_defs_us = NULL;
      }

#ifndef HAVE_NO_LANGEXTRA
      if (option_v1_defs_intl)
      {
         free(option_v1_defs_intl);
         option_v1_defs_intl = NULL;
      }
#endif

      if (values_buf)
      {
         for (i = 0; i < num_options; i++)
         {
            if (values_buf[i])
            {
               free(values_buf[i]);
               values_buf[i] = NULL;
            }
         }

         free(values_buf);
         values_buf = NULL;
      }

      if (variables)
      {
         free(variables);
         variables = NULL;
      }
   }
}

#ifdef __cplusplus
}
#endif

#endif
