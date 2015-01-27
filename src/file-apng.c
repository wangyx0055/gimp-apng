/* GIMP - The GNU Image Manipulation Program
 * Copyright (C) 1995 Spencer Kimball and Peter Mattis
 *
 *   Animated Portable Network Graphics (APNG) plug-in
 *
 *   Copyright 2010 Daisuke Nishikawa (daisuken@users.sourceforge.net)
 *
 *   Original work of the PNG plug-in is:
 *   Copyright 1997-1998 Michael Sweet (mike@easysw.com) and
 *   Daniel Skarda (0rfelyus@atrey.karlin.mff.cuni.cz).
 *   and 1999-2000 Nick Lamb (njl195@zepler.org.uk)
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Contents:
 *
 *   main()                      - Main entry - just call gimp_main()...
 *   query()                     - Respond to a plug-in query...
 *   run()                       - Run the plug-in...
 *   load_image()                - Load a PNG image into a new image window.
 *   read_frame()                - Read a PNG frame into a layer.
 *   respin_cmap()               - Re-order a Gimp colormap for PNG tRNS
 *   save_image()                - Save the specified image to a PNG file.
 *   write_frame()               - Write the specified layer to a PNG frame.
 *   parse_delay_tag()           - Parse delay tag.
 *   parse_ms_tag()              - Parse milli seconds tag.
 *   parse_dispose_op_tag()      - Parse dispose_op tag.
 *   save_compression_callback() - Update the image compression level.
 *   save_interlace_update()     - Update the interlacing option.
 *   save_dialog()               - Pop up the save dialog.
 *
 * Revision History:
 *
 *   see ChangeLog
 */

#include "config.h"

#include <stdlib.h>
#include <errno.h>

#include <glib/gstdio.h>

#include <libgimp/gimp.h>
#include <libgimp/gimpui.h>

#include <png.h>                /* PNG library definitions */

#include "plugin-intl.h"


/*
 * Constants...
 */

#define LOAD_PROC              "file-apng-load"
#define SAVE_PROC              "file-apng-save"
#define SAVE2_PROC             "file-apng-save2"
#define SAVE_DEFAULTS_PROC     "file-apng-save-defaults"
#define GET_DEFAULTS_PROC      "file-apng-get-defaults"
#define SET_DEFAULTS_PROC      "file-apng-set-defaults"
#define PLUG_IN_BINARY         "file-apng"

#define PLUG_IN_VERSION        "0.1.0 - 25 April 2010"
#define SCALE_WIDTH            125

#define DEFAULT_GAMMA          2.20

#define PNG_DEFAULTS_PARASITE  "apng-save-defaults"

/*
 * Structures...
 */

typedef struct
{
  gboolean  interlaced;
  gboolean  bkgd;
  gboolean  gama;
  gboolean  offs;
  gboolean  phys;
  gboolean  time;
  gboolean  comment;
  gboolean  save_transp_pixels;
  gint      compression_level;
#if defined(PNG_APNG_SUPPORTED)
  gboolean  as_animation;
  gboolean  first_frame_is_hidden;
  guint32   num_plays;
  guint16   delay_num;
  guint16   delay_den;
  guint8    dispose_op;
  guint8    blend_op;
#endif
}
PngSaveVals;

typedef struct
{
  gboolean   run;

  GtkWidget *interlaced;
  GtkWidget *bkgd;
  GtkWidget *gama;
  GtkWidget *offs;
  GtkWidget *phys;
  GtkWidget *time;
  GtkWidget *comment;
  GtkWidget *save_transp_pixels;
  GtkObject *compression_level;
#if defined(PNG_APNG_SUPPORTED)
  GtkWidget *as_animation;
  GtkWidget *first_frame_is_hidden;
  GtkObject *num_plays;
#endif
}
PngSaveGui;


/*
 * Local functions...
 */

static void      query                     (void);
static void      run                       (const gchar      *name,
                                            gint              nparams,
                                            const GimpParam  *param,
                                            gint             *nreturn_vals,
                                            GimpParam       **return_vals);

static gint32    load_image                (const gchar      *filename,
                                            gboolean          interactive,
                                            GError          **error);
static gboolean  save_image                (const gchar      *filename,
                                            gint32            image_ID,
                                            gint32            drawable_ID,
                                            gint32            orig_image_ID,
                                            GError          **error);

static void      read_frame                (gint32            layer,
                                            int               bpp,
                                            int               empty,
                                            int               trns,
                                            guchar           *alpha,
                                            png_structp       pp,
                                            png_infop         info,
                                            png_uint_32       frame_width,
                                            png_uint_32       frame_height,
                                            png_uint_32       frame_x_offset,
                                            png_uint_32       frame_y_offset,
                                            GError          **error);
static gboolean  write_frame               (gint32            drawable_ID,
                                            gint              bpp,
                                            guchar            red,
                                            guchar            green,
                                            guchar            blue,
                                            guchar           *remap,
                                            gboolean          as_animation,
                                            png_structp       pp,
                                            png_infop         info,
                                            png_uint_32       offx,
                                            png_uint_32       offy,
                                            png_uint_16       frame_delay_num,
                                            png_uint_16       frame_delay_den,
                                            png_byte          frame_dispose_op,
                                            png_byte          frame_blend_op,
                                            GError          **error);
#if defined(PNG_APNG_SUPPORTED)
static void      parse_delay_tag           (png_uint_16      *delay_num,
                                            png_uint_16      *delay_den,
                                            const gchar      *str);
static gint      parse_ms_tag              (const gchar      *str);
static gint      parse_dispose_op_tag      (const gchar      *str);
#endif

static void      respin_cmap               (png_structp       pp,
                                            png_infop         info,
                                            guchar           *remap,
                                            gint32            image_ID,
                                            GimpDrawable     *drawable);

static gboolean  save_dialog               (gint32            image_ID,
                                            gboolean          alpha);

static void      save_dialog_response      (GtkWidget        *widget,
                                            gint              response_id,
                                            gpointer          data);

static gboolean  ia_has_transparent_pixels (GimpDrawable     *drawable);

static gint      find_unused_ia_color      (GimpDrawable     *drawable,
                                            gint             *colors);

static void      load_defaults             (void);
static void      save_defaults             (void);
static void      load_gui_defaults         (PngSaveGui       *pg);

#if ((GIMP_MAJOR_VERSION < 2) || (GIMP_MAJOR_VERSION == 2 && GIMP_MINOR_VERSION < 7))
static GtkWidget * gimp_export_dialog_new              (const gchar  *format_name,
                                                        const gchar  *role,
                                                        const gchar  *help_id);
static GtkWidget * gimp_export_dialog_get_content_area (GtkWidget    *dialog);
#endif

/*
 * Globals...
 */

const GimpPlugInInfo PLUG_IN_INFO =
{
  NULL,
  NULL,
  query,
  run
};

static const PngSaveVals defaults =
{
  FALSE,
  TRUE,
  FALSE,
  FALSE,
  TRUE,
  TRUE,
  TRUE,
  TRUE,
  9,
#if defined(PNG_APNG_SUPPORTED)
  FALSE,
  FALSE,
  0,
  1,
  100,
  PNG_DISPOSE_OP_NONE,
  PNG_BLEND_OP_OVER
#endif
};

static PngSaveVals pngvals;


/*
 * 'main()' - Main entry - just call gimp_main()...
 */

MAIN ()


/*
 * 'query()' - Respond to a plug-in query...
 */

