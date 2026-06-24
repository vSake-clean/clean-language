#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <poll.h>
#include <stdint.h>
#include <math.h>

/* Built-in cube model (fallback when no .obj loaded) */
#define BUILTIN_NUM_VERTICES 8
#define BUILTIN_NUM_TRIANGLES 12
static const float builtin_vtx[BUILTIN_NUM_VERTICES][3] = {
    {-1,-1,-1},{ 1,-1,-1},{ 1, 1,-1},{-1, 1,-1},
    {-1,-1, 1},{ 1,-1, 1},{ 1, 1, 1},{-1, 1, 1}
};
static const int builtin_tri[BUILTIN_NUM_TRIANGLES][3] = {
    {0,1,2},{0,2,3},{1,5,6},{1,6,2},
    {5,4,7},{5,7,6},{4,0,3},{4,3,7},
    {3,2,6},{3,6,7},{4,5,1},{4,1,0}
};
static const float builtin_norm[BUILTIN_NUM_TRIANGLES][3] = {
    {0,0,-1},{0,0,-1},{1,0,0},{1,0,0},
    {0,0,1},{0,0,1},{-1,0,0},{-1,0,0},
    {0,1,0},{0,1,0},{0,-1,0},{0,-1,0}
};

#define MAX_WINS 16

static Display *dpy = NULL;
static Window wins[MAX_WINS];
static GC gcs[MAX_WINS];
static int win_n = 0;
static XFontStruct *font = NULL;

static int64_t ev_x = 0, ev_y = 0, ev_btn = 0, ev_key = 0;

int64_t clgui_init(void) {
    dpy = XOpenDisplay(NULL);
    if (!dpy) return -1;
    font = XLoadQueryFont(dpy, "fixed");
    if (!font) font = XLoadQueryFont(dpy, "6x13");
    return 0;
}

int64_t clgui_window(int64_t w, int64_t h) {
    if (win_n >= MAX_WINS) return -1;
    int scr = DefaultScreen(dpy);
    Window ww = XCreateSimpleWindow(dpy, RootWindow(dpy, scr),
        0, 0, w, h, 1, BlackPixel(dpy, scr), 0xEBEBEB);
    XSelectInput(dpy, ww, ExposureMask | ButtonPressMask | KeyPressMask | StructureNotifyMask);
    XMapWindow(dpy, ww);
    GC g = XCreateGC(dpy, ww, 0, NULL);
    if (font) XSetFont(dpy, g, font->fid);
    wins[win_n] = ww;
    gcs[win_n] = g;
    return win_n++;
}

void clgui_title(int64_t idx, const char *title) {
    if (idx < 0 || idx >= win_n) return;
    XStoreName(dpy, wins[idx], title);
}

void clgui_fill(int64_t idx, int64_t x, int64_t y, int64_t w, int64_t h, int64_t color) {
    if (idx < 0 || idx >= win_n) return;
    XSetForeground(dpy, gcs[idx], color);
    XFillRectangle(dpy, wins[idx], gcs[idx], x, y, w, h);
}

void clgui_draw_str(int64_t idx, int64_t x, int64_t y, const char *s) {
    if (idx < 0 || idx >= win_n) return;
    XSetForeground(dpy, gcs[idx], 0x000000);
    XDrawString(dpy, wins[idx], gcs[idx], x, y, s, strlen(s));
}

void clgui_draw_int(int64_t idx, int64_t x, int64_t y, int64_t n) {
    if (idx < 0 || idx >= win_n) return;
    char buf[32];
    snprintf(buf, sizeof(buf), "%lld", (long long)n);
    XSetForeground(dpy, gcs[idx], 0x000000);
    XDrawString(dpy, wins[idx], gcs[idx], x, y, buf, strlen(buf));
}

int64_t clgui_str_width(const char *s) {
    if (!font) return 6 * strlen(s);
    return XTextWidth(font, s, strlen(s));
}

int64_t clgui_color(int64_t r, int64_t g, int64_t b) {
    return (r << 16) | (g << 8) | b;
}

