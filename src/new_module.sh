#!/bin/sh

print() { printf '%s' "$1"; }
println() { printf '%s\n' "$1"; }

# Go to the current working directory so things work if people are in a different one and e.g. use ../src/new_module.sh
cd -- "`dirname "$0"`"

# Make sure the template dir exists so we don't let people enter details unnecessarily
if [ ! -d ./template_module ]; then
	println "template_module doesn't exist" >&2
	exit 1
fi

# Ask for module name
print "Name of module (e.g. Oil gauge): "
read module_name

# Determine a "clean" module name for paths: lowercase, no spaces
module="`print "$module_name" | tr [A-Z] [a-z] | sed "s/ /_/g;s/'//g"`"

# Make sure `modules` directory exists and target directory doesn't
mkdir -p modules
module_dir="modules/$module"
if [ -e "$module_dir" ]; then
	println "$module_dir already exists" >&2
	exit 1
fi

# Ask for author name
print "How would you like to be credited? Your name: "
read author

# Copy the template directory
cp -r -- template_module "$module_dir"
cd -- "$module_dir"

# Disallow % in fields that will be used in %-delimited ed substitution
assert_no_percent() {
	case "$1" in
		*"%"*) println "$2 must not contain %" >&2; exit 1 ;;
	esac
}
assert_no_percent "$author" "Author name"
assert_no_percent "$module_name" "Module name"
assert_no_percent "$module" "Module path name"

# Fill in the blanks in the template
# `sed -i` is not portable so we create something like it ourselves
reced() {
	for file in "$1"/*; do
		if [ -f "$file" ]; then
			ed "$file" <<HERE
%s/{YEAR}/$(date +%Y)/
%s%{AUTHOR}%$author%
%s%{MODULE_NAME}%$module_name%
%s%{MODULE}%$module%
wq
HERE
		elif [ -d "$file" ]; then
			reced "$file"
		fi
	done
}
reced .

# Arduino IDE requires .ino sketches to have the same name as their directory
mv -- main.ino "$module.ino"

println "The basic structure for your module is now ready in $module_dir"
