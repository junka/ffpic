/* test_tile_map.c —— 验证 HEVC tile 映射表 CtbAddrRsToTs / CtbAddrTsToRs / TileId 的数学正确性
 *
 * 直接从 coding/hevc.c 的 calc_pps_params() 拷贝而来的最小独立版本；
 * 覆盖 H.265 6.5.1 / 6.5.2 的 (6-3) ~ (6-10) 公式。
 *
 * 手动编译：
 *     cc -I/home/gwm/code/github/ffpic -o test_tile_map tests/test_tile_map.c
 * 不注册到 CMakeLists.txt。
 */

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ====== 最小化的结构体子集 ====== */

struct sps {
    int PicWidthInCtbsY;
    int PicHeightInCtbsY;
    int PicSizeInCtbsY;
    int CtbLog2SizeY;
    int MinTbLog2SizeY;
};

struct pps {
    uint8_t tiles_enabled_flag:1;
    uint8_t uniform_spacing_flag:1;
    uint32_t num_tile_columns_minus1;
    uint32_t num_tile_rows_minus1;
    uint32_t *column_width_minus1;
    uint32_t *row_height_minus1;

    int *CtbAddrTsToRs;
    int *CtbAddrRsToTs;
    int *TileId;
};

/* ====== 从 coding/hevc.c calc_pps_params() 拷贝的纯数学逻辑 ====== */

static void calc_pps_params(struct sps *sps, struct pps *pps) {
    if (pps->CtbAddrTsToRs) { free(pps->CtbAddrTsToRs); pps->CtbAddrTsToRs = NULL; }
    if (pps->CtbAddrRsToTs) { free(pps->CtbAddrRsToTs); pps->CtbAddrRsToTs = NULL; }
    if (pps->TileId)        { free(pps->TileId);        pps->TileId        = NULL; }

    uint32_t *colWidth = calloc((pps->num_tile_columns_minus1 + 1), 4);
    if (pps->uniform_spacing_flag) {
        for (uint32_t i = 0; i <= pps->num_tile_columns_minus1; i++)
            colWidth[i] = ((i + 1) * sps->PicWidthInCtbsY) /
                (pps->num_tile_columns_minus1 + 1) -
            (i * sps->PicWidthInCtbsY) / (pps->num_tile_columns_minus1 + 1);
    } else {
        colWidth[pps->num_tile_columns_minus1] = sps->PicWidthInCtbsY;
        for (uint32_t i = 0; i < pps->num_tile_columns_minus1; i++) {
            colWidth[i] = pps->column_width_minus1[i] + 1;
            colWidth[pps->num_tile_columns_minus1] -= colWidth[i];
        }
    }

    uint32_t *rowHeight = calloc((pps->num_tile_rows_minus1 + 1), 4);
    if (pps->uniform_spacing_flag) {
        for (int j = 0; j <= (int)pps->num_tile_rows_minus1; j++)
            rowHeight[j] =
                ((j + 1) * sps->PicHeightInCtbsY) /
                    (pps->num_tile_rows_minus1 + 1) -
                (j * sps->PicHeightInCtbsY) / (pps->num_tile_rows_minus1 + 1);
    } else {
        rowHeight[pps->num_tile_rows_minus1] = sps->PicHeightInCtbsY;
        for (int j = 0; j < (int)pps->num_tile_rows_minus1; j++) {
            rowHeight[j] = pps->row_height_minus1[j] + 1;
            rowHeight[pps->num_tile_rows_minus1] -= rowHeight[j];
        }
    }

    int *colBd = calloc((pps->num_tile_columns_minus1 + 2), 4);
    colBd[0] = 0;
    for (int i = 0; i <= (int)pps->num_tile_columns_minus1; i++)
        colBd[i + 1] = colBd[i] + colWidth[i];

    int *rowBd = calloc((pps->num_tile_rows_minus1 + 2), 4);
    rowBd[0] = 0;
    for (int j = 0; j <= (int)pps->num_tile_rows_minus1; j++)
        rowBd[j + 1] = rowBd[j] + rowHeight[j];

    pps->CtbAddrRsToTs = calloc(sps->PicSizeInCtbsY, 4);
    for (int ctbAddrRs = 0; ctbAddrRs < sps->PicSizeInCtbsY; ctbAddrRs++) {
        int tbX = ctbAddrRs % sps->PicWidthInCtbsY;
        int tbY = ctbAddrRs / sps->PicWidthInCtbsY;
        int tileX = -1, tileY = -1;
        for (int i = 0; i <= (int)pps->num_tile_columns_minus1; i++)
            if (tbX >= colBd[i]) tileX = i;
        for (int j = 0; j <= (int)pps->num_tile_rows_minus1; j++)
            if (tbY >= rowBd[j]) tileY = j;

        pps->CtbAddrRsToTs[ctbAddrRs] = 0;
        for (int i = 0; i < tileX; i++)
            pps->CtbAddrRsToTs[ctbAddrRs] += rowHeight[tileY] * colWidth[i];
        for (int j = 0; j < tileY; j++)
            pps->CtbAddrRsToTs[ctbAddrRs] +=
                sps->PicWidthInCtbsY * rowHeight[j];
        pps->CtbAddrRsToTs[ctbAddrRs] +=
            ((tbY - rowBd[tileY]) * colWidth[tileX] + tbX - colBd[tileX]);
    }

    pps->CtbAddrTsToRs = calloc(sps->PicSizeInCtbsY, 4);
    for (int ctbAddrRs = 0; ctbAddrRs < sps->PicSizeInCtbsY; ctbAddrRs++)
        pps->CtbAddrTsToRs[pps->CtbAddrRsToTs[ctbAddrRs]] = ctbAddrRs;

    pps->TileId = calloc(sps->PicSizeInCtbsY, sizeof(int));
    for (int j = 0, tileIdx = 0; j <= (int)pps->num_tile_rows_minus1; j++) {
        for (int i = 0; i <= (int)pps->num_tile_columns_minus1; i++, tileIdx++) {
            for (int y = rowBd[j]; y < rowBd[j + 1]; y++) {
                for (int x = colBd[i]; x < colBd[i + 1]; x++) {
                    pps->TileId[pps->CtbAddrRsToTs[y * sps->PicWidthInCtbsY + x]] = tileIdx;
                }
            }
        }
    }

    free(colWidth);
    free(rowHeight);
    free(colBd);
    free(rowBd);
}

