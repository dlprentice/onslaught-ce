// Shared backup-set verification. Platform adapters supply held read-only
// files, the still-selected folders and a proposal only; this interface cannot
// restore.
#pragma once
#include "../../game/ce_career_view.h"
#include "ce_backup_manifest.h"
#include "ce_backup_preview.h"
#include <cstdio>
#include <memory>
#include <utility>

namespace {
namespace ce_backup_verify {
namespace hash_impl {
#include "../../updater/sha256.c"
}
std::string digest(const std::string &s) {
  hash_impl::SHA256_CTX c;
  unsigned char bytes[32];
  static const char hex[] = "0123456789abcdef";
  std::string out;
  hash_impl::sha256_init(&c);
  hash_impl::sha256_update(
      &c, reinterpret_cast<const unsigned char *>(s.data()), s.size());
  hash_impl::sha256_final(&c, bytes);
  for (unsigned char v : bytes) {
    out += hex[v >> 4];
    out += hex[v & 15];
  }
  return out;
}

template <class File, class Parent, class Suggestion, class Current>
bool read(Parent parent, Suggestion suggest, Current current, CePreviewSet &out,
          std::string &error) {
  error = "No readable backup manifest in this folder.";
  File manifest;
  if (!manifest.openRead(parent, "onslaught-backup.json", 524288))
    return false;
  error = "This backup manifest is unsupported or incomplete.";
  ce_preview::Manifest m;
  if (!ce_preview::Parser(manifest.data).parse(m))
    return false;
  CePreviewSet result;
  result.date = m.created;
  result.reason = m.reason.empty() ? "Reason not recorded" : m.reason;
  result.identity = digest(manifest.data);
  result.members = int(m.files.size());
  std::vector<std::unique_ptr<File>> held;
  for (const auto &entry : m.files) {
    bool isCareer = ce_preview::career(entry.name);
    if (!isCareer && ce_preview::fold(entry.name) != "defaultoptions.bea") {
      error = "This backup contains an unsupported file type.";
      return false;
    }
    if (isCareer && entry.size != CE_CAREER_SIZE) {
      error = "A backed-up career has the wrong size.";
      return false;
    }
    std::unique_ptr<File> f(new File);
    error = "A file is missing or no longer matches its backup.";
    if (!f->openRead(parent, entry.name, entry.size) ||
        f->data.size() != entry.size || digest(f->data) != entry.sha)
      return false;
    for (const auto &other : held)
      if (f->sameObject(*other)) {
        error = "This backup refers to the same file more than once.";
        return false;
      }
    if (isCareer) {
      const auto *raw = reinterpret_cast<const unsigned char *>(f->data.data());
      SCeCareerView v{};
      if (!CeCareerViewBuild(raw, int(f->data.size()), &v) ||
          !CeCareerViewPlausible(v)) {
        error = "The verified file is not a readable CE career.";
        return false;
      }
      CePreviewCareer row;
      row.name = entry.name;
      row.bytes.assign(raw, raw + f->data.size());
      row.proposal = suggest();
      result.careers.push_back(std::move(row));
    }
    held.push_back(std::move(f));
  }
  if (result.careers.empty()) {
    error = "This backup contains no CE career.";
    return false;
  }
  error = "The backup changed while it was being verified.";
  if (!manifest.unchanged(parent) || !current())
    return false;
  for (const auto &f : held)
    if (!f->unchanged(parent))
      return false;
  out = std::move(result);
  error.clear();
  return true;
}

} // namespace ce_backup_verify
} // namespace
