#include "ClipboardInjector.hpp"
#include "rm_SceneLineItem.hpp"

#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QFile>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <unistd.h>
#include <vector>
#include <algorithm>

#ifdef __ARM_NEON
#include <arm_neon.h>
#endif

/* ── C bridge to framebuffer-spy (defined in entry.c) ──────────────── */
extern "C" {
    void ci_getFramebufferInfo(void **addr, int *width, int *height,
                               int *type, int *bpl);
}

#define FBSPY_TYPE_RGB565 1
#define FBSPY_TYPE_RGBA   2

/* ── NEON-optimized image processing ──────────────────────────────── */

/*
 * BGRA → Grayscale (NEON: 8 pixels/cycle)
 * Fixed-point: gray = (29·B + 150·G + 77·R) >> 8
 * ≈ 0.114·B + 0.587·G + 0.299·R
 */
static void bgraToGray(const uint8_t *bgra, uint8_t *gray,
                       int pixelCount) {
#ifdef __ARM_NEON
    const uint8x8_t wB = vdup_n_u8(29);
    const uint8x8_t wG = vdup_n_u8(150);
    const uint8x8_t wR = vdup_n_u8(77);

    int i = 0;
    for (; i + 8 <= pixelCount; i += 8) {
        /* vld4 deinterleaves 8 BGRA quads into 4 × uint8x8 */
        uint8x8x4_t px = vld4_u8(bgra + i * 4);
        uint16x8_t acc = vmull_u8(px.val[0], wB);   /* B */
        acc = vmlal_u8(acc, px.val[1], wG);           /* G */
        acc = vmlal_u8(acc, px.val[2], wR);           /* R */
        vst1_u8(gray + i, vshrn_n_u16(acc, 8));
    }
    for (; i < pixelCount; i++) {
        gray[i] = (uint8_t)((29*bgra[i*4] + 150*bgra[i*4+1]
                              + 77*bgra[i*4+2]) >> 8);
    }
#else
    for (int i = 0; i < pixelCount; i++) {
        gray[i] = (uint8_t)((29*bgra[i*4] + 150*bgra[i*4+1]
                              + 77*bgra[i*4+2]) >> 8);
    }
#endif
}

/*
 * RGB565 → Grayscale (NEON: 8 pixels/cycle)
 */
static void rgb565ToGray(const uint16_t *rgb565, uint8_t *gray,
                         int pixelCount) {
#ifdef __ARM_NEON
    int i = 0;
    for (; i + 8 <= pixelCount; i += 8) {
        uint16x8_t px = vld1q_u16(rgb565 + i);
        /* Extract channels */
        uint16x8_t r5 = vshrq_n_u16(px, 11);
        uint16x8_t g6 = vandq_u16(vshrq_n_u16(px, 5), vdupq_n_u16(0x3F));
        uint16x8_t b5 = vandq_u16(px, vdupq_n_u16(0x1F));
        /* Scale to 8-bit and compute luminance (fixed-point) */
        uint16x8_t r8 = vmulq_n_u16(r5, 255/31);
        uint16x8_t g8 = vmulq_n_u16(g6, 255/63);
        uint16x8_t b8 = vmulq_n_u16(b5, 255/31);
        /* gray = (77*R + 150*G + 29*B) >> 8 */
        uint16x8_t lum = vmulq_n_u16(r8, 77);
        lum = vmlaq_n_u16(lum, g8, 150);
        lum = vmlaq_n_u16(lum, b8, 29);
        lum = vshrq_n_u16(lum, 8);
        vst1_u8(gray + i, vmovn_u16(lum));
    }
    for (; i < pixelCount; i++) {
        uint16_t p = rgb565[i];
        uint8_t r = ((p >> 11) & 0x1F) * 255 / 31;
        uint8_t g = ((p >> 5) & 0x3F) * 255 / 63;
        uint8_t b = (p & 0x1F) * 255 / 31;
        gray[i] = (uint8_t)((77*r + 150*g + 29*b) >> 8);
    }
#else
    for (int i = 0; i < pixelCount; i++) {
        uint16_t p = rgb565[i];
        uint8_t r = ((p >> 11) & 0x1F) * 255 / 31;
        uint8_t g = ((p >> 5) & 0x3F) * 255 / 63;
        uint8_t b = (p & 0x1F) * 255 / 31;
        gray[i] = (uint8_t)((77*r + 150*g + 29*b) >> 8);
    }
#endif
}

