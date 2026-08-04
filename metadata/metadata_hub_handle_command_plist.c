/*
 * RTSP Command Endpoint metadata handler for AirPlay 2 only.
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

#include "metadata_hub_handle_command_plist.h"
#include "common.h"
#include "core.h"
#include "hub.h"
#include "utilities/rtsp_message_utilities.h"


// merge items in the second plist into the first

void plist_merge(plist_t base, plist_t changes) {
    if (plist_get_node_type(base) != PLIST_DICT ||
        plist_get_node_type(changes) != PLIST_DICT) {
        return; // only makes sense dict-into-dict at the top
    }

    plist_dict_iter it = NULL;
    plist_dict_new_iter(changes, &it);

    char *key = NULL;
    plist_t new_val = NULL;
    plist_dict_next_item(changes, it, &key, &new_val);

    while (key) {
        plist_t existing = plist_dict_get_item(base, key);

        if (existing &&
            plist_get_node_type(existing) == PLIST_DICT &&
            plist_get_node_type(new_val) == PLIST_DICT) {
            // both are dicts -> recurse instead of overwrite
            plist_merge(existing, new_val);
        } else {
            // scalar, array, or type mismatch -> overwrite
            plist_dict_set_item(base, key, plist_copy(new_val));
        }

        free(key);
        key = NULL;
        plist_dict_next_item(changes, it, &key, &new_val);
    }
    free(it);
}

void metadata_hub_handle_command_plist(const plist_t command_dict) {
  if (command_dict != NULL) {
    plist_t command_type = plist_dict_get_item(command_dict, "type");
    if (command_type != NULL) {
      char *command_type_string = NULL;
      plist_get_string_val(command_type, &command_type_string);
      // debug(1, "Connection %d: POST /command plist type \"%s\" received.",
      // conn->connection_number, command_type_string); debug_log_rtsp_message(1, NULL, req);
      if (command_type_string != NULL) {
        if (strcmp(command_type_string, "updateMRNowPlayingInfo") == 0) {
          plist_t command_params = plist_dict_get_item(command_dict, "params");
          if (command_params != NULL) {
            plist_t command_params_type = plist_dict_get_item(command_params, "type");
            if (command_params_type != NULL) {
              char *command_params_type_string = NULL;
              plist_get_string_val(command_params_type, &command_params_type_string);
              if (command_params_type_string != NULL) {
                if (strcmp(command_params_type_string, "npi-text") == 0) {
                  int merge_policy_is_replace = 0;
                  int metadata_changed = 1; // we can't easily tell if the metadata is changing, unfortunately.
                  // debug(1, "updateMRNowPlayingInfo");
                  metadata_hub_modify_prolog();
                  // we have now playing info (npi-text)
                  // check if the update policy is replace or update
                  plist_t command_params_merge_policy =
                      plist_dict_get_item(command_params, "mergePolicy");
                  if (command_params_merge_policy != NULL) {
                    char *command_params_merge_policy_string = NULL;
                    plist_get_string_val(command_params_merge_policy,
                                         &command_params_merge_policy_string);
                    if (command_params_merge_policy_string != NULL) {
                      if (strcmp(command_params_merge_policy_string, "replace") == 0) {
                        merge_policy_is_replace = 1;
                        metadata_hub_reset_npi(&metadata_store.npi);
                      }
                      free(command_params_merge_policy_string);
                    }
                  }
                  
                  // now get the npi-text parameters themselves                  
                  plist_t npi_params = plist_dict_get_item(command_params, "params");
                  
                  if (merge_policy_is_replace != 0) {
                    debug(4, "replace metadata");
                    if (metadata_store.npi.npi_plist != NULL) {
                      plist_free(metadata_store.npi.npi_plist);
                    }
                    metadata_store.npi.npi_plist = plist_copy(npi_params);
                  } else {
                    debug(4, "merging metadata");
                    plist_merge(metadata_store.npi.npi_plist, npi_params); 
                  }
                  
                  if (metadata_store.npi.npi_plist != NULL) {
                    // If we have a kMRMediaRemoteNowPlayingInfoArtworkData item, which
                    // is the bytes of the file
                    // We need to save it in a file and add the file path to the plist.
                     plist_t pict_item = plist_dict_get_item(
                          metadata_store.npi.npi_plist, "kMRMediaRemoteNowPlayingInfoArtworkData");
                      if (pict_item != NULL) {                       
                        char *buff = NULL;
                        uint64_t length = 0;
                        plist_get_data_val(pict_item, &buff, &length);
                        size_t length_size = length;
                        metadata_changed |= metadata_hub_process_picture(buff, length_size);
                        plist_dict_set_item(metadata_store.npi.npi_plist, "kShairportSyncNowPlayingInfoArtworkFilePath", plist_new_string(metadata_store.npi.cover_art_pathname));
                        plist_dict_remove_item(metadata_store.npi.npi_plist, "kMRMediaRemoteNowPlayingInfoArtworkData"); // remove it
                      }
                    
                    // look for album name
                    plist_t album_item =
                        plist_dict_get_item(metadata_store.npi.npi_plist, "kMRMediaRemoteNowPlayingInfoAlbum");
                    if (album_item != NULL) {
                      char *album_name = NULL;
                      plist_get_string_val(album_item, &album_name);
                      // debug(1, "Send album name: \"%s\".", album_name);
                      metadata_changed |=
                          update_string_record(&metadata_store.npi.album_name, album_name);
                      free(album_name);
                    }

                    // look for track title
                    plist_t track_title_item =
                        plist_dict_get_item(metadata_store.npi.npi_plist, "kMRMediaRemoteNowPlayingInfoTitle");
                    if (track_title_item != NULL) {
                      char *track_title = NULL;
                      plist_get_string_val(track_title_item, &track_title);
                      // debug(1, "Send track title: \"%s\".", track_title);
                      metadata_changed |=
                          update_string_record(&metadata_store.npi.track_name, track_title);
                      free(track_title);
                    }

                    // look for track_number
                    plist_t track_number_item =
                        plist_dict_get_item(metadata_store.npi.npi_plist, "kMRMediaRemoteNowPlayingInfoTrackNumber");
                    if (track_number_item != NULL) {
                      uint64_t track_number;
                      plist_get_uint_val(track_number_item, &track_number);
                      // debug(1, "Send track number: %" PRIu64 ".", track_number);
                      metadata_changed |=
                          update_uint64_record(&metadata_store.npi.track_number, track_number);
                    }

                    // look for track id
                    plist_t track_id_item = plist_dict_get_item(
                        metadata_store.npi.npi_plist, "kMRMediaRemoteNowPlayingInfoUniqueIdentifier");
                    if (track_id_item != NULL) {
                      uint64_t track_id;
                      plist_get_uint_val(track_id_item, &track_id);
                      // debug(1, "Send track id: %" PRIu64 "", track_id);
                      metadata_changed |= update_uint64_record(&metadata_store.npi.item_id, track_id);
                    }

                    // look for song data kind
                    plist_t always_live_item =
                        plist_dict_get_item(metadata_store.npi.npi_plist, "kMRMediaRemoteNowPlayingInfoIsAlwaysLive");
                    if (always_live_item != NULL) {
                      uint8_t always_live;
                      plist_get_bool_val(always_live_item, &always_live);
                      // debug(1, "Send track kind: %u", always_live);
                      metadata_changed |=
                          update_uint64_record(&metadata_store.npi.song_data_kind, always_live);
                    }

                    // look for genre
                    plist_t genre_item =
                        plist_dict_get_item(metadata_store.npi.npi_plist, "kMRMediaRemoteNowPlayingInfoGenre");
                    if (genre_item != NULL) {
                      char *genre_name = NULL;
                      plist_get_string_val(genre_item, &genre_name);
                      // debug(1, "Send genre: \"%s\".", genre_name);
                      metadata_changed |= update_string_record(&metadata_store.npi.genre, genre_name);
                      free(genre_name);
                    }

                    // look for artist name
                    plist_t artist_item =
                        plist_dict_get_item(metadata_store.npi.npi_plist, "kMRMediaRemoteNowPlayingInfoArtist");
                    if (artist_item != NULL) {
                      char *artist_name = NULL;
                      plist_get_string_val(artist_item, &artist_name);
                      // debug(1, "Send artist name: \"%s\".", artist_name);
                      metadata_changed |=
                          update_string_record(&metadata_store.npi.artist_name, artist_name);
                      free(artist_name);
                    }

                    // look for composer name
                    plist_t composer_item =
                        plist_dict_get_item(metadata_store.npi.npi_plist, "kMRMediaRemoteNowPlayingInfoComposer");
                    if (composer_item != NULL) {
                      char *composer_name = NULL;
                      plist_get_string_val(composer_item, &composer_name);
                      // debug(1, "Send composer name: \"%s\".", composer_name);
                      metadata_changed |=
                          update_string_record(&metadata_store.npi.composer, composer_name);
                      free(composer_name);
                    }

                    // look for duration
                    plist_t duration_item =
                        plist_dict_get_item(metadata_store.npi.npi_plist, "kMRMediaRemoteNowPlayingInfoDuration");
                    if (duration_item != NULL) {
                      double duration;
                      plist_get_real_val(duration_item, &duration);
                      // debug(1, "Send duration: %f", duration);
                      duration = duration * 1000000.0; // convert to microseconds
                      metadata_changed |= update_uint64_record(
                          &metadata_store.npi.songtime_in_microseconds, (uint64_t)(duration));
                    }
                  }
                  
                  metadata_hub_modify_epilog(metadata_changed);
                }
                free(command_params_type_string);
              }
            }
          }
        } else if (strcmp(command_type_string, "updateMRSupportedCommands") == 0) {
          plist_t item = plist_dict_get_item(command_dict, "params");
          if (item != NULL) {
            // the item should be a dict
            plist_t item_array = plist_dict_get_item(item, "mrSupportedCommandsFromSender");
            if (item_array != NULL) {
            
              int metadata_changed = 1; // we can't easily tell if the metadata is changing, unfortunately.
                  // debug(1, "updateMRNowPlayingInfo");
              metadata_hub_modify_prolog();
              if (metadata_store.supported_commands_plist != NULL) {
                plist_free(metadata_store.supported_commands_plist);
              }
              metadata_store.supported_commands_plist = plist_copy(item_array);
              // here we have an array of data items
              uint32_t items = plist_array_get_size(item_array);
              if (items != 0) {
                // debug(1, "%u commands found.", items);
                uint32_t item_number;
                for (item_number = 0; item_number < items; item_number++) {
                  plist_t the_item = plist_array_get_item(item_array, item_number);
                  char *buff = NULL;
                  uint64_t length = 0;
                  plist_get_data_val(the_item, &buff, &length);
                  debug(4, "Item %d, length: %" PRId64 " bytes", item_number, length);
                  if ((buff != NULL) && (length >= strlen("bplist00")) &&
                      (strstr(buff, "bplist00") == buff)) {
                    // debug(1,"Contains a plist.");
                    plist_t subsidiary_plist = NULL;
                    plist_from_memory(buff, length, &subsidiary_plist);
                    if (subsidiary_plist) {
                      // for Repeat Mode and Shuffle Modes, look for a kCommandInfoOptionsKey dict.
                      plist_t commandInfoOptionsDict =
                          plist_dict_get_item(subsidiary_plist, "kCommandInfoOptionsKey");
                      if (commandInfoOptionsDict != NULL) {

                        // look for repeat mode
                        plist_t mode_item = plist_dict_get_item(
                            commandInfoOptionsDict, "kMRMediaRemoteCommandInfoRepeatMode");
                        if (mode_item != NULL) {
                          uint64_t repeat_mode = 0;
                          plist_get_uint_val(mode_item, &repeat_mode);
                          // debug(1, "repeat mode is %" PRIu64 ".", repeat_mode);
                          switch (repeat_mode) {
                          case 1:
                            metadata_store.repeat_status = RS_OFF;
                            break;
                          case 2:
                            metadata_store.repeat_status = RS_ONE;
                            break;
                          case 3:
                            metadata_store.repeat_status = RS_ALL;
                            break;
                          default:
                            debug(1, "unrecognised repeat mode: %" PRIu64 ".", repeat_mode);
                          }
                        }

                        // look for shuffle mode
                        mode_item = plist_dict_get_item(commandInfoOptionsDict,
                                                        "kMRMediaRemoteCommandInfoShuffleMode");
                        if (mode_item != NULL) {
                          uint64_t shuffle_mode = 0;
                          plist_get_uint_val(mode_item, &shuffle_mode);
                          // debug(1, "shuffle mode is %" PRIu64 ".", shuffle_mode);
                          switch (shuffle_mode) {
                          case 1:
                            metadata_store.shuffle_status = SS_OFF;
                            break;
                          // we don't know what the difference is here...
                          case 2:
                          case 3:
                            metadata_store.shuffle_status = SS_ON;
                            break;
                          default:
                            debug(1, "unrecognised shuffle mode: %" PRIu64 ".", shuffle_mode);
                          }
                        }

                        char *printable_plist = plist_as_xml_text(commandInfoOptionsDict);
                        if (printable_plist) {
                          debug(2, "\n--\n%s\n--", printable_plist);
                          free(printable_plist);
                        } else {
                          debug(1, "Can't print the plist!");
                        }
                      }
                      plist_free(subsidiary_plist);
                    } else {
                      debug(1, "Can't access the plist!");
                    }
                  }
                  if (buff != NULL)
                    free(buff);
                }
              }
              metadata_hub_modify_epilog(metadata_changed);
            } else {
              debug(1, "POST /command updateMRSupportedCommands has no "
                       "mrSupportedCommandsFromSender item.");
            }
          } else {
            debug(1, "POST /command updateMRSupportedCommands has no params dict.");
          }
        } else if (strcmp(command_type_string, "updateMRPlaybackState") == 0) {
          plist_t item = plist_dict_get_item(command_dict, "params");
          if (item != NULL) {
            // the item should be a dict
            plist_t playback_state_item = plist_dict_get_item(item, "mrPlaybackState");
            if (playback_state_item != NULL) {
              uint64_t playback_state = 0;
              plist_get_uint_val(playback_state_item, &playback_state);
              switch (playback_state) {
              case 1:
                metadata_store.play_status = PS_PLAYING;
                break;
              case 2:
                metadata_store.play_status = PS_PAUSED;
                break;
              case 3:
                metadata_store.play_status = PS_STOPPED;
                break;
              case 4:
                metadata_store.play_status = PS_NOT_AVAILABLE;
                break;
              default:
                debug(1, "unrecognised new play status: %" PRIu64 ".", playback_state);
              }
            } else {
              debug(1, "POST /command updateMRPlaybackState has no mrPlaybackState item.");
            }
          } else {
            debug(1, "POST /command updateMRPlaybackState has no params dict.");
          }
        }
        free(command_type_string);
      }
    }
  }
}