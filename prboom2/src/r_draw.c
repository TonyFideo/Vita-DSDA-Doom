/* Emacs style mode select   -*- C -*-
 *-----------------------------------------------------------------------------
 *
 *
 *  PrBoom: a Doom port merged with LxDoom and LSDLDoom
 *  based on BOOM, a modified and improved DOOM engine
 *  Copyright (C) 1999 by
 *  id Software, Chi Hoang, Lee Killough, Jim Flynn, Rand Phares, Ty Halderman
 *  Copyright (C) 1999-2000 by
 *  Jess Haas, Nicolas Kalkhof, Colin Phipps, Florian Schulze
 *  Copyright 2005, 2006 by
 *  Florian Schulze, Colin Phipps, Neil Stevens, Andrey Budko
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version 2
 *  of the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 *  02111-1307, USA.
 *
 * DESCRIPTION:
 *      The actual span/column drawing functions.
 *      Here find the main potential for optimization,
 *       e.g. inline assembly, different algorithms.
 *
 *-----------------------------------------------------------------------------*/

#include <stdint.h>

#include "doomstat.h"
#include "w_wad.h"
#include "r_main.h"
#include "r_draw.h"
#include "v_video.h"
#include "st_stuff.h"
#include "g_game.h"
#include "am_map.h"
#include "lprintf.h"

#include "dsda/stretch.h"

#ifdef __vita__
#include "vita/vita_system.h"
#endif

//
// All drawing to the view buffer is accomplished in this file.
// The other refresh files only know about ccordinates,
//  not the architecture of the frame buffer.
// Conveniently, the frame buffer is a linear one,
//  and we need only the base address,
//  and the total size == width*height*depth/8.,
//

byte *viewimage;
int  viewwidth;
int  viewheight;

// Color tables for different players,
//  translate a limited part to another
//  (color ramps used for  suit colors).
//

// CPhipps - made const*'s
const byte *tranmap;          // translucency filter maps 256x256   // phares
const byte *main_tranmap;     // killough 4/11/98

//
// R_DrawColumn
// Source is the top of the column to scale.
//

// SoM: OPTIMIZE for ANYRES
typedef enum
{
   COL_NONE,
   COL_OPAQUE,
   COL_TRANS,
   COL_FLEXTRANS,
   COL_FUZZ,
   COL_FLEXADD
} columntype_e;

static int    temp_x = 0;
static int    tempyl[4], tempyh[4];

// e6y: resolution limitation is removed
static byte           *tempbuf;

static int    startx = 0;
static int    temptype = COL_NONE;
static int    commontop, commonbot;
static const byte *temptranmap = NULL;
// SoM 7-28-04: Fix the fuzz problem.
static const byte   *tempfuzzmap;

#ifdef __vita__
typedef struct
{
   const byte *source;
   const lighttable_t *colormap;
   uint32_t frac;
   uint32_t fracstep;
   uint32_t fixedt_heightmask;
   int yl;
   int yh;
} vita_fused_column_t;

static vita_fused_column_t vita_fused_columns[4];
static int vita_fused4_pending;
static vita_wall_fused_fallback_t vita_fused4_flush_reason =
   VITA_WALL_FUSED_FALLBACK_PARTIAL_END;

static void R_VitaFlushDeferred4(int profile_deep);
static int R_VitaTryQueueFused4(
   const draw_column_vars_t *dcvars,
   fixed_t frac,
   fixed_t fracstep
);
#endif

//
// Spectre/Invisibility.
//

#define FUZZTABLE 50
// proff 08/17/98: Changed for high-res
//#define FUZZOFF (SCREENWIDTH)
#define FUZZOFF 1

