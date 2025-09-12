#!/usr/bin/env python3
"""
Dump and clean GCC spec file by removing sanitizer linking blocks.
This script is used in CMake to generate XSan spec files.

This Python script mimics the logic from gcc.cpp to remove sanitizer components
and can be used both as a standalone tool and as part of the CMake build process.

Usage:
    python3 dump_gcc_spec.py --cc=/path/to/gcc/or/g++ [-o /path/to/output]

    --cc: the underlying gcc/g++, used to dump standard SPEC inside this script
    [-o /path/to/output]: alternative option, if not specified, dump the new SPEC to stdout
"""

import argparse
import re
import subprocess
import sys


def is_spec_brace_start(spec, i):
    """Check if position i starts a spec brace pattern '%{'"""
    return (
        i + 1 < len(spec)
        and spec[i : i + 2] == "%{"
        and i + 2 < len(spec)
        and spec[i + 2] != "{"
    )


def extract_sanitizer_comp(spec, header_pattern):
    """Extract sanitizer components similar to gcc.cpp SanitizerComp::extract"""
    components = []
    pos = 0
    while True:
        match = re.search(header_pattern, spec[pos:])
        if not match:
            break
        start_pos = pos + match.start()
        brace_count = 0
        i = start_pos
        while i < len(spec):
            if is_spec_brace_start(spec, i):
                brace_count += 1
            elif spec[i] == "}":
                brace_count -= 1
                if brace_count == 0:
                    end_pos = i + 1
                    break
            i += 1
        else:
            break
        component = spec[start_pos:end_pos]
        for san_name, san_type in SANITIZER_TYPES.items():
            if f"sanitize({san_name})" in component:
                components.append((component, san_type, san_name))
                break
        pos = end_pos
    return components


def extract_sanitizer_specs(spec):
    """Extract sanitizer specs similar to gcc.cpp SanitzerSpec::extract"""
    nodefaultlibs_pattern = r"%\{!nostdlib:%\{!r:%\{!nodefaultlibs:"
    specs = []
    pos = 0
    while True:
        match = re.search(nodefaultlibs_pattern, spec[pos:])
        if not match:
            break
        start_pos = pos + match.start()
        brace_count = 0
        i = start_pos
        while i < len(spec):
            if is_spec_brace_start(spec, i):
                brace_count += 1
            elif spec[i] == "}":
                brace_count -= 1
                if brace_count == 0:
                    end_pos = i + 1
                    break
            i += 1
        else:
            break
        spec_component = spec[start_pos:end_pos]
        if "-lasan" in spec_component:
            specs.append((spec_component, "RtSpec"))
        elif "%{link_libasan}" in spec_component:
            specs.append((spec_component, "DepSpec"))
        else:
            specs.append((spec_component, "Other"))
        pos = end_pos
    return specs


def remove_sanitizer_components(spec):
    """Remove sanitizer components based on gcc.cpp logic"""
    cleaned = spec
    sanitizer_components = extract_sanitizer_comp(cleaned, SANITIZER_HEADER)
    masked_sanitizers = ["address", "thread"]
    for component, san_type, san_name in sanitizer_components:
        if san_name in masked_sanitizers:
            cleaned = cleaned.replace(component, "")
    sanitizer_specs = extract_sanitizer_specs(cleaned)
    for spec_component, spec_type in sanitizer_specs:
        if spec_type == "RtSpec":
            sanitizer_components = extract_sanitizer_comp(
                spec_component, SANITIZER_HEADER
            )
            modified_spec = spec_component
            for component, san_type, san_name in sanitizer_components:
                if san_name in masked_sanitizers:
                    modified_spec = modified_spec.replace(component, "")
            cleaned = cleaned.replace(spec_component, modified_spec)
    cleaned = re.sub(r"\s+", " ", cleaned)
    cleaned = cleaned.strip()
    orig_post_pos = spec.rfind("%(post_link)")
    if "%(post_link)" in cleaned and orig_post_pos != -1:
        post_link_pos = cleaned.rfind("%(post_link)")
        kept = cleaned[: post_link_pos + len("%(post_link)")]
        orig_tail = spec[orig_post_pos + len("%(post_link)") :]
        cleaned = kept + orig_tail
    elif orig_post_pos != -1:
        orig_tail = spec[orig_post_pos:]
        cleaned = cleaned + " " + orig_tail
    else:
        if "%(post_link)" in cleaned:
            post_link_pos = cleaned.rfind("%(post_link)")
            cleaned = cleaned[: post_link_pos + len("%(post_link)")] + " }}}}}}"
        else:
            cleaned = cleaned + " %(post_link) }}}}}}"
    return cleaned


