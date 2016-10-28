/* ----------------------------------------------------------------------------
   libconfig - A library for processing structured configuration files
   Copyright (C) 2005-2014  Mark A Lindner

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

#ifndef __libconfig_hpp
#define __libconfig_hpp

#include <stdio.h>
#include <exception>
#include <string>

#if defined(_WIN32) || defined(__WIN32__) || defined(WIN32)
#if defined(LIBCONFIGXX_STATIC)
#define LIBCONFIGXX_API
#elif defined(LIBCONFIGXX_EXPORTS)
#define LIBCONFIGXX_API __declspec(dllexport)
#else /* ! LIBCONFIGXX_EXPORTS */
#define LIBCONFIGXX_API __declspec(dllimport)
#endif /* LIBCONFIGXX_STATIC */
#else /* ! WIN32 */
#define LIBCONFIGXX_API
#endif /* WIN32 */

#define LIBCONFIGXX_VER_MAJOR    1
#define LIBCONFIGXX_VER_MINOR    5
#define LIBCONFIGXX_VER_REVISION 0

struct config_t; // fwd decl
struct config_setting_t; // fwd decl

namespace libconfig {

class LIBCONFIGXX_API ConfigException : public std::exception { };

class Setting; // fwd decl
class SettingIterator;
class SettingConstIterator;

class LIBCONFIGXX_API SettingException : public ConfigException
{
  public:

  SettingException(const Setting &setting);
  SettingException(const Setting &setting, int idx);
  SettingException(const Setting &setting, const char *name);
  SettingException(const char *path);

  SettingException(const SettingException &other);
  SettingException& operator=(const SettingException &other);

  virtual ~SettingException() throw();

  const char *getPath() const;

  virtual const char *what() const throw();

  private:

  char *_path;
};

class LIBCONFIGXX_API SettingTypeException : public SettingException
{
  public:

  SettingTypeException(const Setting &setting);
  SettingTypeException(const Setting &setting, int idx);
  SettingTypeException(const Setting &setting, const char *name);

  virtual const char *what() const throw();
};

class LIBCONFIGXX_API SettingNotFoundException : public SettingException
{
  public:

  SettingNotFoundException(const char *path);
  SettingNotFoundException(const Setting &setting, int idx);
  SettingNotFoundException(const Setting &setting, const char *name);

  virtual const char *what() const throw();
};

class LIBCONFIGXX_API SettingNameException : public SettingException
{
  public:

  SettingNameException(const Setting &setting, const char *name);

  virtual const char *what() const throw();
};

class LIBCONFIGXX_API FileIOException : public ConfigException
{
  public:

  virtual const char *what() const throw();
};

class LIBCONFIGXX_API ParseException : public ConfigException
{
  public:

  ParseException(const char *file, int line, const char *error);

  ParseException(const ParseException &other);

  virtual ~ParseException() throw();

  inline const char *getFile() const
  { return(_file); }

  inline int getLine() const
  { return(_line); }

  inline const char *getError() const
  { return(_error); }

  virtual const char *what() const throw();

  private:

  const char *_file;
  int _line;
  const char *_error;
};

class LIBCONFIGXX_API Setting
{
  friend class Config;

  public:

  enum Type
  {
    TypeNone = 0,
    // scalar types
    TypeInt,
    TypeInt64,
    TypeFloat,
    TypeString,
    TypeBoolean,
    // aggregate types
    TypeGroup,
    TypeArray,
    TypeList
  };

  enum Format
  {
    FormatDefault = 0,
    FormatHex = 1
  };

  enum Option
  {
    OptionNone = 0,
    OptionAutoConvert = 0x01,
    OptionSemicolonSeparators = 0x02,
    OptionColonAssignmentForGroups = 0x04,
    OptionColonAssignmentForNonGroups = 0x08,
    OptionOpenBraceOnSeparateLine = 0x10
  };

  typedef SettingIterator iterator;
  typedef SettingConstIterator const_iterator;

  public:

  virtual ~Setting();

  inline Type getType() const { return(_type); }

  inline Format getFormat() const { return(_format); }
  void setFormat(Format format);

  operator bool() const;
  operator int() const;
  operator unsigned int() const;
  operator long() const;
  operator unsigned long() const;
  operator long long() const;
  operator unsigned long long() const;
  operator double() const;
  operator float() const;
  operator const char *() const;
  operator std::string() const;

  inline const char *c_str() const
  { return operator const char *(); }

  Setting & operator=(bool value);
  Setting & operator=(int value);
  Setting & operator=(long value);
  Setting & operator=(const long long &value);
  Setting & operator=(const double &value);
  Setting & operator=(float value);
  Setting & operator=(const char *value);
  Setting & operator=(const std::string &value);

