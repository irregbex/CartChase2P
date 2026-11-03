#ifndef DKC1_WALL_SEAMS_H
#define DKC1_WALL_SEAMS_H

#include "dkc1_terrain.h"

/* A verified authored junction, not a general replacement of populated art.
 * The west column belongs to the neighboring corridor at rows 316..322.
 * The east wall repeats every three rows and joins the west column correctly
 * at rows 325..327. Validate the entire junction and that donor strip before
 * using its authentic metatile cells in an entirely offscreen west column.
 * Source and scrolling evidence: docs/WIDESCREEN_WALL_SEAM.md. */
static inline bool Dkc1WallSeamSourceMatches(Dkc1TerrainRead read, void *ctx) {
  static const uint16_t junction[12][2] = {
    {0x40cb, 0x4040}, {0x4001, 0x00ca}, {0x40cb, 0x408c},
    {0x4008, 0x4040}, {0x0100, 0x00ca}, {0x0109, 0x408c},
    {0x0112, 0x4040}, {0x010a, 0x00ca}, {0x00fe, 0x408c},
    {0x0034, 0x4040}, {0x010a, 0x00ca}, {0x00fe, 0x408c},
  };
  if (!read) return false;
  for (unsigned y = 0; y < 12; y++) {
    for (unsigned x = 0; x < 2; x++) {
      uint16_t cell;
      if (!read(ctx, 28u + x, 316u + y, &cell) ||
          cell != junction[y][x]) return false;
    }
  }
  return true;
}

static inline bool Dkc1WallSeamDonorRow(
    uint32_t target_x, uint32_t target_y, uint32_t native_left_x,
    uint32_t *donor_y) {
  if (!donor_y || target_x != 28u || native_left_x != 29u ||
      target_y < 316u || target_y > 322u) return false;
  *donor_y = 325u + (target_y - 316u) % 3u;
  return true;
}

/* The opposite side of the same junction faces a different passage. Its
 * native wall is column 28, so the west-facing correction above cannot be
 * reused. These authored donor triples preserve each native edge cell and
 * supply its two matching interior cells. Both target and donor bytes are
 * checked, including the rows immediately above and below the correction. */
typedef struct Dkc1EastWallDonor {
  uint16_t x, y, edge, inner, outer;
} Dkc1EastWallDonor;
static const Dkc1EastWallDonor kDkc1EastWallDonors[11] = {
  /* The ceiling corner above the upright wall has its own authored pair.
   * Its partial edge is not a generic empty-wall continuation candidate. */
  {25, 284, 0x0007, 0x4005, 0x00d5},
  {50, 307, 0x4001, 0x0040, 0x4040},
  {50, 308, 0x40cb, 0x40ca, 0x00ca},
  {50, 306, 0x4008, 0x008c, 0x408c},
  {50, 308, 0x40cb, 0x40ca, 0x00ca},
  {50, 307, 0x4001, 0x0040, 0x4040},
  {50, 308, 0x40cb, 0x40ca, 0x00ca},
  {50, 306, 0x4008, 0x008c, 0x408c},
  {50, 310, 0x0100, 0x0040, 0x4040},
  {50, 311, 0x0109, 0x010a, 0x410a},
  {32, 300, 0x0112, 0x00fe, 0x40d9},
};

static inline bool Dkc1EastWallSeamSourceMatches(
    Dkc1TerrainRead read, void *ctx) {
  static const uint16_t junction[13][3] = {
    {0x00ce, 0x00c9, 0x4166},
    {0x0007, 0x0000, 0x0000}, {0x4001, 0x0000, 0x0000},
    {0x40cb, 0x0000, 0x0000}, {0x4008, 0x0000, 0x0000},
    {0x40cb, 0x4040, 0x0001}, {0x4001, 0x00ca, 0x00cb},
    {0x40cb, 0x408c, 0x0008}, {0x4008, 0x4040, 0x0001},
    {0x0100, 0x00ca, 0x00cb}, {0x0109, 0x408c, 0x0008},
    {0x0112, 0x4040, 0x0001}, {0x010a, 0x00ca, 0x00cb},
  };
  if (!read) return false;
  for (unsigned y = 0; y < 13; y++) {
    for (unsigned x = 0; x < 3; x++) {
      uint16_t cell;
      if (!read(ctx, 28u + x, 311u + y, &cell) ||
          cell != junction[y][x]) return false;
    }
  }
  for (unsigned y = 0; y < 11; y++) {
    const Dkc1EastWallDonor *d = &kDkc1EastWallDonors[y];
    const uint16_t expected[3] = {d->edge, d->inner, d->outer};
    if (d->edge != junction[y + 1u][0]) return false;
    for (unsigned x = 0; x < 3; x++) {
      uint16_t cell;
      if (!read(ctx, d->x + x, d->y, &cell) ||
          cell != expected[x]) return false;
    }
  }
  return true;
}

static inline bool Dkc1EastWallSeamDonor(
    uint32_t target_x, uint32_t target_y, uint32_t native_right_x,
    uint32_t *donor_x, uint32_t *donor_y) {
  if (!donor_x || !donor_y || native_right_x != 28u ||
      target_x < 29u || target_x > 30u ||
      target_y < 312u || target_y > 322u) return false;
  const Dkc1EastWallDonor *d = &kDkc1EastWallDonors[target_y - 312u];
  *donor_x = d->x + target_x - 28u;
  *donor_y = d->y;
  return true;
}

/* The small western alcove has a separate right wall. Its upright ends at
 * column 12; the two bottom rows end at column 11. Empty cells beyond that
 * wall are hidden by the stock camera. Each replacement strip below exists
 * in the same map and starts with the exact retained wall cell. This is a
 * source-backed capability, not a fallback for ambiguous adjacency chains. */
