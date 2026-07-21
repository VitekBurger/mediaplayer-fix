# Linux native port

This fork replaces the original Windows-only `MediaPlayer.dll` with a Linux
`libMediaPlayer.so` implementation.

The Linux native path keeps the JNI contract and uses:

- FFmpeg for audio/video decoding;
- software YUV/RGB conversion through `libswscale`;
- the OpenGL function table supplied by Minecraft/LWJGL;
- direct upload into the existing Minecraft texture ID.

The old D3D11, D3D12, WGL interop and Windows CUDA paths are not compiled on
Linux. Legacy `DeviceType` values are accepted by the Java API but decode via
the portable software path.

## Build

Install development packages for Java 21, CMake, pkg-config and FFmpeg:

```sh
./native/build-linux.sh
JAVA_HOME=/path/to/jdk-21 ./gradlew :forge:build
```

The native library is copied into `common/src/main/resources/libMediaPlayer.so`
and embedded in the resulting mod JAR.

## Flatpak note

The Prism Launcher Flatpak does not include FFmpeg runtime libraries. For a
local installation, enable the read-only host lookup once:

```sh
./native/enable-flatpak-host-ffmpeg.sh
```

This exposes host libraries below `/run/host/usr/lib` and preserves the KDE
runtime paths in `LD_LIBRARY_PATH` for Prism itself. Restart Prism after
running it. The script does not grant write access. A portable distributable
still needs a matching FFmpeg Flatpak extension or bundled FFmpeg build.
