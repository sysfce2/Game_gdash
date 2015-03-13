/*
 * Copyright (c) 2007, 2008 Czirkos Zoltan <cirix@fw.hu>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#include <gtk/gtk.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include "cave.h"
#include "caveset.h"
#include "caveobject.h"
#include "gtk_gfx.h"
#include "c64_gfx.h"	/* char c64_gfx[] with (almost) original graphics */
#include "settings.h"
#include "util.h"

int cell_size;
GdkPixmap *cells[NUM_OF_CELLS*3];
GdkPixbuf *cells_pb[NUM_OF_CELLS];
GdkPixbuf *combo_pb[NUM_OF_CELLS];

GdkPixbuf *gd_pixbuf_for_builtin;	/* this stores a player image, which is the pixbuf for the settings window */




static GdColor color0, color1, color2, color3, color4, color5;	/* currently used cell colors */
static guint8 *c64_custom_gfx=NULL;
static gboolean using_png_gfx;

/*
	draw an element - usually an arrow or something like that
	over another one.

	the destination element's editor drawing will be used.
	the source element will be a game element.
*/
static void
add_arrow_to_cell(GdElement dest, GdElement src, GdElement arrow, GdkPixbufRotation rotation)
{
	int pixbuf_cell_size=gdk_pixbuf_get_height(cells_pb[0]);
	GdkPixbuf *arrow_pb=gdk_pixbuf_rotate_simple(cells_pb[gd_elements[arrow].image], rotation);	/* arrow */

	if (gd_elements[dest].image>=NUM_OF_CELLS) {
		g_critical("destination index %d>=NUM_OF_CELLS", dest);
		g_assert_not_reached();
	}
	if (cells_pb[gd_elements[dest].image]!=NULL) {
		g_critical("destination index %d!=NULL", dest);
		g_assert_not_reached();
	}

	/* editor image <- game image */
	cells_pb[gd_elements[dest].image]=gdk_pixbuf_copy(cells_pb[ABS(gd_elements[src].image_game)]);
	/* composite arrow to copy */
	gdk_pixbuf_composite (arrow_pb, cells_pb[gd_elements[dest].image], 0, 0, pixbuf_cell_size, pixbuf_cell_size, 0, 0, 1, 1, GDK_INTERP_NEAREST, 255);
	g_object_unref (arrow_pb);
}

static void
copy_cell (int dest, int src)
{
	g_assert(cells_pb[dest]==NULL);
	g_assert(src<NUM_OF_CELLS);
	cells_pb[dest]=gdk_pixbuf_copy (cells_pb[src]);
}

/*
	composite two elements.
*/
static void
create_composite_cell_pixbuf(GdElement dest, GdElement src1, GdElement src2)
{
	int pixbuf_cell_size=gdk_pixbuf_get_height (cells_pb[0]);

	g_assert(gd_elements[dest].image<NUM_OF_CELLS);
	g_assert(cells_pb[gd_elements[dest].image]==NULL);

	/* destination image=source1 */
	cells_pb[gd_elements[dest].image]=gdk_pixbuf_copy(cells_pb[gd_elements[src1].image]);
	/* composite source2 to destination */
	gdk_pixbuf_composite(cells_pb[gd_elements[src2].image], cells_pb[gd_elements[dest].image], 0, 0, pixbuf_cell_size, pixbuf_cell_size, 0, 0, 1, 1, GDK_INTERP_NEAREST, 75);
}

gboolean
gd_is_png_ok_for_theme (const char *filename)
{
	/* load from file */
	int width, height;

	g_assert(filename!=NULL);

	if (!gdk_pixbuf_get_file_info (filename, &width, &height))
		return FALSE; /*"Cannot determine file format!";*/
	if ((width % NUM_OF_CELLS_X != 0) || (height % NUM_OF_CELLS_Y != 0) || (width / NUM_OF_CELLS_X != height / NUM_OF_CELLS_Y))
		return FALSE; /*"Image should contain 16 cells in a row and 32 in a column!";*/

	return TRUE;	/* passed checks */
}

/* remove pixmaps from x server */
static void
free_pixmaps()
{
	int i;
	
	/* if cells already loaded, unref them */
	for (i=0; i<G_N_ELEMENTS(cells); i++)
		if (cells[i]) {
			g_object_unref (cells[i]);
			cells[i]=NULL;
		}
}

