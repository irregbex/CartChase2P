#include <stdio.h>
#include <string.h>
#include "dkc1_wall_seams.h"

/* Fixture read directly from the supported ROM's E9:9E38..E9:A3BA
 * two-column wall junction. A changed cell must invalidate the capability. */
static uint16_t cells[12][2] = {
  {0x40cb,0x4040},{0x4001,0x00ca},{0x40cb,0x408c},
  {0x4008,0x4040},{0x0100,0x00ca},{0x0109,0x408c},
  {0x0112,0x4040},{0x010a,0x00ca},{0x00fe,0x408c},
  {0x0034,0x4040},{0x010a,0x00ca},{0x00fe,0x408c},
};
static bool read_cell(void *ctx,uint32_t x,uint32_t y,uint16_t *cell) {
  if (ctx || x<28 || x>29 || y<316 || y>327) return false;
  *cell=cells[y-316][x-28];return true;
}
/* Independent target and donor records from the right-edge reproduction. */
static const uint16_t east_junction[13][3] = {
  {0x00ce,0x00c9,0x4166},
  {0x0007,0x0000,0x0000},{0x4001,0x0000,0x0000},
  {0x40cb,0x0000,0x0000},{0x4008,0x0000,0x0000},
  {0x40cb,0x4040,0x0001},{0x4001,0x00ca,0x00cb},
  {0x40cb,0x408c,0x0008},{0x4008,0x4040,0x0001},
  {0x0100,0x00ca,0x00cb},{0x0109,0x408c,0x0008},
  {0x0112,0x4040,0x0001},{0x010a,0x00ca,0x00cb},
};
static const uint16_t donor_records[7][5] = {
  {25,284,0x0007,0x4005,0x00d5},
  {50,306,0x4008,0x008c,0x408c},
  {50,307,0x4001,0x0040,0x4040},
  {50,308,0x40cb,0x40ca,0x00ca},
  {50,310,0x0100,0x0040,0x4040},
  {50,311,0x0109,0x010a,0x410a},
  {32,300,0x0112,0x00fe,0x40d9},
};
static uint16_t east_map[512][64];
static bool east_valid[512][64];
static bool read_east(void *ctx,uint32_t x,uint32_t y,uint16_t *cell) {
  if (ctx || x>=64 || y>=512 || !east_valid[y][x]) return false;
  *cell=east_map[y][x];return true;
}
#define CHECK(c) do {if (!(c)) {fprintf(stderr,"wall seam check line %d: %s\n",__LINE__,#c);return 1;}} while (0)
static int test_cove(void) {
  /* Independent ROM fixture for E9:9516..E9:9C9C and the donor strips. */
  static const uint16_t junction[16][4] = {
    {0,0,0,0},{0x009d,0x00c8,0,0},{0x0004,0x00cc,0,0},
    {0x4009,0x4008,0,0},{0x4013,0x4001,0,0},{0x000a,0x40cb,0,0},
    {0x4009,0x4008,0,0},{0x4013,0x4001,0,0},{0x000a,0x40cb,0,0},
    {0x4009,0x4008,0,0},{0x4013,0x4001,0,0},{0x000a,0x00fd,0,0},
    {0x0037,0x4008,0,0},{0x410a,0,0,0},{0x40fe,0,0,0},
    {0x00d1,0x00d2,0x4229,0x4005},
  };
  static const uint16_t donors[8][7] = {
    {34,316,3,0x00c8,0x0040,0x4040,0},
    {34,317,3,0x00cc,0x40ca,0x00ca,0},
    {50,306,3,0x4008,0x008c,0x408c,0},
    {50,307,3,0x4001,0x0040,0x4040,0},
    {50,308,3,0x40cb,0x40ca,0x00ca,0},
    {39,290,3,0x00fd,0x40ca,0x00ca,0},
    {8,311,4,0x410a,0x4109,0x00c9,0x410a},
    {8,312,4,0x40fe,0x4112,0x00d9,0x40fe},
  };
  memset(east_valid,0,sizeof east_valid);
  for (unsigned y=0;y<16;y++) for (unsigned x=0;x<4;x++) {
    east_map[y+298][x+11]=junction[y][x];east_valid[y+298][x+11]=true;
  }
  for (unsigned i=0;i<8;i++) for (unsigned x=0;x<donors[i][2];x++) {
    const uint16_t *d=donors[i];
    east_map[d[1]][d[0]+x]=d[3+x];east_valid[d[1]][d[0]+x]=true;
  }
  CHECK(Dkc1CoveWallSourceMatches(read_east,NULL));
  CHECK(!Dkc1CoveWallSourceMatches(NULL,NULL));
  CHECK(!Dkc1CoveWallSourceMatches(read_east,(void *)1));
  for (unsigned y=0;y<512;y++) for (unsigned x=0;x<64;x++) {
    if (!east_valid[y][x]) continue;
    east_map[y][x]^=0x4000;
    CHECK(!Dkc1CoveWallSourceMatches(read_east,NULL));
    east_map[y][x]^=0x4000;east_valid[y][x]=false;
    CHECK(!Dkc1CoveWallSourceMatches(read_east,NULL));
    east_valid[y][x]=true;
  }
  for (uint32_t y=299;y<=312;y++) {
    const uint32_t anchor=y<311?12:11;
    for (uint32_t edge=0;edge<=14;edge++) for (uint32_t x=10;x<=15;x++) {
      uint32_t dx=99,dy=99;
      bool ok=Dkc1CoveWallDonorCell(x,y,edge,&dx,&dy);
      CHECK(ok==(edge<=anchor && x>anchor && x<=14));
      if (ok) {
        CHECK(x>edge && east_valid[dy][dx]);
        CHECK(east_map[dy][dx-(x-anchor)]==east_map[y][anchor]);
      } else CHECK(dx==99 && dy==99);
    }
  }
  uint32_t dx=99,dy=99;
  CHECK(!Dkc1CoveWallDonorCell(13,298,11,&dx,&dy));
  CHECK(!Dkc1CoveWallDonorCell(13,313,11,&dx,&dy));
  CHECK(!Dkc1CoveWallDonorCell(13,UINT32_MAX,11,&dx,&dy));
  CHECK(!Dkc1CoveWallDonorCell(13,311,UINT32_MAX,&dx,&dy));
  CHECK(!Dkc1CoveWallDonorCell(13,311,11,NULL,&dy));
  CHECK(!Dkc1CoveWallDonorCell(13,311,11,&dx,NULL));
  CHECK(dx==99 && dy==99);
  return 0;
}
static int test_cove_shaft(void) {
  /* Independent clean-ROM records: E9:9310..E9:9596 (target and guards),
   * E9:8C10..E9:8D16 (authored donor strip). */
  static const uint16_t records[9][5] = {
    {294,0x00ed,0x4008,0x008c,0x010b},
    {295,0x40c5,0,0,0},{296,0x40c9,0,0,0},
    {297,0x40d9,0,0,0},{298,0x40c5,0,0,0},
    {299,0x022a,0x00c6,0x00c7,0x009d},
    {280,0x40c5,0x0038,0x0034,0x40c5},
    {281,0x40c9,0x0109,0x010a,0x40c9},
    {282,0x40d9,0x0112,0x00fe,0x40d9},
  };
  memset(east_valid,0,sizeof east_valid);
  for (unsigned i=0;i<9;i++) for (unsigned x=0;x<4;x++) {
    east_map[records[i][0]][8+x]=records[i][1+x];
    east_valid[records[i][0]][8+x]=true;
  }
  CHECK(Dkc1CoveShaftSourceMatches(read_east,NULL));
  CHECK(!Dkc1CoveShaftSourceMatches(NULL,NULL));
  CHECK(!Dkc1CoveShaftSourceMatches(read_east,(void *)1));
  for (unsigned i=0;i<9;i++) for (unsigned x=8;x<=11;x++) {
    unsigned y=records[i][0];east_map[y][x]^=0x4000;
    CHECK(!Dkc1CoveShaftSourceMatches(read_east,NULL));
    east_map[y][x]^=0x4000;east_valid[y][x]=false;
    CHECK(!Dkc1CoveShaftSourceMatches(read_east,NULL));
    east_valid[y][x]=true;
  }
  for (uint32_t y=294;y<=299;y++) for (uint32_t x=7;x<=12;x++)
    for (uint32_t edge=7;edge<=12;edge++) {
      uint32_t dx=99,dy=99;
      bool ok=Dkc1CoveShaftDonorCell(x,y,edge,&dx,&dy);
      CHECK(ok==(y>=295 && y<=298 && x>=9 && x<=11 && edge==8));
      if (ok) {
        CHECK(x>edge && east_valid[dy][dx]);
        CHECK(east_map[dy][8]==east_map[y][8]);
        CHECK(dx==x && dy==280+(y-295)%3);
      } else CHECK(dx==99 && dy==99);
    }
  uint32_t dx=99,dy=99;
  CHECK(!Dkc1CoveShaftDonorCell(UINT32_MAX,295,8,&dx,&dy));
  CHECK(!Dkc1CoveShaftDonorCell(9,UINT32_MAX,8,&dx,&dy));
  CHECK(!Dkc1CoveShaftDonorCell(9,295,UINT32_MAX,&dx,&dy));
  CHECK(!Dkc1CoveShaftDonorCell(9,295,8,NULL,&dy));
  CHECK(!Dkc1CoveShaftDonorCell(9,295,8,&dx,NULL));
  CHECK(dx==99 && dy==99);
  return 0;
}
int main(void) {
  CHECK(Dkc1WallSeamSourceMatches(read_cell,NULL));
  CHECK(!Dkc1WallSeamSourceMatches(NULL,NULL));
  CHECK(!Dkc1WallSeamSourceMatches(read_cell,(void *)1));
  for (unsigned y=0;y<12;y++) for (unsigned x=0;x<2;x++) {
    cells[y][x]^=0x4000;CHECK(!Dkc1WallSeamSourceMatches(read_cell,NULL));cells[y][x]^=0x4000;
  }
  for (uint32_t y=316;y<=322;y++) {
    uint32_t donor=0;
    CHECK(Dkc1WallSeamDonorRow(28,y,29,&donor));
    CHECK(donor>=325 && donor<=327);
    CHECK(cells[y-316][1]==cells[donor-316][1]); // same phase of the native wall
    CHECK(!Dkc1WallSeamDonorRow(28,y,28,&donor)); // native pixels never eligible
    CHECK(!Dkc1WallSeamDonorRow(28,y,30,&donor)); // a different corridor is not eligible
    CHECK(!Dkc1WallSeamDonorRow(27,y,29,&donor));
    CHECK(!Dkc1WallSeamDonorRow(29,y,29,&donor));
  }
  uint32_t donor=99;
  CHECK(!Dkc1WallSeamDonorRow(28,315,29,&donor) && donor==99);
  CHECK(!Dkc1WallSeamDonorRow(28,323,29,&donor) && donor==99);
  CHECK(!Dkc1WallSeamDonorRow(28,316,29,NULL));
  for (unsigned y=0;y<13;y++) for (unsigned x=0;x<3;x++) {
    east_map[311+y][28+x]=east_junction[y][x];east_valid[311+y][28+x]=true;
  }
  for (unsigned i=0;i<7;i++) for (unsigned x=0;x<3;x++) {
    const uint16_t *r=donor_records[i];
    east_map[r[1]][r[0]+x]=r[2+x];east_valid[r[1]][r[0]+x]=true;
  }
  CHECK(Dkc1EastWallSeamSourceMatches(read_east,NULL));
  CHECK(!Dkc1EastWallSeamSourceMatches(NULL,NULL));
  CHECK(!Dkc1EastWallSeamSourceMatches(read_east,(void *)1));
  for (unsigned y=0;y<512;y++) for (unsigned x=0;x<64;x++) {
    if (!east_valid[y][x]) continue;
    east_map[y][x]^=0x4000;
    CHECK(!Dkc1EastWallSeamSourceMatches(read_east,NULL));
    east_map[y][x]^=0x4000;east_valid[y][x]=false;
    CHECK(!Dkc1EastWallSeamSourceMatches(read_east,NULL));
    east_valid[y][x]=true;
  }
  for (uint32_t y=312;y<=322;y++) for (uint32_t x=29;x<=30;x++) {
    uint32_t dx=0,dy=0;
    CHECK(Dkc1EastWallSeamDonor(x,y,28,&dx,&dy));
    CHECK(east_valid[dy][dx]);
    CHECK(east_map[dy][dx-(x-28)]==east_map[y][28]);
    CHECK(!Dkc1EastWallSeamDonor(x,y,29,&dx,&dy));
    CHECK(!Dkc1EastWallSeamDonor(x,y,27,&dx,&dy));
  }
  uint32_t dx=99,dy=99;
  CHECK(!Dkc1EastWallSeamDonor(28,314,28,&dx,&dy));
  CHECK(!Dkc1EastWallSeamDonor(31,314,28,&dx,&dy));
  CHECK(!Dkc1EastWallSeamDonor(29,311,28,&dx,&dy));
  CHECK(!Dkc1EastWallSeamDonor(29,323,28,&dx,&dy));
  CHECK(!Dkc1EastWallSeamDonor(29,314,28,NULL,&dy));
  CHECK(!Dkc1EastWallSeamDonor(29,314,28,&dx,NULL));
  CHECK(dx==99 && dy==99);
  CHECK(test_cove()==0);
  CHECK(test_cove_shaft()==0);
  puts("wall seam source and native containment: PASS");return 0;
}
