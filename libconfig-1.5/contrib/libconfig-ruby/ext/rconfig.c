#include <ruby.h>
#include <libconfig.h>

static VALUE cConfig, cConfigBaseSetting, cConfigSetting, cConfigAggregate;
static VALUE cConfigFormatDefault, cConfigFormatHex;
static VALUE cConfigFixnum, cConfigBignum, cConfigFloat, cConfigBoolean, cConfigString;
static VALUE cConfigGroup, cConfigList, cConfigArray;

static VALUE rSettingNameRegexp;
static VALUE aConfigSettings, aConfigScalars, aConfigAggregates;
static VALUE eConfigParseError, eSettingNotFoundError, eSettingFormatError, eSettingNameError;

static VALUE rconfig_wrap_value(config_setting_t* setting)
{
  switch(config_setting_type(setting)) {
    case CONFIG_TYPE_INT:
      return LONG2FIX(config_setting_get_int(setting));
    
    case CONFIG_TYPE_INT64:
      return rb_ll2inum(config_setting_get_int64(setting));
    
    case CONFIG_TYPE_FLOAT:
      return rb_float_new(config_setting_get_float(setting));
    
    case CONFIG_TYPE_STRING:
      return rb_str_new2(config_setting_get_string(setting));
    
    case CONFIG_TYPE_BOOL:
      return config_setting_get_bool(setting) ? Qtrue : Qfalse;
    
    default:
      rb_bug("unknown value type %d", config_setting_type(setting));
  }
}

static void rconfig_free_setting(config_setting_t* setting)
{
  // dummy
}

static VALUE rconfig_prepare_setting(config_setting_t* setting)
{
  VALUE wrapper = Data_Wrap_Struct(rb_cObject, 0, rconfig_free_setting, setting);
  config_setting_set_hook(setting, (void*) wrapper);
  return wrapper;
}

static void rconfig_destroy_setting(void* hook)
{
  if(hook != NULL) {
    VALUE wrapper = (VALUE) hook;
    rb_iv_set(wrapper, "@setting", Qnil);
  }
}

static VALUE rconfig_wrap_setting(config_setting_t* setting)
{
  VALUE rbSetting = rconfig_prepare_setting(setting);
  
  switch(config_setting_type(setting)) {
    case CONFIG_TYPE_INT:
      return rb_funcall(cConfigFixnum, rb_intern("new"), 2, LONG2FIX(config_setting_get_int(setting)), rbSetting);
    
    case CONFIG_TYPE_INT64:
      return rb_funcall(cConfigBignum, rb_intern("new"), 2, rb_ll2inum(config_setting_get_int64(setting)), rbSetting);
    
    case CONFIG_TYPE_FLOAT:
      return rb_funcall(cConfigFloat, rb_intern("new"), 2, rb_float_new(config_setting_get_float(setting)), rbSetting);
    
    case CONFIG_TYPE_STRING:
      return rb_funcall(cConfigString, rb_intern("new"), 2, rb_str_new2(config_setting_get_string(setting)), rbSetting);
    
    case CONFIG_TYPE_BOOL:
      return rb_funcall(cConfigBoolean, rb_intern("new"), 2, config_setting_get_bool(setting) ? Qtrue : Qfalse, rbSetting);
    
    case CONFIG_TYPE_ARRAY:
      return rb_funcall(cConfigArray, rb_intern("new"), 2, Qnil, rbSetting);
    
    case CONFIG_TYPE_LIST:
      return rb_funcall(cConfigList, rb_intern("new"), 1, rbSetting);
    
    case CONFIG_TYPE_GROUP:
      return rb_funcall(cConfigGroup, rb_intern("new"), 1, rbSetting);
    
    default:
      rb_bug("[r] unknown setting type %d", config_setting_type(setting));
  }
}