int64_t clgui_event(int64_t idx) {
    if (idx < 0 || idx >= win_n) return -1;
    int fd = ConnectionNumber(dpy);
    struct pollfd pfd = {fd, POLLIN, 0};
    while (XPending(dpy) == 0) {
        poll(&pfd, 1, -1);
    }
    XEvent e;
    XNextEvent(dpy, &e);
    switch (e.type) {
    case Expose:
        return e.xexpose.count == 0 ? 1 : 0;
    case ButtonPress:
        ev_x = e.xbutton.x;
        ev_y = e.xbutton.y;
        ev_btn = e.xbutton.button;
        return 2;
    case KeyPress: {
        KeySym ks = XLookupKeysym(&e.xkey, 0);
        ev_key = ks;
        if (ks == XK_Escape) return -1;
        return 3;
    }
    case DestroyNotify:
        return -1;
    default:
        return 0;
    }
}

int64_t clgui_ev_x(void) { return ev_x; }
int64_t clgui_ev_y(void) { return ev_y; }
int64_t clgui_ev_key(void) { return ev_key; }

int64_t clgui_poll(int64_t idx, int64_t ms) {
    if (idx < 0 || idx >= win_n) return -1;
    int fd = ConnectionNumber(dpy);
    struct pollfd pfd = {fd, POLLIN, 0};
    while (XPending(dpy) == 0) {
        int r = poll(&pfd, 1, (int)ms);
        if (r <= 0) return 0;  /* timeout or error */
    }
    XEvent e;
    XNextEvent(dpy, &e);
    switch (e.type) {
    case Expose:
        return e.xexpose.count == 0 ? 1 : 0;
    case ButtonPress:
        ev_x = e.xbutton.x;
        ev_y = e.xbutton.y;
        ev_btn = e.xbutton.button;
        return 2;
    case KeyPress: {
        KeySym ks = XLookupKeysym(&e.xkey, 0);
        ev_key = ks;
        if (ks == XK_Escape) return -1;
        return 3;
    }
    case DestroyNotify:
        return -1;
    default:
        return 0;
    }
}

void clgui_flush(int64_t idx) {
    (void)idx;
    XFlush(dpy);
}

void clgui_close(int64_t idx) {
    if (idx < 0 || idx >= win_n) return;
    XDestroyWindow(dpy, wins[idx]);
    XFreeGC(dpy, gcs[idx]);
}

void print_int(int64_t n) {
    printf("%ld\n", (long)n);
    fflush(stdout);
}

void sleep(int64_t secs) {
    struct timespec ts = {secs, 0};
    nanosleep(&ts, NULL);
}

void print_str(int64_t ptr, int64_t len) {
    fwrite((void*)(intptr_t)ptr, 1, len, stdout);
    fflush(stdout);
}

int64_t calc_expr(void) {
    char buf[256];
    size_t i = 0;
    int c;
    c = getchar();
    if (c == EOF) exit(0);
    ungetc(c, stdin);
    while (i < 255 && (c = getchar()) != EOF && c != '\n') buf[i++] = c;
    buf[i] = 0;
    if (i == 0) return 0;
    char *p = buf;
    int64_t term = 0, result = 0;
    while (*p == ' ') p++;
    while (*p >= '0' && *p <= '9') { term = term * 10 + (*p - '0'); p++; }
    while (1) {
        while (*p == ' ') p++;
        char op = *p;
        if (!op) break;
        p++;
        while (*p == ' ') p++;
        int64_t num = 0;
        while (*p >= '0' && *p <= '9') { num = num * 10 + (*p - '0'); p++; }
        if (op == '+' || op == '-') {
            result += term;
            term = (op == '+') ? num : -num;
        } else {
            if (op == '*' || op == 'x' || op == 'X')
                term *= num;
            else if (op == ':' || op == '/')
                term = num ? term / num : 0;
        }
    }
    return result + term;
}

int64_t read_int(void) {
    char buf[32];
    if (!fgets(buf, sizeof(buf), stdin)) return 0;
    return atoll(buf);
}

/* ============ 3D Engine ============ */

/* ---- Dynamic model data ---- */
static float *mdl_vert = NULL;  /* [nv * 3] original vertices (raw from OBJ) */
static int mdl_nv = 0;
static int (*mdl_tri)[3] = NULL; /* [nt][3] triangle vertex indices */
static float *mdl_norm = NULL;   /* [nt * 3] pre-computed face normals */
static float *mdl_color = NULL;  /* [nt * 3] per-triangle RGB (0-1) */
static int mdl_nt = 0;

