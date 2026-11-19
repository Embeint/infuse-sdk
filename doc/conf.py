# Zephyr documentation build configuration file.
# Reference: https://www.sphinx-doc.org/en/master/usage/configuration.html

import os
import re
import sys
import textwrap
import urllib.request
import xml.etree.ElementTree as ET
from pathlib import Path

INFUSE_BASE = (Path(__file__).parents[1]).resolve()
ZEPHYR_BASE = (Path(__file__).parents[2] / "zephyr").resolve()
ZEPHYR_BUILD = Path(os.environ.get("OUTPUT_DIR")).resolve()

# Add the '_extensions' directory to sys.path, to enable finding Sphinx
# extensions within.
sys.path.insert(0, str(ZEPHYR_BASE / "doc" / "_extensions"))

# Add the '_scripts' directory to sys.path, to enable finding utility
# modules.
sys.path.insert(0, str(ZEPHYR_BASE / "doc" / "_scripts"))

# Add the directory which contains the runners package as well,
# for autodoc directives on runners.xyz.
sys.path.insert(0, str(ZEPHYR_BASE / "scripts" / "west_commands"))

# Add the directory which contains the pytest-twister-pytest
sys.path.insert(0, str(ZEPHYR_BASE / "scripts" / "pylib" / "pytest-twister-harness" / "src"))

try:
    import west as west_found
except ImportError:
    west_found = False

# -- Project --------------------------------------------------------------

project = "Infuse-IoT SDK"
copyright = "2025 Embeint Holdings Pty Ltd"
author = "Embeint Inc"

# parse version from 'VERSION' file
with open(INFUSE_BASE / "VERSION") as f:
    m = re.match(
        (
            r"^VERSION_MAJOR\s*=\s*(\d+)$\n"
            + r"^VERSION_MINOR\s*=\s*(\d+)$\n"
            + r"^PATCHLEVEL\s*=\s*(\d+)$\n"
            + r"^VERSION_TWEAK\s*=\s*\d+$\n"
            + r"^EXTRAVERSION\s*=\s*(.*)$"
        ),
        f.read(),
        re.MULTILINE,
    )

    if not m:
        sys.stderr.write("Warning: Could not extract kernel version\n")
        version = "Unknown"
    else:
        major, minor, patch, extra = m.groups(1)
        version = ".".join((major, minor, patch))
        if extra:
            version += "-" + extra

release = version

# parse Zephyr version from 'VERSION' file, used to link to matching Zephyr API docs
with open(ZEPHYR_BASE / "VERSION") as f:
    zephyr_version_vars = dict(re.findall(r"^(\w+)\s*=\s*(\S*)$", f.read(), re.MULTILINE))
zephyr_version = ".".join(zephyr_version_vars[k] for k in ("VERSION_MAJOR", "VERSION_MINOR", "PATCHLEVEL"))

# parse SDK version from 'SDK_VERSION' file
with open(ZEPHYR_BASE / "SDK_VERSION") as f:
    sdk_version = f.read().strip()

# -- General configuration ------------------------------------------------

extensions = [
    "sphinx_rtd_theme",
    "sphinx.ext.todo",
    "sphinx.ext.extlinks",
    "sphinx.ext.autodoc",
    "sphinx.ext.graphviz",
    "sphinxcontrib.jquery",
    "sphinxcontrib.mscgen",
    "zephyr.application",
    "zephyr.html_redirects",
    "zephyr.kconfig",
    "zephyr.dtcompatible-role",
    "zephyr.link-roles",
    "sphinx_tabs.tabs",
    "sphinx_sitemap",
    "zephyr.doxyrunner",
    "zephyr.doxybridge",
    "zephyr.doxytooltip",
    "zephyr.gh_utils",
    "zephyr.manifest_projects_table",
    "notfound.extension",
    "sphinx_copybutton",
    "sphinx_togglebutton",
    "zephyr.external_content",
    "zephyr.domain",
    "zephyr.api_overview",
]

# Only use image conversion when it is really needed, e.g. LaTeX build.
# Ensure "sphinxcontrib.rsvgconverter" is added before "sphinx.ext.imgconverter"
# as it's better at converting SVG with extended features (like the ones from
# draw.io) to PDF format).
if tags.has("convertimages"):  # pylint: disable=undefined-variable  # noqa: F821
    extensions.append("sphinxcontrib.rsvgconverter")
    extensions.append("sphinx.ext.imgconverter")

templates_path = ["_templates"]

exclude_patterns = ["_build"]

if not west_found:
    exclude_patterns.append("**/*west-apis*")
else:
    exclude_patterns.append("**/*west-not-found*")

pygments_style = "sphinx"
highlight_language = "none"

todo_include_todos = False