/*
 * Gaussian 3×3 blur  —  kernel {1,2,1; 2,4,2; 1,2,1} / 16
 * NEON: processes 8 output pixels per iteration
 */
static void gaussianBlur3x3(uint8_t *img, int w, int h) {
    uint8_t *tmp = (uint8_t *)malloc(w * h);
    if (!tmp) return;
    memcpy(tmp, img, w * h);

    for (int y = 1; y < h - 1; y++) {
        const uint8_t *r0 = tmp + (y - 1) * w;
        const uint8_t *r1 = tmp + y * w;
        const uint8_t *r2 = tmp + (y + 1) * w;
        int x = 1;
#ifdef __ARM_NEON
        for (; x + 8 <= w - 1; x += 8) {
            /* Load 10-pixel spans for the three rows (x-1 .. x+8) */
            uint8x8_t a0 = vld1_u8(r0 + x - 1);
            uint8x8_t a1 = vld1_u8(r0 + x);
            uint8x8_t a2 = vld1_u8(r0 + x + 1);
            uint8x8_t b0 = vld1_u8(r1 + x - 1);
            uint8x8_t b1 = vld1_u8(r1 + x);
            uint8x8_t b2 = vld1_u8(r1 + x + 1);
            uint8x8_t c0 = vld1_u8(r2 + x - 1);
            uint8x8_t c1 = vld1_u8(r2 + x);
            uint8x8_t c2 = vld1_u8(r2 + x + 1);

            /* Widen to 16-bit and apply kernel weights */
            uint16x8_t sum = vaddl_u8(a0, a2);       /* 1*tl + 1*tr */
            sum = vmlal_u8(sum, a1, vdup_n_u8(2));    /* 2*tc */
            sum = vaddw_u8(sum, c0);                  /* 1*bl */
            sum = vaddw_u8(sum, c2);                  /* 1*br */
            sum = vmlal_u8(sum, c1, vdup_n_u8(2));    /* 2*bc */
            sum = vmlal_u8(sum, b0, vdup_n_u8(2));    /* 2*ml */
            sum = vmlal_u8(sum, b2, vdup_n_u8(2));    /* 2*mr */
            sum = vmlal_u8(sum, b1, vdup_n_u8(4));    /* 4*mc */

            vst1_u8(img + y * w + x, vshrn_n_u16(sum, 4));
        }
#endif
        for (; x < w - 1; x++) {
            int s = r0[x-1] + 2*r0[x] + r0[x+1]
                  + 2*r1[x-1] + 4*r1[x] + 2*r1[x+1]
                  + r2[x-1] + 2*r2[x] + r2[x+1];
            img[y * w + x] = (uint8_t)(s >> 4);
        }
    }
    free(tmp);
}

/*
 * Laplacian 3×3 edge detection  —  kernel {1,4,1; 4,-20,4; 1,4,1}
 * NEON: processes 8 output pixels per iteration using 16-bit signed math
 */