/* ---- Renderer state ---- */
static uint32_t *eng_fb = NULL;
static int eng_w = 0, eng_h = 0;
static int64_t eng_win = -1;
static XImage *eng_img = NULL;

static int eng_nv = 0, eng_nt = 0;
static float *eng_cv = NULL;     /* centered/normalized vertices [nv*3] */
static float *eng_xv = NULL;     /* transformed per-frame [nv*3] */
static float *eng_xn = NULL;     /* rotated normals per-frame [nt*3] */
static int *eng_order = NULL;    /* triangle render order [nt] */
static float *eng_depth = NULL;  /* triangle depth [nt] */
static float *eng_zbuf = NULL;   /* Z-buffer [w*h] for per-pixel depth test */

static int64_t eng_t0 = 0;
static int64_t eng_start_ms = 0;

/* ---- CLI args ---- */
static int saved_argc = 0;
static char **saved_argv = NULL;

static int64_t nanos(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000LL + (int64_t)ts.tv_nsec;
}

/* ---- free loaded model ---- */
static void free_model_data(void) {
    free(mdl_vert); mdl_vert = NULL;
    free(mdl_tri);  mdl_tri = NULL;
    free(mdl_norm); mdl_norm = NULL;
    free(mdl_color); mdl_color = NULL;
    mdl_nv = 0; mdl_nt = 0;
}

/* ---- OBJ/MTL parser ---- */
#include <ctype.h>

