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

The Prism Launcher Flatpak must provide compatible FFmpeg runtime libraries.
The host `/usr/lib` is not automatically visible inside the Flatpak sandbox.
If `libavcodec`, `libavformat`, `libswscale` or `libswresample` are absent in
the runtime, the native library will fail to load even though it works on the
host. The final distributable needs a matching FFmpeg runtime extension or a
properly bundled FFmpeg build.
