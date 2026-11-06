#*
#* Created by v1tr10l7 on 02.09.2025.
#* Copyright (c) 2025-2026, Szymon Zemke <v1tr10l7@proton.me>
#*
#* SPDX-License-Identifier: GPL-3
#*/
project('QuantumTerminal', ['c', 'cpp'],
  version: 'v0.1.0', license: 'GPL-3.0',
  default_options: [
    'optimization=3',
    'warning_level=2',
    'werror=true',
    'c_std=c17',
    'cpp_std=c++23',
  ],
)

if not meson.get_compiler('cpp').has_argument('-std=gnu++23')
  error('C++23 support is needed. Please install clang 17 or higher.')
endif

c = meson.get_compiler('c')
cxx = meson.get_compiler('cpp')
ld = cxx.get_linker_id()
arch = host_machine.cpu_family()

cxx_args = [
  '-fno-rtti',
  '-fno-exceptions',
  '-DFMT_HEADER_ONLY',
  '-DFMT_STATIC_THOUSANDS_SEPARATOR=1',
  '-DFMT_USE_LONG_DOUBLE=0',
  '-DFMT_USE_DOUBLE=0',
  '-DFMT_USE_FLOAT=0',
  '-DFMT_USE_FLOAT128=0',
  '-DFMT_USE_EXCEPTIONS=0',
  '-DPRISM_USE_NAMESPACE=1',
  '-Wno-missing-field-initializers',
]
if cxx.get_id() == 'gcc'
  cxx_args += '-Wno-changes-meaning'
endif

extraincs = get_option('extra_incs')
build_tests = get_option('build_tests')

if extraincs != ''
  cxx_args += '-I' + extraincs
endif

datadir = get_option('prefix') / get_option('datadir') / 'quantum-term'
cxx_args += '-DQUANTUM_DATA_DIR="@0@"'.format(datadir)

srcs = files(
  'Source/Graphics/FbDevFramebuffer.cpp',
  'Source/Graphics/Image.cpp',
  'Source/Graphics/ZLib.cpp',
  'Source/Platform/PseudoTerminal.cpp',
  'Source/Terminal/Clipboard.cpp',
  'Source/Terminal/FramebufferConsole.cpp',
  'Source/Terminal/VirtualConsole.cpp',
)
incs = include_directories(
  'Source',
)

deps = [
  dependency('prism'),
]

# The SDL backend is meant for testing on the host
sdl = dependency('sdl2', required: get_option('sdl'))
if sdl.found()
  srcs += files('Source/Graphics/SDLFramebuffer.cpp')
  deps += sdl
  cxx_args += '-DQUANTUM_HAVE_SDL=1'
else
  cxx_args += '-DQUANTUM_HAVE_SDL=0'
endif

# openpty lives in libutil on older glibc versions
util = cxx.find_library('util', required: false)
if util.found()
  deps += util
endif

add_global_arguments(cxx_args, language: 'cpp')

link_args = []
if host_machine.system() != 'linux'
  link_args += ['-lsupc++', '-lgcc_s']
  # Only a linker option, passing it while compiling fails with -Werror
  if cxx.get_id() == 'clang'
    link_args += '-unwindlib=none'
  endif
endif

quantum_lib = static_library(
  'quantum', srcs,
  include_directories: incs,
  dependencies: deps,
)
quantum_dep = declare_dependency(
  link_with: quantum_lib,
  include_directories: incs,
  dependencies: deps,
)

executable(
  'quantum-term', files('Source/main.cpp'),
  link_args: link_args,
  dependencies: quantum_dep, install: true
)

install_data('Resources/background.png', install_dir: datadir)

if build_tests
  subdir('Tests')
endif
