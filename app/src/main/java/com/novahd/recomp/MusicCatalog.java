package com.novahd.recomp;

import java.io.ByteArrayOutputStream;
import java.nio.charset.StandardCharsets;
import java.util.List;

final class MusicCatalog {
    private static final int MAX_BYTES = 512 * 1024;

    // Rows end after five NUL-terminated UTF-8 fields. Never emit a partial row.
    static byte[] encode(List<String[]> rows) {
        ByteArrayOutputStream data = new ByteArrayOutputStream();
        for (String[] row : rows) {
            ByteArrayOutputStream record = new ByteArrayOutputStream();
            for (String field : row) {
                String value = field.replace('\0', ' ');
                int end = value.offsetByCodePoints(0, Math.min(256, value.codePointCount(0, value.length())));
                byte[] bytes = value.substring(0, end).getBytes(StandardCharsets.UTF_8);
                record.write(bytes, 0, bytes.length);
                record.write(0);
            }
            if (record.size() > MAX_BYTES - data.size()) break;
            byte[] bytes = record.toByteArray();
            data.write(bytes, 0, bytes.length);
        }
        return data.toByteArray();
    }

    private MusicCatalog() {}
}
