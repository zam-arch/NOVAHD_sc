#include <assert.h>
#include <stdio.h>
#include "../runtime/catalog_wire.h"

int main(int argc, char **argv) {
    assert(argc == 2);
    FILE *source = fopen(argv[1], "rb");
    assert(source);
    char buffer[128];
    size_t length = fread(buffer, 1, sizeof buffer, source);
    fclose(source);
    const char *cursor = buffer, *end = buffer + length, *fields[TRACK_FIELDS];
    assert(music_catalog_row(&cursor, end, fields));
    assert(!strcmp(fields[TRACK_GROUP], "List") && !strcmp(fields[TRACK_ID], "track-1"));
    assert(!strcmp(fields[TRACK_TITLE], "First song"));
    assert(!strcmp(fields[TRACK_ARTIST], "Andr\xc3\xa9") && !strcmp(fields[TRACK_ALBUM], "Album"));
    const char *row_start = cursor;
    assert(!music_catalog_row(&cursor, end - 1, fields) && cursor == row_start);
    assert(music_catalog_row(&cursor, end, fields));
    assert(!strcmp(fields[TRACK_ID], "track-2") && !*fields[TRACK_ALBUM]);
    assert(cursor == end && !music_catalog_row(&cursor, end, fields));
    puts("Java catalog rows round-trip to the runtime with metadata and UTF-8 intact.");
}