static const int fuzzoffset_org[FUZZTABLE] = {
  FUZZOFF,-FUZZOFF,FUZZOFF,-FUZZOFF,FUZZOFF,FUZZOFF,-FUZZOFF,
  FUZZOFF,FUZZOFF,-FUZZOFF,FUZZOFF,FUZZOFF,FUZZOFF,-FUZZOFF,
  FUZZOFF,FUZZOFF,FUZZOFF,-FUZZOFF,-FUZZOFF,-FUZZOFF,-FUZZOFF,
  FUZZOFF,-FUZZOFF,-FUZZOFF,FUZZOFF,FUZZOFF,FUZZOFF,FUZZOFF,-FUZZOFF,
  FUZZOFF,-FUZZOFF,FUZZOFF,FUZZOFF,-FUZZOFF,-FUZZOFF,FUZZOFF,
  FUZZOFF,-FUZZOFF,-FUZZOFF,-FUZZOFF,-FUZZOFF,FUZZOFF,FUZZOFF,
  FUZZOFF,FUZZOFF,-FUZZOFF,FUZZOFF,FUZZOFF,-FUZZOFF,FUZZOFF
};

static int fuzzoffset[FUZZTABLE];

static int fuzzpos = 0;

// Fuzz cell size for scaled software fuzz
static int fuzzcellsize;
int fuzz_cutoff = false;

// render pipelines
#define RDC_STANDARD      1
#define RDC_TRANSLUCENT   2
#define RDC_TRANSLATED    4
#define RDC_FUZZ          8
// no color mapping
#define RDC_NOCOLMAP     16

draw_vars_t drawvars = {
  NULL, // topleft
  0, // pitch
};

dboolean R_FullView(void)
{
  return viewheight == SCREENHEIGHT;
}

dboolean R_PartialView(void)
{
  return viewheight != SCREENHEIGHT;
}

dboolean R_StatusBarVisible(void)
{
  return R_PartialView() || automap_solid;
}

//
// Error functions that will abort if R_FlushColumns tries to flush
// columns without a column type.
//

static void R_FlushWholeError(void)
{
   I_Error("R_FlushWholeColumns called without being initialized.\n");
}

static void R_FlushHTError(void)
{
   I_Error("R_FlushHTColumns called without being initialized.\n");
}

static void R_QuadFlushError(void)
{
   I_Error("R_FlushQuadColumn called without being initialized.\n");
}

static void (*R_FlushWholeColumns)(void) = R_FlushWholeError;
static void (*R_FlushHTColumns)(void)    = R_FlushHTError;
static void (*R_FlushQuadColumn)(void) = R_QuadFlushError;

static void R_FlushColumns(void)
{
#ifdef __vita__
   const int vita_wall_deep = Vita_ProfileWallDeepActive();

   if (vita_wall_deep)
      Vita_ProfileWallFlush();

   if (vita_fused4_pending)
   {
      R_VitaFlushDeferred4(vita_wall_deep);
      temp_x = 0;
      return;
   }
#endif

   if(temp_x != 4 || commontop >= commonbot)
      R_FlushWholeColumns();
   else
   {
      R_FlushHTColumns();
      R_FlushQuadColumn();
   }
   temp_x = 0;
}

//
// R_ResetColumnBuffer
//
// haleyjd 09/13/04: new function to call from main rendering loop
// which gets rid of the unnecessary reset of various variables during
// column drawing.
//
void R_ResetColumnBuffer(void)
{
   // haleyjd 10/06/05: this must not be done if temp_x == 0!
   if(temp_x)
      R_FlushColumns();
   temptype = COL_NONE;
   R_FlushWholeColumns = R_FlushWholeError;
   R_FlushHTColumns    = R_FlushHTError;
   R_FlushQuadColumn   = R_QuadFlushError;
}

#define R_DRAWCOLUMN_PIPELINE RDC_STANDARD
#define R_FLUSHWHOLE_FUNCNAME R_FlushWhole
#define R_FLUSHHEADTAIL_FUNCNAME R_FlushHT
#define R_FLUSHQUAD_FUNCNAME R_FlushQuad
#include "r_drawflush.inl"

#define R_DRAWCOLUMN_PIPELINE RDC_TRANSLUCENT
#define R_FLUSHWHOLE_FUNCNAME R_FlushWholeTL
#define R_FLUSHHEADTAIL_FUNCNAME R_FlushHTTL
#define R_FLUSHQUAD_FUNCNAME R_FlushQuadTL
#include "r_drawflush.inl"