void
gd_tv_pixbuf (GdkPixbuf *pixbuf)
{
	int width, height, rowstride, n_channels;
	guchar *pixels, *p;
	int x, y;

	n_channels=gdk_pixbuf_get_n_channels (pixbuf);

	g_assert (gdk_pixbuf_get_colorspace (pixbuf) == GDK_COLORSPACE_RGB);
	g_assert (gdk_pixbuf_get_bits_per_sample (pixbuf) == 8);
	g_assert (gdk_pixbuf_get_has_alpha (pixbuf));
	g_assert (n_channels == 4);

	width=gdk_pixbuf_get_width (pixbuf);
	height=gdk_pixbuf_get_height (pixbuf);

	rowstride=gdk_pixbuf_get_rowstride (pixbuf);
	pixels=gdk_pixbuf_get_pixels (pixbuf);

	for (y=0; y<height; y+=2) {
		p=pixels + y * rowstride;
		for (x=0; x<width*n_channels; x+=n_channels) {
			p[x+0]=p[x+0]/5*4;
			p[x+1]=p[x+1]/5*4;
			p[x+2]=p[x+2]/5*4;
		}
	}
}

/* returns true, if the given pixbuf seems to be a c64 imported image. */
static gboolean
check_if_pixbuf_c64_png (GdkPixbuf *pixbuf)
{
	int width, height, rowstride, n_channels;
	guchar *pixels, *p;
	int x, y;

	n_channels=gdk_pixbuf_get_n_channels (pixbuf);

	g_assert (gdk_pixbuf_get_colorspace (pixbuf) == GDK_COLORSPACE_RGB);
	g_assert (gdk_pixbuf_get_bits_per_sample (pixbuf) == 8);
	g_assert (gdk_pixbuf_get_has_alpha (pixbuf));
	g_assert (n_channels == 4);

	width=gdk_pixbuf_get_width (pixbuf);
	height=gdk_pixbuf_get_height (pixbuf);

	rowstride=gdk_pixbuf_get_rowstride (pixbuf);
	pixels=gdk_pixbuf_get_pixels (pixbuf);

	for (y=0; y<height; y++) {
		p=pixels + y * rowstride;
		for (x=0; x<width*n_channels; x++)
			if (p[x]!=0 && p[x]!=255)
				return FALSE;
	}
	return TRUE;
}

/* load cells, eg. create cells_pb and combo_pb
   from a big pixbuf.
*/

