/* tests/test_pool.c - 验证多线程瓦片渲染线程池 + LRU */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include "render_pool.h"

int wmain(void)
{
    FILE *f = fopen("test.pdf", "rb");
    long n;
    unsigned char *data;
    RenderPool *p;
    HBITMAP bmp = NULL;
    int i, w = 0, h = 0;
    FILE *log;

    if (!f) { printf("no test.pdf\n"); return 1; }
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    data = (unsigned char *)malloc((size_t)n);
    if (fread(data, 1, (size_t)n, f) != (size_t)n) { fclose(f); return 1; }
    fclose(f);

    log = fopen("pool_log.txt", "w");
    p = pool_create(NULL, data, (size_t)n, 4);
    if (!p) { printf("pool_create failed\n"); return 1; }
    pool_set_gen(p, 1);
    pool_request(p, 0, 2.0f, 0, 0, 1024, 1024);

    for (i = 0; i < 100 && !bmp; i++) {
        pool_drain(p);
        bmp = pool_lookup(p, 0, 2.0f, 0, 0, &w, &h);
        if (!bmp) Sleep(50);
    }
    if (log) {
        fprintf(log, "tile=%p w=%d h=%d\n", (void *)bmp, w, h);
        fclose(log);
    }
    printf("tile=%p w=%d h=%d\n", (void *)bmp, w, h);

    pool_clear(p);
    pool_destroy(p);
    free(data);
    return bmp ? 0 : 1;
}