#define R_DRAWCOLUMN_PIPELINE RDC_FUZZ
#define R_FLUSHWHOLE_FUNCNAME R_FlushWholeFuzz
#define R_FLUSHHEADTAIL_FUNCNAME R_FlushHTFuzz
#define R_FLUSHQUAD_FUNCNAME R_FlushQuadFuzz
#include "r_drawflush.inl"

#ifdef __vita__
static inline byte R_VitaSampleFusedColumn(
   const vita_fused_column_t *column,
   uint32_t *frac
)
{
   const byte result = column->colormap[
      column->source[(*frac & column->fixedt_heightmask) >> FRACBITS]
   ];

   *frac += column->fracstep;
   return result;
}

static void R_VitaDrawFusedColumnDirect(int lane, int y1, int y2, uint32_t *frac)
{
   const vita_fused_column_t *column = &vita_fused_columns[lane];
   byte *dest;
   int y;

   if (y1 > y2)
      return;

   dest = drawvars.topleft + y1 * drawvars.pitch + startx + lane;
   for (y = y1; y <= y2; ++y)
   {
      *dest = R_VitaSampleFusedColumn(column, frac);
      dest += drawvars.pitch;
   }
}

static void R_VitaFlushDeferred4(int profile_deep)
{
   uint32_t frac[4];
   byte *dest;
   int lane;
   int y;

   if (temp_x != 4 || commontop >= commonbot)
   {
      if (profile_deep)
      {
         const vita_wall_fused_fallback_t reason =
            temp_x == 4 && commontop >= commonbot
               ? VITA_WALL_FUSED_FALLBACK_NO_COMMON
               : vita_fused4_flush_reason;
         Vita_ProfileWallFused4Fallback(reason);
      }

      for (lane = 0; lane < temp_x; ++lane)
      {
         frac[lane] = vita_fused_columns[lane].frac;
         R_VitaDrawFusedColumnDirect(
            lane,
            vita_fused_columns[lane].yl,
            vita_fused_columns[lane].yh,
            &frac[lane]
         );
      }

      vita_fused4_pending = 0;
      vita_fused4_flush_reason = VITA_WALL_FUSED_FALLBACK_PARTIAL_END;
      return;
   }

   if (profile_deep)
      Vita_ProfileWallFused4Success(
         (unsigned int)(commonbot - commontop + 1) * 4u
      );

   /*
    * Preserve the exact per-column frac progression. Heads advance each lane
    * up to the shared body; the fused body then advances all four lanes once
    * per row; tails continue from those exact resulting fractions.
    */
   for (lane = 0; lane < 4; ++lane)
   {
      const vita_fused_column_t *column = &vita_fused_columns[lane];

      frac[lane] = column->frac;
      R_VitaDrawFusedColumnDirect(
         lane,
         column->yl,
         commontop - 1,
         &frac[lane]
      );
   }

   dest = drawvars.topleft + commontop * drawvars.pitch + startx;

   for (y = commontop; y <= commonbot; ++y)
   {
      const byte p0 = R_VitaSampleFusedColumn(&vita_fused_columns[0], &frac[0]);
      const byte p1 = R_VitaSampleFusedColumn(&vita_fused_columns[1], &frac[1]);
      const byte p2 = R_VitaSampleFusedColumn(&vita_fused_columns[2], &frac[2]);
      const byte p3 = R_VitaSampleFusedColumn(&vita_fused_columns[3], &frac[3]);
      const uint32_t packed =
         (uint32_t)p0 |
         ((uint32_t)p1 << 8) |
         ((uint32_t)p2 << 16) |
         ((uint32_t)p3 << 24);

      /*
       * Cortex-A9/Vita permits unaligned word stores. memcpy avoids C aliasing
       * and alignment UB while GCC folds this fixed four-byte copy to one STR.
       */
      memcpy(dest, &packed, sizeof(packed));

      dest += drawvars.pitch;
   }

   for (lane = 0; lane < 4; ++lane)
   {
      const vita_fused_column_t *column = &vita_fused_columns[lane];

      R_VitaDrawFusedColumnDirect(
         lane,
         commonbot + 1,
         column->yh,
         &frac[lane]
      );
   }

   vita_fused4_pending = 0;
   vita_fused4_flush_reason = VITA_WALL_FUSED_FALLBACK_PARTIAL_END;
}

