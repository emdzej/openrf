/* Host-filesystem backends of the file layer (POSIX): directories and disc-image files. Used by the
   SDL build and the tests; a backend without a filesystem (gasm) mounts a VfsSource instead. */
#pragma once
#include "vfs.h"

/* Mounts a data root: a directory (the extracted CD), a disc image (.iso, raw .bin), or a .cue sheet
   (its FILE line names the image, its TRACK line the sector format). False if it can't be mounted;
   whether it holds the game is up to the caller (vfs_exists("ART/ART.CAR")). */
bool vfs_mount_path(const char *path);
/* $OPENRF_DATA if set, else ./cd (tests and tools run from the project root). */
bool vfs_mount_default(void);