static void
loadcells(GdkPixbuf *cells_pixbuf)
{
	int i;
	int pixbuf_cell_size;

	/* now that we have the pixbuf, we can start freeing old graphics. */
	for (i=0; i<G_N_ELEMENTS(cells_pb); i++) {
		if (cells_pb[i]) {
			g_object_unref (cells_pb[i]);
			cells_pb[i]=NULL;
		}
		/* scaled cells for editor combo boxes. created by editor, but we free them if we load a new theme */
		if (combo_pb[i]) {
			g_object_unref (combo_pb[i]);
			combo_pb[i]=NULL;
		}
	}
	/* if we have scaled pixmaps, remove them */
	free_pixmaps();

	/* 8 (NUM_OF_CELLS_X) cells in a row, so divide by it and we get the size of a cell in pixels */
	pixbuf_cell_size=gdk_pixbuf_get_width (cells_pixbuf) / NUM_OF_CELLS_X;

	/* make individual cell pixbufs */
	for (i=0; i < NUM_OF_CELLS_Y*NUM_OF_CELLS_X; i++)
		/* copy one cell */
		cells_pb[i]=gdk_pixbuf_new_subpixbuf (cells_pixbuf, (i%NUM_OF_CELLS_X) * pixbuf_cell_size, (i/NUM_OF_CELLS_X) * pixbuf_cell_size, pixbuf_cell_size, pixbuf_cell_size);

	/* draw some elements, combining them with arrows and the like */
	create_composite_cell_pixbuf(O_STEEL_EXPLODABLE, O_STEEL, O_EXPLODE_1);
	create_composite_cell_pixbuf(O_STEEL_EATABLE, O_STEEL, O_DIRT);
	create_composite_cell_pixbuf(O_BRICK_EATABLE, O_BRICK, O_DIRT);

	create_composite_cell_pixbuf(O_WALLED_KEY_1, O_KEY_1, O_BRICK);
	create_composite_cell_pixbuf(O_WALLED_KEY_2, O_KEY_2, O_BRICK);
	create_composite_cell_pixbuf(O_WALLED_KEY_3, O_KEY_3, O_BRICK);
	create_composite_cell_pixbuf(O_WALLED_DIAMOND, O_DIAMOND, O_BRICK);

	add_arrow_to_cell(O_GUARD_1, O_GUARD_1, O_DOWN_ARROW, GDK_PIXBUF_ROTATE_CLOCKWISE);
	add_arrow_to_cell(O_GUARD_2, O_GUARD_1, O_DOWN_ARROW, GDK_PIXBUF_ROTATE_UPSIDEDOWN);
	add_arrow_to_cell(O_GUARD_3, O_GUARD_1, O_DOWN_ARROW, GDK_PIXBUF_ROTATE_COUNTERCLOCKWISE);
	add_arrow_to_cell(O_GUARD_4, O_GUARD_1, O_DOWN_ARROW, GDK_PIXBUF_ROTATE_NONE);

	add_arrow_to_cell(O_ALT_GUARD_1, O_ALT_GUARD_1, O_DOWN_ARROW, GDK_PIXBUF_ROTATE_CLOCKWISE);
	add_arrow_to_cell(O_ALT_GUARD_2, O_ALT_GUARD_1, O_DOWN_ARROW, GDK_PIXBUF_ROTATE_UPSIDEDOWN);
	add_arrow_to_cell(O_ALT_GUARD_3, O_ALT_GUARD_1, O_DOWN_ARROW, GDK_PIXBUF_ROTATE_COUNTERCLOCKWISE);
	add_arrow_to_cell(O_ALT_GUARD_4, O_ALT_GUARD_1, O_DOWN_ARROW, GDK_PIXBUF_ROTATE_NONE);

	add_arrow_to_cell(O_H_GROWING_WALL, O_GROWING_WALL, O_LEFTRIGHT_ARROW, GDK_PIXBUF_ROTATE_NONE);
	add_arrow_to_cell(O_V_GROWING_WALL, O_GROWING_WALL, O_LEFTRIGHT_ARROW, GDK_PIXBUF_ROTATE_CLOCKWISE);
	add_arrow_to_cell(O_GROWING_WALL, O_GROWING_WALL, O_EVERYDIR_ARROW, GDK_PIXBUF_ROTATE_NONE);

	add_arrow_to_cell(O_BUTTER_1, O_BUTTER_1, O_DOWN_ARROW, GDK_PIXBUF_ROTATE_CLOCKWISE);
	add_arrow_to_cell(O_BUTTER_2, O_BUTTER_1, O_DOWN_ARROW, GDK_PIXBUF_ROTATE_UPSIDEDOWN);
	add_arrow_to_cell(O_BUTTER_3, O_BUTTER_1, O_DOWN_ARROW, GDK_PIXBUF_ROTATE_COUNTERCLOCKWISE);
	add_arrow_to_cell(O_BUTTER_4, O_BUTTER_1, O_DOWN_ARROW, GDK_PIXBUF_ROTATE_NONE);

	add_arrow_to_cell(O_COW_1, O_COW_1, O_DOWN_ARROW, GDK_PIXBUF_ROTATE_CLOCKWISE);
	add_arrow_to_cell(O_COW_2, O_COW_1, O_DOWN_ARROW, GDK_PIXBUF_ROTATE_UPSIDEDOWN);
	add_arrow_to_cell(O_COW_3, O_COW_1, O_DOWN_ARROW, GDK_PIXBUF_ROTATE_COUNTERCLOCKWISE);
	add_arrow_to_cell(O_COW_4, O_COW_1, O_DOWN_ARROW, GDK_PIXBUF_ROTATE_NONE);
	add_arrow_to_cell(O_COW_ENCLOSED_1, O_COW_1, O_GLUED, GDK_PIXBUF_ROTATE_NONE);

	add_arrow_to_cell(O_ALT_BUTTER_1, O_ALT_BUTTER_1, O_DOWN_ARROW, GDK_PIXBUF_ROTATE_CLOCKWISE);
	add_arrow_to_cell(O_ALT_BUTTER_2, O_ALT_BUTTER_1, O_DOWN_ARROW, GDK_PIXBUF_ROTATE_UPSIDEDOWN);
	add_arrow_to_cell(O_ALT_BUTTER_3, O_ALT_BUTTER_1, O_DOWN_ARROW, GDK_PIXBUF_ROTATE_COUNTERCLOCKWISE);
	add_arrow_to_cell(O_ALT_BUTTER_4, O_ALT_BUTTER_1, O_DOWN_ARROW, GDK_PIXBUF_ROTATE_NONE);

	add_arrow_to_cell(O_PLAYER_GLUED, O_PLAYER, O_GLUED, 0);
	add_arrow_to_cell(O_PLAYER, O_PLAYER, O_EXCLAMATION_MARK, 0);
	add_arrow_to_cell(O_STONE_GLUED, O_STONE, O_GLUED, 0);
	add_arrow_to_cell(O_DIAMOND_GLUED, O_DIAMOND, O_GLUED, 0);
	add_arrow_to_cell(O_DIRT_GLUED, O_DIRT, O_GLUED, 0);
	add_arrow_to_cell(O_STONE_F, O_STONE, O_DOWN_ARROW, 0);
	add_arrow_to_cell(O_DIAMOND_F, O_DIAMOND, O_DOWN_ARROW, 0);
	add_arrow_to_cell(O_FALLING_WALL, O_BRICK, O_EXCLAMATION_MARK, 0);
	add_arrow_to_cell(O_FALLING_WALL_F, O_BRICK, O_DOWN_ARROW, 0);

	add_arrow_to_cell(O_STONEFLY_1, O_STONEFLY_1, O_DOWN_ARROW, GDK_PIXBUF_ROTATE_CLOCKWISE);
	add_arrow_to_cell(O_STONEFLY_2, O_STONEFLY_1, O_DOWN_ARROW, GDK_PIXBUF_ROTATE_UPSIDEDOWN);
	add_arrow_to_cell(O_STONEFLY_3, O_STONEFLY_1, O_DOWN_ARROW, GDK_PIXBUF_ROTATE_COUNTERCLOCKWISE);
	add_arrow_to_cell(O_STONEFLY_4, O_STONEFLY_1, O_DOWN_ARROW, GDK_PIXBUF_ROTATE_NONE);

	add_arrow_to_cell(O_BITER_1, O_BITER_1, O_DOWN_ARROW, GDK_PIXBUF_ROTATE_UPSIDEDOWN);
	add_arrow_to_cell(O_BITER_2, O_BITER_1, O_DOWN_ARROW, GDK_PIXBUF_ROTATE_COUNTERCLOCKWISE);
	add_arrow_to_cell(O_BITER_3, O_BITER_1, O_DOWN_ARROW, GDK_PIXBUF_ROTATE_NONE);
	add_arrow_to_cell(O_BITER_4, O_BITER_1, O_DOWN_ARROW, GDK_PIXBUF_ROTATE_CLOCKWISE);

	add_arrow_to_cell(O_PRE_INVIS_OUTBOX, O_OUTBOX_CLOSED, O_GLUED, GDK_PIXBUF_ROTATE_NONE);
	add_arrow_to_cell(O_PRE_OUTBOX, O_OUTBOX_OPEN, O_GLUED, GDK_PIXBUF_ROTATE_NONE);
	add_arrow_to_cell(O_INVIS_OUTBOX, O_OUTBOX_CLOSED, O_OUT, GDK_PIXBUF_ROTATE_NONE);
	add_arrow_to_cell(O_OUTBOX, O_OUTBOX_OPEN, O_OUT, GDK_PIXBUF_ROTATE_NONE);

	add_arrow_to_cell(O_UNKNOWN, O_STEEL, O_EXCLAMATION_MARK, GDK_PIXBUF_ROTATE_NONE);
	add_arrow_to_cell(O_WAITING_STONE, O_STONE, O_EXCLAMATION_MARK, GDK_PIXBUF_ROTATE_NONE);
	
	/* blinking outbox: helps editor, drawing the cave is more simple */
	copy_cell(ABS(gd_elements[O_PRE_OUTBOX].image_simple)+0, gd_elements[O_OUTBOX_OPEN].image_game);
	copy_cell(ABS(gd_elements[O_PRE_OUTBOX].image_simple)+1, gd_elements[O_OUTBOX_OPEN].image_game);
	copy_cell(ABS(gd_elements[O_PRE_OUTBOX].image_simple)+2, gd_elements[O_OUTBOX_OPEN].image_game);
	copy_cell(ABS(gd_elements[O_PRE_OUTBOX].image_simple)+3, gd_elements[O_OUTBOX_OPEN].image_game);
	copy_cell(ABS(gd_elements[O_PRE_OUTBOX].image_simple)+4, gd_elements[O_OUTBOX_CLOSED].image_game);
	copy_cell(ABS(gd_elements[O_PRE_OUTBOX].image_simple)+5, gd_elements[O_OUTBOX_CLOSED].image_game);
	copy_cell(ABS(gd_elements[O_PRE_OUTBOX].image_simple)+6, gd_elements[O_OUTBOX_CLOSED].image_game);
	copy_cell(ABS(gd_elements[O_PRE_OUTBOX].image_simple)+7, gd_elements[O_OUTBOX_CLOSED].image_game);
}

