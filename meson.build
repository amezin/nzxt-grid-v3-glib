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

gio = dependency('gio-2.0', version : '>= 2.44.0')
gusb = dependency('gusb', version : '>= 0.3.5')
gudev = dependency('gudev-1.0')

executable('gridctl', 'main.c', dependencies : [gio, gusb])

executable('enumerate', 'enumerate.c', dependencies : [gio, gudev])