  Setting & lookup(const char *path) const;
  inline Setting & lookup(const std::string &path) const
  { return(lookup(path.c_str())); }

  Setting & operator[](const char *name) const;
  Setting & operator[](int index) const;

  bool lookupValue(const char *name, bool &value) const;
  bool lookupValue(const char *name, int &value) const;
  bool lookupValue(const char *name, unsigned int &value) const;
  bool lookupValue(const char *name, long long &value) const;
  bool lookupValue(const char *name, unsigned long long &value) const;
  bool lookupValue(const char *name, double &value) const;
  bool lookupValue(const char *name, float &value) const;
  bool lookupValue(const char *name, const char *&value) const;
  bool lookupValue(const char *name, std::string &value) const;

  inline bool lookupValue(const std::string &name, bool &value) const
  { return(lookupValue(name.c_str(), value)); }

  inline bool lookupValue(const std::string &name, int &value) const
  { return(lookupValue(name.c_str(), value)); }

  inline bool lookupValue(const std::string &name, unsigned int &value) const
  { return(lookupValue(name.c_str(), value)); }

  inline bool lookupValue(const std::string &name, long long &value) const
  { return(lookupValue(name.c_str(), value)); }

  inline bool lookupValue(const std::string &name,
                          unsigned long long &value) const
  { return(lookupValue(name.c_str(), value)); }

  inline bool lookupValue(const std::string &name, double &value) const
  { return(lookupValue(name.c_str(), value)); }

  inline bool lookupValue(const std::string &name, float &value) const
  { return(lookupValue(name.c_str(), value)); }

  inline bool lookupValue(const std::string &name, const char *&value) const
  { return(lookupValue(name.c_str(), value)); }

  inline bool lookupValue(const std::string &name, std::string &value) const
  { return(lookupValue(name.c_str(), value)); }

  void remove(const char *name);

  inline void remove(const std::string &name)
  { remove(name.c_str()); }

  void remove(unsigned int idx);

  Setting & add(const char *name, Type type);

  inline Setting & add(const std::string &name, Type type)
  { return(add(name.c_str(), type)); }

  Setting & add(Type type);

  bool exists(const char *name) const;

  inline bool exists(const std::string &name) const
  { return(exists(name.c_str())); }

  int getLength() const;
  const char *getName() const;
  std::string getPath() const;
  int getIndex() const;

  const Setting & getParent() const;
  Setting & getParent();

  bool isRoot() const;

  inline bool isGroup() const
  { return(_type == TypeGroup); }

  inline bool isArray() const
  { return(_type == TypeArray); }

  inline bool isList() const
  { return(_type == TypeList); }

  inline bool isAggregate() const
  { return(_type >= TypeGroup); }

  inline bool isScalar() const
  { return((_type > TypeNone) && (_type < TypeGroup)); }

  inline bool isNumber() const
  {
    return((_type == TypeInt) || (_type == TypeInt64) || (_type == TypeFloat));
  }

  unsigned int getSourceLine() const;
  const char *getSourceFile() const;

  iterator begin();
  iterator end();

  const_iterator begin() const;
  const_iterator end() const;

  private:

  config_setting_t *_setting;
  Type _type;
  Format _format;

  Setting(config_setting_t *setting);

  void assertType(Type type) const;
  static Setting & wrapSetting(config_setting_t *setting);

  Setting(const Setting& other); // not supported
  Setting& operator=(const Setting& other); // not supported
};


class LIBCONFIGXX_API SettingIterator
{
  public:

  SettingIterator(Setting &setting, bool endIterator = false);
  SettingIterator(const SettingIterator &other);
  SettingIterator& operator=(const SettingIterator &other);

  // Equality comparison.
  inline bool operator==(SettingIterator const &other) const
  { return((_setting == other._setting) && (_idx == other._idx)); }

  inline bool operator!=(SettingIterator const &other) const
  { return(!operator==(other)); }

  bool operator<(SettingIterator const &other) const;

  // Dereference operators.
  inline Setting & operator*()
  { return((*_setting)[_idx]); }

  inline Setting * operator->()
  { return(&(*_setting)[_idx]); }

  inline const Setting & operator*() const
  { return(*_setting)[_idx]; }
  inline const Setting * operator->() const
  { return(&(*_setting)[_idx]); }

  // Increment and decrement operators.
  SettingIterator & operator++();
  SettingIterator operator++(int);

  SettingIterator & operator--();
  SettingIterator operator--(int);

  // Arithmetic operators.
  SettingIterator operator+(int offset) const;
  SettingIterator & operator+=(int offset);

