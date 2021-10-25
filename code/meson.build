project(
 'timedc_demo',
 'c',
 version: '0.0.1',
 license: 'BSD-3-Clause',
)

deps = dependency('threads')

executable(
   'client',
   'src/client.c',
   'src/common.c',
   include_directories: include_directories('include'),
   build_by_default: true,
   dependencies: deps
)