static void laplacianFilter(const uint8_t *in, uint8_t *out, int w, int h) {
    memset(out, 0, w * h);
    for (int y = 1; y < h - 1; y++) {
        const uint8_t *r0 = in + (y - 1) * w;
        const uint8_t *r1 = in + y * w;
        const uint8_t *r2 = in + (y + 1) * w;
        int x = 1;
#ifdef __ARM_NEON
        for (; x + 8 <= w - 1; x += 8) {
            /* Load shifted spans */
            int16x8_t a0 = vreinterpretq_s16_u16(vmovl_u8(vld1_u8(r0+x-1)));
            int16x8_t a1 = vreinterpretq_s16_u16(vmovl_u8(vld1_u8(r0+x)));
            int16x8_t a2 = vreinterpretq_s16_u16(vmovl_u8(vld1_u8(r0+x+1)));
            int16x8_t b0 = vreinterpretq_s16_u16(vmovl_u8(vld1_u8(r1+x-1)));
            int16x8_t b1 = vreinterpretq_s16_u16(vmovl_u8(vld1_u8(r1+x)));
            int16x8_t b2 = vreinterpretq_s16_u16(vmovl_u8(vld1_u8(r1+x+1)));
            int16x8_t c0 = vreinterpretq_s16_u16(vmovl_u8(vld1_u8(r2+x-1)));
            int16x8_t c1 = vreinterpretq_s16_u16(vmovl_u8(vld1_u8(r2+x)));
            int16x8_t c2 = vreinterpretq_s16_u16(vmovl_u8(vld1_u8(r2+x+1)));

            /* kernel: 1*corners + 4*edges − 20*center */
            int16x8_t sum = vaddq_s16(a0, a2);
            sum = vaddq_s16(sum, c0);
            sum = vaddq_s16(sum, c2);
            sum = vmlaq_n_s16(sum, a1, 4);
            sum = vmlaq_n_s16(sum, b0, 4);
            sum = vmlaq_n_s16(sum, b2, 4);
            sum = vmlaq_n_s16(sum, c1, 4);
            sum = vmlaq_n_s16(sum, b1, -20);

            /* Clamp to [0,255] */
            sum = vmaxq_s16(sum, vdupq_n_s16(0));
            sum = vminq_s16(sum, vdupq_n_s16(255));
            vst1_u8(out + y*w + x, vmovn_u16(vreinterpretq_u16_s16(sum)));
        }
#endif
        for (; x < w - 1; x++) {
            int s = r0[x-1] + 4*r0[x] + r0[x+1]
                  + 4*r1[x-1] - 20*r1[x] + 4*r1[x+1]
                  + r2[x-1] + 4*r2[x] + r2[x+1];
            out[y*w + x] = (uint8_t)(s > 255 ? 255 : (s < 0 ? 0 : s));
        }
    }
}

/*
 * Horizontal line extraction from binary edge image.
 * Returns a flat float array: [x1,y1,x2,y2, ...] and sets *outCount.
 */
static float *extractHorizontalLines(const uint8_t *edge,
                                      int w, int h, int *outCount) {
    /* Worst case: every pixel is a 1px line */
    int cap = w * h;
    float *lines = (float *)malloc(cap * 4 * sizeof(float));
    int n = 0;

    for (int y = 0; y < h; y++) {
        bool inLine = false;
        int from = 0, to = 0;
        for (int x = 0; x < w; x++) {
            bool px = edge[y * w + x] > 0;
            if (inLine) {
                if (px) {
                    to = x;
                    if (x + 1 == w) {
                        lines[n++] = (float)from;
                        lines[n++] = (float)y;
                        lines[n++] = (float)to;
                        lines[n++] = (float)y;
                        inLine = false;
                    }
                } else {
                    lines[n++] = (float)from;
                    lines[n++] = (float)y;
                    lines[n++] = (float)to;
                    lines[n++] = (float)y;
                    inLine = false;
                }
            } else if (px) {
                from = x;
                to = x;
                if (x + 1 == w) {
                    lines[n++] = (float)from;
                    lines[n++] = (float)y;
                    lines[n++] = (float)to;
                    lines[n++] = (float)y;
                } else {
                    inLine = true;
                }
            }
        }
    }
    *outCount = n / 4;
    return lines;
}

/*
 * Serialize lines to the clipboard JSON format and write to disk.
 */
static bool writeClipboardJSON(const float *lines, int lineCount,
                                const char *path) {
    QJsonArray arr;
    for (int i = 0; i < lineCount; i++) {
        float x1 = lines[i*4], y1 = lines[i*4+1];
        float x2 = lines[i*4+2], y2 = lines[i*4+3];

        QJsonArray p1, p2;
        p1 << x1 << y1 << 25 << 25 << 0 << 255;
        p2 << x2 << y2 << 25 << 25 << 0 << 255;

        QJsonArray pts;
        pts << p1 << p2;

        float minX = std::min(x1, x2), minY = std::min(y1, y2);
        float w = std::abs(x2 - x1), h = std::abs(y2 - y1);
        if (w < 1) w = 1;
        if (h < 1) h = 1;

        QJsonArray bounds;
        bounds << minX << minY << w << h;

        QJsonObject obj;
        obj["points"] = pts;
        obj["rgba"] = (double)0xFF000000u;
        obj["color"] = 0;
        obj["bounds"] = bounds;
        obj["tool"] = 17; // FINELINER_2 (from rmscene, fixed-width non-aliased)
        obj["maskScale"] = 1.0;
        obj["thickness"] = 0.1; // Extremely thin to avoid blurring
        arr << obj;
    }

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(QJsonDocument(arr).toJson(QJsonDocument::Compact));
    f.close();
    return true;
}