static int parse_obj(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;

    /* Resolve MTL path (same dir as OBJ) */
    char mtl_dir[1024], mtl_name[256];
    {
        const char *slash = strrchr(path, '/');
        if (slash) {
            int len = (int)(slash - path);
            if (len > 1023) len = 1023;
            memcpy(mtl_dir, path, len); mtl_dir[len] = 0;
        } else {
            mtl_dir[0] = '.'; mtl_dir[1] = 0;
        }
    }

    /* First pass: count vertices and faces */
    int nv = 0, nf = 0;
    {
        char line[1024];
        rewind(f);
        while (fgets(line, sizeof(line), f)) {
            char *p = line;
            while (*p == ' ' || *p == '\t') p++;
            if (p[0] == 'v' && (p[1] == ' ' || p[1] == '\t')) nv++;
            else if (p[0] == 'f' && (p[1] == ' ' || p[1] == '\t')) nf++;
        }
    }

    /* Allocate */
    float *verts = (float *)calloc(nv * 3, sizeof(float));
    int *face_buf = (int *)malloc(nf * 12 * sizeof(int)); /* max 12 verts per face */
    int *face_sizes = (int *)malloc(nf * sizeof(int));
    int *face_mat = (int *)malloc(nf * sizeof(int));
    if (!verts || !face_buf || !face_sizes || !face_mat) {
        free(verts); free(face_buf); free(face_sizes); free(face_mat);
        fclose(f); return -1;
    }

    /* MTL materials */
    typedef struct { char name[256]; float r, g, b; } Mtl;
    Mtl *mtls = NULL;
    int nm = 0, cm = 16;
    mtls = (Mtl *)malloc(cm * sizeof(Mtl));
    if (!mtls) { free(verts); free(face_buf); free(face_sizes); free(face_mat); fclose(f); return -1; }

    int vi = 0, fi = 0;
    int cur_mat = -1;

    rewind(f);
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (!*p || *p == '#' || *p == '\n') continue;

        if (p[0] == 'v' && p[1] == ' ' || p[1] == '\t') {
            float x, y, z;
            if (sscanf(p + 1, "%f %f %f", &x, &y, &z) >= 3 && vi < nv) {
                verts[vi*3+0] = x; verts[vi*3+1] = y; verts[vi*3+2] = z;
                vi++;
            }
        } else if (p[0] == 'f' && (p[1] == ' ' || p[1] == '\t')) {
            int idxs[12];
            int cnt = 0;
            char *q = p + 1;
            while (*q && cnt < 12) {
                while (*q == ' ' || *q == '\t') q++;
                if (!*q || *q == '\n') break;
                int i = atoi(q);
                if (i > 0) idxs[cnt++] = i - 1;
                else if (i < 0 && vi > 0) idxs[cnt++] = vi + i;
                /* skip vt and vn */
                while (*q && *q != ' ' && *q != '\t' && *q != '\n') q++;
            }
            if (cnt >= 3 && fi < nf) {
                int *fb = &face_buf[fi * 12];
                for (int k = 0; k < cnt; k++) fb[k] = idxs[k];
                face_sizes[fi] = cnt;
                face_mat[fi] = cur_mat;
                fi++;
            }
        } else if (strncmp(p, "usemtl", 6) == 0) {
            p += 6;
            while (*p == ' ' || *p == '\t') p++;
            char *nl = p;
            while (*nl && *nl != '\n' && *nl != '\r') nl++;
            *nl = 0;
            /* find material */
            cur_mat = -1;
            for (int k = 0; k < nm; k++) {
                if (strcmp(mtls[k].name, p) == 0) { cur_mat = k; break; }
            }
        } else if (strncmp(p, "mtllib", 6) == 0) {
            p += 6;
            while (*p == ' ' || *p == '\t') p++;
            char *nl = p;
            while (*nl && *nl != '\n' && *nl != '\r') nl++;
            *nl = 0;
            /* Try to load MTL file */
            char mtl_path[1024];
            snprintf(mtl_path, sizeof(mtl_path), "%s/%s", mtl_dir, p);
            FILE *mf = fopen(mtl_path, "rb");
            if (!mf) continue;
            char ml[512];
            int cur_mtl = -1;
            while (fgets(ml, sizeof(ml), mf)) {
                char *mp = ml;
                while (*mp == ' ' || *mp == '\t') mp++;
                if (!*mp || *mp == '#' || *mp == '\n') continue;
                if (strncmp(mp, "newmtl", 6) == 0) {
                    mp += 6;
                    while (*mp == ' ' || *mp == '\t') mp++;
                    char *mnl = mp;
                    while (*mnl && *mnl != '\n' && *mnl != '\r') mnl++;
                    *mnl = 0;
                    if (nm >= cm) { cm *= 2; mtls = (Mtl *)realloc(mtls, cm * sizeof(Mtl)); }
                    strncpy(mtls[nm].name, mp, 255);
                    mtls[nm].r = 0.7f; mtls[nm].g = 0.7f; mtls[nm].b = 0.7f;
                    cur_mtl = nm++;
                } else if (strncmp(mp, "Kd", 2) == 0 && cur_mtl >= 0) {
                    sscanf(mp + 2, "%f %f %f", &mtls[cur_mtl].r, &mtls[cur_mtl].g, &mtls[cur_mtl].b);
                }
            }
            fclose(mf);
        }
    }
    fclose(f);
    nv = vi;

    /* Triangulate faces */
    int total_tris = 0;
    for (int i = 0; i < fi; i++) total_tris += face_sizes[i] - 2;

    int (*tris)[3] = (int (*)[3])malloc(total_tris * sizeof(int[3]));
    float *norms = (float *)malloc(total_tris * 3 * sizeof(float));
    float *colors = (float *)malloc(total_tris * 3 * sizeof(float));
    if (!tris || !norms || !colors) {
        free(verts); free(face_buf); free(face_sizes); free(face_mat);
        free(tris); free(norms); free(colors); free(mtls);
        fclose(f); return -1;
    }

    int ti = 0;
    for (int i = 0; i < fi; i++) {
        int *fb = &face_buf[i * 12];
        int sz = face_sizes[i];
        int mi = face_mat[i];
        float mr = 0.7f, mg = 0.7f, mb = 0.7f;
        if (mi >= 0 && mi < nm) { mr = mtls[mi].r; mg = mtls[mi].g; mb = mtls[mi].b; }
        for (int k = 1; k < sz - 1; k++) {
            int i0 = fb[0], i1 = fb[k], i2 = fb[k + 1];
            if (i0 >= nv || i1 >= nv || i2 >= nv) continue;
            tris[ti][0] = i0; tris[ti][1] = i1; tris[ti][2] = i2;
            /* face normal */
            float ax = verts[i1*3+0] - verts[i0*3+0];
            float ay = verts[i1*3+1] - verts[i0*3+1];
            float az = verts[i1*3+2] - verts[i0*3+2];
            float bx = verts[i2*3+0] - verts[i0*3+0];
            float by = verts[i2*3+1] - verts[i0*3+1];
            float bz = verts[i2*3+2] - verts[i0*3+2];
            float nx = ay*bz - az*by;
            float ny = az*bx - ax*bz;
            float nz = ax*by - ay*bx;
            float len = sqrtf(nx*nx + ny*ny + nz*nz);
            if (len > 0) { nx /= len; ny /= len; nz /= len; }
            norms[ti*3+0] = nx; norms[ti*3+1] = ny; norms[ti*3+2] = nz;
            colors[ti*3+0] = mr; colors[ti*3+1] = mg; colors[ti*3+2] = mb;
            ti++;
        }
    }
    total_tris = ti;

    free(face_buf); free(face_sizes); free(face_mat); free(mtls);

    /* Store in global model */
    free_model_data();
    mdl_vert = verts; mdl_nv = nv;
    mdl_tri = tris; mdl_nt = total_tris;
    mdl_norm = norms; mdl_color = colors;
    return 0;
}