static void rconfig_update_setting(config_setting_t* setting, VALUE value)
{
  switch(config_setting_type(setting)) {
    case CONFIG_TYPE_INT:
      config_setting_set_int(setting, FIX2LONG(value));
      break;
    
    case CONFIG_TYPE_INT64:
      if(TYPE(value) == T_BIGNUM)
        config_setting_set_int64(setting, rb_big2ll(value));
      else // T_FIXNUM
        config_setting_set_int64(setting, FIX2INT(value));
      break;
    
    case CONFIG_TYPE_FLOAT:
// ruby1.9 check
#if HAVE_RB_BLOCK_CALL
      config_setting_set_float(setting, RFLOAT(value)->float_value);
#else
      config_setting_set_float(setting, RFLOAT(value)->value);
#endif
      break;
    
    case CONFIG_TYPE_STRING:
      config_setting_set_string(setting, RSTRING_PTR(value));
      break;
    
    case CONFIG_TYPE_BOOL:
      config_setting_set_bool(setting, value == Qtrue);
      break;
    
    default:
      rb_bug("[w] unknown setting type %d", config_setting_type(setting));
  }
}

static void rconfig_check_setting_type(VALUE object, VALUE value)
{
  if(rb_obj_class(object) == cConfigFixnum) {
    Check_Type(value, T_FIXNUM);
  } else if(rb_obj_class(object) == cConfigBignum) {
    if(TYPE(value) != T_BIGNUM && TYPE(value) != T_FIXNUM)
      rb_raise(rb_eTypeError, "wrong argument type %s (expected Fixnum or Bignum)", rb_obj_classname(value));
  } else if(rb_obj_class(object) == cConfigFloat) {
    Check_Type(value, T_FLOAT);
  } else if(rb_obj_class(object) == cConfigString) {
    Check_Type(value, T_STRING);
  } else if(rb_obj_class(object) == cConfigBoolean) {
    if(value != Qtrue && value != Qfalse)
      rb_raise(rb_eTypeError, "wrong argument type %s (expected boolean)", rb_obj_classname(value));
  } else {
    rb_raise(rb_eException, "never use Config::Setting itself");
  }
}

static int rconfig_do_append(config_setting_t* setting, VALUE target, VALUE name)
{
  int type;
  if(rb_obj_class(target) == cConfigFixnum)
    type = CONFIG_TYPE_INT;
  else if(rb_obj_class(target) == cConfigBignum)
    type = CONFIG_TYPE_INT64;
  else if(rb_obj_class(target) == cConfigFloat)
    type = CONFIG_TYPE_FLOAT;
  else if(rb_obj_class(target) == cConfigString)
    type = CONFIG_TYPE_STRING;
  else if(rb_obj_class(target) == cConfigBoolean)
    type = CONFIG_TYPE_BOOL;
  else if(rb_obj_class(target) == cConfigGroup)
    type = CONFIG_TYPE_GROUP;
  else if(rb_obj_class(target) == cConfigList)
    type = CONFIG_TYPE_LIST;
  else if(rb_obj_class(target) == cConfigArray)
    type = CONFIG_TYPE_ARRAY;
  else
    rb_bug("unknown setting class %s", rb_obj_classname(target));
  
  config_setting_t* new_setting;
  if(name == Qnil) {
    new_setting = config_setting_add(setting, NULL, type);
  } else {
    Check_Type(name, T_STRING);
    new_setting = config_setting_add(setting, RSTRING_PTR(name), type);
  }
  
  if(new_setting == NULL)
    return 0;
  
  VALUE rbNewSetting = rconfig_prepare_setting(new_setting);
  rb_iv_set(target, "@setting", rbNewSetting);
  
  if(rb_ary_includes(aConfigScalars, rb_obj_class(target)) == Qtrue)
    rconfig_update_setting(new_setting, rb_iv_get(target, "@value"));

  if(rb_ary_includes(aConfigAggregates, rb_obj_class(target)) == Qtrue) {
    if(rb_obj_class(target) == cConfigGroup) {
      VALUE hash = rb_iv_get(target, "@hash");
      VALUE children = rb_funcall(hash, rb_intern("keys"), 0);
      int i;
      for(i = 0; i < RARRAY_LEN(children); i++) {
        VALUE key = RARRAY_PTR(children)[i];
        rconfig_do_append(new_setting, rb_hash_aref(hash, key), key);
      }
    } else {
      VALUE children = rb_iv_get(target, "@list");
      int i;
      for(i = 0; i < RARRAY_LEN(children); i++) {
        rconfig_do_append(new_setting, RARRAY_PTR(children)[i], Qnil);
      }
    }
  }
  
  return 1;
}

static VALUE rbConfigBaseSetting_initialize(VALUE self, VALUE setting)
{
  if(setting != Qnil)
    Check_Type(setting, T_DATA);
  rb_iv_set(self, "@setting", setting);
  
  return self;
}