/* ── Existing methods (unchanged) ─────────────────────────────────── */

void ClipboardInjector::sleepMs(int ms) {
    ::usleep(ms * 1000);
}

QList<std::shared_ptr<SceneItem>> ClipboardInjector::loadFromJSON(const QString& path) {
    QList<std::shared_ptr<SceneItem>> items;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        fprintf(stderr, "[clipboard-injector] Error: cannot open %s\n",
                path.toUtf8().constData());
        return items;
    }

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    QJsonArray jsonArray = doc.array();

    fprintf(stderr, "[clipboard-injector] Loading %d lines from %s\n",
           jsonArray.size(), path.toUtf8().constData());

    for (int i = 0; i < jsonArray.size(); i++) {
        QJsonObject obj = jsonArray[i].toObject();
        QJsonArray pointsArray = obj["points"].toArray();
        QList<LinePoint> linePoints;

        for (int j = 0; j < pointsArray.size(); j++) {
            QJsonArray ptArr = pointsArray[j].toArray();
            LinePoint pt;
            pt.x = static_cast<float>(ptArr[0].toDouble());
            pt.y = static_cast<float>(ptArr[1].toDouble());
            pt.speed = static_cast<unsigned short>(ptArr[2].toInt());
            pt.width = static_cast<unsigned short>(ptArr[3].toInt());
            pt.direction = static_cast<unsigned char>(ptArr[4].toInt());
            pt.pressure = static_cast<unsigned char>(ptArr[5].toInt());
            linePoints.append(pt);
        }

        QJsonArray boundsArr = obj["bounds"].toArray();
        QRectF bounds(boundsArr[0].toDouble(), boundsArr[1].toDouble(),
                      boundsArr[2].toDouble(), boundsArr[3].toDouble());

        Line line;
        line.points = linePoints;
        line.bounds = bounds;
        line.rgba = static_cast<unsigned int>(obj["rgba"].toDouble());
        line.color = obj["color"].toInt(0);
        line.tool = obj["tool"].toInt(0x17);
        line.maskScale = obj["maskScale"].toDouble(1.0);
        line.thickness = static_cast<float>(obj["thickness"].toDouble(0.0));

        auto item = std::make_shared<SceneLineItem>(
            SceneLineItem::fromLine(std::move(line)));
        items.push_back(item);
    }

    fprintf(stderr, "[clipboard-injector] Loaded %d scene items\n", items.size());
    return items;
}

bool ClipboardInjector::setupVtablePtr(
        const QList<std::shared_ptr<SceneItem>>& items) {
    if (items.empty()) return false;
    auto* item = reinterpret_cast<SceneLineItem*>(items.first().get());
    SceneLineItem::setupVtable(item->vtable);
    return true;
}

Line ClipboardInjector::createLine(
        const QPointF& start, const QPointF& end) {
    QList<LinePoint> linePoints;
    LinePoint p1;
    p1.x = static_cast<float>(start.x());
    p1.y = static_cast<float>(start.y());
    p1.speed = 25;
    p1.width = 25;
    p1.direction = 0;
    p1.pressure = 255;
    linePoints.append(p1);

    LinePoint p2;
    p2.x = static_cast<float>(end.x());
    p2.y = static_cast<float>(end.y());
    p2.speed = 25;
    p2.width = 25;
    p2.direction = 0;
    p2.pressure = 255;
    linePoints.append(p2);

    QRectF bounds(
        std::min(start.x(), end.x()), std::min(start.y(), end.y()),
        std::abs(end.x() - start.x()), std::abs(end.y() - start.y()));
    return Line::fromPoints(std::move(linePoints), bounds);
}

Line ClipboardInjector::createCircle(const QPointF& center, float radius) {
    QList<LinePoint> linePoints;
    int numPoints = 8;
    for (int i = 0; i <= numPoints; ++i) {
        float angle = i * 2.0f * 3.14159f / numPoints;
        LinePoint pt;
        pt.x = static_cast<float>(center.x() + radius * cos(angle));
        pt.y = static_cast<float>(center.y() + radius * sin(angle));
        pt.speed = 25;
        pt.width = 25;
        pt.direction = 0;
        pt.pressure = 255;
        linePoints.append(pt);
    }

    QRectF bounds(center.x() - radius, center.y() - radius,
                  radius * 2, radius * 2);
    return Line::fromPoints(std::move(linePoints), bounds);
}