static guint32
rgba_pixel_from_color(GdColor col, guint8 a)
{
	guint8 r=(col>>16)&255;
	guint8 g=(col>>8)&255;
	guint8 b=col&255;
#if G_BYTE_ORDER==G_LITTLE_ENDIAN
	return r+(g<<8)+(b<<16)+(a<<24);
#else
	return (r<<24)+(g<<16)+(b<<8)+a;
#endif
}

static void
loadcells_c64 (GdColor c0, GdColor c1, GdColor c2, GdColor c3, GdColor c4, GdColor c5)
{
	const guchar *gfx;	/* currently used graphics, will point to c64_gfx or c64_custom_gfx */
	GdkPixbuf *cells_pixbuf;
	guint32 cols[9];	/* holds rgba for color indexes internally used */
	int rowstride, n_channels;
	guchar *pixels;
	int pos, x, y;

	gfx=c64_custom_gfx?c64_custom_gfx:c64_gfx;

	cols[0]=rgba_pixel_from_color(0, 0);
	cols[1]=rgba_pixel_from_color(c0, 0xff); /* c64 background */
	cols[2]=rgba_pixel_from_color(c1, 0xff); /* foreg1 */
	cols[3]=rgba_pixel_from_color(c2, 0xff); /* foreg2 */
	cols[4]=rgba_pixel_from_color(c3, 0xff); /* foreg3 */
	cols[5]=rgba_pixel_from_color(c4, 0xff); /* amoeba */
	cols[6]=rgba_pixel_from_color(c5, 0xff); /* slime */
	cols[7]=rgba_pixel_from_color(0, 0xff);	/* black, opaque*/
	cols[8]=rgba_pixel_from_color(0xffffff, 0xff);	/* white, opaque*/
	
	cells_pixbuf=gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, NUM_OF_CELLS_X*gfx[0], NUM_OF_CELLS_Y*gfx[0]);
	n_channels=gdk_pixbuf_get_n_channels (cells_pixbuf);
	rowstride=gdk_pixbuf_get_rowstride (cells_pixbuf);	/* bytes / row */
	pixels=gdk_pixbuf_get_pixels (cells_pixbuf);	/* pointer to pixbuf memory */
	
	pos=1;	/* index to gfx array */
	/* create colored pixbuf from c64 graphics, using c0, c1, c2, c3 */
	for (y=0; y<NUM_OF_CELLS_Y*gfx[0]; y++) {
		guint32 *p=(guint32*) (pixels + y * rowstride);	/* write 32bits at once - faster than writing bytes */

		for (x=0; x<NUM_OF_CELLS_X*gfx[0]; x++)
			p[x]=cols[(int) gfx[pos++]];
	}
	
	/* from here, same as any other png */
	loadcells(cells_pixbuf);
	g_object_unref(cells_pixbuf);
}



