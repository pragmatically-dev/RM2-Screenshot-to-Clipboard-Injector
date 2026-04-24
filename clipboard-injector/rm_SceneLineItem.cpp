#include "rm_SceneLineItem.hpp"
#include <cstdio>

void* SceneLineItem::vtable_ptr = nullptr;

void SceneLineItem::setupVtable(void* vtable) {
	printf("[clipboard-injector] Setting SceneLineItem vtable to %p\n", vtable);
	SceneLineItem::vtable_ptr = vtable;
}

SceneLineItem* SceneLineItem::tryCast(SceneItem* item) {
    if (!item) return nullptr;
    auto* lineItem = reinterpret_cast<SceneLineItem*>(item);
    if (lineItem->unk_xc != 0) return nullptr;
    if (lineItem->unk_xe != 0) return nullptr;
    if (lineItem->unk_x78 != 1) return nullptr;
    if (!(lineItem->unk_x20 == 0x0 || (lineItem->unk_x20 == 0x2 && lineItem->unk_x21 == 0x2))) return nullptr;
    return lineItem;
}
