/*
 * Copyright © 2007,2008,2009  Red Hat, Inc.
 * Copyright © 2018  Ebrahim Byagowi
 *
 *  This is part of HarfBuzz, a text shaping library.
 *
 * Permission is hereby granted, without written agreement and without
 * license or royalty fees, to use, copy, modify, and distribute this
 * software and its documentation for any purpose, provided that the
 * above copyright notice and the following two paragraphs appear in
 * all copies of this software.
 *
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE TO ANY PARTY FOR
 * DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES
 * ARISING OUT OF THE USE OF THIS SOFTWARE AND ITS DOCUMENTATION, EVEN
 * IF THE COPYRIGHT HOLDER HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 *
 * THE COPYRIGHT HOLDER SPECIFICALLY DISCLAIMS ANY WARRANTIES, INCLUDING,
 * BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS FOR A PARTICULAR PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS
 * ON AN "AS IS" BASIS, AND THE COPYRIGHT HOLDER HAS NO OBLIGATION TO
 * PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS.
 *
 * Red Hat Author(s): Behdad Esfahbod
 */

#include "hb-static.cc"
#include "hb-open-file.hh"
#include "hb-ot-layout-gdef-table.hh"
#include "hb-ot-layout-gsubgpos.hh"

#include "hb-ot-color-cbdt-table.hh"
#include "hb-ot-color-colr-table.hh"
#include "hb-ot-color-cpal-table.hh"
#include "hb-ot-color-sbix-table.hh"
#include "hb-ot-color-svg-table.hh"

#ifdef HAVE_GLIB
# include <glib.h>
#endif
#include <stdlib.h>
#include <stdio.h>

#if defined(HB_FREETYPE) && defined(HB_CAIRO_FT)
# define GLYPH_DUMP
# include "hb-ft.h"

# include <ft2build.h>
# include FT_FREETYPE_H
# include FT_GLYPH_H

# include <cairo.h>
# include <cairo-ft.h>
# include <cairo-svg.h>

void cbdt_callback (const uint8_t*, unsigned int, unsigned int, unsigned int);
void sbix_callback (const uint8_t*, unsigned int, unsigned int, unsigned int);
void svg_callback (const uint8_t*, unsigned int, unsigned int, unsigned int);
void colr_cpal_rendering (cairo_font_face_t *, unsigned int, unsigned int,
			  const OT::COLR *, const OT::CPAL *);
void dump_glyphs (cairo_font_face_t *, unsigned int, unsigned int);
#endif

using namespace OT;