static VALUE rbConfigBaseSetting_name(VALUE self)
{
  if(rb_iv_get(self, "@setting") != Qnil) {
    config_setting_t* setting = NULL;
    Data_Get_Struct(rb_iv_get(self, "@setting"), config_setting_t, setting);
    return rb_str_new2(config_setting_name(setting));
  } else {
    return Qnil;
  }
}

static VALUE rbConfigBaseSetting_parent(VALUE self)
{
  if(rb_iv_get(self, "@setting") != Qnil) {
    config_setting_t* setting = NULL;
    Data_Get_Struct(rb_iv_get(self, "@setting"), config_setting_t, setting);
    return rconfig_wrap_setting(config_setting_parent(setting));
  } else {
    return Qnil;
  }
}

static VALUE rbConfigBaseSetting_is_root(VALUE self)
{
  if(rb_iv_get(self, "@setting") != Qnil) {
    config_setting_t* setting = NULL;
    Data_Get_Struct(rb_iv_get(self, "@setting"), config_setting_t, setting);
    return config_setting_is_root(setting) ? Qtrue : Qfalse;
  } else {
    return Qnil;
  }
}

static VALUE rbConfigBaseSetting_index(VALUE self)
{
  if(rb_iv_get(self, "@setting") != Qnil) {
    config_setting_t* setting = NULL;
    Data_Get_Struct(rb_iv_get(self, "@setting"), config_setting_t, setting);
    return INT2FIX(config_setting_index(setting));
  } else {
    return Qnil;
  }
}

static VALUE rbConfigBaseSetting_line(VALUE self)
{
  if(rb_iv_get(self, "@setting") != Qnil) {
    config_setting_t* setting = NULL;
    Data_Get_Struct(rb_iv_get(self, "@setting"), config_setting_t, setting);
    return INT2FIX(config_setting_source_line(setting));
  } else {
    return Qnil;
  }
}

static VALUE rbConfigSetting_initialize(int argc, VALUE* argv, VALUE self)
{
  VALUE value, setting;
  rb_scan_args(argc, argv, "11", &value, &setting);

  rb_call_super(1, &setting);

  rconfig_check_setting_type(self, value);
  rb_iv_set(self, "@value", value);

  if(rb_iv_get(self, "@setting") != Qnil) {
    config_setting_t* c_setting = NULL;
    Data_Get_Struct(rb_iv_get(self, "@setting"), config_setting_t, c_setting);
    rb_iv_set(self, "@format", INT2FIX(config_setting_get_format(c_setting)));
  } else {
    rb_iv_set(self, "@format", cConfigFormatDefault);
  }
  
  return self;
}

static VALUE rbConfigSetting_get_value(VALUE self)
{
  if(rb_iv_get(self, "@setting") != Qnil) {
    config_setting_t* setting;
    Data_Get_Struct(rb_iv_get(self, "@setting"), config_setting_t, setting);
    return rconfig_wrap_value(setting);
  } else {
    return rb_iv_get(self, "@value");
  }
}

static VALUE rbConfigSetting_set_value(VALUE self, VALUE new_value)
{
  rconfig_check_setting_type(self, new_value);

  if(rb_iv_get(self, "@setting") != Qnil) {
    config_setting_t* setting;
    Data_Get_Struct(rb_iv_get(self, "@setting"), config_setting_t, setting);
    rconfig_update_setting(setting, new_value);
  }

  rb_iv_set(self, "@value", new_value);
  
  return new_value;
}

static VALUE rbConfigSetting_get_format(VALUE self)
{
  if(rb_iv_get(self, "@setting") != Qnil) {
    config_setting_t* setting;
    Data_Get_Struct(rb_iv_get(self, "@setting"), config_setting_t, setting);
    return INT2FIX(config_setting_get_format(setting));
  } else {
    return rb_iv_get(self, "format");
  }
}

static VALUE rbConfigSetting_set_format(VALUE self, VALUE new_format)
{
  if(rb_iv_get(self, "@setting") != Qnil) {
    config_setting_t* setting;
    Data_Get_Struct(rb_iv_get(self, "@setting"), config_setting_t, setting);
    if(!config_setting_set_format(setting, FIX2INT(new_format)))
      rb_raise(eSettingFormatError, "invalid setting format %d", FIX2INT(new_format));
  }

  rb_iv_set(self, "@format", new_format);
  
  return new_format;
}

