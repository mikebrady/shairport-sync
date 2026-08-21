/*
 * Metadata hub and access methods.
 * Basically, if you need to store metadata
 * (e.g. for use with the dbus interfaces),
 * then you need a metadata hub,
 * where everything is stored
 * This file is part of Shairport Sync.
 * Copyright (c) Mike Brady 2017--2026
 * All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include <stdlib.h>
#include <string.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "config.h"

#include "common.h"
#include "core.h"
#include "dacp.h"
#include "hub.h"
#include "pc_queue.h"

#ifdef CONFIG_MBEDTLS
#include <mbedtls/md5.h>
#include <mbedtls/version.h>
#endif

#ifdef CONFIG_POLARSSL
#include <polarssl/md5.h>
#endif

#ifdef CONFIG_OPENSSL
#include <openssl/evp.h>
#endif

// metadata queue definitions
pc_queue metadata_hub_queue;
#define metadata_hub_queue_size 500
metadata_package metadata_hub_queue_items[metadata_hub_queue_size];
pthread_t metadata_hub_thread;

struct metadata_bundle metadata_store;
metadata_watcher metadata_watchers[number_of_watchers];

int metadata_hub_initialised = 0;

pthread_rwlock_t metadata_hub_re_lock = PTHREAD_RWLOCK_INITIALIZER;

// update *str with the string specified by *data and length
int update_string_record_with_data(char **str, const char *data, size_t length) {
  int changed = 0;
  if (((*str != NULL) && (data != NULL) && (strlen(*str) == length) &&
       (strncmp(*str, data, length) == 0)) ||
      ((*str == NULL) && ((data == NULL) || (length == 0)))) {
    changed = 0;
  } else {
    changed = 1;
    if (*str != NULL)
      free(*str);
    if ((data == NULL) || (length == 0))
      *str = NULL;
    else
      *str = strndup(data, length);
  }
  return changed;
}

// free any pre-assigned string before copying in a new string
// return 1 if anything has changed, 0 otherwise
int update_string_record(char **str, const char *s) {
  int result = 0;
  if (s == NULL)
    result = update_string_record_with_data(str, NULL, 0);
  else
    result = update_string_record_with_data(str, s, strlen(s));
  return result;
}

int update_uint64_record(uint64_record_t *record, const uint64_t value) {
  int changed = 0;
  if (record != NULL) {
    changed = ((record->item != value) || (record->valid == 0));
    record->item = value;
    record->valid = 1;
  } else {
    debug(1, "passing a NULL uint64_record_t pointer to update_uint64_record!");
  }
  return changed;
}

int invalidate_string_record(char **str) {
  return update_string_record(str, NULL);
}

int is_valid_uint64_record(uint64_record_t *record) {
  int valid = 0;
  if (record != NULL) {
    valid = record->valid;
  } else {
    debug(1, "passing a NULL uint64_record_t pointer to is_valid_uint64_record!");
  }
  return valid;
}

void invalidate_uint64_record(uint64_record_t *record) {
  if (record != NULL) {
    record->valid = 0;
  } else {
    debug(1, "passing a NULL uint64_record_t pointer to invalidate_uint64_record!");
  }
}

void metadata_hub_init(void) {
  // debug(1, "Metadata bundle initialisation.");
  memset(&metadata_store, 0, sizeof(metadata_store));
  // zero is inappropriate for an initial airplay volume, as it means 0 dB, i.e. full volume!
  // so we'll use the present config.airplay_volume until we know better...
  metadata_store.airplay_volume = config.airplay_volume;
  metadata_hub_initialised = 1;
}

void metadata_hub_stop(void) {}

void add_metadata_watcher(metadata_watcher fn) {
  int i;
  for (i = 0; i < number_of_watchers; i++) {
    if (metadata_watchers[i] == NULL) {
      metadata_watchers[i] = fn;
      // debug(1, "Added a metadata watcher into slot %d", i);
      break;
    }
  }
}

void run_metadata_watchers(void) {
  int i;
  for (i = 0; i < number_of_watchers; i++) {
    if (metadata_watchers[i]) {
      metadata_watchers[i](&metadata_store);
    }
  }
}

void metadata_hub_unlock_hub_mutex_cleanup(__attribute__((unused)) void *arg) {
  debug(1, "metadata_hub_unlock_hub_mutex_cleanup called.");
  metadata_hub_modify_epilog(0);
}

char *last_metadata_hub_modify_prolog_file = NULL;
int last_metadata_hub_modify_prolog_line;
int metadata_hub_re_lock_access_is_delayed;

void _metadata_hub_modify_prolog(const char *filename, const int linenumber) {
  // always run this before changing an entry or a sequence of entries in the metadata_hub
  // debug(1, "locking metadata hub for writing");
  if (pthread_rwlock_trywrlock(&metadata_hub_re_lock) != 0) {
    if (last_metadata_hub_modify_prolog_file)
      debug(3, "Metadata_hub write lock at \"%s:%d\" is already taken at \"%s:%d\" -- must wait.",
            filename, linenumber, last_metadata_hub_modify_prolog_file,
            last_metadata_hub_modify_prolog_line);
    else
      debug(3, "Metadata_hub write lock is already taken by unknown -- must wait.");
    metadata_hub_re_lock_access_is_delayed = 0;
    pthread_rwlock_wrlock(&metadata_hub_re_lock);
    debug(3, "Okay -- acquired the metadata_hub write lock at \"%s:%d\".", filename, linenumber);
  } else {
    if (last_metadata_hub_modify_prolog_file) {
      free(last_metadata_hub_modify_prolog_file);
    }
    last_metadata_hub_modify_prolog_file = strdup(filename);
    last_metadata_hub_modify_prolog_line = linenumber;
    // debug(3, "Metadata_hub write lock acquired.");
  }
  metadata_hub_re_lock_access_is_delayed = 0;
}

void _metadata_hub_modify_epilog(int modified, const char *filename, const int linenumber) {
#ifdef CONFIG_DACP_CLIENT
  metadata_store.dacp_server_has_been_active =
      metadata_store.dacp_server_active; // set the scanner_has_been_active now.
#endif
  if (modified) {
    debug(2, "run metadata watchers");
    run_metadata_watchers();
  }

  if (metadata_hub_re_lock_access_is_delayed) {
    if (last_metadata_hub_modify_prolog_file) {
      debug(1, "Metadata_hub write lock taken at \"%s:%d\" is freed at \"%s:%d\".",
            last_metadata_hub_modify_prolog_file, last_metadata_hub_modify_prolog_line, filename,
            linenumber);
      free(last_metadata_hub_modify_prolog_file);
      last_metadata_hub_modify_prolog_file = NULL;
    } else {
      debug(1, "Metadata_hub write lock taken at an unknown place is freed at \"%s:%d\".", filename,
            linenumber);
    }
  }
  pthread_rwlock_unlock(&metadata_hub_re_lock);
  // debug(3, "Metadata_hub write lock unlocked.");
}

char *metadata_write_image_file(const char *buf, int len) {

  // warning -- this removes all files from the directory apart from this one, if it exists
  // it will return a path to the image file allocated with malloc.
  // free it if you don't need it.

  int oldState;
  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldState); // make this un-cancellable
  char *path = NULL;                                         // this will be what is returned
  if (strcmp(config.cover_art_cache_dir, "") != 0) { // an empty string means do not write the file

    uint8_t img_md5[16];
    // uint8_t ap_md5[16];

#ifdef CONFIG_OPENSSL
    EVP_MD_CTX *ctx;
    unsigned int img_md5_len = EVP_MD_size(EVP_md5());

    ctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(ctx, EVP_md5(), NULL);
    EVP_DigestUpdate(ctx, buf, len);
    EVP_DigestFinal_ex(ctx, img_md5, &img_md5_len);
    EVP_MD_CTX_free(ctx);
#endif

#ifdef CONFIG_MBEDTLS
#if MBEDTLS_VERSION_MINOR >= 7
    mbedtls_md5_context tctx;
    mbedtls_md5_starts_ret(&tctx);
    mbedtls_md5_update_ret(&tctx, (const unsigned char *)buf, len);
    mbedtls_md5_finish_ret(&tctx, img_md5);
#else
    mbedtls_md5_context tctx;
    mbedtls_md5_starts(&tctx);
    mbedtls_md5_update(&tctx, (const unsigned char *)buf, len);
    mbedtls_md5_finish(&tctx, img_md5);
#endif
#endif

#ifdef CONFIG_POLARSSL
    md5_context tctx;
    md5_starts(&tctx);
    md5_update(&tctx, (const unsigned char *)buf, len);
    md5_finish(&tctx, img_md5);
#endif

    char img_md5_str[33];
    memset(img_md5_str, 0, sizeof(img_md5_str));
    char *ext;
    char png[] = "png";
    char jpg[] = "jpg";
    int i;
    for (i = 0; i < 16; i++)
      snprintf(&img_md5_str[i * 2], 3, "%02x", (uint8_t)img_md5[i]);
    // see if the file is a jpeg or a png
    if (strncmp(buf, "\xFF\xD8\xFF", 3) == 0)
      ext = jpg;
    else if (strncmp(buf, "\x89\x50\x4E\x47\x0D\x0A\x1A\x0A", 8) == 0)
      ext = png;
    else {
      debug(1, "Unidentified image type of cover art -- jpg extension used.");
      ext = jpg;
    }
    mode_t oldumask = umask(000);
    int result = mkpath(config.cover_art_cache_dir, 0777);
    umask(oldumask);
    if ((result == 0) || (result == -EEXIST)) {
      // see if the file exists by opening it.
      // if it exists, we're done
      char *prefix = "cover-";

      size_t pl = strlen(config.cover_art_cache_dir) + 1 + strlen(prefix) + strlen(img_md5_str) +
                  1 + strlen(ext);

      path = malloc(pl + 1);
      snprintf(path, pl + 1, "%s/%s%s.%s", config.cover_art_cache_dir, prefix, img_md5_str, ext);
      int cover_fd = open(path, O_WRONLY | O_CREAT | O_EXCL, S_IRWXU | S_IRGRP | S_IROTH);
      if (cover_fd > 0) {
        // write the contents
        if (write(cover_fd, buf, len) < len) {
          warn("Writing cover art file \"%s\" failed!", path);
          free(path);
          path = NULL;
        }
        close(cover_fd);

        // now delete all other files, if requested
        if (config.retain_coverart == 0) {
          DIR *d;
          struct dirent *dir;
          d = opendir(config.cover_art_cache_dir);
          if (d) {
            int fnl = strlen(prefix) + strlen(img_md5_str) + 1 + strlen(ext) + 1;

            char *full_filename = malloc(fnl);
            if (full_filename == NULL)
              die("Can't allocate memory at metadata_write_image_file.");
            memset(full_filename, 0, fnl);
            snprintf(full_filename, fnl, "%s%s.%s", prefix, img_md5_str, ext);
            int dir_fd = open(config.cover_art_cache_dir, O_DIRECTORY);
            if (dir_fd > 0) {
              while ((dir = readdir(d)) != NULL) {
                if (dir->d_type == DT_REG) {
                  if (strcmp(full_filename, dir->d_name) != 0) {
                    if (unlinkat(dir_fd, dir->d_name, 0) != 0) {
                      debug(1, "Error %d deleting cover art file \"%s\".", errno, dir->d_name);
                    }
                  }
                }
              }
              if (close(dir_fd) < 0)
                debug(1, "Error %d closing directory \"%s\"", errno, config.cover_art_cache_dir);
            } else {
              debug(1, "Can't open the directory \"%s\" for deletion -- error %d.",
                    config.cover_art_cache_dir, errno);
            }
            free(full_filename);
            closedir(d);
          }
        }
      } else {
        //      if (errno == EEXIST)
        //        debug(1, "Cover art file \"%s\" already exists!", path);
        //      else {
        if (errno != EEXIST) {
          warn("Could not open file \"%s\" for writing cover art", path);
          free(path);
          path = NULL;
        }
      }
    } else {
      debug(1, "Couldn't access or create the cover art cache directory \"%s\".",
            config.cover_art_cache_dir);
    }
  }
  pthread_setcancelstate(oldState, NULL);
  return path;
}

int metadata_hub_process_picture(const char *data, const size_t length) {
  int changed = 0;
  if (length > 0) {
    if (data != NULL) {
      char uri[2048];
      if ((length > 16) &&
          (strcmp(config.cover_art_cache_dir, "") != 0)) { // if it's okay to write the file
        char *pathname = metadata_write_image_file(data, length);
        snprintf(uri, sizeof(uri), "file://%s", pathname);
        free(pathname);
        changed = update_string_record(&metadata_store.npi.cover_art_pathname, uri);
      }
    } else {
      debug(1, "faulty picture data -- data NULL with non-zero length!");
    }
  } else { // length of incoming picture is zero...
    changed = update_string_record(&metadata_store.npi.cover_art_pathname, NULL);
  }
  return changed;
}

int metadata_packet_item_changed = 0; // set if any parsed part of a metadata stream changes
metadata_npi_bundle new_npi;
char *temporary_cover_art_pathname = NULL;
uint64_record_t temporary_item_id; 

void metadata_hub_process_metadata(uint32_t type, uint32_t code, char *data, uint32_t length) {
  // metadata coming in from the audio source or from Shairport Sync itself passes through here
  // this has more information about tags, which might be relevant:
  // https://code.google.com/p/ytrack/wiki/DMAP

  // Some metadata items are contained in one metadata packet.
  // The start of the metadata packet is signalled by an 'ssnc' 'mdst' item and
  // the end of it by an 'ssnc 'mden' item.
  // We don't set "changed" for them individually; instead we set it when the  'mden' token
  // comes in if the metadata_packet_item_changed item is set by parsed items
  // within the packet.

  int changed = 0;
  metadata_hub_modify_prolog();
  pthread_cleanup_push(metadata_hub_unlock_hub_mutex_cleanup, NULL);
  if (type == 'core') {
    switch (code) {
    case 'asdk': {
      // get the one-byte number as an unsigned number
      debug(3, "MH Song Data Kind seen: \"%d\" of length %u.", (unsigned)data[0], length);
      metadata_packet_item_changed |=
          update_uint64_record(&metadata_store.npi.song_data_kind, (unsigned)data[0]);
      update_uint64_record(&new_npi.song_data_kind, (unsigned)data[0]);
    } break;
    case 'mper': {
      // get the 64-bit number as a uint64_t by reading two uint32_t s and combining them
      uint64_t vl = ntohl(*(uint32_t *)data); // get the high order 32 bits
      vl = vl << 32;                          // shift them into the correct location
      uint64_t ul = ntohl(*(uint32_t *)(data + sizeof(uint32_t))); // and the low order 32 bits
      vl = vl + ul;
      debug(4, "MH Item ID seen: \"%" PRIx64 "\" of length %u.", vl, length);
      metadata_packet_item_changed |= update_uint64_record(&metadata_store.npi.item_id, vl); // item id
      update_uint64_record(&new_npi.item_id, vl);
    } break;
    case 'astm': {
      uint32_t ui = ntohl(*(uint32_t *)data);
      debug(3, "MH Song Time seen: \"%u\" milliseconds, of length %u.", ui, length);
      metadata_packet_item_changed |=
          update_uint64_record(&metadata_store.npi.songtime_in_microseconds, ui * 1000); // microseconds
      update_uint64_record(&new_npi.songtime_in_microseconds, ui * 1000);
    } break;
    case 'asal':
      metadata_packet_item_changed |=
          update_string_record_with_data(&metadata_store.npi.album_name, data, length);
      update_string_record_with_data(&new_npi.album_name, data, length);
      debug(3, "MH Album name set to: \"%s\"", metadata_store.npi.album_name);
      break;
    case 'asar':
      metadata_packet_item_changed |=
          update_string_record_with_data(&metadata_store.npi.artist_name, data, length);
      update_string_record_with_data(&new_npi.artist_name, data, length);
      debug(3, "MH Artist name set to: \"%s\"", metadata_store.npi.artist_name);
      break;
    case 'assl':
      metadata_packet_item_changed |=
          update_string_record_with_data(&metadata_store.npi.album_artist_name, data, length);
      update_string_record_with_data(&new_npi.album_artist_name, data, length);
      debug(3, "MH Album Artist name set to: \"%s\"", metadata_store.npi.album_artist_name);
      break;
    case 'ascm':
      if (update_string_record_with_data(&metadata_store.npi.comment, data, length)) {
        debug(3, "MH Comment set to: \"%s\"", metadata_store.npi.comment);
        metadata_packet_item_changed |= 1;
      }
      update_string_record_with_data(&new_npi.comment, data, length);
      break;
    case 'asgn':
      if (update_string_record_with_data(&metadata_store.npi.genre, data, length)) {
        debug(3, "MH Genre set to: \"%s\"", metadata_store.npi.genre);
        metadata_packet_item_changed |= 1;
      }
      update_string_record_with_data(&new_npi.genre, data, length);
      break;
    case 'minm':
      if (update_string_record_with_data(&metadata_store.npi.track_name, data, length)) {
        debug(3, "MH Track Name set to: \"%s\"", metadata_store.npi.track_name);
        metadata_packet_item_changed |= 1;
      }
      update_string_record_with_data(&new_npi.track_name, data, length);
      break;
    case 'astn': {
      uint16_t ui = ntohs(*(uint16_t *)data);
      debug(3, "MH Track Number seen: \"%u\" of length %u.", ui, length);
      metadata_packet_item_changed |= update_uint64_record(&metadata_store.npi.track_number, ui);
      update_uint64_record(&new_npi.track_number, ui);
    } break;
    case 'ascp':
      if (update_string_record_with_data(&metadata_store.npi.composer, data, length)) {
        debug(3, "MH Composer set to: \"%s\"", metadata_store.npi.composer);
        metadata_packet_item_changed |= 1;
      }
      update_string_record_with_data(&new_npi.composer, data, length);
      break;
    case 'asdt':
      if (update_string_record_with_data(&metadata_store.npi.song_description, data, length)) {
        debug(3, "MH Song Description set to: \"%s\"", metadata_store.npi.song_description);
        metadata_packet_item_changed |= 1;
      }
      update_string_record_with_data(&new_npi.song_description, data, length);
      break;
    case 'asaa':
      if (update_string_record_with_data(&metadata_store.npi.song_album_artist, data, length)) {
        debug(3, "MH Song Album Artist set to: \"%s\"", metadata_store.npi.song_album_artist);
        metadata_packet_item_changed |= 1;
      }
      update_string_record_with_data(&new_npi.song_album_artist, data, length);
      break;
    case 'assn':
      if (update_string_record_with_data(&metadata_store.npi.sort_name, data, length)) {
        debug(3, "MH Sort Name set to: \"%s\"", metadata_store.npi.sort_name);
        metadata_packet_item_changed |= 1;
      }
      update_string_record_with_data(&new_npi.sort_name, data, length);
      break;
    case 'assa':
      if (update_string_record_with_data(&metadata_store.npi.sort_artist, data, length)) {
        debug(3, "MH Sort Artist set to: \"%s\"", metadata_store.npi.sort_artist);
        metadata_packet_item_changed |= 1;
      }
      update_string_record_with_data(&new_npi.sort_artist, data, length);
      break;
    case 'assu':
      if (update_string_record_with_data(&metadata_store.npi.sort_album, data, length)) {
        debug(3, "MH Sort Album set to: \"%s\"", metadata_store.npi.sort_album);
        metadata_packet_item_changed |= 1;
      }
      update_string_record_with_data(&new_npi.sort_album, data, length);
      break;
    case 'assc':
      if (update_string_record_with_data(&metadata_store.npi.composer, data, length)) {
        debug(3, "MH Sort Composer set to: \"%s\"", metadata_store.npi.sort_composer);
        metadata_packet_item_changed |= 1;
      }
      update_string_record_with_data(&new_npi.composer, data, length);
      break;
    default:
      /*
          {
            char typestring[5];
            *(uint32_t *)typestring = htonl(type);
            typestring[4] = 0;
            char codestring[5];
            *(uint32_t *)codestring = htonl(code);
            codestring[4] = 0;
            char *payload;
            if (length < 2048)
              payload = strndup(data, length);
            else
              payload = NULL;
            debug(1, "MH \"%s\" \"%s\" (%d bytes): \"%s\".", typestring, codestring, length,
         payload);
            if (payload)
              free(payload);
          }
      */
      break;
    }
  } else if (type == 'ssnc') {
    switch (code) {
    // ignore the following
    case 'pcst':
    case 'pcen':
      break;
    case 'dapo': {
        char *dacp_port_string = strndup(data, length);
        debug(3, "DACP port is \"%s\"", dacp_port_string);
        free(dacp_port_string);
      }
      break;
    case 'mdst':
      debug(3, "MH Metadata stream processing start.");
      // There is a difficulty with this NPI metadata as it comes in.
      
      // As it comes in, we don't know whether it is an update to the current NPI data or whether it is for
      // a new track, hence completely new NPI data.
      
      // So, we will do create a new empty NPI structure and add each incoming item to it as well as
      // updating it into the current NPI data. 
      // When we get a track ID, if it's the same as the current NPI track ID,
      // then keep the current updated NPI.
      // Otherwise, copy the new NPI into the current NPI.
      
      metadata_packet_item_changed = 0;
      memset(&new_npi, 0, sizeof(new_npi)); // initialise the new npi structure
      // the picture arrives separately from the npi stuff, and may (?) arrive before or after it, so we have to keep it
      temporary_cover_art_pathname = metadata_store.npi.cover_art_pathname;
      // the item id wold be updated if it changes, so we need to keep the current one
      temporary_item_id = metadata_store.npi.item_id;
      break;
    case 'mden':
      // here, we decide whether to take the updated npi data or to take the
      // completely new one.
      
      new_npi.cover_art_pathname = temporary_cover_art_pathname; // we must preserve any existing picture
      
      // if the track_id of the new npi differs from the current npi
      if ((temporary_item_id.valid != 0) && (new_npi.item_id.valid != 0) && (temporary_item_id.item != new_npi.item_id.item)) {
        debug(3, "MH Metadata detected for a new track: %" PRIu64 ".", new_npi.item_id.item);
        metadata_store.npi = new_npi;
        metadata_packet_item_changed = 1;  
      }
    
      if (metadata_packet_item_changed != 0)
        debug(3, "MH Metadata stream processing end with changes.");
      else
        debug(3, "MH Metadata stream processing end without changes.");
      changed = metadata_packet_item_changed;
      break;
    case 'PICT':
      changed = metadata_hub_process_picture(data, length);
      break;
    case 'clip':
      changed = update_string_record_with_data(&metadata_store.client_ip, data, length);
      if (changed)
        debug(3, "MH Client IP set to: \"%s\"", metadata_store.client_ip);
      break;
    case 'snam':
      changed = update_string_record_with_data(&metadata_store.client_name, data, length);
      if (changed)
        debug(3, "MH Client Name set to: \"%s\"", metadata_store.client_name);
      break;
    case 'prgr':
      changed = update_string_record_with_data(&metadata_store.progress_string, data, length);
      if (changed)
        debug(3, "MH Progress String set to: \"%s\"", metadata_store.progress_string);
      break;
    case 'phbt':
      changed = update_string_record_with_data(&metadata_store.frame_position_string, data, length);
      if (changed)
        debug(3, "MH Frame Position String set to: \"%s\"", metadata_store.frame_position_string);
      break;
    case 'phb0':
      if (update_string_record_with_data(&metadata_store.first_frame_position_string, data,
                                         length)) {
        changed = 1;
        debug(3, "MH First Frame Position String set to: \"%s\"",
              metadata_store.first_frame_position_string);
      }
      break;
    case 'styp':
      if (update_string_record_with_data(&metadata_store.stream_type, data, length)) {
        changed = 1;
        debug(3, "MH Stream Type set to: \"%s\"", metadata_store.stream_type);
      }
      break;
    case 'sdsc':
      if (update_string_record_with_data(&metadata_store.source_format, data, length)) {
        changed = 1;
        debug(3, "MH Source Format set to: \"%s\"", metadata_store.source_format);
      }
      break;
    case 'odsc':
      if (update_string_record_with_data(&metadata_store.output_format, data, length)) {
        changed = 1;
        debug(3, "MH Output Format set to: \"%s\"", metadata_store.output_format);
      }
      break;
    case 'svip':
      if (update_string_record_with_data(&metadata_store.server_ip, data, length)) {
        changed = 1;
        debug(3, "MH Server IP set to: \"%s\"", metadata_store.server_ip);
      }
      break;
    case 'abeg':
      changed = (metadata_store.active_state != AM_ACTIVE);
      metadata_store.active_state = AM_ACTIVE;
      break;
    case 'aend':
      changed = (metadata_store.active_state != AM_INACTIVE);
      metadata_store.active_state = AM_INACTIVE;
      break;
    case 'pres':
    case 'pbeg':
      changed = ((metadata_store.player_state != PS_PLAYING) ||
                 (metadata_store.player_thread_active == 0));
      metadata_store.player_state = PS_PLAYING;
      metadata_store.player_thread_active = 1;
      break;
    case 'pend':
      changed = ((metadata_store.player_state != PS_STOPPED) ||
                 (metadata_store.player_thread_active == 1));
      metadata_store.player_state = PS_STOPPED;
      metadata_store.player_thread_active = 0;
      break;
    case 'paus':
      changed = (metadata_store.player_state != PS_PAUSED);
      metadata_store.player_state = PS_PAUSED;
      break;
      /*
      // not using this anymore.
          case 'pffr': // this is sent when the first frame has been received
          case 'prsm':
            changed = (metadata_store.player_state != PS_PLAYING);
            metadata_store.player_state = PS_PLAYING;
            break;
      */
    case 'pvol': {
      // Note: it's assumed that the config.airplay volume has already been correctly set.
      // int32_t actual_volume;
      // int gv = dacp_get_volume(&actual_volume);
      // metadata_hub_modify_prolog();
      // if ((gv == 200) && (metadata_store.speaker_volume != actual_volume)) {
      //  metadata_store.speaker_volume = actual_volume;
      //  changed = 1;
      //}
      if (metadata_store.airplay_volume != config.airplay_volume) {
        metadata_store.airplay_volume = config.airplay_volume;
        changed = 1;
      }
    } break;
    default: {
      char typestring[5];
      uint32_t tm = htonl(type);
      memcpy(typestring, &tm, sizeof(uint32_t));
      typestring[4] = 0;
      char codestring[5];
      uint32_t cm = htonl(code);
      memcpy(codestring, &cm, sizeof(uint32_t));
      codestring[4] = 0;
      char *payload;
      if (length < 2048)
        payload = strndup(data, length);
      else
        payload = NULL;
      // debug(1, "MH \"%s\" \"%s\" (%d bytes): \"%s\".", typestring, codestring, length, payload);
      if (payload)
        free(payload);
    }
    }
  }
  pthread_cleanup_pop(0); // don't remove the lock
  metadata_hub_modify_epilog(changed);
}