guchar *
c64_gfx_data_from_pixbuf(GdkPixbuf *pixbuf)
{
	int cols[]={
	/* abgr */
	/* 0000 */ 0,	/* transparent */
	/* 0001 */ 0,
	/* 0010 */ 0,
	/* 0011 */ 0,
	/* 0100 */ 0,
	/* 0101 */ 0,
	/* 0110 */ 0,
	/* 0111 */ 0,
	/* 1000 */ 1, /* black - background */
	/* 1001 */ 2, /* red - foreg1 */
	/* 1010 */ 5, /* green - amoeba xxxxx */
	/* 1011 */ 4, /* yellow - foreg3 */
	/* 1100 */ 6, /* blue - slime */
	/* 1101 */ 3,	/* purple - foreg2 */
	/* 1110 */ 7, /* black around arrows (used in editor) is coded as cyan */
	/* 1111 */ 8, /* white is the arrow */
	};
	int width, height, rowstride, n_channels;
	guchar *pixels;
	int x, y;
	guchar *data;
	int out;
	
	g_assert (gdk_pixbuf_get_colorspace (pixbuf) == GDK_COLORSPACE_RGB);
	g_assert (gdk_pixbuf_get_bits_per_sample (pixbuf) == 8);
	g_assert (gdk_pixbuf_get_has_alpha (pixbuf));

	n_channels=gdk_pixbuf_get_n_channels (pixbuf);
	width=gdk_pixbuf_get_width (pixbuf);
	height=gdk_pixbuf_get_height (pixbuf);
	rowstride=gdk_pixbuf_get_rowstride (pixbuf);
	pixels=gdk_pixbuf_get_pixels (pixbuf);

	g_assert (n_channels == 4);
	
	data=g_new(guchar, width*height+1);
	out=0;
	data[out++]=width/NUM_OF_CELLS_X;
	
	for (y=0; y<height; y++)
		for (x=0; x<width; x++) {
			int r, g, b, a, c;

			guchar *p=pixels + y * rowstride + x * n_channels;
			r=p[0];
			g=p[1];
			b=p[2];
			a=p[3];
			c=(a>>7)*8 + (b>>7)*4 + (g>>7)*2 + (r>>7)*1; /* lower 4 bits will be rgba */

			data[out++]=cols[c];
		}
	return data;
}