static VALUE rbConfigAggregate_get(VALUE self, VALUE index);

static VALUE rbConfigAggregate_initialize(int argc, VALUE* argv, VALUE self)
{
  VALUE setting = Qnil;
  if(rb_obj_class(self) == cConfigGroup || rb_obj_class(self) == cConfigList) {
    rb_scan_args(argc, argv, "01", &setting);
  } else if(rb_obj_class(self) == cConfigArray) {
    VALUE type = Qnil;
    rb_scan_args(argc, argv, "02", &type, &setting);
    
    if(type != Qnil && rb_ary_includes(aConfigScalars, type) != Qtrue)
      rb_raise(rb_eTypeError, "invalid setting array type %s", rb_class2name(type));
    
    rb_iv_set(self, "@type", type);
  } else {
    rb_raise(rb_eException, "never create Config::Aggregate itself");
  }

  rb_call_super(1, &setting);

  rb_iv_set(self, "@list", rb_ary_new());
  if(rb_obj_class(self) == cConfigGroup)
    rb_iv_set(self, "@hash", rb_hash_new());
  
  if(setting != Qnil && rb_obj_class(self) == cConfigArray) {
    config_setting_t* c_setting;
    Data_Get_Struct(setting, config_setting_t, c_setting);
    if(config_setting_length(c_setting) > 0)
      rb_iv_set(self, "@type", rb_obj_class(rbConfigAggregate_get(self, INT2FIX(0))));
  }

  return self;
}

static VALUE rbConfigAggregate_size(VALUE self)
{
  config_setting_t* setting = NULL;
  if(rb_iv_get(self, "@setting") != Qnil)
    Data_Get_Struct(rb_iv_get(self, "@setting"), config_setting_t, setting);
  
  if(setting)
    return INT2FIX(config_setting_length(setting));
  else
    return INT2FIX(RARRAY_LEN(rb_iv_get(self, "@list")));
}

static VALUE rbConfigAggregate_get(VALUE self, VALUE index)
{
  config_setting_t* setting = NULL;
  if(rb_iv_get(self, "@setting") != Qnil)
    Data_Get_Struct(rb_iv_get(self, "@setting"), config_setting_t, setting);
  
  VALUE rbTarget = Qnil;
  
  if(TYPE(index) == T_STRING && rb_obj_class(self) == cConfigGroup) {
    if(setting) {
      config_setting_t* target = config_setting_get_member(setting, RSTRING_PTR(index));
      if(target)
        rbTarget = rconfig_wrap_setting(target);
    } else {
      rbTarget = rb_hash_aref(rb_iv_get(self, "@hash"), index);
    }
  } else if(TYPE(index) == T_FIXNUM) {
    if(setting) {
      config_setting_t* target = config_setting_get_elem(setting, FIX2INT(index));
      if(target)
        rbTarget = rconfig_wrap_setting(target);
    } else {
      rbTarget = rb_ary_entry(rb_iv_get(self, "@list"), FIX2INT(index));
    }
  } else {
    rb_raise(rb_eTypeError, "wrong argument type %s (expected String or Fixnum)", rb_obj_classname(index));
  }
  
  if(rbTarget == Qnil)
    if(TYPE(index) == T_STRING)
      rb_raise(eSettingNotFoundError, "setting `%s' not found", RSTRING_PTR(index));
    else
      rb_raise(eSettingNotFoundError, "setting [%d] not found", FIX2INT(index));

  return rbTarget;
}

static VALUE rbConfigAggregate_append(VALUE self, VALUE target)
{
  config_setting_t* setting = NULL;
  if(rb_iv_get(self, "@setting") != Qnil)
    Data_Get_Struct(rb_iv_get(self, "@setting"), config_setting_t, setting);

  Check_Type(target, T_OBJECT);

  VALUE type = rb_iv_get(self, "@type");
  if(rb_obj_class(self) == cConfigArray) {
    if(type != Qnil && type != rb_obj_class(target))
      rb_raise(rb_eTypeError, "wrong argument type %s (expected %s)", rb_obj_classname(target), rb_class2name(type));
    if(type == Qnil && rb_ary_includes(aConfigScalars, rb_obj_class(target)) != Qtrue)
      rb_raise(rb_eTypeError, "invalid setting array type %s", rb_obj_classname(target));
  }

  if(rb_ary_includes(aConfigSettings, rb_obj_class(target)) == Qtrue) {
    if(setting)
      rconfig_do_append(setting, target, Qnil);
    rb_ary_push(rb_iv_get(self, "@list"), target);
  } else {
    rb_raise(rb_eTypeError, "wrong argument type %s (expected Config::BaseSetting)", rb_obj_classname(target));
  }
  
  if(rb_obj_class(self) == cConfigArray && type == Qnil)
    rb_iv_set(self, "@type", rb_obj_class(target));
  
  return target;
}

