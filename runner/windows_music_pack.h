#pragma once
#include <stddef.h>
/* Copies only recognized PCM basenames into a fresh, uniquely named directory.
 * ZIP paths are never used as output paths. Returns a malloc-owned UTF-8 path. */
char *Dkc1WindowsExtractMusicPack(const char *archive,const char *root,char *error,size_t error_size);
int Dkc1WindowsMusicTrackName(const char *name);
