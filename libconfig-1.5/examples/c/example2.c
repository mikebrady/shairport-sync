/* ----------------------------------------------------------------------------
   libconfig - A library for processing structured configuration files
   Copyright (C) 2005-2010  Mark A Lindner

   This file is part of libconfig.

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public License
   as published by the Free Software Foundation; either version 2.1 of
   the License, or (at your option) any later version.

   This library is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Library General Public
   License along with this library; if not, see
   <http://www.gnu.org/licenses/>.
   ----------------------------------------------------------------------------
*/

#include <stdio.h>
#include <stdlib.h>
#include <libconfig.h>

/* This example reads the configuration file 'example.cfg', adds a new
 * movie record to the movies list, and writes the updated configuration to
 * 'updated.cfg'.
 */

int main(int argc, char **argv)
{
  static const char *output_file = "updated.cfg";
  config_t cfg;
  config_setting_t *root, *setting, *movie;

  config_init(&cfg);

  /* Read the file. If there is an error, report it and exit. */
  if(! config_read_file(&cfg, "example.cfg"))
  {
    fprintf(stderr, "%s:%d - %s\n", config_error_file(&cfg),
            config_error_line(&cfg), config_error_text(&cfg));
    config_destroy(&cfg);
    return(EXIT_FAILURE);
  }

  /* Find the 'movies' setting. Add intermediate settings if they don't yet
  * exist.
  */
  root = config_root_setting(&cfg);

  setting = config_setting_get_member(root, "inventory");
  if(!setting)
    setting = config_setting_add(root, "inventory", CONFIG_TYPE_GROUP);

  setting = config_setting_get_member(setting, "movies");
  if(!setting)
    setting = config_setting_add(setting, "movies", CONFIG_TYPE_LIST);

  /* Create the new movie entry. */
  movie = config_setting_add(setting, NULL, CONFIG_TYPE_GROUP);

  setting = config_setting_add(movie, "title", CONFIG_TYPE_STRING);
  config_setting_set_string(setting, "Buckaroo Banzai");

  setting = config_setting_add(movie, "media", CONFIG_TYPE_STRING);
  config_setting_set_string(setting, "DVD");

  setting = config_setting_add(movie, "price", CONFIG_TYPE_FLOAT);
  config_setting_set_float(setting, 12.99);

  setting = config_setting_add(movie, "qty", CONFIG_TYPE_INT);
  config_setting_set_float(setting, 20);

  config_set_options(&cfg, 0);

  /* Write out the updated configuration. */
  if(! config_write_file(&cfg, output_file))
  {
    fprintf(stderr, "Error while writing file.\n");
    config_destroy(&cfg);
    return(EXIT_FAILURE);
  }

  fprintf(stderr, "Updated configuration successfully written to: %s\n",
          output_file);

  config_destroy(&cfg);
  return(EXIT_SUCCESS);
}

/* eof */