/* ---- Setup model centering & allocate render buffers ---- */
void renderer_free(void); /* forward decl */
static int setup_model(void) {
    if (mdl_nv == 0 || mdl_nt == 0) return -1;

    /* Compute centroid and scale from vertices (or builtin_vtx if embedded) */
    float cx = 0, cy = 0, cz = 0;
    int use_builtin = (mdl_vert == NULL);
    for (int i = 0; i < mdl_nv; i++) {
        float x = use_builtin ? builtin_vtx[i][0] : mdl_vert[i*3+0];
        float y = use_builtin ? builtin_vtx[i][1] : mdl_vert[i*3+1];
        float z = use_builtin ? builtin_vtx[i][2] : mdl_vert[i*3+2];
        cx += x; cy += y; cz += z;
    }
    cx /= mdl_nv; cy /= mdl_nv; cz /= mdl_nv;
    float max_r = 0;
    for (int i = 0; i < mdl_nv; i++) {
        float x = use_builtin ? builtin_vtx[i][0] : mdl_vert[i*3+0];
        float y = use_builtin ? builtin_vtx[i][1] : mdl_vert[i*3+1];
        float z = use_builtin ? builtin_vtx[i][2] : mdl_vert[i*3+2];
        float dx = x - cx, dy = y - cy, dz = z - cz;
        float r = dx*dx + dy*dy + dz*dz;
        if (r > max_r) max_r = r;
    }
    float scale = sqrtf(max_r);
    if (scale < 0.001f) scale = 1;

    /* Free old centered vertices if any */
    free(eng_cv); eng_cv = NULL;
    free(eng_xv); eng_xv = NULL;
    free(eng_xn); eng_xn = NULL;
    free(eng_order); eng_order = NULL;
    free(eng_depth); eng_depth = NULL;

    eng_nv = mdl_nv;
    eng_nt = mdl_nt;

    eng_cv = (float *)malloc(eng_nv * 3 * sizeof(float));
    eng_xv = (float *)malloc(eng_nv * 3 * sizeof(float));
    eng_xn = (float *)malloc(eng_nt * 3 * sizeof(float));
    eng_order = (int *)malloc(eng_nt * sizeof(int));
    eng_depth = (float *)malloc(eng_nt * sizeof(float));
    if (!eng_cv || !eng_xv || !eng_xn || !eng_order || !eng_depth) {
        renderer_free(); return -1;
    }

    for (int i = 0; i < eng_nv; i++) {
        float x = use_builtin ? builtin_vtx[i][0] : mdl_vert[i*3+0];
        float y = use_builtin ? builtin_vtx[i][1] : mdl_vert[i*3+1];
        float z = use_builtin ? builtin_vtx[i][2] : mdl_vert[i*3+2];
        eng_cv[i*3+0] = (x - cx) / scale;
        eng_cv[i*3+1] = (y - cy) / scale;
        eng_cv[i*3+2] = (z - cz) / scale;
    }
    for (int i = 0; i < eng_nt; i++) eng_order[i] = i;
    return 0;
}