static int R_VitaTryQueueFused4(
   const draw_column_vars_t *dcvars,
   fixed_t frac,
   fixed_t fracstep
)
{
   vita_fused_column_t *column;
   const unsigned int height = (unsigned int)dcvars->texheight;
   const int base_eligible =
      (dcvars->flags & DRAW_COLUMN_VITA_FUSED4) &&
      !(dcvars->flags & DRAW_COLUMN_ISPATCH) &&
      !dcvars->drawingmasked &&
      !dcvars->pspritepostheight &&
      dcvars->source &&
      dcvars->colormap;
   const int eligible =
      base_eligible && height && !(height & (height - 1u));

   if (!eligible)
   {
      if (Vita_ProfileWallDeepActive())
      {
         if (base_eligible && !height)
            Vita_ProfileWallFusedReject(VITA_WALL_FUSED_REJECT_ZERO_HEIGHT);
         else if (base_eligible && (height & (height - 1u)))
            Vita_ProfileWallFusedReject(VITA_WALL_FUSED_REJECT_NON_POT);
         else
            Vita_ProfileWallFusedReject(VITA_WALL_FUSED_REJECT_OTHER);
      }

      if (vita_fused4_pending)
      {
         if (base_eligible && !height)
            vita_fused4_flush_reason = VITA_WALL_FUSED_FALLBACK_ZERO_HEIGHT;
         else if (base_eligible && (height & (height - 1u)))
            vita_fused4_flush_reason = VITA_WALL_FUSED_FALLBACK_NON_POT;
         else
            vita_fused4_flush_reason = VITA_WALL_FUSED_FALLBACK_OTHER;
         R_FlushColumns();
      }
      return 0;
   }

   /* Never mix deferred columns with an already materialized legacy batch. */
   if (temp_x && !vita_fused4_pending)
      R_FlushColumns();

   if (temp_x == 4)
      R_FlushColumns();

   if (temp_x && temp_x + startx != dcvars->x)
   {
      vita_fused4_flush_reason =
         dcvars->x == startx + temp_x - 1
            ? VITA_WALL_FUSED_FALLBACK_SAME_X
            : VITA_WALL_FUSED_FALLBACK_X_GAP;
      R_FlushColumns();
   }

   if (!temp_x)
   {
      vita_fused4_flush_reason = VITA_WALL_FUSED_FALLBACK_PARTIAL_END;
      startx = dcvars->x;
      tempyl[0] = commontop = dcvars->yl;
      tempyh[0] = commonbot = dcvars->yh;
      temptype = COL_OPAQUE;
      vita_fused4_pending = 1;
   }
   else
   {
      tempyl[temp_x] = dcvars->yl;
      tempyh[temp_x] = dcvars->yh;

      if (dcvars->yl > commontop)
         commontop = dcvars->yl;
      if (dcvars->yh < commonbot)
         commonbot = dcvars->yh;
   }

   column = &vita_fused_columns[temp_x];
   column->source = dcvars->source;
   column->colormap = dcvars->colormap;
   column->frac = (uint32_t)frac;
   column->fracstep = (uint32_t)fracstep;
   column->fixedt_heightmask = ((height - 1u) << FRACBITS) | (FRACUNIT - 1u);
   column->yl = dcvars->yl;
   column->yh = dcvars->yh;

   ++temp_x;
   return 1;
}

void R_VitaFlushDeferredWallColumns(void)
{
   if (vita_fused4_pending)
   {
      vita_fused4_flush_reason = VITA_WALL_FUSED_FALLBACK_PARTIAL_END;
      R_FlushColumns();
   }
}
#endif

//
// R_DrawColumn
//

//
// A column is a vertical slice/span from a wall texture that,
//  given the DOOM style restrictions on the view orientation,
//  will always have constant z depth.
// Thus a special case loop for very fast rendering can
//  be used. It has also been used with Wolfenstein 3D.
//

byte *translationtables;

#define R_DRAWCOLUMN_PIPELINE_TYPE RDC_PIPELINE_STANDARD
#define R_DRAWCOLUMN_PIPELINE_BASE RDC_STANDARD