int
main (int argc, char **argv)
{
  if (argc != 2) {
    fprintf (stderr, "usage: %s font-file.ttf\n", argv[0]);
    exit (1);
  }

  hb_blob_t *blob = hb_blob_create_from_file (argv[1]);
  unsigned int len;
  const char *font_data = hb_blob_get_data (blob, &len);
  printf ("Opened font file %s: %d bytes long\n", argv[1], len);

  hb_blob_t *font_blob = hb_sanitize_context_t().sanitize_blob<OpenTypeFontFile> (blob);
  const OpenTypeFontFile* sanitized = font_blob->as<OpenTypeFontFile> ();
  if (!font_blob->data)
  {
    printf ("Sanitization of the file wasn't successful. Exit");
    return 1;
  }
  const OpenTypeFontFile& ot = *sanitized;


  switch (ot.get_tag ()) {
  case OpenTypeFontFile::TrueTypeTag:
    printf ("OpenType font with TrueType outlines\n");
    break;
  case OpenTypeFontFile::CFFTag:
    printf ("OpenType font with CFF (Type1) outlines\n");
    break;
  case OpenTypeFontFile::TTCTag:
    printf ("TrueType Collection of OpenType fonts\n");
    break;
  case OpenTypeFontFile::TrueTag:
    printf ("Obsolete Apple TrueType font\n");
    break;
  case OpenTypeFontFile::Typ1Tag:
    printf ("Obsolete Apple Type1 font in SFNT container\n");
    break;
  case OpenTypeFontFile::DFontTag:
    printf ("DFont Mac Resource Fork\n");
    break;
  default:
    printf ("Unknown font format\n");
    break;
  }

  int num_fonts = ot.get_face_count ();
  printf ("%d font(s) found in file\n", num_fonts);
  for (int n_font = 0; n_font < num_fonts; n_font++) {
    const OpenTypeFontFace &font = ot.get_face (n_font);
    printf ("Font %d of %d:\n", n_font, num_fonts);

    int num_tables = font.get_table_count ();
    printf ("  %d table(s) found in font\n", num_tables);
    for (int n_table = 0; n_table < num_tables; n_table++) {
      const OpenTypeTable &table = font.get_table (n_table);
      printf ("  Table %2d of %2d: %.4s (0x%08x+0x%08x)\n", n_table, num_tables,
	      (const char *) table.tag,
	      (unsigned int) table.offset,
	      (unsigned int) table.length);

      switch (table.tag) {

      case HB_OT_TAG_GSUB:
      case HB_OT_TAG_GPOS:
	{

	const GSUBGPOS &g = *CastP<GSUBGPOS> (font_data + table.offset);

	int num_scripts = g.get_script_count ();
	printf ("    %d script(s) found in table\n", num_scripts);
	for (int n_script = 0; n_script < num_scripts; n_script++) {
	  const Script &script = g.get_script (n_script);
	  printf ("    Script %2d of %2d: %.4s\n", n_script, num_scripts,
	          (const char *)g.get_script_tag(n_script));

	  if (!script.has_default_lang_sys())
	    printf ("      No default language system\n");
	  int num_langsys = script.get_lang_sys_count ();
	  printf ("      %d language system(s) found in script\n", num_langsys);
	  for (int n_langsys = script.has_default_lang_sys() ? -1 : 0; n_langsys < num_langsys; n_langsys++) {
	    const LangSys &langsys = n_langsys == -1
				   ? script.get_default_lang_sys ()
				   : script.get_lang_sys (n_langsys);
	    if (n_langsys == -1)
	      printf ("      Default Language System\n");
	    else
	      printf ("      Language System %2d of %2d: %.4s\n", n_langsys, num_langsys,
		      (const char *)script.get_lang_sys_tag (n_langsys));
	    if (!langsys.has_required_feature ())
	      printf ("        No required feature\n");
	    else
	      printf ("        Required feature index: %d\n",
		      langsys.get_required_feature_index ());

	    int num_features = langsys.get_feature_count ();
	    printf ("        %d feature(s) found in language system\n", num_features);
	    for (int n_feature = 0; n_feature < num_features; n_feature++) {
	      printf ("        Feature index %2d of %2d: %d\n", n_feature, num_features,
	              langsys.get_feature_index (n_feature));
	    }
	  }
	}

	int num_features = g.get_feature_count ();
	printf ("    %d feature(s) found in table\n", num_features);
	for (int n_feature = 0; n_feature < num_features; n_feature++) {
	  const Feature &feature = g.get_feature (n_feature);
	  int num_lookups = feature.get_lookup_count ();
	  printf ("    Feature %2d of %2d: %c%c%c%c\n", n_feature, num_features,
	          HB_UNTAG(g.get_feature_tag(n_feature)));

	  printf ("        %d lookup(s) found in feature\n", num_lookups);
	  for (int n_lookup = 0; n_lookup < num_lookups; n_lookup++) {
	    printf ("        Lookup index %2d of %2d: %d\n", n_lookup, num_lookups,
	            feature.get_lookup_index (n_lookup));
	  }
	}

	int num_lookups = g.get_lookup_count ();
	printf ("    %d lookup(s) found in table\n", num_lookups);
	for (int n_lookup = 0; n_lookup < num_lookups; n_lookup++) {
	  const Lookup &lookup = g.get_lookup (n_lookup);
	  printf ("    Lookup %2d of %2d: type %d, props 0x%04X\n", n_lookup, num_lookups,
	          lookup.get_type(), lookup.get_props());
	}

	}
	break;

      case GDEF::tableTag:
	{

	const GDEF &gdef = *CastP<GDEF> (font_data + table.offset);

	printf ("    Has %sglyph classes\n",
		  gdef.has_glyph_classes () ? "" : "no ");
	printf ("    Has %smark attachment types\n",
		  gdef.has_mark_attachment_types () ? "" : "no ");
	printf ("    Has %sattach points\n",
		  gdef.has_attach_points () ? "" : "no ");
	printf ("    Has %slig carets\n",
		  gdef.has_lig_carets () ? "" : "no ");
	printf ("    Has %smark sets\n",
		  gdef.has_mark_sets () ? "" : "no ");
	break;
	}
      }
    }

#ifdef GLYPH_DUMP
    // dump related parts
    hb_face_t *face = hb_face_create (blob, n_font);
    hb_font_t *font = hb_font_create (face);

    OT::CBDT::accelerator_t cbdt;
    cbdt.init (face);
    cbdt.dump (cbdt_callback);
    cbdt.fini ();

    OT::sbix::accelerator_t sbix;
    sbix.init (face);
    sbix.dump (sbix_callback);
    sbix.fini ();

    OT::SVG::accelerator_t svg;
    svg.init (face);
    svg.dump (svg_callback);
    svg.fini ();

    hb_blob_t* colr_blob = hb_sanitize_context_t ().reference_table<OT::COLR> (face);
    const OT::COLR *colr = colr_blob->as<OT::COLR> ();

    hb_blob_t* cpal_blob = hb_sanitize_context_t ().reference_table<OT::CPAL> (face);
    const OT::CPAL *cpal = cpal_blob->as<OT::CPAL> ();

    cairo_font_face_t *cairo_face;
    {
      FT_Library library;
      FT_Init_FreeType (&library);
      FT_Face ftface;
      FT_New_Face (library, argv[1], 0, &ftface);
      cairo_face = cairo_ft_font_face_create_for_ft_face (ftface, n_font);
    }
    unsigned int num_glyphs = hb_face_get_glyph_count (face);
    unsigned int upem = hb_face_get_upem (face);
    colr_cpal_rendering (cairo_face, upem, num_glyphs, colr, cpal);
    dump_glyphs (cairo_face, upem, num_glyphs);

    hb_font_destroy (font);
    hb_face_destroy (face);
#endif
  }

  hb_blob_destroy (blob);

  return 0;
}

