#!/usr/bin/ruby
require "mkmf"

unless pkg_config('libconfig')
  puts 'failure: need libconfig'
  exit 1
end

have_func('rb_block_call', 'ruby/ruby.h')

create_makefile("rconfig")
