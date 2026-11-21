// SPDX-License-Identifier: GPL-3.0-only
#pragma once

bool PortCeCataloguePreviewOpen(unsigned slot);
void PortCeCataloguePreviewClose();
// Releases the picture only: a sound keeps playing after its details box closes, until another is chosen or the page is left.
void PortCeCataloguePictureClose();
void PortCeCataloguePreviewDraw(float above);
