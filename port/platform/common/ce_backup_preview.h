// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "ce_backup_preview_ui.h"
#include <cstdint>
#include <string>
#include <vector>
struct CePreviewCareer {
  std::string name, proposal;
  std::vector<unsigned char> bytes;
};
struct CePreviewSet {
  std::string date, reason, identity;
  int members = 0;
  std::vector<CePreviewCareer> careers;
};
bool CePreviewRead(const std::string &set, const std::string &user,
                   const std::string &game, const std::string &package,
                   CePreviewSet &out, std::string &error);
struct CePreviewState {
  enum Phase { Idle, Picking, Working, Verified, Error } phase = Idle;
  std::string message;
  const CePreviewSet *set = nullptr;
  uint64_t generation = 0;
  std::vector<std::string> choices;
  // Parallel to choices: what each listed set held when the list was made.
  struct Holds {
    bool verified = false, options = false;
    int careers = 0;
    std::string reason;
  };
  std::vector<Holds> holds;
};
bool CePreviewList(const std::string &root, const std::string &user,
                   const std::string &game, const std::string &package,
                   std::vector<std::string> &sets, std::string &error);
void CePreviewOpen(const char *user, const char *game, const char *root);
void CePreviewSelectSet(unsigned index);
void CePreviewLeave();
void CePreviewChoose();
void CePreviewAgain();