#define R_DRAWCOLUMN_FUNCNAME_COMPOSITE(postfix) R_DrawColumn ## postfix
#define R_FLUSHWHOLE_FUNCNAME R_FlushWhole
#define R_FLUSHHEADTAIL_FUNCNAME R_FlushHT
#define R_FLUSHQUAD_FUNCNAME R_FlushQuad
#include "r_drawcolpipeline.inl"

#undef R_DRAWCOLUMN_PIPELINE_BASE
#undef R_DRAWCOLUMN_PIPELINE_TYPE

// Here is the version of R_DrawColumn that deals with translucent  // phares
// textures and sprites. It's identical to R_DrawColumn except      //    |
// for the spot where the color index is stuffed into *dest. At     //    V
// that point, the existing color index and the new color index
// are mapped through the TRANMAP lump filters to get a new color
// index whose RGB values are the average of the existing and new
// colors.
//
// Since we're concerned about performance, the 'translucent or
// opaque' decision is made outside this routine, not down where the
// actual code differences are.

#define R_DRAWCOLUMN_PIPELINE_TYPE RDC_PIPELINE_TRANSLUCENT
#define R_DRAWCOLUMN_PIPELINE_BASE RDC_TRANSLUCENT

#define R_DRAWCOLUMN_FUNCNAME_COMPOSITE(postfix) R_DrawTLColumn ## postfix
#define R_FLUSHWHOLE_FUNCNAME R_FlushWholeTL
#define R_FLUSHHEADTAIL_FUNCNAME R_FlushHTTL
#define R_FLUSHQUAD_FUNCNAME R_FlushQuadTL
#include "r_drawcolpipeline.inl"

#undef R_DRAWCOLUMN_PIPELINE_BASE
#undef R_DRAWCOLUMN_PIPELINE_TYPE

//
// R_DrawTranslatedColumn
// Used to draw player sprites
//  with the green colorramp mapped to others.
// Could be used with different translation
//  tables, e.g. the lighter colored version
//  of the BaronOfHell, the HellKnight, uses
//  identical sprites, kinda brightened up.
//

#define R_DRAWCOLUMN_PIPELINE_TYPE RDC_PIPELINE_TRANSLATED
#define R_DRAWCOLUMN_PIPELINE_BASE RDC_TRANSLATED

#define R_DRAWCOLUMN_FUNCNAME_COMPOSITE(postfix) R_DrawTranslatedColumn ## postfix
#define R_FLUSHWHOLE_FUNCNAME R_FlushWhole
#define R_FLUSHHEADTAIL_FUNCNAME R_FlushHT
#define R_FLUSHQUAD_FUNCNAME R_FlushQuad
#include "r_drawcolpipeline.inl"

#undef R_DRAWCOLUMN_PIPELINE_BASE
#undef R_DRAWCOLUMN_PIPELINE_TYPE

//
// Framebuffer postprocessing.
// Creates a fuzzy image by copying pixels
//  from adjacent ones to left and right.
// Used with an all black colormap, this
//  could create the SHADOW effect,
//  i.e. spectres and invisible players.
//

#define R_DRAWCOLUMN_PIPELINE_TYPE RDC_PIPELINE_FUZZ
#define R_DRAWCOLUMN_PIPELINE_BASE RDC_FUZZ

#define R_DRAWCOLUMN_FUNCNAME_COMPOSITE(postfix) R_DrawFuzzColumn ## postfix
#define R_FLUSHWHOLE_FUNCNAME R_FlushWholeFuzz
#define R_FLUSHHEADTAIL_FUNCNAME R_FlushHTFuzz
#define R_FLUSHQUAD_FUNCNAME R_FlushQuadFuzz
#include "r_drawcolpipeline.inl"

#undef R_DRAWCOLUMN_PIPELINE_BASE
#undef R_DRAWCOLUMN_PIPELINE_TYPE

static R_DrawColumn_f drawcolumnfuncs[RDRAW_FILTER_MAXFILTERS][RDC_PIPELINE_MAXPIPELINES] = {
  {
    R_DrawColumn_PointUV,
    R_DrawTLColumn_PointUV,
    R_DrawTranslatedColumn_PointUV,
    R_DrawFuzzColumn_PointUV,
  },
  {
    R_DrawColumn_PointUV_PointZ,
    R_DrawTLColumn_PointUV_PointZ,
    R_DrawTranslatedColumn_PointUV_PointZ,
    R_DrawFuzzColumn_PointUV_PointZ,
  },
};