static void
query (void)
{
  static const GimpParamDef load_args[] =
  {
    { GIMP_PDB_INT32,  "run-mode",     "Interactive, non-interactive" },
    { GIMP_PDB_STRING, "filename",     "The name of the file to load" },
    { GIMP_PDB_STRING, "raw-filename", "The name of the file to load" }
  };
  static const GimpParamDef load_return_vals[] =
  {
    { GIMP_PDB_IMAGE, "image", "Output image" }
  };

#define COMMON_SAVE_ARGS \
    { GIMP_PDB_INT32,    "run-mode",     "Interactive, non-interactive" }, \
    { GIMP_PDB_IMAGE,    "image",        "Input image"                  }, \
    { GIMP_PDB_DRAWABLE, "drawable",     "Drawable to save"             }, \
    { GIMP_PDB_STRING,   "filename",     "The name of the file to save the image in"}, \
    { GIMP_PDB_STRING,   "raw-filename", "The name of the file to save the image in"}

#define OLD_CONFIG_ARGS \
    { GIMP_PDB_INT32, "interlace",   "Use Adam7 interlacing?"            }, \
    { GIMP_PDB_INT32, "compression", "Deflate Compression factor (0--9)" }, \
    { GIMP_PDB_INT32, "bkgd",        "Write bKGD chunk?"                 }, \
    { GIMP_PDB_INT32, "gama",        "Write gAMA chunk?"                 }, \
    { GIMP_PDB_INT32, "offs",        "Write oFFs chunk?"                 }, \
    { GIMP_PDB_INT32, "phys",        "Write pHYs chunk?"                 }, \
    { GIMP_PDB_INT32, "time",        "Write tIME chunk?"                 }

#define FULL_CONFIG_ARGS \
    OLD_CONFIG_ARGS,                                                        \
    { GIMP_PDB_INT32, "comment", "Write comment?"                        }, \
    { GIMP_PDB_INT32, "svtrans", "Preserve color of transparent pixels?" }

  static const GimpParamDef save_args[] =
  {
    COMMON_SAVE_ARGS,
    OLD_CONFIG_ARGS
  };

  static const GimpParamDef save_args2[] =
  {
    COMMON_SAVE_ARGS,
    FULL_CONFIG_ARGS
  };

  static const GimpParamDef save_args_defaults[] =
  {
    COMMON_SAVE_ARGS
  };

  static const GimpParamDef save_get_defaults_return_vals[] =
  {
    FULL_CONFIG_ARGS
  };

  static const GimpParamDef save_args_set_defaults[] =
  {
    FULL_CONFIG_ARGS
  };

  gchar *help_path;
  gchar *help_uri;

  gimp_plugin_domain_register (GETTEXT_PACKAGE, LOCALEDIR);

  help_path = g_build_filename (DATADIR, "help", NULL);
  help_uri = g_filename_to_uri (help_path, NULL, NULL);
  g_free (help_path);

  gimp_plugin_help_register ("http://sourceforge.net/projects/gimp-apng/",
                             help_uri);
  g_free (help_uri);

  gimp_install_procedure (LOAD_PROC,
                          "Loads files in PNG+APNG file format",
                          "This plug-in loads Portable Network Graphics "
                          "(PNG+APNG) files.",
                          "Daisuke Nishikawa <daisuken@users.sourceforge.net>, "
                          "Michael Sweet <mike@easysw.com>, "
                          "Daniel Skarda <0rfelyus@atrey.karlin.mff.cuni.cz>",
                          "Daisuke Nishikawa <daisuken@users.sourceforge.net>, "
                          "Michael Sweet <mike@easysw.com>, "
                          "Daniel Skarda <0rfelyus@atrey.karlin.mff.cuni.cz>, "
                          "Nick Lamb <njl195@zepler.org.uk>",
                          PLUG_IN_VERSION,
                          N_("PNG+APNG image"),
                          NULL,
                          GIMP_PLUGIN,
                          G_N_ELEMENTS (load_args),
                          G_N_ELEMENTS (load_return_vals),
                          load_args, load_return_vals);

  gimp_register_file_handler_mime (LOAD_PROC, "image/png");
  gimp_register_magic_load_handler (LOAD_PROC,
                                    "png", "", "0,string,\211PNG\r\n\032\n");

  gimp_install_procedure (SAVE_PROC,
                          "Saves files in PNG+APNG file format",
                          "This plug-in saves Portable Network Graphics "
                          "(PNG+APNG) files.",
                          "Daisuke Nishikawa <daisuken@users.sourceforge.net>, "
                          "Michael Sweet <mike@easysw.com>, "
                          "Daniel Skarda <0rfelyus@atrey.karlin.mff.cuni.cz>",
                          "Daisuke Nishikawa <daisuken@users.sourceforge.net>, "
                          "Michael Sweet <mike@easysw.com>, "
                          "Daniel Skarda <0rfelyus@atrey.karlin.mff.cuni.cz>, "
                          "Nick Lamb <njl195@zepler.org.uk>",
                          PLUG_IN_VERSION,
                          N_("PNG+APNG image"),
                          "RGB*,GRAY*,INDEXED*",
                          GIMP_PLUGIN,
                          G_N_ELEMENTS (save_args), 0,
                          save_args, NULL);

  gimp_install_procedure (SAVE2_PROC,
                          "Saves files in PNG+APNG file format",
                          "This plug-in saves Portable Network Graphics "
                          "(PNG+APNG) files. "
                          "This procedure adds 2 extra parameters to "
                          "file-png-save that allows to control whether "
                          "image comments are saved and whether transparent "
                          "pixels are saved or nullified.",
                          "Daisuke Nishikawa <daisuken@users.sourceforge.net>, "
                          "Michael Sweet <mike@easysw.com>, "
                          "Daniel Skarda <0rfelyus@atrey.karlin.mff.cuni.cz>",
                          "Daisuke Nishikawa <daisuken@users.sourceforge.net>, "
                          "Michael Sweet <mike@easysw.com>, "
                          "Daniel Skarda <0rfelyus@atrey.karlin.mff.cuni.cz>, "
                          "Nick Lamb <njl195@zepler.org.uk>",
                          PLUG_IN_VERSION,
                          N_("PNG+APNG image"),
                          "RGB*,GRAY*,INDEXED*",
                          GIMP_PLUGIN,
                          G_N_ELEMENTS (save_args2), 0,
                          save_args2, NULL);

  gimp_install_procedure (SAVE_DEFAULTS_PROC,
                          "Saves files in PNG file format",
                          "This plug-in saves Portable Network Graphics (PNG) "
                          "files, using the default settings stored as "
                          "a parasite.",
                          "Daisuke Nishikawa <daisuken@users.sourceforge.net>, "
                          "Michael Sweet <mike@easysw.com>, "
                          "Daniel Skarda <0rfelyus@atrey.karlin.mff.cuni.cz>",
                          "Daisuke Nishikawa <daisuken@users.sourceforge.net>, "
                          "Michael Sweet <mike@easysw.com>, "
                          "Daniel Skarda <0rfelyus@atrey.karlin.mff.cuni.cz>, "
                          "Nick Lamb <njl195@zepler.org.uk>",
                          PLUG_IN_VERSION,
                          N_("PNG+APNG image"),
                          "RGB*,GRAY*,INDEXED*",
                          GIMP_PLUGIN,
                          G_N_ELEMENTS (save_args_defaults), 0,
                          save_args_defaults, NULL);

  gimp_register_file_handler_mime (SAVE_DEFAULTS_PROC, "image/png");
  gimp_register_save_handler (SAVE_DEFAULTS_PROC, "png", "");

  gimp_install_procedure (GET_DEFAULTS_PROC,
                          "Get the current set of defaults used by the "
                          "PNG file save plug-in",
                          "This procedure returns the current set of "
                          "defaults stored as a parasite for the PNG "
                          "save plug-in. "
                          "These defaults are used to seed the UI, by the "
                          "file_png_save_defaults procedure, and by "
                          "gimp_file_save when it detects to use PNG.",
                          "Daisuke Nishikawa <daisuken@users.sourceforge.net>, "
                          "Michael Sweet <mike@easysw.com>, "
                          "Daniel Skarda <0rfelyus@atrey.karlin.mff.cuni.cz>",
                          "Daisuke Nishikawa <daisuken@users.sourceforge.net>, "
                          "Michael Sweet <mike@easysw.com>, "
                          "Daniel Skarda <0rfelyus@atrey.karlin.mff.cuni.cz>, "
                          "Nick Lamb <njl195@zepler.org.uk>",
                          PLUG_IN_VERSION,
                          NULL,
                          NULL,
                          GIMP_PLUGIN,
                          0, G_N_ELEMENTS (save_get_defaults_return_vals),
                          NULL, save_get_defaults_return_vals);

  gimp_install_procedure (SET_DEFAULTS_PROC,
                          "Set the current set of defaults used by the "
                          "PNG file save plug-in",
                          "This procedure set the current set of defaults "
                          "stored as a parasite for the PNG save plug-in. "
                          "These defaults are used to seed the UI, by the "
                          "file_png_save_defaults procedure, and by "
                          "gimp_file_save when it detects to use PNG.",
                          "Daisuke Nishikawa <daisuken@users.sourceforge.net>, "
                          "Michael Sweet <mike@easysw.com>, "
                          "Daniel Skarda <0rfelyus@atrey.karlin.mff.cuni.cz>",
                          "Daisuke Nishikawa <daisuken@users.sourceforge.net>, "
                          "Michael Sweet <mike@easysw.com>, "
                          "Daniel Skarda <0rfelyus@atrey.karlin.mff.cuni.cz>, "
                          "Nick Lamb <njl195@zepler.org.uk>",
                          PLUG_IN_VERSION,
                          NULL,
                          NULL,
                          GIMP_PLUGIN,
                          G_N_ELEMENTS (save_args_set_defaults), 0,
                          save_args_set_defaults, NULL);
}


/*
 * 'run()' - Run the plug-in...
 */

