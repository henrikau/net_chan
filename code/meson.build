project(
 'timedc_demo',
 'c',
 version: '0.0.1',
 license: 'BSD-3-Clause',
)

deps = dependency('threads')

timedc_avtp = static_library('timedc_avtp',
			'src/common.c',
			include_directories: include_directories('include'),
			dependencies: deps
		       )

executable(
   'client',
   'src/client.c',
   include_directories: include_directories('include'),
   build_by_default: true,
   link_with : timedc_avtp)


t = executable('testprog',
	       'test/test.c',
	       'test/unity.c',
	       include_directories: include_directories('include'),
	       build_by_default: true,
	       dependencies: deps
	      )
test('first test', t)
