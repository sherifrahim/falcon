// Copyright (c) 2026 Falcon. Personal fork of Brave.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_FALCON_MEDIA_MEDIA_SERVICE_H_
#define BRAVE_BROWSER_FALCON_MEDIA_MEDIA_SERVICE_H_

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "base/no_destructor.h"
#include "base/sequence_checker.h"
#include "base/time/time.h"
#include "base/values.h"
#include "build/build_config.h"
#include "url/gurl.h"

#if BUILDFLAG(IS_WIN)
#include "base/win/scoped_handle.h"
#endif

class Profile;

namespace falcon {

// Drives the bundled yt-dlp (+ffmpeg) sidecar for things aria2 cannot do:
// HLS/DASH playlists and site players (YouTube-class). One job = one yt-dlp
// process; progress is parsed from stdout and exposed to the downloader page.
// UI thread only.
class MediaService {
 public:
  struct Job {
    int id = 0;
    GURL url;
    GURL referer;
    std::string title;
    std::string selector;  // yt-dlp -f selector or preset name
    // starting, downloading, merging, done, error, cancelled
    std::string status = "starting";
    double percent = 0;
    std::string speed;
    std::string eta;
    int64_t downloaded = 0;
    int64_t total = 0;
    base::FilePath path;
    std::string error;
    base::Time started = base::Time::Now();
#if BUILDFLAG(IS_WIN)
    base::win::ScopedHandle job_object;  // kills yt-dlp + ffmpeg together
#endif
  };

  static MediaService* Get();
  MediaService(const MediaService&) = delete;
  MediaService& operator=(const MediaService&) = delete;

  // yt-dlp.exe is shipped next to the browser; false if it is missing.
  static bool IsAvailable();

  // Runs `yt-dlp -J` and returns {title, duration, thumbnail, formats:[...]}
  // or {error}. Cookies for |url| are exported from |profile|.
  using ProbeCallback = base::OnceCallback<void(base::DictValue)>;
  void Probe(Profile* profile,
             const GURL& url,
             const GURL& referer,
             ProbeCallback callback);

  // Extra knobs for Start(): whole playlist instead of the single video,
  // subtitles (language list, e.g. "en,en.*"), audio container for the
  // "audio" preset (m4a/mp3/opus), and a friendly title hint.
  struct Options {
    bool playlist = false;
    std::string subtitles;
    std::string audio_format = "m4a";
  };

  // Starts a download. |selector| is a yt-dlp format selector ("bv*+ba/b",
  // "137+140", ...) or one of the presets: best, 1080, 720, 480, 360, audio.
  // Returns the job id (0 when yt-dlp is unavailable / url invalid).
  int Start(Profile* profile,
            const GURL& url,
            const GURL& referer,
            const std::string& selector,
            const Options& options);
  int Start(Profile* profile,
            const GURL& url,
            const GURL& referer,
            const std::string& selector);
  void Cancel(int id);
  void Remove(int id);

  base::ListValue JobsAsList() const;
  const Job* GetJob(int id) const;

  struct Launch;  // everything the blocking sequence needs (media_service.cc)

 private:
  friend class base::NoDestructor<MediaService>;
  MediaService();
  ~MediaService();

  void OnCookiesForProbe(Profile* profile,
                         std::unique_ptr<Launch> launch,
                         ProbeCallback callback,
                         std::string cookie_file_content);
  void OnCookiesForStart(int id,
                         std::unique_ptr<Launch> launch,
                         std::string cookie_file_content);
  void OnProbeDone(ProbeCallback callback, base::DictValue result);
  void OnOutputLine(int id, std::string line);
  void OnExited(int id, bool launched, int exit_code);

  int next_id_ = 1;
  std::map<int, Job> jobs_;

  SEQUENCE_CHECKER(sequence_checker_);
  base::WeakPtrFactory<MediaService> weak_factory_{this};
};

// Directory for media downloads (category folder for video) and the sidecar
// locations, shared with the UI.
base::FilePath MediaDownloadDirectory(Profile* profile);
base::FilePath YtDlpPath();
base::FilePath FfmpegDirectory();

}  // namespace falcon

#endif  // BRAVE_BROWSER_FALCON_MEDIA_MEDIA_SERVICE_H_