/* ---- Public API ---- */

int64_t renderer_init(int64_t win_idx, int64_t w, int64_t h) {
    eng_win = win_idx;
    eng_w = (int)w;
    eng_h = (int)h;

    eng_fb = (uint32_t *)malloc(eng_w * eng_h * sizeof(uint32_t));
    if (!eng_fb) return -1;

    eng_zbuf = (float *)malloc(eng_w * eng_h * sizeof(float));
    if (!eng_zbuf) { free(eng_fb); eng_fb = NULL; return -1; }

    int scr = DefaultScreen(dpy);
    eng_img = XCreateImage(dpy, DefaultVisual(dpy, scr), 24, ZPixmap,
                           0, (char*)eng_fb, eng_w, eng_h, 32, 0);
    if (!eng_img) { renderer_free(); return -1; }

    /* Use embedded Apple model by default */
    free_model_data();
    mdl_vert = NULL;
    mdl_nv = BUILTIN_NUM_VERTICES;
    mdl_nt = BUILTIN_NUM_TRIANGLES;
    mdl_tri = NULL;
    mdl_norm = NULL;
    mdl_color = NULL;

    if (setup_model() != 0) { renderer_free(); return -1; }
    eng_t0 = nanos();
    return 0;
}

int64_t renderer_load_obj(int64_t win_idx, int64_t w, int64_t h, int64_t path_ptr) {
    eng_win = win_idx;
    eng_w = (int)w;
    eng_h = (int)h;

    /* Allocate framebuffer if not yet */
    if (!eng_fb) {
        eng_fb = (uint32_t *)malloc(eng_w * eng_h * sizeof(uint32_t));
        if (!eng_fb) return -1;
        eng_zbuf = (float *)malloc(eng_w * eng_h * sizeof(float));
        if (!eng_zbuf) { free(eng_fb); eng_fb = NULL; return -1; }
        int scr = DefaultScreen(dpy);
        eng_img = XCreateImage(dpy, DefaultVisual(dpy, scr), 24, ZPixmap,
                               0, (char*)eng_fb, eng_w, eng_h, 32, 0);
        if (!eng_img) { renderer_free(); return -1; }
    }

    const char *path = (const char *)(intptr_t)path_ptr;
    if (parse_obj(path) != 0) return -1;
    if (setup_model() != 0) { renderer_free(); return -1; }
    eng_t0 = nanos();
    return 0;
}

void renderer_free(void) {
    if (eng_img) { eng_img->data = NULL; XDestroyImage(eng_img); eng_img = NULL; }
    free(eng_fb); eng_fb = NULL;
    free(eng_zbuf); eng_zbuf = NULL;
    free(eng_cv); eng_cv = NULL;
    free(eng_xv); eng_xv = NULL;
    free(eng_xn); eng_xn = NULL;
    free(eng_order); eng_order = NULL;
    free(eng_depth); eng_depth = NULL;
    free_model_data();
    eng_nv = 0; eng_nt = 0; eng_w = 0; eng_h = 0;
}

static void rotate(double angle_y) {
    float s = (float)sin(angle_y * M_PI / 180.0);
    float c = (float)cos(angle_y * M_PI / 180.0);
    for (int i = 0; i < eng_nv; i++) {
        float x = eng_cv[i*3+0], y = eng_cv[i*3+1], z = eng_cv[i*3+2];
        eng_xv[i*3+0] = x*c + z*s;
        eng_xv[i*3+1] = y;
        eng_xv[i*3+2] = -x*s + z*c;
    }
    for (int i = 0; i < eng_nt; i++) {
        float nx, ny, nz;
        if (mdl_norm) {
            nx = mdl_norm[i*3+0]; ny = mdl_norm[i*3+1]; nz = mdl_norm[i*3+2];
        } else {
            nx = builtin_norm[i][0]; ny = builtin_norm[i][1]; nz = builtin_norm[i][2];
        }
        eng_xn[i*3+0] = nx*c + nz*s;
        eng_xn[i*3+1] = ny;
        eng_xn[i*3+2] = -nx*s + nz*c;
    }
}

