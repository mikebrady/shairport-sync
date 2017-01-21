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

/* This example constructs a new configuration in memory and writes it to
 * 'newconfig.cfg'.
 */

int main(int argc, char **argv)
{
  static const char *output_file = "newconfig.cfg";
  config_t cfg;
  config_setting_t *root, *setting, *group, *array;
  int i;

  config_init(&cfg);
  root = config_root_setting(&cfg);

  /* Add some settings to the configuration. */
  group = config_setting_add(root, "address", CONFIG_TYPE_GROUP);

  setting = config_setting_add(group, "street", CONFIG_TYPE_STRING);
  config_setting_set_string(setting, "1 Woz Way");

  setting = config_setting_add(group, "city", CONFIG_TYPE_STRING);
  config_setting_set_string(setting, "San Jose");

  setting = config_setting_add(group, "state", CONFIG_TYPE_STRING);
  config_setting_set_string(setting, "CA");

  setting = config_setting_add(group, "zip", CONFIG_TYPE_INT);
  config_setting_set_int(setting, 95110);

  array = config_setting_add(root, "numbers", CONFIG_TYPE_ARRAY);

  for(i = 0; i < 10; ++i)
  {
    setting = config_setting_add(array, NULL, CONFIG_TYPE_INT);
    config_setting_set_int(setting, 10 * i);
  }

  /* Write out the new configuration. */
  if(! config_write_file(&cfg, output_file))
  {
    fprintf(stderr, "Error while writing file.\n");
    config_destroy(&cfg);
    return(EXIT_FAILURE);
  }

  fprintf(stderr, "New configuration successfully written to: %s\n",
          output_file);

  config_destroy(&cfg);
  return(EXIT_SUCCESS);
}

/* eof */
