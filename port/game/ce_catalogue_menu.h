// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "ce_asset_catalog.h"

// A page of installed entries. The source list stays open until the player leaves the catalogue.
// Categories 0 (models) and 1 (textures) are installed files. Category 2 lists the sounds the
// running session has loaded so far: a snapshot, never an installed or complete inventory.
// Categories 3 and up are the installed spoken lines, one per language folder of data/sounds that has
// a MessageBox folder, the running language first. At most CE_CATALOGUE_GROUPS categories in all.
enum { CE_CATALOGUE_SLOTS = 12, CE_CATALOGUE_GROUPS = 11 };
unsigned PortCeCatalogueGroups();
void PortCeCatalogueGroupLabel(int group, char* out, unsigned size);
void PortCeCatalogueOpen();
void PortCeCatalogueClose();
unsigned PortCeCatalogueCount(int category);
void PortCeCatalogueBrowse(int category);
void PortCeCatalogueTurn();
int PortCeCataloguePageNumber();
int PortCeCataloguePageCount();
unsigned PortCeCatalogueVisible();
// Filtering never changes the installed snapshot. Selection is an asset identity,
// retained across pages, details, clear and a return through the category page.
enum { CE_CATALOGUE_QUERY_MAX = 32 };
const char* PortCeCatalogueQuery();
unsigned PortCeCatalogueMatches();
void PortCeCatalogueFilter(const char* text);
void PortCeCatalogueSelect(unsigned slot);
unsigned PortCeCatalogueSelectedSlot();
void PortCeCatalogueTitle(unsigned slot, char* out, unsigned size);
void PortCeCatalogueDetails(unsigned slot, char* out, unsigned size);
bool PortCeCatalogueIsTexture(unsigned slot);
bool PortCeCatalogueReadTexture(unsigned slot, CeAssetTextureReader reader, void* context);
// True for a visible row of category 2. Play forwards only the selected row and is false
// when nothing started. Leaving category 2, reopening or closing ends its preview.
bool PortCeCatalogueIsSound(unsigned slot);
bool PortCeCataloguePlaySound(unsigned slot);
// Why the last PlaySound of a spoken line played nothing, in plain words; empty after a good start.
const char* PortCeCatalogueSoundProblem();
// False for a speech group none of whose lines the game data's text names (they are named by their files instead).
bool PortCeCatalogueGroupWorded(int group);
// A short tag for a row, for telling apart rows whose words look the same: its file's own name without extension
// ("003 east sphere a"), or for an effect its short name in the bank ("N HUD ring").
void PortCeCatalogueRowTag(unsigned slot, char* out, unsigned size);
// For a list without pages (the sound lists' list box): brings the match at index onto the current page, so that slot
// index % CE_CATALOGUE_SLOTS is that match for every slot call above.
void PortCeCatalogueShowItem(unsigned index);
// The selected item's place among the matches, or -1 when it is not one of them.
int PortCeCatalogueSelectedIndex();
// True while the collection being browsed is sound effects or a language's speech.
bool PortCeCatalogueBrowsingSounds();
// The collection being browsed (a group number, as PortCeCatalogueGroupLabel takes).
int PortCeCatalogueCategory();
// A row's group and its name alone, for lists that show each group once as a heading ("Bosses", then "Warspite");
// an empty group for lists without groups (speech).
void PortCeCatalogueItemParts(unsigned slot, char* group, unsigned group_size, char* name, unsigned name_size);
// A texture's size in pixels and whether its format keeps transparency; false for anything else or an unreadable header.
bool PortCeCatalogueTextureSize(unsigned slot, unsigned* width, unsigned* height, bool* transparency);
// The goodie whose model a Models row is (the Catalogue can turn it in its pane), or -1.
int PortCeCatalogueModelGoodie(unsigned slot);
