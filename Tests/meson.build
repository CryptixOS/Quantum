#*
#* Created by v1tr10l7 on 02.09.2025.
#* Copyright (c) 2025-2026, Szymon Zemke <v1tr10l7@proton.me>
#*
#* SPDX-License-Identifier: GPL-3
#*/

tests = [
  'Example',
  'Shell',
]
test_incs = include_directories('.')
foreach name : tests
  test_exe = executable(
    name, [files(name / 'main.cpp')],
    cpp_args: ['-Wno-unused-parameter'],
    include_directories: test_incs,
    dependencies: quantum_dep
  )
  test(name, test_exe, timeout: 60)
endforeach
