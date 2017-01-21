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

#include <iostream>
#include <iomanip>
#include <cstdlib>
#include <libconfig.h++>

using namespace std;
using namespace libconfig;

// This example constructs a new configuration in memory and writes it to
// 'newconfig.cfg'.

int main(int argc, char **argv)
{
  static const char *output_file = "newconfig.cfg";
  Config cfg;

  Setting &root = cfg.getRoot();

  // Add some settings to the configuration.
  Setting &address = root.add("address", Setting::TypeGroup);

  address.add("street", Setting::TypeString) = "1 Woz Way";
  address.add("city", Setting::TypeString) = "San Jose";
  address.add("state", Setting::TypeString) = "CA";
  address.add("zip", Setting::TypeInt) = 95110;

  Setting &array = root.add("array", Setting::TypeArray);

  for(int i = 0; i < 10; ++i)
    array.add(Setting::TypeInt) = 10 * i;

  // Write out the new configuration.
  try
  {
    cfg.writeFile(output_file);
    cerr << "New configuration successfully written to: " << output_file
         << endl;

  }
  catch(const FileIOException &fioex)
  {
    cerr << "I/O error while writing file: " << output_file << endl;
    return(EXIT_FAILURE);
  }

  return(EXIT_SUCCESS);
}

// eof