void metadata_hub_close(void) {}

void metadata_hub_thread_cleanup_function(__attribute__((unused)) void *arg) {
  // debug(2, "metadata_hub_thread_cleanup_function called");
  metadata_hub_close();
}

void *metadata_hub_thread_function(__attribute__((unused)) void *ignore) {
  //  #include <syscall.h>
  //  debug(1, "metadata_hub_thread_function PID %d", syscall(SYS_gettid));
  metadata_package pack;
  pthread_cleanup_push(metadata_hub_thread_cleanup_function, NULL);
  while (1) {
    pc_queue_get_item(&metadata_hub_queue, &pack);
    pthread_cleanup_push(metadata_pack_cleanup_function, (void *)&pack);
    if (pack.carrier) {
      debug(4, "                    hub: type %x, code %x, length %u, message %d.", pack.type,
            pack.code, pack.length, pack.carrier->index_number);
    } else {
      debug(4, "                    hub: type %x, code %x, length %u.", pack.type, pack.code,
            pack.length);
    }
    metadata_hub_process_metadata(pack.type, pack.code, pack.data, pack.length);
    debug(4, "                    hub: done.");
    pthread_cleanup_pop(1);
  }
  pthread_cleanup_pop(1); // will never happen
  pthread_exit(NULL);
}