nitpick_ignore = [
    # ignore C standard identifiers (they are not defined in Zephyr docs)
    ("c:identifier", "FILE"),
    ("c:identifier", "int8_t"),
    ("c:identifier", "int16_t"),
    ("c:identifier", "int32_t"),
    ("c:identifier", "int64_t"),
    ("c:identifier", "intptr_t"),
    ("c:identifier", "off_t"),
    ("c:identifier", "size_t"),
    ("c:identifier", "ssize_t"),
    ("c:identifier", "time_t"),
    ("c:identifier", "uint8_t"),
    ("c:identifier", "uint16_t"),
    ("c:identifier", "uint32_t"),
    ("c:identifier", "uint64_t"),
    ("c:identifier", "uintptr_t"),
    ("c:identifier", "va_list"),
]

SDK_URL_BASE = "https://github.com/zephyrproject-rtos/sdk-ng/releases/download"

rst_epilog = f"""
.. include:: /substitutions.txt

.. |sdk-version-literal| replace:: ``{sdk_version}``
.. |sdk-version-trim| unicode:: {sdk_version}
   :trim:
.. |sdk-version-ltrim| unicode:: {sdk_version}
   :ltrim:
.. _Zephyr SDK bundle: https://github.com/zephyrproject-rtos/sdk-ng/releases/tag/v{sdk_version}
.. |sdk-url-linux| replace::
   `{SDK_URL_BASE}/v{sdk_version}/zephyr-sdk-{sdk_version}_linux-x86_64.tar.xz`
.. |sdk-url-linux-sha| replace::
   `{SDK_URL_BASE}/v{sdk_version}/sha256.sum`
.. |sdk-url-macos| replace::
   `{SDK_URL_BASE}/v{sdk_version}/zephyr-sdk-{sdk_version}_macos-x86_64.tar.xz`
.. |sdk-url-macos-sha| replace::
   `{SDK_URL_BASE}/v{sdk_version}/sha256.sum`
.. |sdk-url-windows| replace::
   `{SDK_URL_BASE}/v{sdk_version}/zephyr-sdk-{sdk_version}_windows-x86_64.7z`
"""

# -- Options for HTML output ----------------------------------------------

html_theme = "sphinx_rtd_theme"
html_theme_options = {
    "logo_only": True,
    "prev_next_buttons_location": None,
    "navigation_depth": 5,
}
html_baseurl = "https://docs.dev.infuse-iot.com/latest/"
html_title = "Infuse-IoT SDK Documentation"
html_logo = str(INFUSE_BASE / "doc" / "_static" / "images" / "infuse-dark.svg")
html_favicon = str(INFUSE_BASE / "doc" / "_static" / "images" / "favicon.png")
html_static_path = [str(INFUSE_BASE / "doc" / "_static")]
html_last_updated_fmt = "%b %d, %Y"
html_domain_indices = False
html_split_index = True
html_show_sourcelink = False
html_show_sphinx = False
html_search_scorer = str(ZEPHYR_BASE / "doc" / "_static" / "js" / "scorer.js")
html_additional_pages = {"gsearch": "gsearch.html"}

is_release = tags.has("release")  # pylint: disable=undefined-variable  # noqa: F821
reference_prefix = ""
if tags.has("publish"):  # pylint: disable=undefined-variable  # noqa: F821
    reference_prefix = f"/{version}" if is_release else "/latest"
docs_title = "Docs / {}".format(version if is_release else "Latest")
html_context = {
    "show_license": True,
    "docs_title": docs_title,
    "is_release": is_release,
    "current_version": version,
    "versions": (("latest", "/"),),
    "display_gh_links": True,
    "reference_links": {
        "Cloud API": "https://api.infuse-iot.com/docs",
        "Embedded Doxygen": f"{reference_prefix}/doxygen/html/index.html",
        "Embedded Kconfig": f"{reference_prefix}/kconfig.html",
        "Embedded Devicetree": f"{reference_prefix}/build/dts/api/bindings.html",
    },
    # Set google_searchengine_id to your Search Engine ID to replace built-in search
    # engine with Google's Programmable Search Engine.
    # See https://programmablesearchengine.google.com/ for details.
    # "google_searchengine_id": "746031aa0d56d4912",
}

# -- Options for LaTeX output ---------------------------------------------