static VALUE rbConfigGroup_append(VALUE self, VALUE name, VALUE target)
{
  Check_Type(name, T_STRING);
  Check_Type(target, T_OBJECT);
  
  config_setting_t* setting = NULL;
  if(rb_iv_get(self, "@setting") != Qnil)
    Data_Get_Struct(rb_iv_get(self, "@setting"), config_setting_t, setting);
  
  if(rb_ary_includes(aConfigSettings, rb_obj_class(target)) == Qtrue) {
    if(rb_reg_match(rSettingNameRegexp, name) == Qnil)
      rb_raise(eSettingNameError, "setting name `%s' contains invalid characters", RSTRING_PTR(name));
    if(setting) {
      if(!rconfig_do_append(setting, target, name))
        rb_raise(eSettingNameError, "setting `%s' already exists", RSTRING_PTR(name));
    } else if(rb_hash_aref(rb_iv_get(self, "@hash"), name) != Qnil) {
      rb_raise(eSettingNameError, "setting `%s' already exists", RSTRING_PTR(name));
    }
    rb_ary_push(rb_iv_get(self, "@list"), target);
    rb_hash_aset(rb_iv_get(self, "@hash"), name, target);
  } else {
    rb_raise(rb_eTypeError, "wrong argument type %s (expected Config::BaseSetting)", rb_obj_classname(target));
  }
  
  return target;
}

static VALUE rbConfigAggregate_delete(VALUE self, VALUE target)
{
  config_setting_t* setting = NULL;
  if(rb_iv_get(self, "@setting") != Qnil)
    Data_Get_Struct(rb_iv_get(self, "@setting"), config_setting_t, setting);
  
  VALUE hash = rb_iv_get(self, "@hash"), list = rb_iv_get(self, "@list");
  
  if(TYPE(target) == T_STRING && rb_obj_class(self) == cConfigGroup) {
    if(setting)
      config_setting_remove(setting, RSTRING_PTR(target));
    
    rb_ary_delete_at(list, rb_hash_aref(hash, target));
    rb_hash_delete(hash, target);
  } else if(TYPE(target) == T_FIXNUM) {
    int index = FIX2INT(target);
    if(setting)
      config_setting_remove_elem(setting, index);

    if(rb_obj_class(self) == cConfigGroup)
      rb_hash_delete(hash, rbConfigBaseSetting_name(rb_ary_entry(list, index)));
    rb_ary_delete_at(list, index);
  } else if(rb_ary_includes(aConfigSettings, rb_obj_class(target)) == Qtrue) {
    VALUE name = rbConfigBaseSetting_name(target);
    if(setting)
      config_setting_remove(setting, RSTRING_PTR(name));

    if(rb_obj_class(self) == cConfigGroup)
      rb_hash_delete(hash, name);
    rb_ary_delete(list, target);
  } else {
    if(rb_obj_class(self) == cConfigGroup)
      rb_raise(rb_eTypeError, "wrong argument type %s (expected String, Fixnum or Config::BaseSetting)", rb_obj_classname(target));
    else
      rb_raise(rb_eTypeError, "wrong argument type %s (expected Fixnum or Config::BaseSetting)", rb_obj_classname(target));
  }
  
  return Qnil;
}

static VALUE rbConfig_initialize(VALUE self)
{
  config_t* config = (config_t*) malloc(sizeof(config_t));
  config_init(config);
  config_set_destructor(config, &rconfig_destroy_setting);

  VALUE rbConfig = Data_Wrap_Struct(rb_cObject, 0, config_destroy, config);
  rb_iv_set(self, "@config", rbConfig);

  return self;
}