typedef struct Dkc1CoveWallDonor {
  uint16_t anchor_x, x, y, edge, inner, outer, far;
} Dkc1CoveWallDonor;
static const Dkc1CoveWallDonor kDkc1CoveWallDonors[14] = {
  {12,34,316,0x00c8,0x0040,0x4040,0},
  {12,34,317,0x00cc,0x40ca,0x00ca,0},
  {12,50,306,0x4008,0x008c,0x408c,0},
  {12,50,307,0x4001,0x0040,0x4040,0},
  {12,50,308,0x40cb,0x40ca,0x00ca,0},
  {12,50,306,0x4008,0x008c,0x408c,0},
  {12,50,307,0x4001,0x0040,0x4040,0},
  {12,50,308,0x40cb,0x40ca,0x00ca,0},
  {12,50,306,0x4008,0x008c,0x408c,0},
  {12,50,307,0x4001,0x0040,0x4040,0},
  {12,39,290,0x00fd,0x40ca,0x00ca,0},
  {12,50,306,0x4008,0x008c,0x408c,0},
  {11, 8,311,0x410a,0x4109,0x00c9,0x410a},
  {11, 8,312,0x40fe,0x4112,0x00d9,0x40fe},
};

static inline bool Dkc1CoveWallSourceMatches(Dkc1TerrainRead read, void *ctx) {
  static const uint16_t junction[16][4] = {
    {0x0000,0x0000,0x0000,0x0000},
    {0x009d,0x00c8,0x0000,0x0000},
    {0x0004,0x00cc,0x0000,0x0000},
    {0x4009,0x4008,0x0000,0x0000},
    {0x4013,0x4001,0x0000,0x0000},
    {0x000a,0x40cb,0x0000,0x0000},
    {0x4009,0x4008,0x0000,0x0000},
    {0x4013,0x4001,0x0000,0x0000},
    {0x000a,0x40cb,0x0000,0x0000},
    {0x4009,0x4008,0x0000,0x0000},
    {0x4013,0x4001,0x0000,0x0000},
    {0x000a,0x00fd,0x0000,0x0000},
    {0x0037,0x4008,0x0000,0x0000},
    {0x410a,0x0000,0x0000,0x0000},
    {0x40fe,0x0000,0x0000,0x0000},
    {0x00d1,0x00d2,0x4229,0x4005},
  };
  if (!read) return false;
  for (unsigned y=0; y<16; y++) {
    for (unsigned x=0; x<4; x++) {
      uint16_t cell;
      if (!read(ctx,11u+x,298u+y,&cell) || cell!=junction[y][x])
        return false;
    }
  }
  for (unsigned y=0; y<14; y++) {
    const Dkc1CoveWallDonor *d=&kDkc1CoveWallDonors[y];
    const uint16_t expected[4]={d->edge,d->inner,d->outer,d->far};
    if (d->edge!=junction[y+1u][d->anchor_x-11u]) return false;
    for (unsigned x=0; x<=14u-d->anchor_x; x++) {
      uint16_t cell;
      if (!read(ctx,d->x+x,d->y,&cell) || cell!=expected[x]) return false;
    }
  }
  return true;
}

static inline bool Dkc1CoveWallDonorCell(
    uint32_t target_x, uint32_t target_y, uint32_t native_right_x,
    uint32_t *donor_x, uint32_t *donor_y) {
  if (!donor_x || !donor_y || target_y<299u || target_y>312u)
    return false;
  const Dkc1CoveWallDonor *d=&kDkc1CoveWallDonors[target_y-299u];
  if (native_right_x>d->anchor_x || target_x<=d->anchor_x || target_x>14u)
    return false;
  *donor_x=d->x+target_x-d->anchor_x;
  *donor_y=d->y;
  return true;
}

/* Above the alcove, the western shaft's stock edge is column 8. Four
 * unused rows beyond it interrupt an otherwise continuous upright wall.
 * Rows 280..282 supply a complete, three-row strip with the same edge
 * cells. Keep this signature separate from the lower, wider alcove. */
static inline bool Dkc1CoveShaftSourceMatches(Dkc1TerrainRead read, void *ctx) {
  static const uint16_t junction[6][4] = {
    {0x00ed,0x4008,0x008c,0x010b},
    {0x40c5,0x0000,0x0000,0x0000},
    {0x40c9,0x0000,0x0000,0x0000},
    {0x40d9,0x0000,0x0000,0x0000},
    {0x40c5,0x0000,0x0000,0x0000},
    {0x022a,0x00c6,0x00c7,0x009d},
  };
  static const uint16_t strip[3][4] = {
    {0x40c5,0x0038,0x0034,0x40c5},
    {0x40c9,0x0109,0x010a,0x40c9},
    {0x40d9,0x0112,0x00fe,0x40d9},
  };
  if (!read) return false;
  for (unsigned y=0; y<6; y++) for (unsigned x=0; x<4; x++) {
    uint16_t cell;
    if (!read(ctx,8u+x,294u+y,&cell) || cell!=junction[y][x])
      return false;
  }
  for (unsigned y=0; y<3; y++) for (unsigned x=0; x<4; x++) {
    uint16_t cell;
    if (!read(ctx,8u+x,280u+y,&cell) || cell!=strip[y][x])
      return false;
  }
  return true;
}

static inline bool Dkc1CoveShaftDonorCell(
    uint32_t target_x, uint32_t target_y, uint32_t native_right_x,
    uint32_t *donor_x, uint32_t *donor_y) {
  if (!donor_x || !donor_y || native_right_x!=8u ||
      target_x<9u || target_x>11u || target_y<295u || target_y>298u)
    return false;
  *donor_x=target_x;
  *donor_y=280u+(target_y-295u)%3u;
  return true;
}

#endif