latex_elements = {
    "papersize": "a4paper",
    "maketitle": (ZEPHYR_BASE / "doc" / "_static" / "latex" / "title.tex").read_text(),
    "preamble": (ZEPHYR_BASE / "doc" / "_static" / "latex" / "preamble.tex").read_text(),
    "makeindex": r"\usepackage[columns=1]{idxlayout}\makeindex",
    "fontpkg": textwrap.dedent(r"""
                                    \usepackage{noto}
                                    \usepackage{inconsolata-nerd-font}
                                    \usepackage[T1]{fontenc}
                                """),
    "sphinxsetup": ",".join(
        (
            # NOTE: colors match those found in light.css stylesheet
            "verbatimwithframe=false",
            "VerbatimColor={HTML}{f0f2f4}",
            "InnerLinkColor={HTML}{2980b9}",
            "warningBgColor={HTML}{e9a499}",
            "warningborder=0pt",
            r"HeaderFamily=\rmfamily\bfseries",
        )
    ),
}
latex_logo = str(ZEPHYR_BASE / "doc" / "_static" / "images" / "logo-latex.pdf")
latex_documents = [
    ("index-tex", "zephyr.tex", "Zephyr Project Documentation", author, "manual"),
]
latex_engine = "xelatex"

# -- Options for zephyr.doxyrunner plugin ---------------------------------

doxyrunner_doxygen = os.environ.get("DOXYGEN_EXECUTABLE", "doxygen")


def _zephyr_doxygen_tagfiles() -> str:
    """Generate a Doxygen tag file so references to Zephyr APIs link to the Zephyr docs

    The published Zephyr tag file is filtered down to the symbols Infuse actually mentions:

    * Structs and unions are kept if their name appears in the Infuse headers. Their fields are
      only kept if referenced as ``struct::field``, as common field names such as ``size`` or
      ``data`` would otherwise generate false links.
    * Group members (functions, macros, enums, ...) are kept if their name appears in the
      Infuse headers, along with their containing group.
    * File compounds are dropped, as they mostly contain libc types (``uint8_t``, ``bool``).
    * Structs and unions that Infuse also defines are dropped, as Doxygen prefers the external
      definition and would omit the Infuse one.
    """
    url = f"https://docs.zephyrproject.org/{zephyr_version}/doxygen/html"
    upstream = ZEPHYR_BUILD / f"zephyr-{zephyr_version}-upstream.tag"
    tagfile = ZEPHYR_BUILD / "zephyr-external.tag"

    if not upstream.exists():
        try:
            with urllib.request.urlopen(f"{url}/zephyr.tag", timeout=60) as r:
                data = r.read()
        except OSError as e:
            sys.stderr.write(f"Warning: Could not fetch Zephyr Doxygen tag file ({e})\n")
            return ""
        upstream.parent.mkdir(parents=True, exist_ok=True)
        upstream.write_bytes(data)

    try:
        root = ET.parse(upstream).getroot()
    except ET.ParseError as e:
        sys.stderr.write(f"Warning: Could not parse Zephyr Doxygen tag file ({e})\n")
        return ""

    identifiers = set()
    qualified_fields = set()
    infuse_compounds = set()
    # Must cover the INPUT list in infuse.doxyfile.in
    doxygen_inputs = [INFUSE_BASE / "doc" / "_doxygen" / "mainpage.md", INFUSE_BASE / "doc" / "_doxygen" / "groups.dox"]
    for include_dir in ("include", "generated/include", "generated/include_always"):
        doxygen_inputs += (INFUSE_BASE / include_dir).rglob("*.h")
    for path in doxygen_inputs:
        text = path.read_text()
        # Group names can contain '-' (e.g. 'sys-util')
        identifiers.update(re.findall(r"[A-Za-z_][\w-]*", text))
        identifiers.update(re.findall(r"\w+", text))
        qualified_fields.update(re.findall(r"(\w+)::(\w+)", text))
        infuse_compounds.update(re.findall(r"^\s*(?:struct|union)\s+(\w+)\s*\{", text, re.MULTILINE))

    for compound in list(root):
        kind = compound.get("kind")
        name = compound.findtext("name")
        if kind in ("struct", "union"):
            if name not in identifiers or name in infuse_compounds:
                root.remove(compound)
                continue
            for member in compound.findall("member"):
                if (name, member.findtext("name")) not in qualified_fields:
                    compound.remove(member)
        elif kind == "group":
            for member in compound.findall("member"):
                if member.findtext("name") not in identifiers:
                    compound.remove(member)
            if name not in identifiers and compound.find("member") is None:
                root.remove(compound)
        else:
            root.remove(compound)

    ET.ElementTree(root).write(tagfile, encoding="utf-8", xml_declaration=True)
    return f'"{tagfile} = {url}"'


doxyrunner_projects = {
    "infuse": {
        "doxyfile": INFUSE_BASE / "doc" / "infuse.doxyfile.in",
        "outdir": ZEPHYR_BUILD / "doxygen",
        "fmt": True,
        "fmt_vars": {
            "INFUSE_BASE": str(INFUSE_BASE),
            "ZEPHYR_BASE": str(ZEPHYR_BASE),
            "ZEPHYR_VERSION": version,
            "ZEPHYR_TAGFILES": _zephyr_doxygen_tagfiles(),
        },
        "outdir_var": "DOXY_OUT",
    },
}

