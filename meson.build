project('gridctl', 'c',
    version: '0.1',
    meson_version : '>=0.47.0',
    default_options : [
        'buildtype=debugoptimized',
        'warning_level=1',
        'c_std=gnu11',
    ],
)

cc = meson.get_compiler('c')

deps = [
    dependency('gio-2.0'),
]

if host_machine.system() == 'windows'
    deps += [
        dependency('wing'),
        cc.find_library('hid'),
        cc.find_library('setupapi'),
    ]
else
    deps += [
        dependency('gio-unix-2.0'),
        dependency('gudev-1.0'),
    ]
endif

if host_machine.system() == 'windows'
    executable('gridctl', 'main-win.c', dependencies : deps, install : true)
    executable('enumerate', 'enumerate-win.c', dependencies : deps, install : true)
else
    executable('gridctl', 'main.c', dependencies : deps, install : true)
    executable('enumerate', 'enumerate.c', dependencies : deps, install : true)
endif