def dump_gcc_specs(cc_path):
    """Dump GCC specs using the specified compiler"""
    try:
        result = subprocess.run(
            [cc_path, "-dumpspecs"], capture_output=True, text=True, check=True
        )
        return result.stdout
    except subprocess.CalledProcessError as e:
        print(f"Error: Failed to dump specs from {cc_path}: {e}", file=sys.stderr)
        sys.exit(1)
    except FileNotFoundError:
        print(f"Error: Compiler not found: {cc_path}", file=sys.stderr)
        sys.exit(1)


def extract_link_command(specs_output):
    """Extract the link_command from GCC specs output"""
    link_command_match = re.search(r"\*link_command:[^\n]*\n([^\n]*)", specs_output)
    if not link_command_match:
        print("Error: Could not find *link_command in GCC specs", file=sys.stderr)
        sys.exit(1)
    return link_command_match.group(1)


def main():
    parser = argparse.ArgumentParser(
        description="Dump and clean GCC spec file by removing sanitizer linking blocks",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
This script is used in CMake to generate XSan spec files. It can also be used
as a standalone tool to dump and clean GCC specs.

Examples:
    # Dump and clean specs from gcc, output to stdout
    python3 dump_gcc_spec.py --cc=gcc
    
    # Dump and clean specs from g++, save to file
    python3 dump_gcc_spec.py --cc=g++ -o cleaned_spec.txt
    
    # Use custom compiler path
    python3 dump_gcc_spec.py --cc=/usr/bin/gcc-11 -o output.spec
        """,
    )

    parser.add_argument(
        "--cc",
        required=True,
        help="The underlying gcc/g++, used to dump standard SPEC inside this script",
    )

    parser.add_argument(
        "-o",
        "--output",
        help="Output file path. If not specified, dump the new SPEC to stdout",
    )

    args = parser.parse_args()

    # Dump specs from the specified compiler
    specs_output = dump_gcc_specs(args.cc)

    # Extract link command
    original_link_command = extract_link_command(specs_output)

    # Check if sanitizer blocks exist
    if "%{%:sanitize" in original_link_command:
        print(
            f"Found sanitizer blocks in link command, removing them dynamically",
            file=sys.stderr,
        )
        cleaned_link_command = remove_sanitizer_components(original_link_command)
        print(
            f"Original length: {len(original_link_command)}, Cleaned length: {len(cleaned_link_command)}",
            file=sys.stderr,
        )
    else:
        print(
            f"No sanitizer blocks found, using original link command", file=sys.stderr
        )
        cleaned_link_command = original_link_command

    # Format the final spec
    final_spec = f"*link_command:\n{cleaned_link_command}\n"

    # Output the result
    if args.output:
        try:
            with open(args.output, "w") as f:
                f.write(final_spec)
            print(f"Cleaned spec written to: {args.output}", file=sys.stderr)
        except IOError as e:
            print(f"Error: Failed to write to {args.output}: {e}", file=sys.stderr)
            sys.exit(1)
    else:
        print(final_spec)


# Constants
SANITIZER_TYPES = {
    "address": "ASan",
    "thread": "TSan",
    "leak": "LSan",
    "hwaddress": "HWASan",
    "undefined": "UBSan",
}

SANITIZER_HEADER = r"%\{%:sanitize\("

if __name__ == "__main__":
    main()
