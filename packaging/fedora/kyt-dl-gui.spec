Name:           kyt-dl-gui
Version:        0.1.1
Release:        1%{?dist}
Summary:        Desktop application for downloading video and audio with yt-dlp
License:        LicenseRef-Unknown
URL:            https://github.com/misha908473-sketch/kyt-dl-gui
Source0:        %{name}-%{version}.tar.gz
Source1:        kyt-dl-gui.desktop

BuildRequires:  cmake
BuildRequires:  gcc-c++
BuildRequires:  make
BuildRequires:  qt6-qtbase-devel
Requires:       yt-dlp
Requires:       ffmpeg-free

%description
Kyt-dl GUI is a Qt desktop application for downloading video and audio
using yt-dlp.

%prep
%setup -q -n %{name}-%{version}

%build
%cmake -DCMAKE_BUILD_TYPE=Release
%cmake_build

%install
%cmake_install
install -Dpm0644 %{SOURCE1} %{buildroot}%{_datadir}/applications/kyt-dl-gui.desktop

%files
%{_bindir}/ytdlp-gui
%{_datadir}/applications/kyt-dl-gui.desktop
%doc README.md

%changelog
* Sun Nov 15 2026 Kyt-dl GUI contributors <misha908473-sketch@users.noreply.github.com> - 0.0.1-1
- Initial Fedora package for release 0.0.1