static void project(void) {
    float focal = 1.732f;
    float cam_dist = 3.0f;
    float hw = eng_w * 0.5f, hh = eng_h * 0.5f;
    for (int i = 0; i < eng_nv; i++) {
        float x = eng_xv[i*3+0], y = eng_xv[i*3+1], z = eng_xv[i*3+2];
        float zv = z + cam_dist;
        if (zv < 0.1f) zv = 0.1f;
        eng_xv[i*3+0] = (x * focal / zv) * hw + hw;
        eng_xv[i*3+1] = (-y * focal / zv) * hh + hh;
        eng_xv[i*3+2] = zv;
    }
}

static void sort_triangles(void) {
    for (int i = 0; i < eng_nt; i++) {
        const int *t;
        if (mdl_tri) t = mdl_tri[i];
        else t = builtin_tri[i];
        eng_depth[i] = (eng_xv[t[0]*3+2] + eng_xv[t[1]*3+2] + eng_xv[t[2]*3+2]) * 0.333333f;
    }
    for (int i = 1; i < eng_nt; i++) {
        int key = eng_order[i];
        float kd = eng_depth[key];
        int j = i - 1;
        while (j >= 0 && eng_depth[eng_order[j]] < kd) { eng_order[j+1] = eng_order[j]; j--; }
        eng_order[j+1] = key;
    }
}

/* Depth comparator for qsort (descending for painter's algorithm fallback) */
static int tri_depth_desc(const void *a, const void *b) {
    int ia = *(const int *)a, ib = *(const int *)b;
    float da = eng_depth[ia], db = eng_depth[ib];
    return (da > db) ? -1 : (da < db) ? 1 : 0;
}