R_DrawColumn_f R_GetDrawColumnFunc(enum column_pipeline_e type, enum draw_filter_type_e filterz) {
  R_DrawColumn_f result = drawcolumnfuncs[filterz][type];
  if (result == NULL)
    I_Error("R_GetDrawColumnFunc: undefined function (%d, %d)", type, filterz);
  return result;
}

void R_SetDefaultDrawColumnVars(draw_column_vars_t *dcvars) {
  dcvars->x = dcvars->yl = dcvars->yh = 0;
  dcvars->iscale = dcvars->texturemid = dcvars->texheight = 0;
  dcvars->source = dcvars->prevsource = dcvars->nextsource = NULL;
  dcvars->colormap = colormaps[0];
  dcvars->translation = NULL;
  dcvars->edgeslope = dcvars->drawingmasked = 0;
  dcvars->flags = 0;

  // [AR] mark weapon sprite
  dcvars->isplayersprite = false;
  dcvars->pspritepostheight = 0;

  // heretic
  dcvars->baseclip = -1;
}

//
// R_InitTranslationTables
// Creates the translation tables to map
//  the green color ramp to gray, brown, red.
// Assumes a given structure of the PLAYPAL.
// Could be read from a lump instead.
//

byte playernumtotrans[MAX_MAXPLAYERS];

// HERETIC_TODO: player colors
const byte player_colors[] = { 0x70, 0x60, 0x40, 0x20 };

void R_InitTranslationTables (void)
{
  int i, j;
#define MAXTRANS 3
  byte transtocolour[MAXTRANS];

  if (hexen)
  {
    int lumpnum = W_GetNumForName("trantbl0");
    translationtables = Z_Malloc(256 * 3 * (g_maxplayers - 1));

    for (i = 0; i < g_maxplayers; i++)
      playernumtotrans[i] = i;

    for (i = 0; i < 3 * (g_maxplayers - 1); i++)
    {
        const byte* transLump = W_LumpByNum(lumpnum + i);
        memcpy(translationtables + i * 256, transLump, 256);
    }

    return;
  }

  // killough 5/2/98:
  // Remove dependency of colormaps aligned on 256-byte boundary

  if (translationtables == NULL) // CPhipps - allow multiple calls
    translationtables = Z_Malloc(256*MAXTRANS);

  for (i=0; i<MAXTRANS; i++) transtocolour[i] = 255;

  for (i = 0; i < g_maxplayers; i++) {
    byte wantcolour = player_colors[i];
    playernumtotrans[i] = 0;
    if (wantcolour != 0x70) // Not green, would like translation
      for (j = 0; j < MAXTRANS; j++)
        if (transtocolour[j] == 255) {
          transtocolour[j] = wantcolour;
          playernumtotrans[i] = j + 1;
          break;
        }
  }

  // translate just the 16 green colors
  for (i=0; i<256; i++)
    if (i >= 0x70 && i<= 0x7f)
    {
      // CPhipps - configurable player colours
      translationtables[i] = colormaps[0][(i&0xf) + transtocolour[0]];
      translationtables[i+256] = colormaps[0][(i&0xf) + transtocolour[1]];
      translationtables[i+512] = colormaps[0][(i&0xf) + transtocolour[2]];
    }
    else  // Keep all other colors as is.
      translationtables[i]=translationtables[i+256]=translationtables[i+512]=i;
}

//
// R_DrawSpan
// With DOOM style restrictions on view orientation,
//  the floors and ceilings consist of horizontal slices
//  or spans with constant z depth.
// However, rotation around the world z axis is possible,
//  thus this mapping, while simpler and faster than
//  perspective correct texture mapping, has to traverse
//  the texture at an angle in all but a few cases.
// In consequence, flats are not stored by column (like walls),
//  and the inner loop has to step in texture space u and v.
//