static void free_pps(struct pps *pps) {
    free(pps->CtbAddrRsToTs);
    free(pps->CtbAddrTsToRs);
    free(pps->TileId);
    free(pps->column_width_minus1);
    free(pps->row_height_minus1);
    memset(pps, 0, sizeof(*pps));
}

static void dump_tables(const struct sps *sps, const struct pps *pps, const char *tag) {
    printf("\n== %s: Pic %dx%d CtBs, tiles %ux%u (uniform=%d) ==\n",
           tag, sps->PicWidthInCtbsY, sps->PicHeightInCtbsY,
           pps->num_tile_columns_minus1 + 1, pps->num_tile_rows_minus1 + 1,
           pps->uniform_spacing_flag);
    printf("ctbAddrRsToTs: [");
    for (int i = 0; i < sps->PicSizeInCtbsY; i++)
        printf("%d%s", pps->CtbAddrRsToTs[i], i+1 < sps->PicSizeInCtbsY ? "," : "");
    printf("]\nctbAddrTsToRs: [");
    for (int i = 0; i < sps->PicSizeInCtbsY; i++)
        printf("%d%s", pps->CtbAddrTsToRs[i], i+1 < sps->PicSizeInCtbsY ? "," : "");
    printf("]\ntileId@TS:    [");
    for (int i = 0; i < sps->PicSizeInCtbsY; i++)
        printf("%d%s", pps->TileId[i], i+1 < sps->PicSizeInCtbsY ? "," : "");
    printf("]\n");
}

/* ====== 场景定义 ====== */

/* 场景 A：单 tile（tiles_enabled_flag=0 → uniform_spacing_flag=0 也被当作 uniform=1 逻辑处理，
 * 最终 colWidth[0]=PicWidth, rowHeight[0]=PicHeight → 恒等映射）*/
