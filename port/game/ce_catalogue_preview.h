// SPDX-License-Identifier: GPL-3.0-only
#pragma once

bool PortCeCataloguePreviewOpen(unsigned slot);
void PortCeCataloguePreviewClose();
// Releases the picture only: a sound keeps playing after its details box closes, until another is chosen or the page is left.
void PortCeCataloguePictureClose();
void PortCeCataloguePreviewDraw(float above);
// The list box's picture pane (ce_catalogue_list.cpp): the picture fitted to the rectangle on a mid grey; two lines under it; the clock.
bool PortCeCataloguePreviewDrawIn(float left, float top, float right, float bottom, bool with_picture, bool checker);
void PortCeCataloguePaneText(const char* first, const char* second, float left, float right, float y);
float PortCeCatalogueNow();
