// Copyright (c) 2021 The Orbit Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "MoveFilesToDocuments/MoveFilesToDocuments.h"

#include <QObject>
#include <QString>
#include <thread>

#include "MoveFilesDialog.h"
#include "MoveFilesProcess.h"
#include "OrbitBase/File.h"
#include "OrbitPaths/Paths.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_format.h"

namespace orbit_move_files_to_documents {

static bool IsDirectoryEmpty(const std::filesystem::path& directory) {
  auto exists_or_error = orbit_base::FileExists(directory);
  if (exists_or_error.has_error()) {
    ORBIT_ERROR("Unable to check for existence of \"%s\": %s", directory.string(),
                exists_or_error.error().message());
    return false;
  }

  if (!exists_or_error.value()) {
    return true;
  }

  auto file_list_or_error = orbit_base::ListFilesInDirectory(directory);
  if (file_list_or_error.has_error()) {
    ORBIT_ERROR("Unable to list directory \"%s\": %s", directory.string(),
                file_list_or_error.error().message());
    return false;
  }

  return file_list_or_error.value().empty();
}

void TryMoveSavedDataLocationIfNeeded() {
  constexpr const char* kEnvDontMoveData = "ORBIT_DONT_MOVE_FROM_APPDATA";
  if (char* value = std::getenv(kEnvDontMoveData);
      value != nullptr && value != std::string{"0"} && absl::AsciiStrToLower(value) != "false") {
    return;
  }

  if (IsDirectoryEmpty(orbit_paths::GetPresetDirPriorTo1_66Unsafe()) &&
      IsDirectoryEmpty(orbit_paths::GetCaptureDirPriorTo1_66Unsafe())) {
    return;
  }

  std::thread::id main_thread_id = std::this_thread::get_id();

  MoveFilesDialog dialog;
  MoveFilesProcess process;

  QObject::connect(&process, &MoveFilesProcess::generalError, &dialog,
                   [&dialog, main_thread_id](const QString& error_message) {
                     ORBIT_CHECK(main_thread_id == std::this_thread::get_id());
                     dialog.AddText(absl::StrFormat("Error: %s", error_message.toStdString()));
                   });

  QObject::connect(
      &process, &MoveFilesProcess::moveDirectoryStarted, &dialog,
      [&dialog, main_thread_id](const QString& from_dir_path, const QString& to_dir_path,
                                quint64 number_of_files) {
        ORBIT_CHECK(main_thread_id == std::this_thread::get_id());
        dialog.AddText(absl::StrFormat(R"(Moving %d files from "%s" to "%s"...)", number_of_files,
                                       from_dir_path.toStdString(), to_dir_path.toStdString()));
      });

  QObject::connect(&process, &MoveFilesProcess::moveDirectoryDone, &dialog,
                   [&dialog, main_thread_id]() {
                     ORBIT_CHECK(main_thread_id == std::this_thread::get_id());
                     dialog.AddText("Done.\n");
                   });

  QObject::connect(
      &process, &MoveFilesProcess::moveFileStarted, &dialog,
      [&dialog, main_thread_id](const QString& from_path) {
        ORBIT_CHECK(main_thread_id == std::this_thread::get_id());
        dialog.AddText(absl::StrFormat("        Moving \"%s\"...", from_path.toStdString()));
      });

  QObject::connect(&process, &MoveFilesProcess::moveFileDone, &dialog, [&dialog, main_thread_id]() {
    ORBIT_CHECK(main_thread_id == std::this_thread::get_id());
    dialog.AddText("        Done.");
  });

  QObject::connect(&process, &MoveFilesProcess::processFinished, &dialog,
                   [&dialog, main_thread_id]() {
                     ORBIT_CHECK(main_thread_id == std::this_thread::get_id());
                     dialog.AddText("Finished.");
                     dialog.OnMoveFinished();
                   });

  QObject::connect(&process, &MoveFilesProcess::processInterrupted, &dialog,
                   [&dialog, main_thread_id]() {
                     ORBIT_CHECK(main_thread_id == std::this_thread::get_id());
                     dialog.AddText("Interrupted.");
                     dialog.OnMoveInterrupted();
                   });

  // Intentionally use the version of `QObject::connect` with three arguments, so that
  // `MoveFilesProcess::RequestInterruption` is called asynchronously to `MoveFilesProcess::Run`.
  // Otherwise, `MoveFilesProcess::RequestInterruption` would be queued on `MoveFilesProcess`, which
  // would cause `MoveFilesProcess::RequestInterruption` to be executed only after
  // `MoveFilesProcess::Run` has completed.
  QObject::connect(&dialog, &MoveFilesDialog::interruptionRequested,
                   [&process]() { process.RequestInterruption(); });

  process.Start();
  dialog.exec();
}

}  // namespace orbit_move_files_to_documents