  SettingIterator operator-(int offset) const;
  SettingIterator & operator-=(int offset);

  int operator-(const SettingIterator &other) const;

  private:

  Setting *_setting;

  int _count;
  int _idx;
};

SettingIterator operator+(int offset, const SettingIterator &si);

class LIBCONFIGXX_API SettingConstIterator
{
  public:

  SettingConstIterator(const Setting &setting, bool endIterator = false);
  SettingConstIterator(const SettingConstIterator &rhs);
  SettingConstIterator& operator=(const SettingConstIterator &rhs);

  // Equality comparison.
  bool operator==(SettingConstIterator const &other) const
  { return((_setting == other._setting) && (_idx == other._idx)); }

  inline bool operator!=(SettingConstIterator const &other) const
  { return(!operator==(other)); }

  // Dereference operators.
  inline Setting const & operator*()
  { return((*_setting)[_idx]); }
  inline Setting const * operator->()
  { return(&(*_setting)[_idx]); }

  inline const Setting& operator*() const
  { return((*_setting)[_idx]); }
  inline const Setting * operator->() const
  { return(&(*_setting)[_idx]); }

  // Increment and decrement operators.
  SettingConstIterator & operator++();
  SettingConstIterator operator++(int);

  SettingConstIterator & operator--();
  SettingConstIterator operator--(int);

  // Arithmetic operators.
  SettingConstIterator operator+(int offset) const;
  SettingConstIterator & operator+=(int offset);

  SettingConstIterator operator-(int offset) const;
  SettingConstIterator & operator-=(int offset);

  int operator-(const SettingConstIterator &other) const;

  private:

  const Setting *_setting;

  int _count;
  int _idx;
};

SettingConstIterator operator+(int offset, const SettingConstIterator &si);

class LIBCONFIGXX_API Config
{
  public:

  Config();
  virtual ~Config();

  void setOptions(int options);
  int getOptions() const;

  void setAutoConvert(bool flag);
  bool getAutoConvert() const;

  void setDefaultFormat(Setting::Format format);
  inline Setting::Format getDefaultFormat() const
  { return(_defaultFormat); }

  void setTabWidth(unsigned short width);
  unsigned short getTabWidth() const;

  void setIncludeDir(const char *includeDir);
  const char *getIncludeDir() const;

  void read(FILE *stream);
  void write(FILE *stream) const;

  void readString(const char *str);
  inline void readString(const std::string &str)
  { return(readString(str.c_str())); }

  void readFile(const char *filename);
  void writeFile(const char *filename);

  Setting & lookup(const char *path) const;
  inline Setting & lookup(const std::string &path) const
  { return(lookup(path.c_str())); }

  bool exists(const char *path) const;
  inline bool exists(const std::string &path) const
  { return(exists(path.c_str())); }

  bool lookupValue(const char *path, bool &value) const;
  bool lookupValue(const char *path, int &value) const;
  bool lookupValue(const char *path, unsigned int &value) const;
  bool lookupValue(const char *path, long long &value) const;
  bool lookupValue(const char *path, unsigned long long &value) const;
  bool lookupValue(const char *path, double &value) const;
  bool lookupValue(const char *path, float &value) const;
  bool lookupValue(const char *path, const char *&value) const;
  bool lookupValue(const char *path, std::string &value) const;

  inline bool lookupValue(const std::string &path, bool &value) const
  { return(lookupValue(path.c_str(), value)); }

  inline bool lookupValue(const std::string &path, int &value) const
  { return(lookupValue(path.c_str(), value)); }

  inline bool lookupValue(const std::string &path, unsigned int &value) const
  { return(lookupValue(path.c_str(), value)); }

  inline bool lookupValue(const std::string &path, long long &value) const
  { return(lookupValue(path.c_str(), value)); }

  inline bool lookupValue(const std::string &path,
                          unsigned long long &value) const
  { return(lookupValue(path.c_str(), value)); }

  inline bool lookupValue(const std::string &path, double &value) const
  { return(lookupValue(path.c_str(), value)); }

  inline bool lookupValue(const std::string &path, float &value) const
  { return(lookupValue(path.c_str(), value)); }

  inline bool lookupValue(const std::string &path, const char *&value) const
  { return(lookupValue(path.c_str(), value)); }

  inline bool lookupValue(const std::string &path, std::string &value) const
  { return(lookupValue(path.c_str(), value)); }

  Setting & getRoot() const;

  private:

  static void ConfigDestructor(void *arg);
  void handleError() const;

  config_t *_config;
  Setting::Format _defaultFormat;

  Config(const Config& other); // not supported
  Config& operator=(const Config& other); // not supported
};

} // namespace libconfig

#endif // __libconfig_hpp