# -- Options for zephyr.doxybridge plugin ---------------------------------

doxybridge_projects = {"zephyr": doxyrunner_projects["infuse"]["outdir"]}

# -- Options for html_redirect plugin -------------------------------------

html_redirect_pages = ()

# -- Options for zephyr.link-roles ----------------------------------------

link_roles_manifest_project = "infuse-sdk"
link_roles_manifest_baseurl = "https://github.com/Embeint/infuse-sdk"

# -- Options for notfound.extension ---------------------------------------

notfound_urls_prefix = f"/{version}/" if is_release else "/latest/"

# -- Options for zephyr.gh_utils ------------------------------------------

gh_link_version = f"v{version}" if is_release else "main"
gh_link_base_url = "https://github.com/Embeint/infuse-sdk"
gh_link_prefixes = {
    "samples/.*": "",
    "boards/.*": "",
    "snippets/.*": "",
    ".*": "doc",
}
gh_link_exclude = [
    "reference/kconfig.*",
    "build/dts/api/bindings.*",
    "build/dts/api/compatibles.*",
]

# -- Options for zephyr.kconfig -------------------------------------------

kconfig_generate_db = True
kconfig_ext_paths = [INFUSE_BASE, ZEPHYR_BASE]
kconfig_gh_link_base_url = "https://github.com/Embeint/infuse-sdk"
kconfig_zephyr_version = f"v{version}" if is_release else "main"
os.environ["ZEPHYR_MEMFAULT_FIRMWARE_SDK_KCONFIG"] = str(
    (Path(__file__).parents[2] / "modules" / "lib" / "memfault" / "ports" / "zephyr" / "Kconfig.3_7_0").resolve()
)

# -- Options for zephyr.external_content ----------------------------------

external_content_contents = [
    (INFUSE_BASE / "doc", "[!_]*"),
    (INFUSE_BASE, "boards/**/*.rst"),
    (INFUSE_BASE, "boards/**/doc"),
    (INFUSE_BASE, "samples/**/*.html"),
    (INFUSE_BASE, "samples/**/*.rst"),
    (INFUSE_BASE, "samples/**/doc"),
    (INFUSE_BASE, "snippets/**/*.rst"),
    (INFUSE_BASE, "snippets/**/doc"),
]
external_content_keep = [
    "reference/kconfig/*",
    "build/dts/api/bindings.rst",
    "build/dts/api/bindings/**/*",
    "build/dts/api/compatibles/**/*",
]

# -- Options for zephyr.domain --------------------------------------------

zephyr_breathe_insert_related_samples = True
zephyr_generate_hw_features = False

# -- Options for sphinx.ext.graphviz --------------------------------------

graphviz_dot = os.environ.get("DOT_EXECUTABLE", "dot")
graphviz_output_format = "svg"
graphviz_dot_args = [
    "-Gbgcolor=transparent",
    "-Nstyle=filled",
    "-Nfillcolor=white",
    "-Ncolor=gray60",
    "-Nfontcolor=gray25",
    "-Ecolor=gray60",
    "-Gfontname=system-ui,-apple-system,Segoe UI,Roboto,Helvetica Neue,Arial,Noto Sans,sans-serif",
    "-Nfontname=system-ui,-apple-system,Segoe UI,Roboto,Helvetica Neue,Arial,Noto Sans,sans-serif",
    "-Efontname=SFMono-Regular,Menlo,Monaco,Consolas,Liberation Mono,Courier New,Courier,monospace",
]

# -- Options for sphinx_copybutton ----------------------------------------

copybutton_prompt_text = r"\$ |uart:~\$ "
copybutton_prompt_is_regexp = True

# -- Options for sphinx-sitemap ----------------------------------------

sitemap_url_scheme = "{link}"

# -- Linkcheck options ----------------------------------------------------

linkcheck_ignore = [
    r"https://github.com/zephyrproject-rtos/zephyr/issues/.*",
    r"https://github.com/Embeint/infuse-sdk/issues/.*",
]

extlinks = {
    "github": ("https://github.com/Embeint/infuse-sdk/issues/%s", "GitHub #%s"),
}

linkcheck_timeout = 30
linkcheck_workers = 10
linkcheck_anchors = False

# -- Options for zephyr.api_overview --------------------------------------

api_overview_doxygen_out_dir = str(doxyrunner_projects["infuse"]["outdir"])
api_overview_base_url = "https://github.com/Embeint/infuse-sdk"


def setup(app):
    # theme customizations
    app.add_css_file("css/custom.css")
    app.add_js_file("js/custom.js")