static void
run (const gchar      *name,
     gint              nparams,
     const GimpParam  *param,
     gint             *nreturn_vals,
     GimpParam       **return_vals)
{
  static GimpParam  values[10];
  GimpRunMode       run_mode;
  GimpPDBStatusType status = GIMP_PDB_SUCCESS;
  gint32            image_ID;
  gint32            drawable_ID;
  gint32            orig_image_ID;
  GimpExportReturn  export = GIMP_EXPORT_CANCEL;
  GError           *error  = NULL;

  INIT_I18N ();

  *nreturn_vals = 1;
  *return_vals = values;

  values[0].type          = GIMP_PDB_STATUS;
  values[0].data.d_status = GIMP_PDB_EXECUTION_ERROR;

  if (strcmp (name, LOAD_PROC) == 0)
    {
      run_mode = param[0].data.d_int32;

      image_ID = load_image (param[1].data.d_string,
                             run_mode == GIMP_RUN_INTERACTIVE, &error);

      if (image_ID != -1)
        {
          *nreturn_vals = 2;
          values[1].type = GIMP_PDB_IMAGE;
          values[1].data.d_image = image_ID;
        }
      else
        {
          status = GIMP_PDB_EXECUTION_ERROR;
        }
    }
  else if (strcmp (name, SAVE_PROC)  == 0 ||
           strcmp (name, SAVE2_PROC) == 0 ||
           strcmp (name, SAVE_DEFAULTS_PROC) == 0)
    {
      gboolean alpha;

      run_mode    = param[0].data.d_int32;
      image_ID    = orig_image_ID = param[1].data.d_int32;
      drawable_ID = param[2].data.d_int32;

      load_defaults ();

      switch (run_mode)
        {
        case GIMP_RUN_INTERACTIVE:
          gimp_ui_init (PLUG_IN_BINARY, FALSE);

          /*
           * Possibly retrieve data...
           */
          gimp_get_data (SAVE_PROC, &pngvals);

          alpha = gimp_drawable_has_alpha (drawable_ID);

          /*
           * If the image has no transparency, then there is usually
           * no need to save a bKGD chunk.  For more information, see:
           * http://bugzilla.gnome.org/show_bug.cgi?id=92395
           */
          if (! alpha)
            pngvals.bkgd = FALSE;

          /*
           * Then acquire information with a dialog...
           */
          if (! save_dialog (orig_image_ID, alpha))
            status = GIMP_PDB_CANCEL;
          break;

        case GIMP_RUN_NONINTERACTIVE:
          /*
           * Make sure all the arguments are there!
           */
          if (nparams != 5)
            {
              if (nparams != 12 && nparams != 14)
                {
                  status = GIMP_PDB_CALLING_ERROR;
                }
              else
                {
                  pngvals.interlaced        = param[5].data.d_int32;
                  pngvals.compression_level = param[6].data.d_int32;
                  pngvals.bkgd              = param[7].data.d_int32;
                  pngvals.gama              = param[8].data.d_int32;
                  pngvals.offs              = param[9].data.d_int32;
                  pngvals.phys              = param[10].data.d_int32;
                  pngvals.time              = param[11].data.d_int32;

                  if (nparams == 14)
                    {
                      pngvals.comment            = param[12].data.d_int32;
                      pngvals.save_transp_pixels = param[13].data.d_int32;
                    }
                  else
                    {
                      pngvals.comment            = TRUE;
                      pngvals.save_transp_pixels = TRUE;
                    }

                  if (pngvals.compression_level < 0 ||
                      pngvals.compression_level > 9)
                    {
                      status = GIMP_PDB_CALLING_ERROR;
                    }
                }
            }
          break;

        case GIMP_RUN_WITH_LAST_VALS:
          /*
           * Possibly retrieve data...
           */
          gimp_get_data (SAVE_PROC, &pngvals);
          break;

        default:
          break;
        }

      /*  eventually export the image */
      switch (run_mode)
        {
        case GIMP_RUN_INTERACTIVE:
        case GIMP_RUN_WITH_LAST_VALS:
          {
            GimpExportCapabilities capabilities =
              GIMP_EXPORT_CAN_HANDLE_RGB |
              GIMP_EXPORT_CAN_HANDLE_GRAY |
              GIMP_EXPORT_CAN_HANDLE_INDEXED |
              GIMP_EXPORT_CAN_HANDLE_ALPHA;

#if defined(PNG_APNG_SUPPORTED)
            if (pngvals.as_animation)
              capabilities |= GIMP_EXPORT_CAN_HANDLE_LAYERS;
#endif

            export = gimp_export_image (&image_ID, &drawable_ID, NULL,
                                        capabilities);

            if (export == GIMP_EXPORT_CANCEL)
              {
                *nreturn_vals = 1;
                values[0].data.d_status = GIMP_PDB_CANCEL;
                return;
              }
          }
          break;
        default:
          break;
        }

      if (status == GIMP_PDB_SUCCESS)
        {
          if (save_image (param[3].data.d_string,
                          image_ID, drawable_ID, orig_image_ID, &error))
            {
              gimp_set_data (SAVE_PROC, &pngvals, sizeof (pngvals));
            }
          else
            {
              status = GIMP_PDB_EXECUTION_ERROR;
            }
        }

      if (export == GIMP_EXPORT_EXPORT)
        gimp_image_delete (image_ID);
    }
  else if (strcmp (name, GET_DEFAULTS_PROC) == 0)
    {
      load_defaults ();

      *nreturn_vals = 10;

#define SET_VALUE(index, field)        G_STMT_START { \
 values[(index)].type = GIMP_PDB_INT32;        \
 values[(index)].data.d_int32 = pngvals.field; \
} G_STMT_END

      SET_VALUE (1, interlaced);
      SET_VALUE (2, compression_level);
      SET_VALUE (3, bkgd);
      SET_VALUE (4, gama);
      SET_VALUE (5, offs);
      SET_VALUE (6, phys);
      SET_VALUE (7, time);
      SET_VALUE (8, comment);
      SET_VALUE (9, save_transp_pixels);

#undef SET_VALUE
    }
  else if (strcmp (name, SET_DEFAULTS_PROC) == 0)
    {
      if (nparams == 9)
        {
          pngvals.interlaced          = param[0].data.d_int32;
          pngvals.compression_level   = param[1].data.d_int32;
          pngvals.bkgd                = param[2].data.d_int32;
          pngvals.gama                = param[3].data.d_int32;
          pngvals.offs                = param[4].data.d_int32;
          pngvals.phys                = param[5].data.d_int32;
          pngvals.time                = param[6].data.d_int32;
          pngvals.comment             = param[7].data.d_int32;
          pngvals.save_transp_pixels  = param[8].data.d_int32;

          save_defaults ();
        }
      else
        {
          status = GIMP_PDB_CALLING_ERROR;
        }
    }
  else
    {
      status = GIMP_PDB_CALLING_ERROR;
    }

  if (status != GIMP_PDB_SUCCESS && error)
    {
      *nreturn_vals = 2;
      values[1].type          = GIMP_PDB_STRING;
      values[1].data.d_string = error->message;
    }

  values[0].data.d_status = status;
}


struct read_error_data
{
  GimpPixelRgn *pixel_rgn;       /* Pixel region for layer */
  guchar       *pixel;           /* Pixel data */
  GimpDrawable *drawable;        /* Drawable for layer */
  guint32       width;           /* png_infop->width */
  guint32       height;          /* png_infop->height */
  gint          bpp;             /* Bytes per pixel */
  gint          tile_height;     /* Height of tile in GIMP */
  gint          begin;           /* Beginning tile row */
  gint          end;             /* Ending tile row */
  gint          num;             /* Number of rows to load */
};

static void
on_read_error (png_structp png_ptr, png_const_charp error_msg)
{
  struct read_error_data *error_data = png_get_error_ptr (png_ptr);
  gint                    begin;
  gint                    end;
  gint                    num;

  g_warning (_("Error loading PNG file: %s"), error_msg);

  /* Flush the current half-read row of tiles */

  gimp_pixel_rgn_set_rect (error_data->pixel_rgn, error_data->pixel, 0,
                                error_data->begin, error_data->drawable->width,
                                error_data->num);

  /* Fill the rest of the rows of tiles with 0s */

  memset (error_data->pixel, 0,
          error_data->tile_height * error_data->width * error_data->bpp);

  for (begin = error_data->begin + error_data->tile_height,
        end = error_data->end + error_data->tile_height;
        begin < error_data->height;
        begin += error_data->tile_height, end += error_data->tile_height)
    {
      if (end > error_data->height)
        end = error_data->height;

      num = end - begin;

      gimp_pixel_rgn_set_rect (error_data->pixel_rgn, error_data->pixel, 0,
                                begin,
                                error_data->drawable->width, num);
    }

  longjmp (png_jmpbuf (png_ptr), 1);
}

/*
 * 'load_image()' - Load a PNG image into a new image window.
 */

