#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>

#include "xovi.h"

void registerQmldiff();
extern char *program_invocation_short_name;

/* FramebufferConfig matches framebuffer-spy.h */
struct FramebufferConfig {
    void *framebufferAddress;
    int width, height, type, bpl;
    bool requiresReload;
};

#define FBSPY_TYPE_RGB565 1
#define FBSPY_TYPE_RGBA   2

/*
 * C bridge: framebuffer-spy symbols use $ in identifiers (GCC extension).
 * C++ cannot use $, so we wrap them here for ClipboardInjector.cpp to call.
 */
void ci_getFramebufferInfo(void **addr, int *width, int *height,
                           int *type, int *bpl) {
    if (!framebuffer_spy$getFramebufferConfig) {
        fprintf(stderr, "[clipboard-injector] framebuffer-spy not available\n");
        *addr = NULL;
        return;
    }

    struct FramebufferConfig cfg =
        ((struct FramebufferConfig (*)()) framebuffer_spy$getFramebufferConfig)();

    /* Refresh if the device requires it (rM1 mmap'd framebuffer) */
    if (cfg.requiresReload && framebuffer_spy$refreshFramebuffer) {
        ((void (*)()) framebuffer_spy$refreshFramebuffer)();
    }

    *addr  = cfg.framebufferAddress;
    *width = cfg.width;
    *height= cfg.height;
    *type  = cfg.type;
    *bpl   = cfg.bpl;

    fprintf(stderr, "[clipboard-injector] FB: %p %dx%d type=%d bpl=%d\n",
            *addr, *width, *height, *type, *bpl);
}

void _xovi_construct() {
    if (strstr(program_invocation_short_name, "worker") != NULL) {
        return;
    }
	printf("[clipboard-injector] Registering ClipboardInjector\n");
    Environment->requireExtension("qt-resource-rebuilder", 0, 2, 0);
	registerQmldiff();
	qt_resource_rebuilder$qmldiff_add_external_diff(r$clipboard_injector, "Clipboard injector");
}