#ifdef GLYPH_DUMP
void
cbdt_callback (const uint8_t* data, unsigned int length,
	       unsigned int group, unsigned int gid)
{
  char output_path[255];
  sprintf (output_path, "out/cbdt-%d-%d.png", group, gid);
  FILE *f = fopen (output_path, "wb");
  fwrite (data, 1, length, f);
  fclose (f);
}

void
sbix_callback (const uint8_t* data, unsigned int length,
	       unsigned int group, unsigned int gid)
{
  char output_path[255];
  sprintf (output_path, "out/sbix-%d-%d.png", group, gid);
  FILE *f = fopen (output_path, "wb");
  fwrite (data, 1, length, f);
  fclose (f);
}

void
svg_callback (const uint8_t* data, unsigned int length,
	      unsigned int start_glyph, unsigned int end_glyph)
{
  char output_path[255];
  if (start_glyph == end_glyph)
    sprintf (output_path, "out/svg-%d.svg", start_glyph);
  else
    sprintf (output_path, "out/svg-%d-%d.svg", start_glyph, end_glyph);

  // append "z" if the content is gzipped
  if ((data[0] == 0x1F) && (data[1] == 0x8B))
    strcat (output_path, "z");

  FILE *f = fopen (output_path, "wb");
  fwrite (data, 1, length, f);
  fclose (f);
}