void metadata_hub_queue_init() {
  // create a pc_queue for the metadata hub
  pc_queue_init(&metadata_hub_queue, (char *)&metadata_hub_queue_items, sizeof(metadata_package),
                metadata_hub_queue_size, "hub");
  if (named_pthread_create(&metadata_hub_thread, NULL, metadata_hub_thread_function, NULL,
                           "metadata hub") != 0)
    debug(1, "Failed to create metadata hub thread!");
}

void metadata_hub_queue_stop() {
  // debug(2, "metadata stop hub thread.");
  pthread_cancel(metadata_hub_thread);
  pthread_join(metadata_hub_thread, NULL);
  pc_queue_delete(&metadata_hub_queue);
  // debug(2, "metadata stop hub done.");
}

int send_metadata_to_hub_queue(const uint32_t type, const uint32_t code, const char *data,
                               const uint32_t length, rtsp_message *carrier, int block) {
  return send_metadata_to_queue(&metadata_hub_queue, type, code, data, length, carrier, block);
}

// reset all now playing information
void metadata_hub_reset_npi(metadata_npi_bundle *npi) {
  debug(4, "metadata_hub_reset_npi");
  invalidate_string_record(&npi->cover_art_pathname);
  invalidate_uint64_record(&npi->item_id);
  npi->item_composite_id_is_valid = 0;
  invalidate_uint64_record(&npi->song_data_kind);
  invalidate_string_record(&npi->track_name);
  invalidate_uint64_record(&npi->track_number);
  invalidate_string_record(&npi->artist_name);
  invalidate_string_record(&npi->album_artist_name);
  invalidate_string_record(&npi->album_name);
  invalidate_string_record(&npi->genre);
  invalidate_string_record(&npi->comment);
  invalidate_string_record(&npi->composer);
  invalidate_string_record(&npi->file_kind);
  invalidate_string_record(&npi->song_description);
  invalidate_string_record(&npi->song_album_artist);
  invalidate_string_record(&npi->sort_name);
  invalidate_string_record(&npi->sort_artist);
  invalidate_string_record(&npi->sort_album);
  invalidate_string_record(&npi->sort_composer);
  invalidate_uint64_record(&npi->songtime_in_microseconds);
#ifdef CONFIG_AIRPLAY_2
  if (npi->npi_plist != NULL) {
    plist_free(npi->npi_plist);
    npi->npi_plist = NULL;
  }
#endif
}
