project(
    'cz-services',
    'c','cpp',
    version : run_command('cat', files('./VERSION'), check : false).stdout().strip(),
    meson_version: '>= 0.60.0',
    default_options: [
        'warning_level=2',
        'buildtype=release',
        'cpp_std=c++20'])

VERSION_MAJOR = meson.project_version().split('.')[0]
VERSION_MINOR = meson.project_version().split('.')[1]
VERSION_PATCH = meson.project_version().split('.')[2]
HEADERS_INSTALL_DIR = join_paths(get_option('prefix'), get_option('includedir'), 'CZ/Services')
CONF_FILE_NAME = 'CZServicesVersion.h'

conf = configuration_data()
conf.set('VERSION_MAJOR', VERSION_MAJOR)
conf.set('VERSION_MINOR', VERSION_MINOR)
conf.set('VERSION_PATCH', VERSION_PATCH)
conf_file = configure_file(
    input : 'src/CZ/Services/' + CONF_FILE_NAME + '.in',
    output : CONF_FILE_NAME,
    configuration : conf)

install_data(
    conf_file,
    install_dir: HEADERS_INSTALL_DIR)

# -------------- DEPENDENCIES --------------

pkg         = import('pkgconfig')
cz_core_dep = dependency('cz-core', fallback:['cz-core', 'cz_core_dep'])
systemd_dep = dependency(['libsystemd', 'libelogind', 'basu'])

# -------------- HEADERS --------------

header_files = run_command('find', './src/CZ/Services', '-type', 'f', '-name', '*.h', '-maxdepth', '1', check : false).stdout().strip().split('\n')
install_headers(header_files, install_dir : HEADERS_INSTALL_DIR)

# -------------- LIBRARY --------------

cz_services = library(
    'cz-services',
    sources : run_command('find', './src/CZ/Services', '-type', 'f', '-name', '*.cpp', check : false).stdout().strip().split('\n'),
    include_directories : ['./src'],
    dependencies : [cz_core_dep, systemd_dep],
    soversion: VERSION_MAJOR,
    install : true)

cz_services_dep = declare_dependency(
    dependencies: [cz_core_dep, systemd_dep],
    include_directories : ['./src/', './src/CZ'],
    link_with : cz_services)

pkg.generate(
    cz_services,
    name: 'Services',
    description: 'C++ clients of the system services a desktop needs (Wi-Fi, ...).',
    version: meson.project_version(),
    subdirs: ['CZ/Services'],
    filebase: 'cz-services')