static int test_single_tile(void) {
    struct sps sps = { .PicWidthInCtbsY=6, .PicHeightInCtbsY=4,
                       .PicSizeInCtbsY=24, .CtbLog2SizeY=6, .MinTbLog2SizeY=2 };
    struct pps pps = {0};
    pps.num_tile_columns_minus1 = 0;  /* 默认值 */
    pps.num_tile_rows_minus1    = 0;

    calc_pps_params(&sps, &pps);
    dump_tables(&sps, &pps, "single_tile (6x4)");

    for (int rs = 0; rs < 24; rs++) {
        assert(pps.CtbAddrRsToTs[rs] == rs);          /* 恒等 */
        assert(pps.CtbAddrTsToRs[rs] == rs);          /* 逆也恒等 */
        assert(pps.TileId[rs] == 0);                  /* 一个 tile，TileId=0 */
    }
    free_pps(&pps);
    printf("  PASS\n");
    return 0;
}

/* 场景 B：uniform_spacing_flag=1, 2x2 tile, Pic=6x4 CTB
 * colWidth = [6/2 - 0, 12/2 - 6/2] = [3, 3]
 * rowHeight = [4/2 - 0, 8/2 - 4/2] = [2, 2]
 * colBd = [0, 3, 6]; rowBd = [0, 2, 4]
 *
 * rs → (tbX,tbY) → (tileX,tileY) → ts 推导：
 *   rs=0  → (0,0) → (0,0) → 0 + 0 + 0 = 0
 *   rs=1  → (1,0) → (0,0) → 1
 *   rs=2  → (2,0) → (0,0) → 2
 *   rs=3  → (3,0) → (1,0) → 0 + 0 + 0 + (0-0)*3 + (3-3) = 0?
 *     不对，重新按代码算：
 *       tileX=1, tileY=0
 *       val = 0 (tileX=1, for i=0..0: rowHeight[0]*colWidth[0]=2*3=6)
 *             + 0 (tileY=0)
 *             + (0-0)*colWidth[1] + (3-colBd[1]=3-3)=0+0 = 0? 错
 *       重新写代码的三个项：
 *         1) tileX=1: i<1 → i=0 → rowHeight[0]*colWidth[0] = 2*3 = 6
 *         2) tileY=0: j<0 → 0
 *         3) (tbY-rowBd[tileY])*colWidth[tileX] + (tbX-colBd[tileX])
 *            = (0-0)*3 + (3-3) = 0
 *       total = 6!
 *   规范 (6-7) 写的是：CtbAddrRsToTs[colBd[tileX]+rowBd[tileY]*PicWidthInCtbsY]
 *     + colWidth[tileX]*(y-rowBd[tileY]) + (x-colBd[tileX])
 *   其中 CtbAddrRsToTs[tile 起始 rs] 是 tile 起始的 ts。tile 起始 rs=colBd[tileX]+rowBd[tileY]*PW = 3+0*6=3。
 *     规范的 CtbAddrRsToTs[3] 等于：tile 1 起始 ts。tile 0 覆盖 rs 0,1,2 → ts 0,1,2。tile 1 起始 ts=3。
 *     所以规范 CtbAddrRsToTs[3] = 3；然后 colWidth[tileX]*(y-rowBd[tileY]) + (x-colBd[tileX]) = 3*(0-0)+0 = 0。total=3。
 *
 * 啊！我发现代码里有 bug！代码的第一项是：
 *   for (i=0..tileX-1) rowHeight[tileY]*colWidth[i]
 * 这是 tileY 行的所有列宽之和 × rowHeight[tileY]。对于 tileY=0, tileX=1：
 *   rowHeight[0]=2, colWidth[0]=3 → 2*3=6
 * 但规范 (6-7) 中 tile 起始 rs=colBd[tileX]+rowBd[tileY]*PW=3，这个 rs 的 ts 应该是 tile 0 的 CTU 数 = rowHeight[0]*colWidth[0] = 2*3 = 6。
 *
 * 等等，规范 (6-7) 写法：CtbAddrRsToTs[x + y*PW]
 *   = CtbAddrRsToTs[colBd[tileX] + rowBd[tileY]*PW]
 *     + colWidth[tileX]*(y - rowBd[tileY])
 *     + (x - colBd[tileX])
 *
 * 其中 colBd[tileX] + rowBd[tileY]*PW 就是 tile 内左上 CTU 的 rs。我们记它为 rs_tile_start。
 * 那 CtbAddrRsToTs[rs_tile_start]（tile 起始的 rs 对应 ts）是什么？
 *   它等于 tile 0..tileY-1 的总 CTU 数 + tile 行 tileY 中 tile 列 0..tileX-1 的 CTU 数
 *   = rowHeight[tileY] * sum(colWidth[0..tileX-1]) + PW * sum(rowHeight[0..tileY-1])
 *
 * 这正是代码里第一项+第二项！那第三项就是当前 tile 内偏移。
 *
 * 那我刚才按规范算 rs=3 (x=3, y=0, tileX=1, tileY=0)：
 *   CtbAddrRsToTs[rs_tile_start=colBd[1]+rowBd[0]*6=3+0=3]
 *     = rowHeight[0]*colWidth[0] + 0 = 2*3 = 6
 *   + colWidth[1]*(0-0) + (3-3)
 *   = 6
 *
 * 好，那规范算出来 ts=6？让我看 tile 扫描顺序：tile 0 先扫完所有 CTU，然后 tile 1，tile 2，tile 3。
 * Tile 0 覆盖 rs 0,1,2（row 0） + rs 6,7,8（row 1）= 共 6 个 CTU，ts 0..5。
 * Tile 1 覆盖 rs 3,4,5（row 0） + rs 9,10,11（row 1）= 共 6 个 CTU，ts 6..11。
 * 所以 rs=3（tile 1 第一个 CTU）→ ts=6，与代码和规范计算都一致。✅
 *
 * TS 顺序（tile 扫描）为：
 *   Tile0: rs [0,1,2, 6,7,8]     → ts 0,1,2,3,4,5
 *   Tile1: rs [3,4,5, 9,10,11]   → ts 6,7,8,9,10,11
 *   Tile2: rs [12,13,14, 18,19,20] → ts 12,13,14,15,16,17
 *   Tile3: rs [15,16,17, 21,22,23] → ts 18,19,20,21,22,23
 */
