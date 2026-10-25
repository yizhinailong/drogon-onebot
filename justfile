platform := os()
build_dir := "./build"
publish_dir := "./publish"
exe_suffix := if platform == "windows" { ".exe" } else { "" }

default:
    @cmake --version
    @ninja --version
    @ccache --version
    @just --list

init:
    @vcpkg install drogon

configure profile="debug" *args:
    @cmake --preset "{{ platform }}-{{ profile }}" {{ args }}

build profile="debug":
    @[ -f "{{ build_dir }}/{{ platform }}-{{ profile }}/CMakeCache.txt" ] || cmake --preset "{{ platform }}-{{ profile }}"
    @cmake --build --preset "{{ platform }}-{{ profile }}"
    @cmake -E copy_if_different "{{ build_dir }}/{{ platform }}-{{ profile }}/compile_commands.json" "{{ build_dir }}/compile_commands.json"

publish profile="release": (build profile)
    @cmake --install "{{ build_dir }}/{{ platform }}-{{ profile }}"

run target profile="debug": (build profile)
    @{{ build_dir }}/{{ platform }}-{{ profile }}/tests/{{ target }}{{ exe_suffix }}

clean profile="":
    @cmake -E rm -rf {{ if profile == "" { build_dir + " " + publish_dir } else { build_dir + "/" + platform + "-" + profile } }}
