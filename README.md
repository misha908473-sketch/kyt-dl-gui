# kyt-dlp GUI

[English](README.md) | [Русский](README.ru.md)

A small desktop app for downloading video and music. Paste a link, choose the quality, and click “Download”. The app uses [yt-dlp](https://github.com/yt-dlp/yt-dlp) and FFmpeg.

![Kyt-dl GUI main window](screenshots/main-window-2026-11-15.svg)

## Features

- Downloads video and audio from websites supported by yt-dlp.
- Shows the title, creator, duration, and thumbnail when the website provides them.
- Lets you choose video quality or save and convert audio to MP3, M4A, AAC, OPUS, FLAC, or WAV.
- Downloads YouTube and YouTube Music playlists, with an option to select individual tracks.
- Shows downloads and saved files in one list.
- Lets you open a file, show it in its folder, copy it, move it, or delete it.
- Supports a download queue, parallel downloads, and saving files to a USB drive.

yt-dlp and FFmpeg must be installed to download and process media. Website support depends on yt-dlp and the websites themselves. Signing in to an account through the app is not supported yet.

## Install on Arch Linux

Download `kyt-dl-gui-0.0.1-1-x86_64.pkg.tar.zst` from [Releases](https://github.com/misha908473-sketch/kyt-dl-gui/releases), then install it:

```sh
sudo pacman -U ./kyt-dl-gui-0.0.1-1-x86_64.pkg.tar.zst
```

Install `yt-dlp` and `ffmpeg` too if they are not already installed.

## Install on Debian

Download `kyt-dl-gui_0.0.1-1_amd64.deb` from [Releases](https://github.com/misha908473-sketch/kyt-dl-gui/releases), then install it:

```sh
sudo apt install ./kyt-dl-gui_0.0.1-1_amd64.deb
```

## Install on Fedora

Download `kyt-dl-gui-0.0.1-1.fc45.x86_64.rpm` from [Releases](https://github.com/misha908473-sketch/kyt-dl-gui/releases), then install it:

```sh
sudo dnf install ./kyt-dl-gui-0.0.1-1.fc45.x86_64.rpm
```

## Build from source

You need a C++17 compiler, CMake, and Qt 5.15 or Qt 6 with the Widgets, Network, and Concurrent modules. Linux builds also need DBus.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
./build/ytdlp-gui
```

After launching the app, you can set the path to yt-dlp in its settings. FFmpeg must be available in `PATH`.

## Checks

To build and run the included checks:

```sh
cmake -S . -B build-checks -DYTDLP_GUI_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build-checks -j2
ctest --test-dir build-checks --output-on-failure
```

The checks use a fake downloader and do not download media from websites.
