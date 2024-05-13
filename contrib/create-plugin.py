#!/usr/bin/python3
#
# Copyright 2023 Richard Hughes <richard@hughsie.com>
#
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# pylint: disable=invalid-name,missing-docstring,consider-using-f-string

import os
import datetime
import argparse
import glob
import sys
from jinja2 import Environment, FileSystemLoader, select_autoescape

subst = {}


# convert a snake-case name into CamelCase
def _fix_case(value: str) -> str:
    return "".join(
        [tmp[0].upper() + tmp[1:].lower() for tmp in value.replace("_", "-").split("-")]
    )


def _subst_add_string(key: str, value: str, add_alternates=True) -> None:
    # sanity check
    if not value.isascii():
        raise NotImplementedError(f"{key} can only be ASCII, got {value}")
    if len(value) < 2:
        raise NotImplementedError(f"{key} has to be at least length 3, got {value}")

    subst[key] = value

    if add_alternates:
        subst[key.lower()] = value.lower()
        subst[key.upper()] = value.upper()


def _subst_replace(data: str) -> str:
    for key, value in subst.items():
        data = data.replace(key, value)
    return data


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--vendor",
        type=str,
        help="Vendor name",
        required=True,
    )
    parser.add_argument(
        "--example",
        type=str,
        help="Plugin basename",
        required=True,
    )
    parser.add_argument("--parent", type=str, default="Usb", help="Device parent GType")
    parser.add_argument(
        "--year", type=int, default=datetime.date.today().year, help="Copyright year"
    )
    parser.add_argument("--author", type=str, help="Copyright author", required=True)
    parser.add_argument("--email", type=str, help="Copyright email", required=True)
    args = parser.parse_args()

    try:
        vendor: str = _fix_case(args.vendor)
        example: str = _fix_case(args.example)
        _subst_add_string("VendorExample", vendor + example, add_alternates=False)
        _subst_add_string(
            "vendor_example",
            args.vendor.replace("-", "_") + "_" + args.example.replace("-", "_"),
        )
        _subst_add_string(
            "vendor-example",
            args.vendor.replace("_", "-") + "-" + args.example.replace("_", "-"),
            add_alternates=False,
        )
        _subst_add_string(
            "vendor_dash_example", subst["vendor-example"], add_alternates=False
        )
        _subst_add_string("Vendor", _fix_case(args.vendor))
        _subst_add_string("Example", _fix_case(args.example))
        _subst_add_string("Parent", args.parent)
        _subst_add_string("Year", str(args.year))
        _subst_add_string("Author", args.author)
        _subst_add_string("Email", args.email)
    except NotImplementedError as e:
        print(e)
        sys.exit(1)
    print(subst)

    template_src: str = "vendor-example"
    os.makedirs(os.path.join("plugins", _subst_replace(template_src)), exist_ok=True)

    srcdir: str = sys.argv[0].rsplit("/", maxsplit=2)[0]
    env = Environment(
        loader=FileSystemLoader(srcdir),
        autoescape=select_autoescape(),
        keep_trailing_newline=True,
    )
    for fn in glob.iglob(f"{srcdir}/plugins/{template_src}/*"):
        fn_rel: str = os.path.relpath(fn, srcdir)
        template = env.get_template(fn_rel)
        filename: str = _subst_replace(fn_rel.replace(".in", ""))
        with open(filename, "wb") as f_dst:
            f_dst.write(template.render(subst).encode())
        print(f"wrote {filename}")