void
colr_cpal_rendering (cairo_font_face_t *cairo_face, unsigned int upem, unsigned int num_glyphs,
		     const OT::COLR *colr, const OT::CPAL *cpal)
{
  for (unsigned int i = 0; i < num_glyphs; ++i)
  {
    unsigned int first_layer_index, num_layers;
    if (colr->get_base_glyph_record (i, &first_layer_index, &num_layers))
    {
      // Measure
      cairo_text_extents_t extents;
      {
	cairo_surface_t *surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, 1, 1);
	cairo_t *cr = cairo_create (surface);
	cairo_set_font_face (cr, cairo_face);
	cairo_set_font_size (cr, upem);

	cairo_glyph_t *glyphs = (cairo_glyph_t *) calloc (num_layers, sizeof (cairo_glyph_t));
	for (unsigned int j = 0; j < num_layers; ++j)
	{
	  hb_codepoint_t glyph_id;
	  unsigned int color_index;
	  colr->get_layer_record (first_layer_index + j, &glyph_id, &color_index);
	  glyphs[j].index = glyph_id;
	}
	cairo_glyph_extents (cr, glyphs, num_layers, &extents);
	free (glyphs);
	cairo_surface_destroy (surface);
	cairo_destroy (cr);
      }

      // Add a slight margin
      extents.width += extents.width / 10;
      extents.height += extents.height / 10;
      extents.x_bearing -= extents.width / 20;
      extents.y_bearing -= extents.height / 20;

      // Render
      unsigned int pallet_count = cpal->get_palette_count ();
      for (unsigned int pallet = 0; pallet < pallet_count; ++pallet) {
	char output_path[255];

	// If we have more than one pallet, use a better namin
	if (pallet_count == 1)
	  sprintf (output_path, "out/colr-%d.svg", i);
	else
	  sprintf (output_path, "out/colr-%d-%d.svg", i, pallet);

	cairo_surface_t *surface = cairo_svg_surface_create (output_path, extents.width, extents.height);
	cairo_t *cr = cairo_create (surface);
	cairo_set_font_face (cr, cairo_face);
	cairo_set_font_size (cr, upem);

	for (unsigned int j = 0; j < num_layers; ++j)
	{
	  hb_codepoint_t glyph_id;
	  unsigned int color_index;
	  colr->get_layer_record (first_layer_index + j, &glyph_id, &color_index);

	  uint32_t color = cpal->get_color_record_argb (color_index, pallet);
	  int alpha = color & 0xFF;
	  int r = (color >> 8) & 0xFF;
	  int g = (color >> 16) & 0xFF;
	  int b = (color >> 24) & 0xFF;
	  cairo_set_source_rgba (cr, r / 255.f, g / 255.f, b / 255.f, alpha);

	  cairo_glyph_t glyph;
	  glyph.index = glyph_id;
	  glyph.x = -extents.x_bearing;
	  glyph.y = -extents.y_bearing;
	  cairo_show_glyphs (cr, &glyph, 1);
	}

	cairo_surface_destroy (surface);
	cairo_destroy (cr);
      }
    }
  }
}

void dump_glyphs (cairo_font_face_t *cairo_face,
		  unsigned int upem, unsigned int num_glyphs) {
  // Dump every glyph available on the font
  for (unsigned int i = 0; i < num_glyphs; ++i)
  {
    cairo_text_extents_t extents;
    cairo_glyph_t glyph = {0};
    glyph.index = i;

    // Measure
    {
      cairo_surface_t *surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, 1, 1);
      cairo_t *cr = cairo_create (surface);
      cairo_set_font_face (cr, cairo_face);
      cairo_set_font_size (cr, upem);

      cairo_glyph_extents (cr, &glyph, 1, &extents);
      cairo_surface_destroy (surface);
      cairo_destroy (cr);
    }

    // Add a slight margin
    extents.width += extents.width / 10;
    extents.height += extents.height / 10;
    extents.x_bearing -= extents.width / 20;
    extents.y_bearing -= extents.height / 20;

    // Render
    {
      char output_path[255];
      sprintf (output_path, "out/%d.svg", i);
      cairo_surface_t *surface = cairo_svg_surface_create (output_path, extents.width, extents.height);
      cairo_t *cr = cairo_create (surface);
      cairo_set_font_face (cr, cairo_face);
      cairo_set_font_size (cr, upem);
      glyph.x = -extents.x_bearing;
      glyph.y = -extents.y_bearing;
      cairo_show_glyphs (cr, &glyph, 1);
      cairo_surface_destroy (surface);
      cairo_destroy (cr);
    }
  }
}
#endif