static VALUE rbConfig_read_bang(VALUE self, VALUE path)
{
  Check_Type(path, T_STRING);  

  config_t* config;
  Data_Get_Struct(rb_iv_get(self, "@config"), config_t, config);
  
  if(!config_read_file(config, RSTRING_PTR(path))) {
    if(config_error_line(config) == 0)
      rb_raise(rb_eIOError, "cannot load config: I/O error");
    else
      rb_raise(eConfigParseError, "cannot parse config on line %d: `%s'", 
                                  config_error_line(config), config_error_text(config));
  }
  
  return Qtrue;
}

static VALUE rbConfig_write_bang(VALUE self, VALUE path)
{
  Check_Type(path, T_STRING);  

  config_t* config;
  Data_Get_Struct(rb_iv_get(self, "@config"), config_t, config);
  
  if(!config_write_file(config, RSTRING_PTR(path)))
    rb_raise(rb_eIOError, "cannot save config: I/O error");
  
  return Qtrue;
}

static VALUE rbConfig_read(VALUE self, VALUE path)
{
  Check_Type(path, T_STRING);  

  config_t* config;
  Data_Get_Struct(rb_iv_get(self, "@config"), config_t, config);
  
  return config_read_file(config, RSTRING_PTR(path)) ? Qtrue : Qfalse;
}

static VALUE rbConfig_write(VALUE self, VALUE path)
{
  Check_Type(path, T_STRING);  

  config_t* config;
  Data_Get_Struct(rb_iv_get(self, "@config"), config_t, config);
  
  return config_write_file(config, RSTRING_PTR(path)) ? Qtrue : Qfalse;
}

static VALUE rbConfig_root(VALUE self)
{
  config_t* config;
  Data_Get_Struct(rb_iv_get(self, "@config"), config_t, config);
  
  return rconfig_wrap_setting(config_root_setting(config));
}

static VALUE rbConfig_lookup(VALUE self, VALUE handle)
{
  if(TYPE(handle) == T_STRING) {
    config_t* config;
    Data_Get_Struct(rb_iv_get(self, "@config"), config_t, config);
    
    config_setting_t* setting;
    setting = config_lookup(config, RSTRING_PTR(handle));
    
    if(setting == NULL)
      rb_raise(eSettingNotFoundError, "setting `%s' not found", RSTRING_PTR(handle));
    
    return rconfig_wrap_setting(setting);
  } else if(TYPE(handle) == T_FIXNUM) {
    return rbConfigAggregate_get(rbConfig_root(self), handle);
  } else {
    rb_raise(rb_eTypeError, "wrong argument type %s (expected String or Fixnum)", rb_obj_classname(handle));
  }
}

static VALUE rbConfig_append(VALUE self, VALUE name, VALUE target)
{
  return rbConfigGroup_append(rbConfig_root(self), name, target);
}

static VALUE rbConfig_delete(VALUE self, VALUE name)
{
  return rbConfigAggregate_delete(rbConfig_root(self), name);
}

static VALUE rbConfig_size(VALUE self)
{
  return rbConfigAggregate_size(rbConfig_root(self));
}