static gint32
load_image (const gchar  *filename,
            gboolean      interactive,
            GError      **error)
{
  int i,                        /* Looping var */
    trns,                       /* Transparency present */
    bpp,                        /* Bytes per pixel */
    image_type,                 /* Type of image */
    layer_type,                 /* Type of drawable/layer */
    empty,                      /* Number of fully transparent indices */
    num;                        /* Number of rows to load */
  FILE *fp;                     /* File pointer */
  volatile gint32 image = -1;   /* Image -- preserved against setjmp() */
  gint32 layer;                 /* Layer */
  gint offset_x = 0;            /* Offset x from origin */
  gint offset_y = 0;            /* Offset y from origin */
  png_structp pp;               /* PNG read pointer */
  png_infop info;               /* PNG info pointers */
  guchar alpha[256],            /* Index -> Alpha */
   *alpha_ptr;                  /* Temporary pointer */

  png_textp  text;
  gint       num_texts;

  pp = png_create_read_struct (PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
  info = png_create_info_struct (pp);

  if (setjmp (png_jmpbuf (pp)))
    {
      g_set_error (error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
                   _("Error while reading '%s'. File corrupted?"),
                   gimp_filename_to_utf8 (filename));
      return image;
    }

  /* initialise image here, thus avoiding compiler warnings */

  image = -1;

  /*
   * Open the file and initialize the PNG read "engine"...
   */

  fp = g_fopen (filename, "rb");

  if (fp == NULL)
    {
      g_set_error (error, G_FILE_ERROR, g_file_error_from_errno (errno),
                   _("Could not open '%s' for reading: %s"),
                   gimp_filename_to_utf8 (filename), g_strerror (errno));
      return -1;
    }

  png_init_io (pp, fp);

  gimp_progress_init_printf (_("Opening '%s'"),
                             gimp_filename_to_utf8 (filename));

  /*
   * Get the image dimensions and create the image...
   */

  png_read_info (pp, info);

  /*
   * Latest attempt, this should be my best yet :)
   */

  if (png_get_bit_depth (pp, info) == 16)
    {
      png_set_strip_16 (pp);
    }

  if (png_get_color_type (pp, info) == PNG_COLOR_TYPE_GRAY &&
      png_get_bit_depth (pp, info) < 8)
    {
      png_set_expand (pp);
    }

  if (png_get_color_type (pp, info) == PNG_COLOR_TYPE_PALETTE &&
      png_get_bit_depth (pp, info) < 8)
    {
      png_set_packing (pp);
    }

  /*
   * Expand G+tRNS to GA, RGB+tRNS to RGBA
   */

  if (png_get_color_type (pp, info) != PNG_COLOR_TYPE_PALETTE &&
      png_get_valid (pp, info, PNG_INFO_tRNS))
    {
      png_set_expand (pp);
    }

  /*
   * Special handling for INDEXED + tRNS (transparency palette)
   */

  if (png_get_valid (pp, info, PNG_INFO_tRNS) &&
      png_get_color_type (pp, info) == PNG_COLOR_TYPE_PALETTE)
    {
      png_get_tRNS (pp, info, &alpha_ptr, &num, NULL);
      /* Copy the existing alpha values from the tRNS chunk */
      for (i = 0; i < num; ++i)
        alpha[i] = alpha_ptr[i];
      /* And set any others to fully opaque (255)  */
      for (i = num; i < 256; ++i)
        alpha[i] = 255;
      trns = 1;
    }
  else
    {
      trns = 0;
    }

  /*
   * Update the info structures after the transformations take effect
   */

  png_read_update_info (pp, info);

  switch (png_get_color_type (pp, info))
    {
    case PNG_COLOR_TYPE_RGB:           /* RGB */
      bpp = 3;
      image_type = GIMP_RGB;
      layer_type = GIMP_RGB_IMAGE;
      break;

    case PNG_COLOR_TYPE_RGB_ALPHA:     /* RGBA */
      bpp = 4;
      image_type = GIMP_RGB;
      layer_type = GIMP_RGBA_IMAGE;
      break;

    case PNG_COLOR_TYPE_GRAY:          /* Grayscale */
      bpp = 1;
      image_type = GIMP_GRAY;
      layer_type = GIMP_GRAY_IMAGE;
      break;

    case PNG_COLOR_TYPE_GRAY_ALPHA:    /* Grayscale + alpha */
      bpp = 2;
      image_type = GIMP_GRAY;
      layer_type = GIMP_GRAYA_IMAGE;
      break;

    case PNG_COLOR_TYPE_PALETTE:       /* Indexed */
      bpp = 1;
      image_type = GIMP_INDEXED;
      layer_type = GIMP_INDEXED_IMAGE;
      break;

    default:                           /* Aie! Unknown type */
      g_set_error (error, 0, 0,
                   _("Unknown color model in PNG file '%s'."),
                   gimp_filename_to_utf8 (filename));
      return -1;
    }

  image = gimp_image_new (png_get_image_width (pp, info),
                          png_get_image_height (pp, info), image_type);
  if (image == -1)
    {
      g_set_error (error, 0, 0,
                   "Could not create new image for '%s': %s",
                   gimp_filename_to_utf8 (filename), gimp_get_pdb_error ());
      return -1;
    }

  /*
   * Find out everything we can about the image resolution
   * This is only practical with the new 1.0 APIs, I'm afraid
   * due to a bug in libpng-1.0.6, see png-implement for details
   */

  if (png_get_valid (pp, info, PNG_INFO_gAMA))
    {
      GimpParasite *parasite;
      gchar         buf[G_ASCII_DTOSTR_BUF_SIZE];
      gdouble       gamma;

      png_get_gAMA (pp, info, &gamma);

      g_ascii_dtostr (buf, sizeof (buf), gamma);

      parasite = gimp_parasite_new ("gamma",
                                    GIMP_PARASITE_PERSISTENT,
                                    strlen (buf) + 1, buf);
      gimp_image_parasite_attach (image, parasite);
      gimp_parasite_free (parasite);
    }

  if (png_get_valid (pp, info, PNG_INFO_oFFs))
    {
      offset_x = png_get_x_offset_pixels (pp, info);
      offset_y = png_get_y_offset_pixels (pp, info);

      if ((abs (offset_x) > png_get_image_width (pp, info)) ||
          (abs (offset_y) > png_get_image_height (pp, info)))
        {
          if (interactive)
            g_message (_("The PNG file specifies an offset that caused "
                         "the layer to be positioned outside the image."));
        }
    }

  if (png_get_valid (pp, info, PNG_INFO_pHYs))
    {
      png_uint_32  xres;
      png_uint_32  yres;
      gint         unit_type;

      if (png_get_pHYs (pp, info, &xres, &yres, &unit_type))
        {
          switch (unit_type)
            {
            case PNG_RESOLUTION_UNKNOWN:
              {
                gdouble image_xres, image_yres;

                gimp_image_get_resolution (image, &image_xres, &image_yres);

                if (xres > yres)
                  image_xres = image_yres * (gdouble) xres / (gdouble) yres;
                else
                  image_yres = image_xres * (gdouble) yres / (gdouble) xres;

                gimp_image_set_resolution (image, image_xres, image_yres);
              }
              break;

            case PNG_RESOLUTION_METER:
              gimp_image_set_resolution (image,
                                         (gdouble) xres * 0.0254,
                                         (gdouble) yres * 0.0254);
              break;

            default:
              break;
            }
        }

    }

  gimp_image_set_filename (image, filename);

  /*
   * Load the colormap as necessary...
   */

  empty = 0; /* by default assume no full transparent palette entries */

  if (png_get_color_type (pp, info) & PNG_COLOR_MASK_PALETTE)
    {
      png_colorp palette;
      int num_palette;

      if (png_get_PLTE (pp, info, &palette, &num_palette))
        {
          if (png_get_valid (pp, info, PNG_INFO_tRNS))
            {
              for (empty = 0; empty < 256 && alpha[empty] == 0; ++empty)
                /* Calculates number of fully transparent "empty" entries */;

              /*  keep at least one entry  */
              empty = MIN (empty, num_palette - 1);

              gimp_image_set_colormap (image, (guchar *) (palette + empty),
                                       num_palette - empty);
            }
          else
            {
              gimp_image_set_colormap (image, (guchar *) palette,
                                       num_palette);
            }
        }
    }

#if defined(PNG_APNG_SUPPORTED)
  if (png_get_valid (pp, info, PNG_INFO_acTL))
    {
      gchar       *framename;
      gchar       *framename_ptr;
      png_uint_32  num_frames;
      png_uint_32  num_plays;
      png_byte     is_hidden;
      png_uint_32  frame;
      png_byte     previous_dispose_op = PNG_DISPOSE_OP_NONE;

      num_frames = png_get_num_frames(pp, info);
      num_plays = png_get_num_plays(pp, info);
      is_hidden = png_get_first_frame_is_hidden(pp, info);
      for (frame = 0; frame < num_frames; frame++)
        {
          gint         delay = -1;
          png_uint_32  frame_width;
          png_uint_32  frame_height;
          png_uint_32  frame_x_offset = 0;
          png_uint_32  frame_y_offset = 0;
          png_byte     frame_dispose_op;

          png_read_frame_head(pp, info);
          if (png_get_valid (pp, info, PNG_INFO_fcTL))
            {
              png_uint_16  frame_delay_num;
              png_uint_16  frame_delay_den;
              png_byte     frame_blend_op;

              png_get_next_frame_fcTL(pp, info,
                                      &frame_width, &frame_height,
                                      &frame_x_offset, &frame_y_offset,
                                      &frame_delay_num, &frame_delay_den,
                                      &frame_dispose_op, &frame_blend_op);
              if (frame_delay_den == 0)
                frame_delay_den = 100;

              delay = frame_delay_num * 1000 / frame_delay_den;
            }
          else
            {
              /*
               * The first frame doesn't have an fcTL so it's expected 
               * to be hidden, but we'll extract it anyway
               */

              frame_width = png_get_image_width (pp, info);
              frame_height = png_get_image_height (pp, info);
              frame_dispose_op = PNG_DISPOSE_OP_NONE;
            }

          if (frame == 0)
            {
              if (delay < 0)
                framename = g_strdup (_("Background"));
              else
                framename = g_strdup_printf (_("Background (%d%s)"),
                                             delay, "ms");

              if (frame_dispose_op == PNG_DISPOSE_OP_PREVIOUS)
                frame_dispose_op = PNG_DISPOSE_OP_BACKGROUND;
            }
          else
            {
              gimp_progress_set_text_printf (_("Opening '%s' (frame %d)"),
                                             gimp_filename_to_utf8 (filename),
                                             frame);
              gimp_progress_pulse ();

              if (delay < 0)
                framename = g_strdup_printf (_("Frame %d"), frame + 1);
              else
                framename = g_strdup_printf (_("Frame %d (%d%s)"), frame + 1,
                                             delay, "ms");
            }

          switch (previous_dispose_op)
            {
            case PNG_DISPOSE_OP_NONE:
              framename_ptr = framename;
              framename = g_strconcat (framename, " (combine)", NULL);
              g_free (framename_ptr);
              break;
            case PNG_DISPOSE_OP_BACKGROUND:
              framename_ptr = framename;
              framename = g_strconcat (framename, " (replace)", NULL);
              g_free (framename_ptr);
              break;
            case PNG_DISPOSE_OP_PREVIOUS: /* For now, cannot handle this */
              framename_ptr = framename;
              framename = g_strconcat (framename, " (combine) (!)", NULL);
              g_free (framename_ptr);
              break;
            default:
              g_message ("dispose_op got corrupted.");
              break;
            }
          previous_dispose_op = frame_dispose_op;

          layer = gimp_layer_new (image, framename, frame_width, frame_height,
                                  layer_type, 100, GIMP_NORMAL_MODE);
          g_free (framename);
          gimp_image_add_layer (image, layer, 0);

          if (offset_x != 0 && offset_y != 0)
            gimp_layer_set_offsets (layer, offset_x, offset_y);

          gimp_layer_translate (layer,
                                (gint) frame_x_offset, (gint) frame_y_offset);

          read_frame (layer, bpp, empty, trns, alpha, pp, info,
                      frame_width, frame_height,
                      frame_x_offset, frame_y_offset, error);
        }
    }
  else
#endif
    {
      /*
       * Create the "background" layer to hold the image...
       */

      layer = gimp_layer_new (image, _("Background"),
                              png_get_image_width (pp, info),
                              png_get_image_height (pp, info),
                              layer_type, 100, GIMP_NORMAL_MODE);
      gimp_image_add_layer (image, layer, 0);

      if (offset_x != 0 && offset_y != 0)
        gimp_layer_set_offsets (layer, offset_x, offset_y);

      read_frame (layer, bpp, empty, trns, alpha, pp, info,
                  png_get_image_width (pp, info),
                  png_get_image_height (pp, info),
                  0, 0, error);
    }

  png_read_end (pp, info);

  if (png_get_text (pp, info, &text, &num_texts))
    {
      gchar *comment = NULL;

      for (i = 0; i < num_texts && !comment; i++, text++)
        {
          if (text->key == NULL || strcmp (text->key, "Comment"))
            continue;

          if (text->text_length > 0)   /*  tEXt  */
            {
              comment = g_convert (text->text, -1,
                                   "UTF-8", "ISO-8859-1",
                                   NULL, NULL, NULL);
            }
          else if (g_utf8_validate (text->text, -1, NULL))
            {                          /*  iTXt  */
              comment = g_strdup (text->text);
            }
        }

      if (comment && *comment)
        {
          GimpParasite *parasite;

          parasite = gimp_parasite_new ("gimp-comment",
                                        GIMP_PARASITE_PERSISTENT,
                                        strlen (comment) + 1, comment);
          gimp_image_parasite_attach (image, parasite);
          gimp_parasite_free (parasite);
        }

      g_free (comment);
    }

#if defined(PNG_iCCP_SUPPORTED)
  /*
   * Get the iCCP (colour profile) chunk, if any, and attach it as
   * a parasite
   */

  {
    png_uint_32 proflen;
    png_charp   profname, profile;
    int         profcomp;

    if (png_get_iCCP (pp, info, &profname, &profcomp, &profile, &proflen))
      {
        GimpParasite *parasite;

        parasite = gimp_parasite_new ("icc-profile",
                                      GIMP_PARASITE_PERSISTENT |
                                      GIMP_PARASITE_UNDOABLE,
                                      proflen, profile);

        gimp_image_parasite_attach (image, parasite);
        gimp_parasite_free (parasite);

        if (profname)
          {
            gchar *tmp = g_convert (profname, strlen (profname),
                                    "ISO-8859-1", "UTF-8", NULL, NULL, NULL);

            if (tmp)
              {
                parasite = gimp_parasite_new ("icc-profile-name",
                                              GIMP_PARASITE_PERSISTENT |
                                              GIMP_PARASITE_UNDOABLE,
                                              strlen (tmp), tmp);
                gimp_image_parasite_attach (image, parasite);
                gimp_parasite_free (parasite);

                g_free (tmp);
              }
          }
      }
  }
#endif

  /*
   * Done with the file...
   */

  png_destroy_read_struct (&pp, &info, NULL);
  fclose (fp);

  return image;
}

/*
 * 'read_frame()' - Read a PNG frame into a layer.
 */

static void
read_frame (gint32        layer,
            int           bpp,
            int           empty,
            int           trns,
            guchar       *alpha,
            png_structp   pp,
            png_infop     info,
            png_uint_32   frame_width,
            png_uint_32   frame_height,
            png_uint_32   frame_x_offset,
            png_uint_32   frame_y_offset,
            GError      **error)
{
  int i,                        /* Looping var */
    num_passes,                 /* Number of interlace passes in file */
    pass,                       /* Current pass in file */
    tile_height,                /* Height of tile in GIMP */
    begin,                      /* Beginning tile row */
    end,                        /* Ending tile row */
    num;                        /* Number of rows to load */
  GimpDrawable *drawable;       /* Drawable for layer */
  GimpPixelRgn pixel_rgn;       /* Pixel region for layer */
  guchar **pixels,              /* Pixel rows */
   *pixel;                      /* Pixel data */
  struct read_error_data
   error_data;

  /*
   * Get the drawable and set the pixel region for our load...
   */

  drawable = gimp_drawable_get (layer);

  gimp_pixel_rgn_init (&pixel_rgn, drawable, 0, 0, drawable->width,
                       drawable->height, TRUE, FALSE);

  /*
   * Temporary buffer...
   */

  tile_height = gimp_tile_height ();
  pixel = g_new0 (guchar, tile_height * frame_width * bpp);
  pixels = g_new (guchar *, tile_height);

  for (i = 0; i < tile_height; i++)
    pixels[i] = pixel + frame_width * png_get_channels(pp, info) * i;

  /* Install our own error handler to handle incomplete PNG files better */
  error_data.drawable    = drawable;
  error_data.pixel       = pixel;
  error_data.tile_height = tile_height;
  error_data.width       = frame_width;
  error_data.height      = frame_height;
  error_data.bpp         = bpp;
  error_data.pixel_rgn   = &pixel_rgn;

  png_set_error_fn (pp, &error_data, on_read_error, NULL);

  /*
   * Turn on interlace handling... libpng returns just 1 (ie single pass)
   * if the image is not interlaced
   */

  num_passes = png_set_interlace_handling (pp);

  for (pass = 0; pass < num_passes; pass++)
    {
      /*
       * This works if you are only reading one row at a time...
       */

      for (begin = 0, end = tile_height;
           begin < frame_height; begin += tile_height, end += tile_height)
        {
          if (end > frame_height)
            end = frame_height;

          num = end - begin;

          if (pass != 0)        /* to handle interlaced PiNGs */
            gimp_pixel_rgn_get_rect (&pixel_rgn, pixel, 0, begin,
                                     drawable->width, num);

          error_data.begin = begin;
          error_data.end   = end;
          error_data.num   = num;

          png_read_rows (pp, pixels, NULL, num);

          gimp_pixel_rgn_set_rect (&pixel_rgn, pixel, 0, begin,
                                   drawable->width, num);

          memset (pixel, 0, tile_height * frame_width * bpp);

          gimp_progress_update (((gdouble) pass +
                                 (gdouble) end / (gdouble) frame_height) /
                                (gdouble) num_passes);
        }
    }

  /* Switch back to default error handler */
  png_set_error_fn (pp, NULL, NULL, NULL);

  g_free (pixel);
  g_free (pixels);

  if (trns)
    {
      gimp_layer_add_alpha (layer);
      drawable = gimp_drawable_get (layer);
      gimp_pixel_rgn_init (&pixel_rgn, drawable, 0, 0, drawable->width,
                           drawable->height, TRUE, FALSE);

      pixel = g_new (guchar, tile_height * drawable->width * 2); /* bpp == 1 */

      for (begin = 0, end = tile_height;
           begin < drawable->height; begin += tile_height, end += tile_height)
        {
          if (end > drawable->height)
            end = drawable->height;
          num = end - begin;

          gimp_pixel_rgn_get_rect (&pixel_rgn, pixel, 0, begin,
                                   drawable->width, num);

          for (i = 0; i < tile_height * drawable->width; ++i)
            {
              pixel[i * 2 + 1] = alpha[pixel[i * 2]];
              pixel[i * 2] -= empty;
            }

          gimp_pixel_rgn_set_rect (&pixel_rgn, pixel, 0, begin,
                                   drawable->width, num);
        }

      g_free (pixel);
    }

  /*
   * Update the display...
   */

  gimp_drawable_flush (drawable);
  gimp_drawable_detach (drawable);
}


/*
 * 'save_image ()' - Save the specified image to a PNG file.
 */

static gboolean
save_image (const gchar  *filename,
            gint32        image_ID,
            gint32        drawable_ID,
            gint32        orig_image_ID,
            GError      **error)
{
  gint i,                       /* Looping vars */
    bpp = 0,                    /* Bytes per pixel */
    drawable_type;              /* Type of drawable/layer */
  FILE *fp;                     /* File pointer */
  GimpDrawable *drawable;       /* Drawable for layer */
  png_structp pp;               /* PNG read pointer */
  png_infop info;               /* PNG info pointer */
  gint num_colors;              /* Number of colors in colormap */
  gint offx, offy;              /* Drawable offsets from origin */
  gdouble xres, yres;           /* GIMP resolution (dpi) */
  gint32 *layers;               /* Layers */
  gint nlayers;                 /* Number of Layers */
  int color_type;               /* PNG color type */
  int bit_depth;                /* PNG bit depth */
  png_color_16 background;      /* Background color */
  png_time mod_time;            /* Modification time (ie NOW) */
  guchar red, green, blue;      /* Used for palette background */
  time_t cutime;                /* Time since epoch */
  struct tm *gmt;               /* GMT broken down */

  guchar remap[256];            /* Re-mapping for the palette */

  png_textp  text = NULL;

  if (pngvals.comment)
    {
      GimpParasite *parasite;
      gsize text_length = 0;

      parasite = gimp_image_parasite_find (orig_image_ID, "gimp-comment");
      if (parasite)
        {
          gchar *comment = g_strndup (gimp_parasite_data (parasite),
                                      gimp_parasite_data_size (parasite));

          gimp_parasite_free (parasite);

          text = g_new0 (png_text, 1);
          text->key         = "Comment";

#ifdef PNG_iTXt_SUPPORTED

          text->compression = PNG_ITXT_COMPRESSION_NONE;
          text->text        = comment;
          text->itxt_length = strlen (comment);

#else

          text->compression = PNG_TEXT_COMPRESSION_NONE;
          text->text        = g_convert (comment, -1,
                                         "ISO-8859-1", "UTF-8",
                                         NULL, &text_length,
                                         NULL);
          text->text_length = text_length;

#endif

          if (!text->text)
            {
              g_free (text);
              text = NULL;
            }
        }
    }

  pp = png_create_write_struct (PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
  info = png_create_info_struct (pp);

  if (setjmp (png_jmpbuf (pp)))
    {
      g_set_error (error, 0, 0,
                   _("Error while saving '%s'. Could not save image."),
                   gimp_filename_to_utf8 (filename));
      return FALSE;
    }

  if (text)
    png_set_text (pp, info, text, 1);

  /*
   * Open the file and initialize the PNG write "engine"...
   */

  fp = g_fopen (filename, "wb");
  if (fp == NULL)
    {
      g_set_error (error, G_FILE_ERROR, g_file_error_from_errno (errno),
                   _("Could not open '%s' for writing: %s"),
                   gimp_filename_to_utf8 (filename), g_strerror (errno));
      return FALSE;
    }

  png_init_io (pp, fp);

  gimp_progress_init_printf (_("Saving '%s'"),
                             gimp_filename_to_utf8 (filename));

  /*
   * Get the drawable for the current image...
   */

  layers = gimp_image_get_layers (image_ID, &nlayers);
  drawable = gimp_drawable_get (layers[0]);
  drawable_type = gimp_drawable_type (layers[0]);

  /*
   * Set color type and remember bytes per pixel count
   */

  switch (drawable_type)
    {
    case GIMP_RGB_IMAGE:
      color_type = PNG_COLOR_TYPE_RGB;
      bpp = 3;
      break;

    case GIMP_RGBA_IMAGE:
      color_type = PNG_COLOR_TYPE_RGB_ALPHA;
      bpp = 4;
      break;

    case GIMP_GRAY_IMAGE:
      color_type = PNG_COLOR_TYPE_GRAY;
      bpp = 1;
      break;

    case GIMP_GRAYA_IMAGE:
      color_type = PNG_COLOR_TYPE_GRAY_ALPHA;
      bpp = 2;
      break;

    case GIMP_INDEXED_IMAGE:
      color_type = PNG_COLOR_TYPE_PALETTE;
      bpp = 1;
      break;

    case GIMP_INDEXEDA_IMAGE:
      color_type = PNG_COLOR_TYPE_PALETTE;
      bpp = 2;
      break;

    default:
      g_set_error (error, 0, 0, "Image type can't be saved as PNG");
      return FALSE;
    }

  /*
   * Fix bit depths for (possibly) smaller colormap images
   */

  bit_depth = 8;
  if (color_type == PNG_COLOR_TYPE_PALETTE)
    {
      if (num_colors <= 2)
        bit_depth = 1;
      else if (num_colors <= 4)
        bit_depth = 2;
      else if (num_colors <= 16)
        bit_depth = 4;
      /* otherwise the default is fine */
    }

  /*
   * Set the image dimensions, bit depth, interlacing and compression
   */

  png_set_IHDR(pp, info, drawable->width, drawable->height,
               bit_depth, color_type,
               pngvals.interlaced, PNG_COMPRESSION_TYPE_BASE,
               PNG_FILTER_TYPE_BASE);
  png_set_compression_level (pp, pngvals.compression_level);

  /*
   * Initialise remap[]
   */
  for (i = 0; i < 256; i++)
    remap[i] = i;

  if (color_type == PNG_COLOR_TYPE_PALETTE)
    {
      if (bpp == 1)
        {
          png_colorp palette;

          palette = (png_colorp) gimp_image_get_colormap (image_ID, &num_colors),
          png_set_PLTE (pp, info, palette, num_colors);
        }
      else
        {
          /* fix up transparency */
          respin_cmap (pp, info, remap, image_ID, drawable);
        }
    }

  /* All this stuff is optional extras, if the user is aiming for smallest
     possible file size she can turn them all off */

  if (pngvals.bkgd)
    {
      GimpRGB color;

      gimp_context_get_background (&color);
      gimp_rgb_get_uchar (&color, &red, &green, &blue);

      background.index = 0;
      background.red = red;
      background.green = green;
      background.blue = blue;
      background.gray = gimp_rgb_luminance_uchar (&color);
      png_set_bKGD (pp, info, &background);
    }
  else
    {
      /* used to save_transp_pixels */
      red = green = blue = 0;
    }

  if (pngvals.gama)
    {
      GimpParasite *parasite;
      gdouble       gamma = 1.0 / DEFAULT_GAMMA;

      parasite = gimp_image_parasite_find (orig_image_ID, "gamma");
      if (parasite)
        {
          gamma = g_ascii_strtod (gimp_parasite_data (parasite), NULL);
          gimp_parasite_free (parasite);
        }

      png_set_gAMA (pp, info, gamma);
    }

  offx = 0;
  offy = 0;
  if (pngvals.offs)
    {
      gimp_drawable_offsets (drawable_ID, &offx, &offy);
      if (offx != 0 || offy != 0)
        {
          png_set_oFFs (pp, info, offx, offy, PNG_OFFSET_PIXEL);
        }
    }

  if (pngvals.phys)
    {
      gimp_image_get_resolution (orig_image_ID, &xres, &yres);
      png_set_pHYs (pp, info, RINT (xres / 0.0254), RINT (yres / 0.0254),
                    PNG_RESOLUTION_METER);
    }

  if (pngvals.time)
    {
      cutime = time (NULL);     /* time right NOW */
      gmt = gmtime (&cutime);

      mod_time.year = gmt->tm_year + 1900;
      mod_time.month = gmt->tm_mon + 1;
      mod_time.day = gmt->tm_mday;
      mod_time.hour = gmt->tm_hour;
      mod_time.minute = gmt->tm_min;
      mod_time.second = gmt->tm_sec;
      png_set_tIME (pp, info, &mod_time);
    }

#if defined(PNG_iCCP_SUPPORTED)
  {
    GimpParasite *profile_parasite;
    gchar        *profile_name = NULL;

    profile_parasite = gimp_image_parasite_find (orig_image_ID, "icc-profile");

    if (profile_parasite)
      {
        GimpParasite *parasite = gimp_image_parasite_find (orig_image_ID,
                                                           "icc-profile-name");
        if (parasite)
          profile_name = g_convert (gimp_parasite_data (parasite),
                                    gimp_parasite_data_size (parasite),
                                    "UTF-8", "ISO-8859-1", NULL, NULL, NULL);

        png_set_iCCP (pp, info,
                      profile_name ? profile_name : "ICC profile", 0,
                      (gchar *) gimp_parasite_data (profile_parasite),
                      gimp_parasite_data_size (profile_parasite));

        g_free (profile_name);
      }
    else if (! pngvals.gama)
      {
        png_set_sRGB (pp, info, 0);
      }
  }
#endif

#if defined(PNG_APNG_SUPPORTED)
  if (nlayers > 1)
    {
      png_uint_32 num_plays;
      png_byte first_frame_is_hidden;

      num_plays = pngvals.num_plays;
      first_frame_is_hidden = pngvals.first_frame_is_hidden;
      png_set_acTL (pp, info, nlayers, num_plays);
      png_set_first_frame_is_hidden (pp, info, first_frame_is_hidden);
    }
#endif

  png_write_info (pp, info);

  /*
   * Convert unpacked pixels to packed if necessary
   */

  if (color_type == PNG_COLOR_TYPE_PALETTE && bit_depth < 8)
    png_set_packing (pp);

#if defined(PNG_APNG_SUPPORTED)
  if (nlayers > 1)
    {
      for (i = nlayers - 1; i >= 0; i--)
        {
          gchar        *layer_name;
          png_uint_16   frame_delay_num;
          png_uint_16   frame_delay_den;
          png_byte      frame_dispose_op;
          png_byte      frame_blend_op;

          layer_name = gimp_drawable_get_name (layers[i]);
          parse_delay_tag (&frame_delay_num, &frame_delay_den, layer_name);
          frame_dispose_op = parse_dispose_op_tag (layer_name);
          frame_blend_op = pngvals.blend_op;
          write_frame (layers[i], bpp, red, green, blue, remap, TRUE,
                       pp, info, offx, offy,
                       frame_delay_num, frame_delay_den,
                       frame_dispose_op, frame_blend_op,
                       error);
        }
    }
  else
#endif
    {
      write_frame (layers[0], bpp, red, green, blue, remap, FALSE,
                   pp, info, offx, offy, 0, 0, 0, 0, error);
    }

  png_write_end (pp, info);
  png_destroy_write_struct (&pp, &info);

  /*
   * Done with the file...
   */

  if (text)
    {
      g_free (text->text);
      g_free (text);
    }

  fclose (fp);

  return TRUE;
}

/*
 * 'write_frame ()' - Write the specified frame.
 */

static gboolean
write_frame (gint32        drawable_ID,
             gint          bpp,
             guchar        red,
             guchar        green,
             guchar        blue,
             guchar       *remap,
             gboolean      as_animation,
             png_structp   pp,
             png_infop     info,
             png_uint_32   offx,
             png_uint_32   offy,
             png_uint_16   frame_delay_num,
             png_uint_16   frame_delay_den,
             png_byte      frame_dispose_op,
             png_byte      frame_blend_op,
             GError      **error)
{
  gint i, k,                    /* Looping vars */
    num_passes,                 /* Number of interlace passes in file */
    pass,                       /* Current pass in file */
    tile_height,                /* Height of tile in GIMP */
    begin,                      /* Beginning tile row */
    end,                        /* Ending tile row */
    num;                        /* Number of rows to load */
  GimpDrawable *drawable;       /* Drawable for layer */
  GimpPixelRgn pixel_rgn;       /* Pixel region for layer */
  guchar **pixels,              /* Pixel rows */
   *fixed,                      /* Fixed-up pixel data */
   *pixel;                      /* Pixel data */

  /*
   * Get the drawable for the current image...
   */

  drawable = gimp_drawable_get (drawable_ID);

  /*
   * Turn on interlace handling...
   */

  if (pngvals.interlaced)
    num_passes = png_set_interlace_handling (pp);
  else
    num_passes = 1;

  /*
   * Allocate memory for "tile_height" rows and save the image...
   */

  tile_height = gimp_tile_height ();
  pixel = g_new (guchar, tile_height * drawable->width * bpp);
  pixels = g_new (guchar *, tile_height);

  for (i = 0; i < tile_height; i++)
    pixels[i] = pixel + drawable->width * bpp * i;

  gimp_pixel_rgn_init (&pixel_rgn, drawable, 0, 0, drawable->width,
                       drawable->height, FALSE, FALSE);

#if defined(PNG_APNG_SUPPORTED)
  if (as_animation)
    {
      gint         offset_x;
      gint         offset_y;
      png_uint_32  frame_x_offset;
      png_uint_32  frame_y_offset;

      gimp_drawable_offsets (drawable_ID, &offset_x, &offset_y);
      frame_x_offset = offset_x - offx;
      frame_y_offset = offset_y - offy;
      png_write_frame_head (pp, info, (png_bytepp)&pixel,
                            drawable->width, drawable->height,
                            frame_x_offset, frame_y_offset,
                            frame_delay_num, frame_delay_den,
                            frame_dispose_op, frame_blend_op);
    }
#endif

  for (pass = 0; pass < num_passes; pass++)
    {
      /* This works if you are only writing one row at a time... */
      for (begin = 0, end = tile_height;
           begin < drawable->height; begin += tile_height, end += tile_height)
        {
          if (end > drawable->height)
            end = drawable->height;

          num = end - begin;

          gimp_pixel_rgn_get_rect (&pixel_rgn, pixel, 0, begin,
                                   drawable->width, num);
          /*if we are with a RGBA image and have to pre-multiply the alpha channel */
          if (bpp == 4 && ! pngvals.save_transp_pixels)
            {
              for (i = 0; i < num; ++i)
                {
                  fixed = pixels[i];
                  for (k = 0; k < drawable->width; ++k)
                    {
                      gint aux = k << 2;

                      if (! fixed[aux + 3])
                        {
                          fixed[aux + 0] = red;
                          fixed[aux + 1] = green;
                          fixed[aux + 2] = blue;
                        }
                    }
                }
            }

          /* If we're dealing with a paletted image with
           * transparency set, write out the remapped palette */
          if (png_get_valid (pp, info, PNG_INFO_tRNS))
            {
              guchar inverse_remap[256];

              for (i = 0; i < 256; i++)
                inverse_remap[ remap[i] ] = i;

              for (i = 0; i < num; ++i)
                {
                  fixed = pixels[i];
                  for (k = 0; k < drawable->width; ++k)
                    {
                      fixed[k] = (fixed[k*2+1] > 127) ?
                                 inverse_remap[ fixed[k*2] ] :
                                 0;
                    }
                }
            }
          /* Otherwise if we have a paletted image and transparency
           * couldn't be set, we ignore the alpha channel */
          else if (png_get_valid (pp, info, PNG_INFO_PLTE) && bpp == 2)
            {
              for (i = 0; i < num; ++i)
                {
                  fixed = pixels[i];
                  for (k = 0; k < drawable->width; ++k)
                    {
                      fixed[k] = fixed[k * 2];
                    }
                }
            }

          png_write_rows (pp, pixels, num);

          gimp_progress_update (((double) pass + (double) end /
                                 (double) png_get_image_height (pp, info)) /
                                (double) num_passes);
        }
    }

  g_free (pixel);
  g_free (pixels);

#if defined(PNG_APNG_SUPPORTED)
  if (as_animation)
    {
      png_write_frame_tail (pp, info);
    }
#endif

  return TRUE;
}

#if defined(PNG_APNG_SUPPORTED)
static void
parse_delay_tag (png_uint_16 *delay_num,
                 png_uint_16 *delay_den,
                 const gchar *str)
{
  gint delay;
  gint n;

  delay = parse_ms_tag (str);
  if (delay < 0)
    {
      *delay_num = pngvals.delay_num;
      *delay_den = pngvals.delay_den;
      return;
    }

  for (n = 1000; n > 0; n /= 10)
    {
      if ((delay % n) == 0)
        break;
    }

  *delay_num = delay / n;
  *delay_den = 1000 / n;
}

static int
parse_ms_tag (const gchar *str)
{
  gint sum = 0;
  gint offset = 0;
  gint length;

  length = strlen(str);

find_another_bra:

  while ((offset < length) && (str[offset] != '('))
    offset++;

  if (offset >= length)
    return(-1);

  if (! g_ascii_isdigit (str[++offset]))
    goto find_another_bra;

  do
    {
      sum *= 10;
      sum += str[offset] - '0';
      offset++;
    }
  while ((offset < length) && (g_ascii_isdigit (str[offset])));

  if (length - offset <= 2)
    return(-3);

  if ((g_ascii_toupper (str[offset]) != 'M')
      || (g_ascii_toupper (str[offset+1]) != 'S'))
    return -4;

  return sum;
}

static gint
parse_dispose_op_tag (const gchar *str)
{
  gint offset = 0;
  gint length;

  length = strlen(str);

  while ((offset + 9) <= length)
    {
      if (strncmp(&str[offset], "(combine)", 9) == 0)
        return PNG_DISPOSE_OP_NONE;
      if (strncmp(&str[offset], "(replace)", 9) == 0)
        return PNG_DISPOSE_OP_BACKGROUND;
      offset++;
    }

  return pngvals.dispose_op;
}
#endif

static gboolean
ia_has_transparent_pixels (GimpDrawable *drawable)
{
  GimpPixelRgn  pixel_rgn;
  gpointer      pr;
  guchar       *pixel_row;
  gint          row, col;
  guchar       *pixel;

  gimp_pixel_rgn_init (&pixel_rgn, drawable, 0, 0,
                       drawable->width, drawable->height, FALSE, FALSE);

  for (pr = gimp_pixel_rgns_register (1, &pixel_rgn);
       pr != NULL;
       pr = gimp_pixel_rgns_process (pr))
    {
      pixel_row = pixel_rgn.data;
      for (row = 0; row < pixel_rgn.h; row++)
        {
          pixel = pixel_row;
          for (col = 0; col < pixel_rgn.w; col++)
            {
              if (pixel[1] <= 127)
                return TRUE;
              pixel += 2;
            }
          pixel_row += pixel_rgn.rowstride;
        }
    }

  return FALSE;
}

/* Try to find a color in the palette which isn't actually
 * used in the image, so that we can use it as the transparency
 * index. Taken from gif.c */
static gint
find_unused_ia_color (GimpDrawable *drawable,
                      gint         *colors)
{
  gboolean      ix_used[256];
  gboolean      trans_used = FALSE;
  GimpPixelRgn  pixel_rgn;
  gpointer      pr;
  gint          row, col;
  gint          i;

  for (i = 0; i < *colors; i++)
    ix_used[i] = FALSE;

  gimp_pixel_rgn_init (&pixel_rgn, drawable, 0, 0,
                       drawable->width, drawable->height, FALSE, FALSE);

  for (pr = gimp_pixel_rgns_register (1, &pixel_rgn);
       pr != NULL;
       pr = gimp_pixel_rgns_process (pr))
    {
      const guchar *pixel_row = pixel_rgn.data;

      for (row = 0; row < pixel_rgn.h; row++)
        {
          const guchar *pixel = pixel_row;

          for (col = 0; col < pixel_rgn.w; col++)
            {
              if (pixel[1] > 127)
                ix_used[ pixel[0] ] = TRUE;
              else
                trans_used = TRUE;

              pixel += 2;
            }

          pixel_row += pixel_rgn.rowstride;
        }
    }

  /* If there is no transparency, ignore alpha. */
  if (trans_used == FALSE)
    return -1;

  for (i = 0; i < *colors; i++)
    {
      if (ix_used[i] == FALSE)
        return i;
    }

  /* Couldn't find an unused color index within the number of
     bits per pixel we wanted.  Will have to increment the number
     of colors in the image and assign a transparent pixel there. */
  if ((*colors) < 256)
    {
      (*colors)++;

      return (*colors) - 1;
    }

  return -1;
}


static void
respin_cmap (png_structp   pp,
             png_infop     info,
             guchar       *remap,
             gint32        image_ID,
             GimpDrawable *drawable)
{
  static const guchar trans[] = { 0 };

  gint          colors;
  guchar       *before;

  before = gimp_image_get_colormap (image_ID, &colors);

  /*
   * Make sure there is something in the colormap.
   */
  if (colors == 0)
    {
      before = g_new0 (guchar, 3);
      colors = 1;
    }

  /* Try to find an entry which isn't actually used in the
     image, for a transparency index. */

  if (ia_has_transparent_pixels (drawable))
    {
      gint transparent = find_unused_ia_color (drawable, &colors);

      if (transparent != -1)        /* we have a winner for a transparent
                                     * index - do like gif2png and swap
                                     * index 0 and index transparent */
        {
          png_color palette[256];
          gint      i;

          png_set_tRNS (pp, info, (png_bytep) trans, 1, NULL);

          /* Transform all pixels with a value = transparent to
           * 0 and vice versa to compensate for re-ordering in palette
           * due to png_set_tRNS() */

          remap[0] = transparent;
          for (i = 1; i <= transparent; i++)
            remap[i] = i - 1;

          /* Copy from index 0 to index transparent - 1 to index 1 to
           * transparent of after, then from transparent+1 to colors-1
           * unchanged, and finally from index transparent to index 0. */

          for (i = 0; i < colors; i++)
            {
              palette[i].red = before[3 * remap[i]];
              palette[i].green = before[3 * remap[i] + 1];
              palette[i].blue = before[3 * remap[i] + 2];
            }

          png_set_PLTE (pp, info, palette, colors);
        }
      else
        {
          /* Inform the user that we couldn't losslessly save the
           * transparency & just use the full palette */
          g_message (_("Couldn't losslessly save transparency, "
                       "saving opacity instead."));
          png_set_PLTE (pp, info, (png_colorp) before, colors);
        }
    }
  else
    {
      png_set_PLTE (pp, info, (png_colorp) before, colors);
    }

}

static GtkWidget *
toggle_button_init (GtkBuilder  *builder,
                    const gchar *name,
                    gboolean     initial_value,
                    gboolean    *value_pointer)
{
  GtkWidget *toggle = NULL;

  toggle = GTK_WIDGET (gtk_builder_get_object (builder, name));
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (toggle), initial_value);
  g_signal_connect (toggle, "toggled",
                    G_CALLBACK (gimp_toggle_button_update),
                    value_pointer);

  return toggle;
}

static gboolean
save_dialog (gint32    image_ID,
             gboolean  alpha)
{
  PngSaveGui    pg;
  GtkWidget    *dialog;
  GtkBuilder   *builder;
  gchar        *ui_file;
  GimpParasite *parasite;
  GError       *error = NULL;

  /* Dialog init */
  dialog = gimp_export_dialog_new (_("PNG"), PLUG_IN_BINARY, SAVE_PROC);
  g_signal_connect (dialog, "response",
                    G_CALLBACK (save_dialog_response),
                    &pg);
  g_signal_connect (dialog, "destroy",
                    G_CALLBACK (gtk_main_quit),
                    NULL);

  /* GtkBuilder init */
  builder = gtk_builder_new ();
  ui_file = g_build_filename (DATADIR, "ui/plug-ins/plug-in-file-apng.ui",
                              NULL);

  if (! gtk_builder_add_from_file (builder, ui_file, &error))
    {
      gchar *display_name = g_filename_display_name (ui_file);

      g_printerr (_("Error loading UI file '%s': %s"),
                  display_name, error ? error->message : "???");

      g_free (display_name);
    }

  g_free (ui_file);

  /* Main vbox */
  gtk_container_add (GTK_CONTAINER (gimp_export_dialog_get_content_area (dialog)),
                     GTK_WIDGET (gtk_builder_get_object (builder, "main-vbox")));

  /* Toggles */
  pg.interlaced = toggle_button_init (builder, "interlace",
                                      pngvals.interlaced,
                                      &pngvals.interlaced);
  pg.bkgd = toggle_button_init (builder, "save-background-color",
                                pngvals.bkgd,
                                &pngvals.bkgd);
  pg.gama = toggle_button_init (builder, "save-gamma",
                                pngvals.gama,
                                &pngvals.gama);
  pg.offs = toggle_button_init (builder, "save-layer-offset",
                                pngvals.offs,
                                &pngvals.offs);
  pg.phys = toggle_button_init (builder, "save-resolution",
                                pngvals.phys,
                                &pngvals.phys);
  pg.time = toggle_button_init (builder, "save-creation-time",
                                pngvals.time,
                                &pngvals.time);
#if defined(PNG_APNG_SUPPORTED)
  pg.as_animation = toggle_button_init (builder, "as-animation",
                                        pngvals.as_animation,
                                        &pngvals.as_animation);
  pg.first_frame_is_hidden = toggle_button_init (builder, "first-frame-is-hidden",
                                                 pngvals.first_frame_is_hidden,
                                                 &pngvals.first_frame_is_hidden);
#endif

  /* Comment toggle */
  parasite = gimp_image_parasite_find (image_ID, "gimp-comment");
  pg.comment =
    toggle_button_init (builder, "save-comment",
                        pngvals.comment && parasite != NULL,
                        &pngvals.comment);
  gtk_widget_set_sensitive (pg.comment, parasite != NULL);
  gimp_parasite_free (parasite);

  /* Transparent pixels toggle */
  pg.save_transp_pixels =
    toggle_button_init (builder,
                        "save-transparent-pixels",
                        alpha && pngvals.save_transp_pixels,
                        &pngvals.save_transp_pixels);
  gtk_widget_set_sensitive (pg.save_transp_pixels, alpha);

  /* Compression level scale */
  pg.compression_level =
    GTK_OBJECT (gtk_builder_get_object (builder, "compression-level"));
  g_signal_connect (pg.compression_level, "value-changed",
                    G_CALLBACK (gimp_int_adjustment_update),
                    &pngvals.compression_level);

#if defined(PNG_APNG_SUPPORTED)
  /* Number of plays */
  pg.num_plays =
    GTK_OBJECT (gtk_builder_get_object (builder, "num_plays"));
  g_signal_connect (pg.num_plays, "value-changed",
                    G_CALLBACK (gimp_int_adjustment_update),
                    &pngvals.num_plays);
#endif

  /* Load/save defaults buttons */
  g_signal_connect_swapped (gtk_builder_get_object (builder, "load-defaults"),
                            "clicked",
                            G_CALLBACK (load_gui_defaults),
                            &pg);

  g_signal_connect_swapped (gtk_builder_get_object (builder, "save-defaults"),
                            "clicked",
                            G_CALLBACK (save_defaults),
                            &pg);

  /* Show dialog and run */
  gtk_widget_show (dialog);

  pg.run = FALSE;

  gtk_main ();

  return pg.run;
}

static void
save_dialog_response (GtkWidget *widget,
                      gint       response_id,
                      gpointer   data)
{
  PngSaveGui *pg = data;

  switch (response_id)
    {
    case GTK_RESPONSE_OK:
      pg->run = TRUE;

    default:
      gtk_widget_destroy (widget);
      break;
    }
}

static void
load_defaults (void)
{
  GimpParasite *parasite;

  parasite = gimp_parasite_find (PNG_DEFAULTS_PARASITE);

  if (parasite)
    {
      gchar        *def_str;
      PngSaveVals   tmpvals;
      gint          num_fields;

      def_str = g_strndup (gimp_parasite_data (parasite),
                           gimp_parasite_data_size (parasite));

      gimp_parasite_free (parasite);

      num_fields = sscanf (def_str, "%d %d %d %d %d %d %d %d %d",
                           &tmpvals.interlaced,
                           &tmpvals.bkgd,
                           &tmpvals.gama,
                           &tmpvals.offs,
                           &tmpvals.phys,
                           &tmpvals.time,
                           &tmpvals.comment,
                           &tmpvals.save_transp_pixels,
                           &tmpvals.compression_level);

      g_free (def_str);

      if (num_fields == 9)
        {
          memcpy (&pngvals, &tmpvals, sizeof (tmpvals));
          return;
        }
    }

  memcpy (&pngvals, &defaults, sizeof (defaults));
}

static void
save_defaults (void)
{
  GimpParasite *parasite;
  gchar        *def_str;

  def_str = g_strdup_printf ("%d %d %d %d %d %d %d %d %d",
                             pngvals.interlaced,
                             pngvals.bkgd,
                             pngvals.gama,
                             pngvals.offs,
                             pngvals.phys,
                             pngvals.time,
                             pngvals.comment,
                             pngvals.save_transp_pixels,
                             pngvals.compression_level);

  parasite = gimp_parasite_new (PNG_DEFAULTS_PARASITE,
                                GIMP_PARASITE_PERSISTENT,
                                strlen (def_str), def_str);

  gimp_parasite_attach (parasite);

  gimp_parasite_free (parasite);
  g_free (def_str);
}

static void
load_gui_defaults (PngSaveGui *pg)
{
  load_defaults ();

#if ((GTK_MAJOR_VERSION > 2) || (GTK_MAJOR_VERSION == 2 && GTK_MINOR_VERSION > 18))
#define SET_ACTIVE(field) \
  if (gtk_widget_is_sensitive (pg->field)) \
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (pg->field), pngvals.field)
#else
#define SET_ACTIVE(field) \
  if (GTK_WIDGET_IS_SENSITIVE (pg->field)) \
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (pg->field), pngvals.field)
#endif

  SET_ACTIVE (interlaced);
  SET_ACTIVE (bkgd);
  SET_ACTIVE (gama);
  SET_ACTIVE (offs);
  SET_ACTIVE (phys);
  SET_ACTIVE (time);
  SET_ACTIVE (comment);
  SET_ACTIVE (save_transp_pixels);

