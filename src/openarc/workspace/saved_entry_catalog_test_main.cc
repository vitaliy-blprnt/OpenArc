// Copyright 2026 OpenArc contributors. SPDX-License-Identifier: BSD-3-Clause

// Standalone harness only; Chromium's unit-test launcher supplies its own main.
#include <cstdio>

#include "base/at_exit.h"
#include "base/command_line.h"
#include "base/files/file.h"
#include "base/files/file_path.h"
#include "base/i18n/icu_util.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/resource/resource_bundle.h"

int main(int argc, char** argv) {
  base::AtExitManager at_exit;
  base::CommandLine::Init(argc, argv);
  if (argc < 3) {
    std::fprintf(stderr, "Usage: catalog_tests <icudtl.dat> <en.pak>\n");
    return 2;
  }
  base::File icu_data(base::FilePath(argv[1]),
                      base::File::FLAG_OPEN | base::File::FLAG_READ);
  if (!icu_data.IsValid() ||
      !base::i18n::InitializeICUWithFileDescriptor(
          icu_data.GetPlatformFile(), base::MemoryMappedFile::Region::kWholeFile)) {
    return 3;
  }
  ui::ResourceBundle::InitSharedInstanceWithPakPath(base::FilePath(argv[2]));
  testing::InitGoogleTest(&argc, argv);
  const int result = RUN_ALL_TESTS();
  ui::ResourceBundle::CleanupSharedInstance();
  return result;
}