static int test_uniform_2x2(void) {
    struct sps sps = { .PicWidthInCtbsY=6, .PicHeightInCtbsY=4,
                       .PicSizeInCtbsY=24, .CtbLog2SizeY=6, .MinTbLog2SizeY=2 };
    struct pps pps = {0};
    pps.uniform_spacing_flag = 1;
    pps.num_tile_columns_minus1 = 1;  /* 2 列 */
    pps.num_tile_rows_minus1    = 1;  /* 2 行 */

    calc_pps_params(&sps, &pps);
    dump_tables(&sps, &pps, "uniform 2x2, Pic 6x4");

    /* 期望值（Python 对拍得到） */
    int expected_rs2ts[24] = {
        0, 1, 2, 6, 7, 8,
        3, 4, 5, 9,10,11,
        12,13,14,18,19,20,
        15,16,17, 21,22,23,
    };
    int expected_tileid[24] = {
        0,0,0, 1,1,1,
        0,0,0, 1,1,1,
        2,2,2, 3,3,3,
        2,2,2, 3,3,3,
    };
    for (int rs = 0; rs < 24; rs++) {
        assert(pps.CtbAddrRsToTs[rs] == expected_rs2ts[rs]);
        assert(pps.TileId[pps.CtbAddrRsToTs[rs]] == expected_tileid[rs]);
        assert(pps.CtbAddrTsToRs[pps.CtbAddrRsToTs[rs]] == rs);  /* 双射 */
    }
    /* TileId 按 tile 扫描顺序 (0,1,2,3) 递增 */
    for (int ts = 0; ts < 6; ts++)   assert(pps.TileId[ts] == 0);
    for (int ts = 6; ts < 12; ts++)  assert(pps.TileId[ts] == 1);
    for (int ts = 12; ts < 18; ts++) assert(pps.TileId[ts] == 2);
    for (int ts = 18; ts < 24; ts++) assert(pps.TileId[ts] == 3);

    free_pps(&pps);
    printf("  PASS\n");
    return 0;
}

/* 场景 C：uniform_spacing_flag=1, 1x1 tile —— 同样应恒等（与场景 A 等价）*/
static int test_uniform_1x1(void) {
    struct sps sps = { .PicWidthInCtbsY=3, .PicHeightInCtbsY=5,
                       .PicSizeInCtbsY=15, .CtbLog2SizeY=6, .MinTbLog2SizeY=2 };
    struct pps pps = {0};
    pps.uniform_spacing_flag = 1;
    pps.num_tile_columns_minus1 = 0;
    pps.num_tile_rows_minus1    = 0;

    calc_pps_params(&sps, &pps);
    dump_tables(&sps, &pps, "uniform 1x1, Pic 3x5");

    for (int rs = 0; rs < 15; rs++) {
        assert(pps.CtbAddrRsToTs[rs] == rs);
        assert(pps.CtbAddrTsToRs[rs] == rs);
        assert(pps.TileId[rs] == 0);
    }
    free_pps(&pps);
    printf("  PASS\n");
    return 0;
}

