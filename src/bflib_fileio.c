/******************************************************************************/
// Bullfrog Engine Emulation Library - for use to remake classic games like
// Syndicate Wars, Magic Carpet or Dungeon Keeper.
/******************************************************************************/
/** @file bflib_fileio.c
 *     File handling routines wrapper.
 * @par Purpose:
 *     Buffer library for file i/o and directory manage routines.
 *     These should be used for all file access in the game.
 * @par Comment:
 *     Wraps standard c file handling routines. You could say this has no purpose,
 *     but here it is anyway.
 * @author   Tomasz Lis
 * @date     10 Feb 2008 - 30 Dec 2008
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
#include "kfx_memory.h"
#include "pre_inc.h"
#include "bflib_fileio.h"

#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include "bflib_basics.h"
#include "platform/PlatformManager.h"

#include "post_inc.h"

/******************************************************************************/

short LbFileExists(const char *fname)
{
  return PlatformManager_FileExists(fname);
}

int LbFilePosition(TbFileHandle handle)
{
  return PlatformManager_FilePosition(handle);
}

int create_directory_for_file(const char * fname)
{
  const int size = strlen(fname) + 1;
  char * tmp = (char *) KfxAlloc(size);
  char * separator = strchr(fname, '/');

  while (separator != NULL) {
    memcpy(tmp, fname, separator - fname);
    tmp[separator - fname] = 0;
    if (PlatformManager_MakeDirectory(tmp) != 0) {
      if (errno != EEXIST) {
        KfxFree(tmp);
        return 0;
      }
    }
    separator = strchr(++separator, '/');
  }
  KfxFree(tmp);
  return 1;
}

TbFileHandle LbFileOpen(const char *fname, const unsigned char accmode)
{
  unsigned char mode = accmode;

  if ( !LbFileExists(fname) )
  {
#ifdef __DEBUG
    LbSyncLog("LbFileOpen: file doesn't exist\n");
#endif
    if ( mode == Lb_FILE_MODE_READ_ONLY )
      return NULL;
    if ( mode == Lb_FILE_MODE_OLD )
      mode = Lb_FILE_MODE_NEW;
  }
  if ( mode == Lb_FILE_MODE_NEW )
  {
    if (!create_directory_for_file(fname))
      return NULL;
  }
#ifdef __DEBUG
  LbSyncLog("LbFileOpen: mode=%d\n", mode);
#endif
  return PlatformManager_FileOpen(fname, mode);
}

int LbFileClose(TbFileHandle handle)
{
  return PlatformManager_FileClose(handle);
}

TbBool LbFileEof(TbFileHandle handle)
{
  return PlatformManager_FileEof(handle);
}

int LbFileSeek(TbFileHandle handle, long offset, unsigned char origin)
{
  return PlatformManager_FileSeek(handle, offset, origin);
}

int LbFileRead(TbFileHandle handle, void *buffer, unsigned long len)
{
  return PlatformManager_FileRead(handle, buffer, len);
}

long LbFileWrite(TbFileHandle handle, const void *buffer, const unsigned long len)
{
  return PlatformManager_FileWrite(handle, buffer, len);
}

short LbFileFlush(TbFileHandle handle)
{
  return PlatformManager_FileFlush(handle);
}

long LbFileLengthHandle(TbFileHandle handle)
{
  int pos = LbFilePosition(handle);
  LbFileSeek(handle, 0, Lb_FILE_SEEK_END);
  long result = LbFilePosition(handle);
  LbFileSeek(handle, pos, Lb_FILE_SEEK_BEGINNING);
  return result;
}

long LbFileLength(const char *fname)
{
  return PlatformManager_FileLength(fname);
}

int LbFileDelete(const char *filename)
{
  return PlatformManager_FileDelete(filename);
}

int LbDirectoryCurrent(char *buf, unsigned long buflen)
{
  if ( PlatformManager_GetCurrentDirectory(buf, buflen) >= 0 )
  {
    if ( buf[1] == ':' )
      memmove(buf, buf+2, strlen(buf+2) + 1);
    int len = strlen(buf);
    if ( len>1 )
    {
      if ( buf[len-2] == '\\' )
        buf[len-2] = '\0';
    }
    return 1;
  }
  return -1;
}

int LbFileMakeFullPath(const short append_cur_dir,
  const char *directory, const char *filename, char *buf, const unsigned long len)
{
  if (filename==NULL)
    { buf[0]='\0'; return -1; }
  unsigned long namestart;
  if ( append_cur_dir )
  {
    if ( LbDirectoryCurrent(buf, len-2) == -1 )
    { buf[0]='\0'; return -1; }
    namestart = strlen(buf);
    if ( (namestart>0) && (buf[namestart-1]!='\\') && (buf[namestart-1]!='/'))
    {
      buf[namestart] = '/';
      namestart++;
    }
  } else
  {
    namestart = 0;
  }
  buf[namestart] = '\0';

  if ( directory != NULL )
  {
      int copy_len = strlen(directory);
      if (len - 2 <= namestart + copy_len - 1)
          return -1;
      memcpy(buf + namestart, directory, copy_len);
      namestart += copy_len - 1;
      if ((namestart > 0) && (buf[namestart - 1] != '\\') && (buf[namestart - 1] != '/'))
      {
          buf[namestart] = '/';
          namestart++;
    }
    buf[namestart] = '\0';
  }
  if ( strlen(filename)+namestart-1 < len )
  {
    const char *ptr = filename;
    int invlen;
    for (invlen=-1;invlen!=0;invlen--)
    {
     if (*ptr++ == 0)
       {invlen--;break;}
    }
    int copy_len = ~invlen;
    const char* copy_src = &ptr[-copy_len];
    char* copy_dst = buf;
    for (invlen=-1;invlen!=0;invlen--)
    {
     if (*copy_dst++ == 0)
       {invlen--;break;}
    }
    memcpy(copy_dst-1, copy_src, copy_len);
    return 1;
  }
  return -1;
}

/******************************************************************************/