static void draw_triangle(int ti) {
    const int *t;
    float *col;
    if (mdl_tri) {
        t = mdl_tri[ti];
        col = &mdl_color[ti * 3];
    } else {
        t = builtin_tri[ti];
        col = NULL;
    }
    /* Read projected coords and 1/camera-z for perspective-correct depth */
    float v[3][3]; /* [vertex][x=0,y=1,z=1/z] */
    for (int k = 0; k < 3; k++) {
        v[k][0] = eng_xv[t[k]*3+0];
        v[k][1] = eng_xv[t[k]*3+1];
        v[k][2] = 1.0f / eng_xv[t[k]*3+2];
    }

    /* Back-face culling: screen-space area */
    if ((v[1][0]-v[0][0])*(v[2][1]-v[0][1]) - (v[2][0]-v[0][0])*(v[1][1]-v[0][1]) <= 0) return;

    float nx = eng_xn[ti*3+0], ny = eng_xn[ti*3+1], nz = eng_xn[ti*3+2];
    float lx = 0.3f, ly = 0.8f, lz = 0.5f;
    float ll = sqrtf(lx*lx + ly*ly + lz*lz);
    lx /= ll; ly /= ll; lz /= ll;
    float diff = nx*lx + ny*ly + nz*lz;
    if (diff < 0) diff = 0;
    float bright = 0.15f + 0.85f * diff;

    float base_r = 0.7f, base_g = 0.7f, base_b = 0.7f;
    if (col) { base_r = col[0]; base_g = col[1]; base_b = col[2]; }
    else     { base_r = 180.0f/255; base_g = 40.0f/255; base_b = 40.0f/255; }

    int r = (int)(base_r * 255 * bright); if (r > 255) r = 255;
    int g = (int)(base_g * 255 * bright); if (g > 255) g = 255;
    int b = (int)(base_b * 255 * bright); if (b > 255) b = 255;
    uint32_t color = (r << 16) | (g << 8) | b;

    /* Sort vertices by Y */
    int vi[3] = {0, 1, 2};
    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 2-i; j++)
            if (v[vi[j]][1] > v[vi[j+1]][1]) { int tmp = vi[j]; vi[j] = vi[j+1]; vi[j+1] = tmp; }

    /* Reorder into vx[],vy[],vz[] sorted by Y */
    float vx[3], vy[3], vz[3];
    for (int k = 0; k < 3; k++) {
        vx[k] = v[vi[k]][0];
        vy[k] = v[vi[k]][1];
        vz[k] = v[vi[k]][2];
    }

    int y0i = (int)vy[0], y1i = (int)vy[1], y2i = (int)vy[2];
    if (y0i < 0) y0i = 0;
    if (y2i >= eng_h) y2i = eng_h - 1;
    if (y0i >= eng_h || y2i < 0) return;

    float dx20 = vx[2] - vx[0], dy20 = vy[2] - vy[0];
    float dx10 = vx[1] - vx[0], dy10 = vy[1] - vy[0];
    float dx21 = vx[2] - vx[1], dy21 = vy[2] - vy[1];
    float dz20 = vz[2] - vz[0];
    float dz10 = vz[1] - vz[0];
    float dz21 = vz[2] - vz[1];

    for (int y = y0i; y <= y2i; y++) {
        float fy = (float)y + 0.5f;
        float xl, xr, zl, zr;
        if (y < y1i) {
            float t20 = dy20 != 0 ? (fy - vy[0]) / dy20 : 0;
            float t10 = dy10 != 0 ? (fy - vy[0]) / dy10 : 0;
            xl = vx[0] + t20 * dx20;
            xr = vx[0] + t10 * dx10;
            zl = vz[0] + t20 * dz20;
            zr = vz[0] + t10 * dz10;
        } else {
            float t20 = dy20 != 0 ? (fy - vy[0]) / dy20 : 0;
            float t21 = dy21 != 0 ? (fy - vy[1]) / dy21 : 0;
            xl = vx[0] + t20 * dx20;
            xr = vx[1] + t21 * dx21;
            zl = vz[0] + t20 * dz20;
            zr = vz[1] + t21 * dz21;
        }
        if (xl > xr) {
            float tmp = xl; xl = xr; xr = tmp;
            tmp = zl; zl = zr; zr = tmp;
        }

        /* Use (int) truncation so pixels at shared edges are always covered
         * by both adjacent triangles. Z-buffer resolves the overlap harmlessly. */
        int xli = (int)xl;
        int xri = (int)xr;
        if (xli < 0) xli = 0;
        if (xri >= eng_w) xri = eng_w - 1;
        if (xli > xri) continue;

        uint32_t *row = &eng_fb[y * eng_w];
        float *zrow = &eng_zbuf[y * eng_w];
        float dx = xr - xl;
        if (xli == xri) {
            /* Single pixel — sample at its center */
            float t = (xli + 0.5f - xl) / dx;
            float iz = zl + (zr - zl) * t;
            if (iz >= zrow[xli]) { zrow[xli] = iz; row[xli] = color; }
        } else {
            for (int x = xli; x <= xri; x++) {
                float t = (x + 0.5f - xl) / dx;
                float iz = zl + (zr - zl) * t;
                if (iz >= zrow[x]) { zrow[x] = iz; row[x] = color; }
            }
        }
    }
}

int64_t renderer_frame(int64_t win_idx) {
    (void)win_idx;
    int64_t now = nanos();
    double angle_y = fmod((double)(now - eng_t0) / 1.0e9 * 30.0, 360.0);

    /* Clear framebuffer and Z-buffer (0 = infinitely far in 1/z space) */
    int npix = eng_w * eng_h;
    for (int i = 0; i < npix; i++) {
        eng_fb[i] = 0x1a1a2e;
        eng_zbuf[i] = 0.0f;
    }
    rotate(angle_y);
    project();
    for (int i = 0; i < eng_nt; i++) draw_triangle(i);
    XPutImage(dpy, wins[eng_win], gcs[eng_win], eng_img, 0, 0, 0, 0, eng_w, eng_h);
    XFlush(dpy);
    return 0;
}

/* ---- CLI argument support ---- */
int64_t clgui_argc(void) { return saved_argc; }
int64_t clgui_argv(int64_t n) {
    if (n < 0 || n >= saved_argc) return 0;
    return (int64_t)(intptr_t)saved_argv[n];
}

/* wrapper main() that captures args then calls Clean's main */
int clean_main(void);
int main(int argc, char **argv) {
    saved_argc = argc;
    saved_argv = argv;
    return clean_main();
}
