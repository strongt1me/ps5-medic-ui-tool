/* Hilfsdatei fuer PS5 Medic (nicht von websrv): ersetzt fs.h, sys.h und websrv.h, von denen hbldr.c nur fs_readfile braucht. */
#pragma once

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

/* Liest eine ganze Datei in einen malloc-Puffer (0 bei Fehler); `size` (optional) bekommt die Laenge. Nach websrv src/fs.c. */
static inline uint8_t*
fs_readfile(const char* path, size_t* size) {
  uint8_t* buf;
  long len;
  FILE* file;

  if(!(file=fopen(path, "rb"))) {
    return 0;
  }
  if(fseek(file, 0, SEEK_END) || (len=ftell(file)) < 0 || fseek(file, 0, SEEK_SET)) {
    fclose(file);
    return 0;
  }
  if(!(buf=malloc(len ? len : 1))) {
    fclose(file);
    return 0;
  }
  if(fread(buf, 1, len, file) != (size_t)len) {
    free(buf);
    fclose(file);
    return 0;
  }
  fclose(file);
  if(size) {
    *size = (size_t)len;
  }
  return buf;
}
