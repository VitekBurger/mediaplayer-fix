package net.hacker.mediaplayer;

import net.minecraft.client.resources.sounds.SoundInstance;
import net.minecraft.network.chat.Component;
import net.minecraft.world.entity.Entity;
import org.apache.logging.log4j.LogManager;
import org.apache.logging.log4j.Logger;

import java.lang.ref.Cleaner;
import java.util.function.BiFunction;

public final class MediaPlayer {
    public static final String MOD_ID = "mediaplayer";
    public static final Logger LOGGER = LogManager.getLogger();
    static final Cleaner cleaner = Cleaner.create();
    public static BiFunction<Audio, Entity, SoundInstance> audioFactory;

    static {
        var os = System.getProperty("os.name", "").toLowerCase(java.util.Locale.ROOT);
        var name = os.contains("win") ? "MediaPlayer.dll" :
            os.contains("linux") ? "libMediaPlayer.so" : null;
        if (name == null) {
            throw new RuntimeException("MediaPlayer has no native backend for OS: " + os);
        }
        var resource = "/" + name;
        try (var in = MediaPlayer.class.getResourceAsStream(resource)) {
            if (in == null) throw new UnsatisfiedLinkError("Missing native library: " + resource);
            var lib = java.nio.file.Files.createTempFile("MediaPlayer-", name);
            java.nio.file.Files.write(lib, in.readAllBytes());
            lib.toFile().deleteOnExit();
            System.load(lib.toAbsolutePath().toString());
        } catch (Throwable e) {
            throw new RuntimeException("Unable to load " + resource, e);
        }
    }

    public static String getText(String key) {
        return Component.translatable(key).getString();
    }

    public static native void init(long proc);
}