gboolean
gd_loadcells_file (const char *filename)
{
	int width, height;
	GdkPixbuf *cells_pixbuf;
	GError *error=NULL;

	/* load cell graphics */
	/* load from file */
	if (!gdk_pixbuf_get_file_info (filename, &width, &height)) {
		g_warning("%s: cannot determine file format", gd_filename_to_utf8(filename));
		return FALSE;
	}
	if ((width % NUM_OF_CELLS_X != 0) || (height % NUM_OF_CELLS_Y != 0) || (width / NUM_OF_CELLS_X != height / NUM_OF_CELLS_Y)) {
		g_warning("%s: image should contain %d cells in a row and %d in a column!", gd_filename_to_utf8(filename), NUM_OF_CELLS_X, NUM_OF_CELLS_Y);
		return FALSE;
	}
	cells_pixbuf=gdk_pixbuf_new_from_file (filename, &error);
	if (error) {
		g_warning("%s", error->message);
		g_error_free(error);
		return FALSE;
	}

	if (check_if_pixbuf_c64_png(cells_pixbuf)) {
		/* c64 pixbuf with a small number of colors which can be changed */
		g_free(c64_custom_gfx);
		c64_custom_gfx=c64_gfx_data_from_pixbuf(cells_pixbuf);
		using_png_gfx=FALSE;
	} else {
		/* normal, "truecolor" pixbuf */
		g_free(c64_custom_gfx);
		c64_custom_gfx=NULL;
		loadcells(cells_pixbuf);
		using_png_gfx=TRUE;
	}
	g_object_unref(cells_pixbuf);

	return TRUE;	
}

void
gd_loadcells_default()
{
	g_free(c64_custom_gfx);
	c64_custom_gfx=NULL;
	using_png_gfx=FALSE;
	gd_select_pixbuf_colors(gd_c64_colors[0].rgb, gd_c64_colors[8].rgb, gd_c64_colors[12].rgb, gd_c64_colors[1].rgb, gd_c64_colors[1].rgb, gd_c64_colors[1].rgb);	/* just to set some default */
}

/* creates gdkpixmaps for drawing.
   if cell size is 0, sets it to default size=the same as pixbufs loaded from png. */
