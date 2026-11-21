// SPDX-License-Identifier: GPL-3.0-only
// port/game/ce_catalogue_english.h: a small general list of ordinary English words, lower case, three letters or more, for the
// Catalogue's naming rule (ce_catalogue_menu.cpp). It is data, not knowledge of any asset: no entry is chosen for a file's name.
// A run-together word in a file name is split only when every piece is a word of this list or of the game's own text.
#pragma once

static const char* const kCeCatalogueEnglish[] = {
    // things and places
    "air", "arch", "arm", "army", "ash", "axe", "steam", "team", "bag", "ball", "band", "bank", "bar", "barrel", "base", "basket", "beam", "bed",
    "bell", "belt", "bench", "bin", "bird", "blade", "block", "board", "boat", "body", "bolt", "bomb", "bone", "book", "boot",
    "boss", "bottle", "bow", "bowl", "box", "brain", "branch", "brick", "bridge", "bucket", "bug", "building", "bullet", "bush",
    "cab", "cabin", "cable", "cage", "camp", "can", "cannon", "cap", "car", "card", "cart", "case", "castle", "cave", "cell",
    "chain", "chair", "chest", "chip", "city", "claw", "cliff", "clock", "cloud", "coat", "coin", "column", "cone", "core",
    "corner", "crane", "crate", "crew", "cross", "crown", "cup", "cutscene", "dam", "deck", "desk", "dish", "dock", "dog", "dome", "door",
    "dot", "drill", "drum", "dude", "dust", "ear", "earth", "edge", "egg", "engine", "eye", "face", "fan", "farm", "fence",
    "field", "fin", "finger", "fire", "fish", "flag", "flame", "floor", "flower", "fog", "foot", "fork", "fort", "frame",
    "fuel", "gate", "gear", "glass", "glove", "goal", "grass", "grid", "ground", "gun", "hall", "hammer", "hand", "hat",
    "head", "heart", "hill", "hole", "hook", "horn", "horse", "hose", "house", "hull", "hut", "ice", "iron", "island", "jar",
    "jaw", "jet", "key", "kit", "knife", "lake", "lamp", "land", "leaf", "leg", "lens", "lid", "light", "line", "link", "lock",
    "log", "loop", "mask", "mast", "mat", "metal", "mine", "mirror", "moon", "motor", "mount", "mouth", "mud", "nail", "neck",
    "nest", "net", "node", "nose", "nut", "oak", "oil", "orb", "oven", "pad", "page", "pan", "panel", "paper", "park", "path",
    "pen", "pie", "pin", "pipe", "pit", "plane", "plant", "plate", "plug", "pod", "pole", "pond", "pool", "port", "post",
    "pot", "pump", "rack", "rail", "rain", "ramp", "ring", "river", "road", "rock", "rod", "roof", "room", "root", "rope",
    "sail", "sand", "screen", "sea", "seat", "shaft", "shed", "shell", "shield", "ship", "shoe", "shop", "sign", "silo", "sky",
    "slab", "slot", "smoke", "snow", "sock", "soil", "spike", "spot", "spring", "stack", "stair", "stand", "star", "steel",
    "stem", "step", "stick", "stone", "storm", "stove", "street", "string", "sun", "switch", "table", "tail", "tank", "tape",
    "tent", "tile", "tip", "tool", "tooth", "top", "torch", "tower", "town", "toy", "track", "trap", "tray", "tree", "truck",
    "tube", "tunnel", "van", "vent", "wall", "wheel", "wind", "window", "wing", "wire", "wood", "wreck", "wreckage", "yard",
    // shapes, parts and materials
    "ant", "block", "circle", "cube", "curve", "disc", "frame", "half", "inner", "outer", "piece", "plate", "point", "ray",
    "rim", "round", "section", "shape", "side", "sphere", "square", "strip", "unit", "zone",
    // people and creatures
    "bear", "bee", "boy", "cat", "cow", "fly", "fox", "girl", "guard", "king", "man", "men", "pilot", "rat", "soldier",
    "spider", "troop", "wolf", "worm",
    // describing words
    "back", "bad", "big", "black", "blue", "bright", "broken", "brown", "burnt", "clean", "clear", "cold", "dark", "dead",
    "deep", "dirty", "dry", "east", "empty", "far", "fast", "fat", "flat", "front", "full", "giant", "gold", "good", "great",
    "green", "grey", "hard", "heavy", "high", "hot", "huge", "large", "left", "light", "little", "long", "loose", "low",
    "main", "mid", "middle", "mini", "new", "north", "old", "open", "orange", "pink", "purple", "rear", "red", "right",
    "rough", "short", "silver", "slow", "small", "soft", "south", "tall", "thick", "thin", "tiny", "top", "upper", "west",
    "wet", "white", "wide", "wild", "yellow", "young",
    // ordinary words that read as a letter before another word ("f" + "light"): known whole, so never split into a code
    "blight", "blink", "block", "bloom", "brace", "brush", "clamp", "clash", "crack", "craft", "cramp", "crash", "crate", "crawl",
    "crush", "drain", "drift", "flight", "flash", "fleet", "float", "flock", "glow", "grind", "grip", "plank", "plight", "plot",
    "print", "scale", "scrap", "scrape", "shark", "sharp", "shore", "slant", "slash", "slate", "slight", "sling", "slope",
    "smash", "spark", "spill", "spine", "sprint", "stall", "stank", "sting", "stock", "store", "story", "strap", "stray",
    "sweep", "swing", "trail", "train", "trash", "tread", "twin", "wheel",
    // actions
    "blast", "build", "burn", "carry", "climb", "cut", "dig", "drop", "fall", "fly", "glow", "grow", "hit", "jump", "kick",
    "lift", "load", "move", "pull", "push", "roll", "run", "shot", "spin", "walk",
};