/* 场景 D：显式列宽 (non-uniform)
 * num_tile_columns_minus1=1, num_tile_rows_minus1=1
 * column_width_minus1[0] = 1 → colWidth[0] = 2
 * colWidth[1] = PicWidthInCtbsY(6) - 2 = 4
 * row 显式：row_height_minus1[0]=1 → rowHeight[0]=2, rowHeight[1]=4-2=2
 * colBd = [0, 2, 6]; rowBd = [0, 2, 4]
 *
 * Tile 0: rs 0,1 + 6,7 → ts 0,1,2,3
 * Tile 1: rs 2,3,4,5 + 8,9,10,11 → ts 4..11
 * Tile 2: rs 12,13 + 18,19 → ts 12..15
 * Tile 3: rs 14..17 + 20..23 → ts 16..23
 */
static int test_non_uniform(void) {
    struct sps sps = { .PicWidthInCtbsY=6, .PicHeightInCtbsY=4,
                       .PicSizeInCtbsY=24, .CtbLog2SizeY=6, .MinTbLog2SizeY=2 };
    struct pps pps = {0};
    pps.uniform_spacing_flag = 0;
    pps.num_tile_columns_minus1 = 1;
    pps.num_tile_rows_minus1    = 1;
    pps.column_width_minus1 = malloc(sizeof(uint32_t) * 1); /* num_tile_columns_minus1 */
    pps.column_width_minus1[0] = 1;                           /* colWidth[0] = 2 */
    pps.row_height_minus1 = malloc(sizeof(uint32_t) * 1);
    pps.row_height_minus1[0] = 1;                             /* rowHeight[0] = 2 */

    calc_pps_params(&sps, &pps);
    dump_tables(&sps, &pps, "non-uniform col=[2,4] row=[2,2], Pic 6x4");

    /* Python 对拍期望值 */
    int expected_rs2ts[24] = {
        0,1,4,5,6,7, 2,3,8,9,10,11, 12,13,16,17,18,19, 14,15,20,21,22,23,
    };
    for (int rs = 0; rs < 24; rs++) {
        assert(pps.CtbAddrRsToTs[rs] == expected_rs2ts[rs]);
        assert(pps.CtbAddrTsToRs[expected_rs2ts[rs]] == rs); /* 双射 */
    }
    /* TileId: tile0=0, tile1=1, tile2=2, tile3=3 */
    for (int ts = 0; ts < 4; ts++)    assert(pps.TileId[ts] == 0);
    for (int ts = 4; ts < 12; ts++)   assert(pps.TileId[ts] == 1);
    for (int ts = 12; ts < 16; ts++)  assert(pps.TileId[ts] == 2);
    for (int ts = 16; ts < 24; ts++)  assert(pps.TileId[ts] == 3);

    free_pps(&pps);
    printf("  PASS\n");
    return 0;
}

/* 场景 E：余数分配 —— PicWidthInCtbsY=5, PicHeightInCtbsY=5, 2x2 uniform
 * colWidth: ((i+1)*5)/2 - (i*5)/2 = [2, 5-2=3] → colBd=[0,2,5]
 * rowHeight: 同样 [2, 3] → rowBd=[0,2,5]
 * 所有 tile CTU 数之和 = 2*2 + 3*2 + 2*3 + 3*3 = 4+6+6+9 = 25 ✔
 *
 * Tile 0 (rs 0,1 + 5,6):   ts 0,1,2,3
 * Tile 1 (rs 2,3,4 + 7,8,9): ts 4..9
 * Tile 2 (rs 10,11 + 15,16):  ts 10..13
 * Tile 3 (rs 12,13,14 + 17..24): ts 14..24
 */