void
gd_create_pixmaps ()
{
	int i;
	int pixbuf_cell_size;
	GdkWindow *window;

	g_assert(cells_pb!=NULL);
	g_assert(cells_pb[0]!=NULL);

	/* every cell has the same size */
	pixbuf_cell_size=gdk_pixbuf_get_width (cells_pb[0]);
	cell_size=pixbuf_cell_size*gd_cell_scale;
	window=gdk_get_default_root_window();

	/* if pixmaps are already created, return */
	for (i=0; i < NUM_OF_CELLS; i++)
		if (cells[i])
			return;
		
	for (i=0; i < NUM_OF_CELLS; i++)
		if (cells_pb[i]) {
			GdkPixbuf *selected, *normal, *element;

			/* scale the cell.
			 * scale every cell on its own, or else some pixels might be merged on borders */
			normal=gdk_pixbuf_scale_simple (cells_pb[i], cell_size, cell_size, gd_gfx_interpolation ? GDK_INTERP_BILINEAR : GDK_INTERP_NEAREST);
			if (gd_tv_emulation)
				gd_tv_pixbuf(normal);

			/* create pixmap containing pixbuf */
			cells[i]=gdk_pixmap_new (window, cell_size, cell_size, -1);
			cells[NUM_OF_CELLS + i]=gdk_pixmap_new (window, cell_size, cell_size, -1);
			cells[2 * NUM_OF_CELLS + i]=gdk_pixmap_new (window, cell_size, cell_size, -1);

			/* here no scaling is done, so interp_nearest is ok. */
			selected=gdk_pixbuf_composite_color_simple (normal, cell_size, cell_size, GDK_INTERP_NEAREST, 128, 1, gd_select_color, gd_select_color);
			element=gdk_pixbuf_composite_color_simple (normal, cell_size, cell_size, GDK_INTERP_NEAREST, 128, 1, gd_flash_color, gd_flash_color);

			gdk_draw_pixbuf (cells[i], NULL, normal, 0, 0, 0, 0, cell_size, cell_size, GDK_RGB_DITHER_MAX, 0, 0);
			gdk_draw_pixbuf (cells[NUM_OF_CELLS + i], NULL, element, 0, 0, 0, 0, cell_size, cell_size, GDK_RGB_DITHER_MAX, 0, 0);
			gdk_draw_pixbuf (cells[2 * NUM_OF_CELLS + i], NULL, selected, 0, 0, 0, 0, cell_size, cell_size, GDK_RGB_DITHER_MAX, 0, 0);

			g_object_unref (normal);
			g_object_unref (selected);
			g_object_unref (element);
		}
}

void
gd_select_pixbuf_colors (GdColor c0, GdColor c1, GdColor c2, GdColor c3, GdColor c4, GdColor c5)
{
	/* if non-c64 gfx, nothing to do */
	if (using_png_gfx) {
		/* nothing to do */
	} else
	if (c0!=color0 || c1!=color1 || c2!=color2 || c3!=color3 || c4!=color4 || c5!=color5) {
		/* if not the same colors as requested before */
		color0=c0;
		color1=c1;
		color2=c2;
		color3=c3;
		color4=c4;
		color5=c5;

		loadcells_c64(c0, c1, c2, c3, c4, c5);
	}
}


GdkPixbuf *
get_element_pixbuf_with_border(int index)
{
	if (!combo_pb[index]) {
		/* create small size pixbuf if needed. */
		int x, y;
		GdkPixbuf *pixbuf, *pixbuf_border;

		/* scale pixbuf to that specified by gtk */
		gtk_icon_size_lookup (GTK_ICON_SIZE_MENU, &x, &y);
		pixbuf=gdk_pixbuf_scale_simple (cells_pb[index], x, y, GDK_INTERP_BILINEAR);
		/* draw a little black border around image, makes the icons look much better */
		pixbuf_border=gdk_pixbuf_new (GDK_COLORSPACE_RGB, gdk_pixbuf_get_has_alpha (pixbuf), 8, x + 2, y + 2);
		gdk_pixbuf_fill (pixbuf_border, 0x000000ff);	/* RGBA: opaque black */
		gdk_pixbuf_copy_area (pixbuf, 0, 0, x, y, pixbuf_border, 1, 1);
		g_object_unref (pixbuf);
		combo_pb[index]=pixbuf_border;
	}
	return combo_pb[index];
}