void R_DrawSpan(draw_span_vars_t *dsvars) {
  unsigned count = dsvars->x2 - dsvars->x1 + 1;
  fixed_t xfrac = dsvars->xfrac;
  fixed_t yfrac = dsvars->yfrac;
  const fixed_t xstep = dsvars->xstep;
  const fixed_t ystep = dsvars->ystep;
  const byte *source = dsvars->source;
  const byte *colormap = dsvars->colormap;
  byte *dest = drawvars.topleft + dsvars->y*drawvars.pitch + dsvars->x1;

#ifdef __vita__
  while (count >= 4) {
    uint32_t packed;
    fixed_t xtemp;
    fixed_t ytemp;
    fixed_t spot;
    byte p0, p1, p2, p3;

    xtemp = (xfrac >> 16) & 63;
    ytemp = (yfrac >> 10) & 4032;
    spot = xtemp | ytemp;
    p0 = colormap[source[spot]];
    xfrac += xstep;
    yfrac += ystep;

    xtemp = (xfrac >> 16) & 63;
    ytemp = (yfrac >> 10) & 4032;
    spot = xtemp | ytemp;
    p1 = colormap[source[spot]];
    xfrac += xstep;
    yfrac += ystep;

    xtemp = (xfrac >> 16) & 63;
    ytemp = (yfrac >> 10) & 4032;
    spot = xtemp | ytemp;
    p2 = colormap[source[spot]];
    xfrac += xstep;
    yfrac += ystep;

    xtemp = (xfrac >> 16) & 63;
    ytemp = (yfrac >> 10) & 4032;
    spot = xtemp | ytemp;
    p3 = colormap[source[spot]];
    xfrac += xstep;
    yfrac += ystep;

    packed =
      (uint32_t)p0 |
      ((uint32_t)p1 << 8) |
      ((uint32_t)p2 << 16) |
      ((uint32_t)p3 << 24);
    memcpy(dest, &packed, sizeof(packed));
    dest += 4;
    count -= 4;
  }
#endif

  while (count) {
    const fixed_t xtemp = (xfrac >> 16) & 63;
    const fixed_t ytemp = (yfrac >> 10) & 4032;
    const fixed_t spot = xtemp | ytemp;
    xfrac += xstep;
    yfrac += ystep;
    *dest++ = colormap[source[spot]];
    count--;
  }
}

void R_InitBuffersRes(void)
{
  extern byte *solidcol;

  if (solidcol) Z_Free(solidcol);
  if (tempbuf) Z_Free(tempbuf);

  solidcol = Z_Calloc(1, SCREENWIDTH * sizeof(*solidcol));
  tempbuf = Z_Calloc(1, (SCREENHEIGHT * 4) * sizeof(*tempbuf));

  temp_x = 0;
}

//
// R_InitBuffer
// Creats lookup tables that avoid
//  multiplies and other hazzles
//  for getting the framebuffer address
//  of a pixel to draw.
//

void R_InitBuffer(int width, int height)
{
  int i;

  drawvars.topleft = screens[0].data;
  drawvars.pitch = screens[0].pitch;

  for (i=0; i<FUZZTABLE; i++)
    fuzzoffset[i] = fuzzoffset_org[i]*screens[0].pitch;
  
  if (!tallscreen)
    fuzzcellsize = (SCREENHEIGHT + 100) / 200;
  else
    fuzzcellsize = (SCREENWIDTH + 160) / 320;
}

//
// R_FillBackColor
// Fills the statusbar widescreen area
// with a color
//

void R_FillBackColor (void)
{
  extern patchnum_t stbarbg;
  static byte col;
  static byte col_top;
  static int prevlump = -1;
  const int stbar_top = SCREENHEIGHT - ST_SCALED_HEIGHT;
  const int ST_SCALED_BORDER = brdr_b.height * patches_scaley/2;
  int lump = stbarbg.lumpnum;

  if (prevlump != lump)
  {
    const unsigned char *playpal = V_GetPlaypal();
    SDL_Color stbar_color = V_GetPatchColor(lump);
    int r = stbar_color.r;
    int g = stbar_color.g;
    int b = stbar_color.b;

    // Convert to palette and tune down saturation
    col = V_BestColor(playpal, r/3, g/3, b/3);
    col_top = V_BestColor(playpal, r/2, g/2, b/2);

    // If colors are the same, brighten top
    if (col_top == col)
      col_top = V_BestColor(playpal, r, g, b);

    prevlump = lump;
  }

  V_BeginMenuDraw();
  V_FillRect(1, 0, stbar_top, SCREENWIDTH, ST_SCALED_BORDER, col_top);
  V_FillRect(1, 0, stbar_top + ST_SCALED_BORDER, SCREENWIDTH, ST_SCALED_HEIGHT - ST_SCALED_BORDER, col);
  V_EndMenuDraw();
}