void Init_rconfig()
{
  cConfig = rb_define_class("Config", rb_cObject);
  rb_define_method(cConfig, "initialize", rbConfig_initialize, 0);
  rb_define_method(cConfig, "read!", rbConfig_read_bang, 1);
  rb_define_method(cConfig, "write!", rbConfig_write_bang, 1);
  rb_define_method(cConfig, "read", rbConfig_read, 1);
  rb_define_method(cConfig, "write", rbConfig_write, 1);
  rb_define_method(cConfig, "root", rbConfig_root, 0);
  rb_define_method(cConfig, "lookup", rbConfig_lookup, 1);
  rb_define_method(cConfig, "[]", rbConfig_lookup, 1);
  rb_define_method(cConfig, "append", rbConfig_append, 2);
  rb_define_method(cConfig, "delete", rbConfig_delete, 1);
  rb_define_method(cConfig, "size", rbConfig_size, 0);
  
  cConfigBaseSetting = rb_define_class_under(cConfig, "BaseSetting", rb_cObject);
  rb_define_method(cConfigBaseSetting, "initialize", rbConfigBaseSetting_initialize, 1);
  rb_define_method(cConfigBaseSetting, "name", rbConfigBaseSetting_name, 0);
  rb_define_method(cConfigBaseSetting, "parent", rbConfigBaseSetting_parent, 0);
  rb_define_method(cConfigBaseSetting, "root?", rbConfigBaseSetting_is_root, 0);
  rb_define_method(cConfigBaseSetting, "index", rbConfigBaseSetting_index, 0);
  rb_define_method(cConfigBaseSetting, "line", rbConfigBaseSetting_line, 0);

  cConfigSetting = rb_define_class_under(cConfig, "Setting", cConfigBaseSetting);
  rb_define_method(cConfigSetting, "initialize", rbConfigSetting_initialize, -1);
  rb_define_method(cConfigSetting, "value", rbConfigSetting_get_value, 0);
  rb_define_method(cConfigSetting, "value=", rbConfigSetting_set_value, 1);
  rb_define_method(cConfigSetting, "format", rbConfigSetting_get_format, 0);
  rb_define_method(cConfigSetting, "format=", rbConfigSetting_set_format, 1);
  
  cConfigFormatDefault = INT2FIX(CONFIG_FORMAT_DEFAULT);
  rb_define_const(cConfig, "FORMAT_DEFAULT", cConfigFormatDefault);
  cConfigFormatHex = INT2FIX(CONFIG_FORMAT_HEX);
  rb_define_const(cConfig, "FORMAT_HEX", cConfigFormatHex);
  
  cConfigFixnum = rb_define_class_under(cConfig, "Fixnum", cConfigSetting);
  cConfigBignum = rb_define_class_under(cConfig, "Bignum", cConfigSetting);
  cConfigFloat = rb_define_class_under(cConfig, "Float", cConfigSetting);
  cConfigBoolean = rb_define_class_under(cConfig, "Boolean", cConfigSetting);
  cConfigString = rb_define_class_under(cConfig, "String", cConfigSetting);
  
  cConfigAggregate = rb_define_class_under(cConfig, "Aggregate", cConfigBaseSetting);
  rb_define_method(cConfigAggregate, "initialize", rbConfigAggregate_initialize, -1);
  rb_define_method(cConfigAggregate, "size", rbConfigAggregate_size, 0);
  rb_define_method(cConfigAggregate, "get", rbConfigAggregate_get, 1);
  rb_define_method(cConfigAggregate, "[]", rbConfigAggregate_get, 1);
  rb_define_method(cConfigAggregate, "delete", rbConfigAggregate_delete, 1);
  
  cConfigGroup = rb_define_class_under(cConfig, "Group", cConfigAggregate);
  rb_define_method(cConfigGroup, "append", rbConfigGroup_append, 2);
  cConfigArray = rb_define_class_under(cConfig, "Array", cConfigAggregate);
  rb_define_method(cConfigArray, "append", rbConfigAggregate_append, 1);
  rb_define_method(cConfigArray, "<<", rbConfigAggregate_append, 1);
  cConfigList = rb_define_class_under(cConfig, "List", cConfigAggregate);
  rb_define_method(cConfigList, "append", rbConfigAggregate_append, 1);
  rb_define_method(cConfigList, "<<", rbConfigAggregate_append, 1);
  
  aConfigScalars = rb_ary_new3(5, cConfigFixnum, cConfigBignum, cConfigFloat, cConfigBoolean, cConfigString);
  aConfigAggregates = rb_ary_new3(3, cConfigGroup, cConfigArray, cConfigList);
  aConfigSettings = rb_ary_plus(aConfigScalars, aConfigAggregates);
  
  rb_define_const(cConfig, "SCALARS", aConfigScalars);
  rb_define_const(cConfig, "AGGREGATES", aConfigAggregates);
  rb_define_const(cConfig, "SETTINGS", aConfigSettings);
  
  char* settingNameRegexp = "^[A-Za-z*][A-Za-z\\-_*]*$";
  rSettingNameRegexp = rb_reg_new(settingNameRegexp, strlen(settingNameRegexp), 0);
  
  eConfigParseError = rb_define_class("ConfigParseError", rb_eException);
  eSettingNotFoundError = rb_define_class("SettingNotFoundError", rb_eException);
  eSettingFormatError = rb_define_class("SettingFormatError", rb_eException);
  eSettingNameError = rb_define_class("SettingNameError", rb_eException);
}