/*
	returns a cell pixbuf, scaled to gtk icon size.
	it also adds a little black border, which makes them look much better
*/
GdkPixbuf *
gd_get_element_pixbuf_with_border (GdElement element)
{
	int index;
	/* which pixbuf to show? */
	index=ABS (gd_elements[element].image);
	return get_element_pixbuf_with_border(index);
}

/*
	returns a cell pixbuf, scaled to gtk icon size.
	it also adds a little black border, which makes them look much better
*/
GdkPixbuf *
gd_get_element_pixbuf_simple_with_border (GdElement element)
{
	int index;
	/* which pixbuf to show? */
	index=ABS(gd_elements[element].image_simple);
	return get_element_pixbuf_with_border(index);
}

/** 
	creates a pixbuf, which shows the cave.
	if width and height are given (nonzero),
	scale pixbuf proportionally, so it fits in width*height
	pixels. otherwise return in original size.
	up to the caller to unref the returned pixbuf.
	also up to the caller to call this function only for rendered caves.
*/
GdkPixbuf *
gd_drawcave_to_pixbuf(const Cave * cave, const int width, const int height, const gboolean game_view)
{
	int x, y;
	int cell_size;
	GdkPixbuf *pixbuf, *scaled;
	float scale;
	int x1, y1, x2, y2;
	
	g_assert(cave->map!=NULL);
	if (game_view) {
		/* if showing the visible part only */
		x1=cave->x1;
		y1=cave->y1;
		x2=cave->x2;
		y2=cave->y2;
	} else {
		/* showing entire cave - for example, overview in editor */
		x1=0;
		y1=0;
		x2=cave->w-1;
		y2=cave->h-1;
	}

	gd_select_pixbuf_colors(cave->color0, cave->color1, cave->color2, cave->color3, cave->color4, cave->color5);

	/* get size of one cell in the original pixbuf */
	cell_size=gdk_pixbuf_get_width (cells_pb[0]);

	/* add four pixels black border */
	pixbuf=gdk_pixbuf_new (GDK_COLORSPACE_RGB, gdk_pixbuf_get_has_alpha (cells_pb[0]), 8, (x2-x1+1)*cell_size+4, (y2-y1+1)*cell_size+4);
	gdk_pixbuf_fill (pixbuf, 0x000000ff);	/* fill with opaque black */
	
	/* take visible part into consideration */
	for (y=y1; y<=y2; y++)
		for (x=x1; x<=x2; x++) {
			GdElement element=cave->map[y][x]&O_MASK;
			int draw;
			
			if (game_view) {
				/* visual effects */
				switch(element) {
					case O_DIRT:
						element=cave->dirt_looks_like;
						break;
					case O_GROWING_WALL:
					case O_H_GROWING_WALL:
					case O_V_GROWING_WALL:
						element=cave->expanding_wall_looks_like;
						break;
					default:
						/* we check that this element has no visual effect. */
						g_assert((gd_elements[element].properties & P_VISUAL_EFFECT) == 0);
						break;
				}
				draw=ABS(gd_elements[element].image_simple);				/* pixbuf like in the editor */
			}
			else
				draw=gd_elements[element].image;				/* pixbuf like in the editor */
			gdk_pixbuf_copy_area (cells_pb[draw], 0, 0, cell_size, cell_size, pixbuf, (x-x1)*cell_size+2, (y-y1)*cell_size+2);
		}

	/* if requested size is 0, return unscaled */
	if (width == 0 || height == 0)
		return pixbuf;

	/* decide which direction fits in rectangle */
	/* cells are squares... no need to know cell_size here */
	if ((float) gdk_pixbuf_get_width (pixbuf) / (float) gdk_pixbuf_get_height (pixbuf) >= (float) width / (float) height)
		scale=width / ((float) gdk_pixbuf_get_width (pixbuf));
	else
		scale=height / ((float) gdk_pixbuf_get_height (pixbuf));

	/* scale to specified size */
	scaled=gdk_pixbuf_scale_simple (pixbuf, gdk_pixbuf_get_width (pixbuf)*scale, gdk_pixbuf_get_height (pixbuf)*scale, GDK_INTERP_BILINEAR);
	g_object_unref (pixbuf);
	
	return scaled;
}



