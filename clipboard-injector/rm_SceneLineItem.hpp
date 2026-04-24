#pragma once

#include <memory>
#include "rm_Line.hpp"
#include "rm_SceneItem.hpp"

struct SceneLineItem : public SceneItem {
    unsigned char unk_x4;
    int pageIndex;
    short unk_xc;
    short unk_xe;
    int sourceLayerId;
    short unk_x14;
    short unk_x16;
    int unk_x18;
    void* unk_x1c;
    unsigned char unk_x20;
    unsigned char unk_x21;
    void* unk_x24[3];
    Line line;
    int unk_x78;
    int unk_x7c[3];

    static void* vtable_ptr;
    static void setupVtable(void* vtable);

    static SceneLineItem fromLine(Line &&line) {
        SceneLineItem item = {};
        item.vtable = vtable_ptr;
        item.unk_x4 = 3;
        item.pageIndex = 0xE;
        item.unk_xe = 1;
        item.sourceLayerId = 0xB;
        item.line = line;
        item.unk_x78 = 1;
        return item;
    }

    static SceneLineItem* tryCast(SceneItem* item);
};
