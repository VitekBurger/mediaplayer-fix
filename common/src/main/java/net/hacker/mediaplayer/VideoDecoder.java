package net.hacker.mediaplayer;

import net.minecraft.client.Minecraft;

import java.io.File;
import java.lang.ref.Cleaner;

import static net.hacker.mediaplayer.MediaPlayer.LOGGER;
import static org.lwjgl.glfw.GLFW.glfwGetTime;

public final class VideoDecoder implements AutoCloseable {
    @NativeUsed
    private final long ptr;
    @NativeUsed
    private double frameRate;
    private final Cleaner.Cleanable cleaner;
    public final VideoFrame frame;
    public final AudioDecoder audio;
    private double lastTime;
    private double deltaTime;
    private final Minecraft minecraft = Minecraft.getInstance();

    public VideoDecoder(String path, DeviceType type) {
        AudioDecoder a = null;
        try {
            a = new AudioDecoder(path);
        } catch (Throwable e) {
            LOGGER.warn("Failed to create audio decoder", e);
        }
        audio = a;
        frame = new VideoFrame();
        frame.setFilter(false, false);
        var p = ptr = open(path, frame.getId(), type.value);
        cleaner = MediaPlayer.cleaner.register(this, () -> release(p));
        lastTime = glfwGetTime();
        deltaTime = 0.0;
    }

    public double getFrameRate() {
        return frameRate;
    }

    public void fetch() {
        var currentTime = glfwGetTime();
        var frameInterval = currentTime - lastTime;
        lastTime = currentTime;
        deltaTime += frameInterval;
        if (deltaTime >= frameRate) {
            deltaTime -= frameRate;
            if (!minecraft.isPaused()) decode();
        }
    }

    private native long open(String path, int texture, int type);

    private static native void release(long ptr);

    public native void decode();

    public static VideoDecoder create(File file) {
        try {
            // Linux native currently uses the portable software FFmpeg path.
            // The native side intentionally accepts all legacy device values and
            // falls back to software decoding, preserving the public API.
            return new VideoDecoder(file.getAbsolutePath(), DeviceType.NONE);
        } catch (Throwable e) {
            throw new RuntimeException(e);
        }
    }

    @Override
    public void close() {
        if (cleaner != null) cleaner.clean();
    }
}
