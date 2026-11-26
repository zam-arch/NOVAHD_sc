package com.novahd.recomp;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;

public final class MusicCatalogWireTest {
    public static void main(String[] args) throws Exception {
        byte[] wire = MusicCatalog.encode(List.of(
                new String[]{"List", "track-1", "First\0song", "André", "Album"},
                new String[]{"List", "track-2", "Second", "", ""}));
        Files.write(Path.of(args[0]), wire);
    }
}