//
// R_FillBackScreen
// Fills the back screen with a pattern
//  for variable screen sizes
// Also draws a beveled edge.
//
// CPhipps - patch drawing updated

void R_FillBackScreen (void)
{
  int automap = automap_solid;

  if (grnrock.lumpnum == 0)
    return;

  V_BeginUIDraw();

  // e6y: wide-res
  if (ratio_multiplier != ratio_scale || wide_offsety)
  {
    int only_stbar;

    only_stbar = V_IsSoftwareMode() || automap || R_PartialView();

    if (only_stbar && ST_SCALED_OFFSETX > 0)
    {
      int stbar_top = SCREENHEIGHT - ST_SCALED_HEIGHT;
      int stbar_solid_bg = dsda_IntConfig(dsda_config_sts_solid_bg_color);

      if (stbar_solid_bg)
      {
        R_FillBackColor();
        V_EndUIDraw();
        return;
      }

      if (V_IsOpenGLMode()) // OpenGL has no way to adjust y-offset independent from height
        V_FillFlat(grnrock.lumpnum, 1, 0, 0, SCREENWIDTH, SCREENHEIGHT, VPT_STRETCH);
      else
        V_FillFlat(grnrock.lumpnum, 1, 0, stbar_top, SCREENWIDTH, ST_SCALED_HEIGHT, VPT_STRETCH);

      // heretic_note: I think this looks bad, so I'm skipping it...
      if (!heretic)
      {
        // line between view and status bar
        V_FillPatch(brdr_b.lumpnum, 1, 0, stbar_top, ST_SCALED_OFFSETX, brdr_b.height, VPT_NONE);
        V_FillPatch(brdr_b.lumpnum, 1, SCREENWIDTH - ST_SCALED_OFFSETX, stbar_top, ST_SCALED_OFFSETX, brdr_b.height, VPT_NONE);
      }
    }
  }

  V_EndUIDraw();
}

//
// Copy a screen buffer.
//

static void R_CopyScreenBufferSection(int x, int y, int count)
{
  if (V_IsSoftwareMode())
    memcpy(screens[0].data+y*screens[0].pitch+x,
           screens[1].data+y*screens[1].pitch+x,
           count);   // LFB copy.
}

//
// R_DrawViewBorder
// Draws the border around the view
//  for different size windows?
//

void R_DrawViewBorder(void)
{
  int i;

  if (V_IsOpenGLMode()) {
    // proff 11/99: we don't have a backscreen in OpenGL from where we can copy this
    R_FillBackScreen();
    return;
  }

  // e6y: wide-res
  if ((ratio_multiplier != ratio_scale || wide_offsety) && R_StatusBarVisible())
  {
    for (i = SCREENHEIGHT - ST_SCALED_HEIGHT; i < SCREENHEIGHT; i++)
    {
      R_CopyScreenBufferSection(0, i, ST_SCALED_OFFSETX);
      R_CopyScreenBufferSection(SCREENWIDTH - ST_SCALED_OFFSETX, i, ST_SCALED_OFFSETX);
    }
  }
}

void R_SetFuzzPos(int fp)
{
  fuzzpos = fp;
}

int R_GetFuzzPos()
{
  return fuzzpos;
}

void R_ResetFuzzCol(int height)
{
  R_ResetColumnBuffer();

  fuzzpos = (fuzzpos + (height / fuzzcellsize)) % FUZZTABLE;
}

void R_CheckFuzzCol(int x, int height)
{
  if (!(x % fuzzcellsize))
    R_ResetFuzzCol(height);
}