static int test_remainder_allocation(void) {
    struct sps sps = { .PicWidthInCtbsY=5, .PicHeightInCtbsY=5,
                       .PicSizeInCtbsY=25, .CtbLog2SizeY=6, .MinTbLog2SizeY=2 };
    struct pps pps = {0};
    pps.uniform_spacing_flag = 1;
    pps.num_tile_columns_minus1 = 1;
    pps.num_tile_rows_minus1    = 1;

    calc_pps_params(&sps, &pps);
    dump_tables(&sps, &pps, "uniform 2x2, Pic 5x5 (余数)");

    for (int rs = 0; rs < 25; rs++) {
        assert(pps.CtbAddrTsToRs[pps.CtbAddrRsToTs[rs]] == rs); /* 双射 */
    }
    /* Python 对拍期望值 */
    int expected_rs2ts[25] = {
        0,1,4,5,6, 2,3,7,8,9, 10,11,16,17,18, 12,13,19,20,21, 14,15,22,23,24,
    };
    for (int rs = 0; rs < 25; rs++)
        assert(pps.CtbAddrRsToTs[rs] == expected_rs2ts[rs]);

    /* TileId row-major: Tile0=0, Tile1=1, Tile2=2, Tile3=3 */
    int cnt[4] = {0};
    for (int ts = 0; ts < 25; ts++)
        cnt[pps.TileId[ts]]++;
    assert(cnt[0] == 4);
    assert(cnt[1] == 6);
    assert(cnt[2] == 6);
    assert(cnt[3] == 9);

    free_pps(&pps);
    printf("  PASS\n");
    return 0;
}

/* 场景 F：非 uniform 显式列宽，num_tile_columns_minus1=2（3 列）
 * colWidth_minus1=[0, 1] → colWidth=[1, 2, PicWidthInCtbsY(7)-1-2=4]
 * row 也显式：row_height_minus1=[0] → rowHeight=[1, 2]  PicHeightInCtbsY=3 所以 3-1=2 行
 *
 * colBd=[0, 1, 3, 7], rowBd=[0, 1, 3]
 * TileId row-major:
 *   j=0 (tileY=0, rowBd[0..1)=row 0): i=0→tile0, i=1→tile1, i=2→tile2
 *   j=1 (tileY=1, rowBd[1..3)=row 1,2): i=0→tile3, i=1→tile4, i=2→tile5
 */
static int test_three_columns(void) {
    struct sps sps = { .PicWidthInCtbsY=7, .PicHeightInCtbsY=3,
                       .PicSizeInCtbsY=21, .CtbLog2SizeY=6, .MinTbLog2SizeY=2 };
    struct pps pps = {0};
    pps.uniform_spacing_flag = 0;
    pps.num_tile_columns_minus1 = 2;
    pps.num_tile_rows_minus1    = 1;
    pps.column_width_minus1 = malloc(sizeof(uint32_t) * 2);
    pps.column_width_minus1[0] = 0;  /* colWidth[0] = 1 */
    pps.column_width_minus1[1] = 1;  /* colWidth[1] = 2, 最后一列 = 7-1-2 = 4 */
    pps.row_height_minus1 = malloc(sizeof(uint32_t) * 1);
    pps.row_height_minus1[0] = 0;  /* rowHeight[0] = 1, 最后一行 = 3-1 = 2 */

    calc_pps_params(&sps, &pps);
    dump_tables(&sps, &pps, "non-uniform 3-col, Pic 7x3");

    /* Python 对拍期望值 */
    int expected_rs2ts[21] = {
        0,1,2,3,4,5,6, 7,9,10,13,14,15,16, 8,11,12,17,18,19,20,
    };
    for (int rs = 0; rs < 21; rs++) {
        assert(pps.CtbAddrRsToTs[rs] == expected_rs2ts[rs]);
        assert(pps.CtbAddrTsToRs[expected_rs2ts[rs]] == rs);
    }

    int cnt[6] = {0};
    for (int ts = 0; ts < 21; ts++) cnt[pps.TileId[ts]]++;
    assert(cnt[0] == 1);
    assert(cnt[1] == 2);
    assert(cnt[2] == 4);
    assert(cnt[3] == 2);
    assert(cnt[4] == 4);
    assert(cnt[5] == 8);

    free_pps(&pps);
    printf("  PASS\n");
    return 0;
}

int main(void) {
    int rc = 0;
    rc |= test_single_tile();
    rc |= test_uniform_1x1();
    rc |= test_uniform_2x2();
    rc |= test_non_uniform();
    rc |= test_remainder_allocation();
    rc |= test_three_columns();
    if (!rc) printf("\nAll tile mapping tests passed.\n");
    return rc;
}