#undef SET_ACTIVE

  gtk_adjustment_set_value (GTK_ADJUSTMENT (pg->compression_level),
                            pngvals.compression_level);
}

#if ((GIMP_MAJOR_VERSION < 2) || (GIMP_MAJOR_VERSION == 2 && GIMP_MINOR_VERSION < 7))
/*
 * Following functions are available in GIMP 2.7.
 * gimp_export_dialog_new ()
 * gimp_export_dialog_get_content_area ()
 */
static GtkWidget *
gimp_export_dialog_new (const gchar *format_name,
                        const gchar *role,
                        const gchar *help_id)
{
  GtkWidget *dialog = NULL;
  GtkWidget *button = NULL;
  gchar     *title  = g_strconcat (_("Export Image as "), format_name, NULL);

  dialog = gimp_dialog_new (title, role,
                            NULL, 0,
                            gimp_standard_help_func, help_id,
                            GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                            NULL);

  button = gimp_dialog_add_button (GIMP_DIALOG (dialog),
                                   _("_Export"), GTK_RESPONSE_OK);
  gtk_button_set_image (GTK_BUTTON (button),
                        gtk_image_new_from_stock (GTK_STOCK_SAVE,
                                                  GTK_ICON_SIZE_BUTTON));

  gtk_dialog_set_alternative_button_order (GTK_DIALOG (dialog),
                                           GTK_RESPONSE_OK,
                                           GTK_RESPONSE_CANCEL,
                                           -1);

  gimp_window_set_transient (GTK_WINDOW (dialog));

  g_free (title);

  return dialog;
}

static GtkWidget *
gimp_export_dialog_get_content_area (GtkWidget *dialog)
{
  return gtk_dialog_get_content_area (GTK_DIALOG (dialog));
}
#endif
