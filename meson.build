project('gridctl', 'c',
    version: '0.1',
    meson_version : '>=0.47.0',
    default_options : [
        'buildtype=debugoptimized',
        'warning_level=1',
        'c_std=c11',
    ],
)

gnome = import('gnome')

deps = [
    dependency('gio-2.0'),
    dependency('gio-unix-2.0'),
    dependency('gudev-1.0')
]

executable('gridctl', 'main.c', dependencies : deps)
executable('enumerate', 'enumerate.c', dependencies : deps)