/* ══════════════════════════════════════════════════════════════════════
 *  captureArea  — read framebuffer region → edge detect → clipboard JSON
 *
 *  Coordinates are in framebuffer pixels (not scene-view coordinates).
 *  The RM2 framebuffer is 1404 × 1872  BGRA (type=2, bpl=5616).
 * ══════════════════════════════════════════════════════════════════════ */
bool ClipboardInjector::captureArea(int rx, int ry, int rw, int rh) {
    /* ── 1. Get framebuffer ─────────────────────────────────────────── */
    void  *fbAddr = nullptr;
    int    fbW = 0, fbH = 0, fbType = 0, fbBpl = 0;
    ci_getFramebufferInfo(&fbAddr, &fbW, &fbH, &fbType, &fbBpl);

    if (!fbAddr || fbW <= 0 || fbH <= 0) {
        fprintf(stderr, "[clipboard-injector] captureArea: no framebuffer!\n");
        return false;
    }

    /* Clamp the requested rectangle to framebuffer bounds */
    int x0, y0, x1, y1;
    if (rw <= 0 || rh <= 0) {
        /* Full framebuffer capture */
        x0 = 0; y0 = 0;
        x1 = fbW; y1 = fbH;
    } else {
        x0 = std::max(0, rx);
        y0 = std::max(0, ry);
        x1 = std::min(fbW, rx + rw);
        y1 = std::min(fbH, ry + rh);
    }
    int cw = x1 - x0;
    int ch = y1 - y0;
    if (cw <= 2 || ch <= 2) {
        fprintf(stderr, "[clipboard-injector] captureArea: region too small\n");
        return false;
    }

    fprintf(stderr, "[clipboard-injector] captureArea: crop %d×%d @ (%d,%d) "
            "from FB %d×%d type=%d\n", cw, ch, x0, y0, fbW, fbH, fbType);

    /* ── 2. Crop + convert to grayscale ─────────────────────────────── */
    uint8_t *gray = (uint8_t *)malloc(cw * ch);
    if (!gray) return false;

    if (fbType == FBSPY_TYPE_RGBA) {
        /* Each row: fbBpl bytes = fbW * 4 bytes of BGRA */
        uint8_t *rowBuf = (uint8_t *)malloc(cw * 4);
        for (int y = 0; y < ch; y++) {
            const uint8_t *src = (const uint8_t *)fbAddr
                                 + (y0 + y) * fbBpl + x0 * 4;
            memcpy(rowBuf, src, cw * 4);
            bgraToGray(rowBuf, gray + y * cw, cw);
        }
        free(rowBuf);
    } else {
        /* RGB565: each pixel = 2 bytes */
        int stride = fbBpl / 2; /* pixels per row in FB */
        uint16_t *rowBuf = (uint16_t *)malloc(cw * 2);
        for (int y = 0; y < ch; y++) {
            const uint16_t *src = (const uint16_t *)fbAddr
                                  + (y0 + y) * stride + x0;
            memcpy(rowBuf, src, cw * 2);
            rgb565ToGray(rowBuf, gray + y * cw, cw);
        }
        free(rowBuf);
    }

    /* ── 3. Edge detection  (Gaussian blur → Laplacian) ─────────────── */
    gaussianBlur3x3(gray, cw, ch);

    uint8_t *edges = (uint8_t *)malloc(cw * ch);
    if (!edges) { free(gray); return false; }

    laplacianFilter(gray, edges, cw, ch);
    free(gray);

    /* ── 4. Extract horizontal lines ────────────────────────────────── */
    int lineCount = 0;
    float *lines = extractHorizontalLines(edges, cw, ch, &lineCount);
    free(edges);

    fprintf(stderr, "[clipboard-injector] captureArea: %d lines extracted\n",
            lineCount);

    if (lineCount == 0) {
        free(lines);
        return false;
    }

    /* ── 5. Write clipboard JSON ────────────────────────────────────── */
    bool ok = writeClipboardJSON(lines, lineCount, "/tmp/clipboard_inject.json");
    free(lines);

    fprintf(stderr, "[clipboard-injector] captureArea: JSON written, ok=%d\n", ok);
    return ok;
}
