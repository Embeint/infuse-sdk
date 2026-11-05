# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Embeint Holdings Pty Ltd

"""Shared CVE checking support for `west release-cve-check` and `scripts/ci/workspace_cve_check.py`.

The package list to check is converted into a SPDX 3.0 JSON-LD SBOM, which is analysed by
sbom-cve-check (https://github.com/bootlin/sbom-cve-check), together with the OpenVEX
statements in `<project>/scripts/vex/*.openvex.json`.
"""

import csv
import dataclasses
import datetime
import glob
import json
import pathlib
import re
import shutil
import subprocess
from collections.abc import Iterable, Mapping
from typing import Protocol

ZEPHYR_CPE_PRODUCT = "zephyrproject:zephyr"
# OpenVEX files, relative to each west project
OPENVEX_GLOB = "scripts/vex/*.openvex.json"
# OpenVEX does not allow custom fields, so fix commits are recorded in "status_notes"
FIX_COMMITS_RE = re.compile(r"Fix commits:((?:\s+[0-9a-f]{40})+)\s*$")
# Above NVD (50), below annotation databases (100+). With the default (equal) priorities
# the affected version ranges of both databases are merged and the coarser NVD ranges win.
CVELIST_PRIORITY = 60
VULNERABLE_STATUSES = ("under_investigation", "affected")
DEFAULT_DATABASES_DIR = pathlib.Path("~/.cache/sbom_cve_check/databases").expanduser()


class Log(Protocol):
    """Logging interface, implemented by WestCommand"""

    def dbg(self, msg: str): ...
    def inf(self, msg: str): ...
    def wrn(self, msg: str): ...


@dataclasses.dataclass
class SpdxPackage:
    name: str
    spdx_id: str = ""
    version: str = ""
    cpes: list[str] = dataclasses.field(default_factory=list)
    purls: list[str] = dataclasses.field(default_factory=list)
    source: str = ""
    # Files contained in the package (compiled source files for "<module>-sources")
    files: list[str] = dataclasses.field(default_factory=list)
    # Compiled source files the package was built from
    sources: set[str] = dataclasses.field(default_factory=set)


def cpe_product(cpe: str) -> str:
    """'cpe:2.3:a:vendor:product:...' -> 'vendor:product'"""
    fields = cpe.split(":")
    return ":".join(fields[3:5]) if len(fields) > 5 else cpe


def cpe_version(cpe: str) -> str:
    fields = cpe.split(":")
    return fields[5] if len(fields) > 5 else ""


def zephyr_cpe(version: str) -> str:
    return f"cpe:2.3:o:{ZEPHYR_CPE_PRODUCT}:{version}:-:*:*:*:*:*:*"


def git(repo: pathlib.Path, *args: str) -> subprocess.CompletedProcess:
    return subprocess.run(["git", "-C", str(repo), *args], capture_output=True, text=True)


def write_spdx3(output: pathlib.Path, name: str, packages: list[SpdxPackage], tool: str):
    """Write the packages to a SPDX 3.0 JSON-LD SBOM"""
    from spdx_python_model import v3_0_1 as spdx3

    base = f"https://spdx.org/spdxdocs/infuse-iot/{name}"
    objset = spdx3.SHACLObjectSet()

    tool_element = objset.add(spdx3.Tool(_id=f"{base}/tool/{tool.replace(' ', '-')}", name=tool))
    creation_info = spdx3.CreationInfo(
        specVersion="3.0.1",
        created=datetime.datetime.now(datetime.timezone.utc).replace(microsecond=0),
        createdUsing=[tool_element],
    )
    # Agent is required to create elements, but needs creation info itself
    agent = spdx3.SoftwareAgent(_id=f"{base}/agent/infuse-iot", name="Infuse-IoT")
    creation_info.createdBy = [agent]
    agent.creationInfo = creation_info
    tool_element.creationInfo = creation_info
    objset.add(agent)

    pkg_elements = []
    for idx, pkg in enumerate(packages):
        ids = [
            spdx3.ExternalIdentifier(externalIdentifierType=spdx3.ExternalIdentifierType.cpe23, identifier=cpe)
            for cpe in pkg.cpes
        ]
        ids += [
            spdx3.ExternalIdentifier(externalIdentifierType=spdx3.ExternalIdentifierType.packageUrl, identifier=purl)
            for purl in pkg.purls
        ]
        element = spdx3.software_Package(
            _id=f"{base}/package/{idx}-{pkg.name}",
            creationInfo=creation_info,
            name=pkg.name,
            software_packageVersion=pkg.version,
            software_primaryPurpose=spdx3.software_SoftwarePurpose.install,
            externalIdentifier=ids,
        )
        if pkg.purls:
            element.software_packageUrl = pkg.purls[0]
        pkg_elements.append(objset.add(element))

        # sbom-cve-check names results after the "specification" package that generates
        # each installed package (the recipe in Yocto terms).
        spec = spdx3.software_Package(
            _id=f"{base}/package-spec/{idx}-{pkg.name}",
            creationInfo=creation_info,
            name=pkg.name,
            software_packageVersion=pkg.version,
            software_primaryPurpose=spdx3.software_SoftwarePurpose.specification,
        )
        pkg_elements.append(objset.add(spec))
        pkg_elements.append(
            objset.add(
                spdx3.LifecycleScopedRelationship(
                    _id=f"{base}/relationship/{idx}-{pkg.name}",
                    creationInfo=creation_info,
                    from_=spec,
                    relationshipType=spdx3.RelationshipType.generates,
                    to=[element],
                    scope=spdx3.LifecycleScopeType.build,
                )
            )
        )

        if not pkg.sources:
            continue

        # sbom-cve-check marks CVEs as not affected when none of the source files they
        # affect are inputs of the build that output the package.
        build = objset.add(
            spdx3.build_Build(
                _id=f"{base}/build/{idx}-{pkg.name}",
                creationInfo=creation_info,
                name=pkg.name,
                build_buildType="https://docs.zephyrproject.org/latest/develop/west/build-flash-debug.html",
            )
        )
        source_files = [
            objset.add(
                spdx3.software_File(
                    _id=f"{base}/file/{idx}-{pkg.name}/{file_idx}",
                    creationInfo=creation_info,
                    name=source,
                    software_primaryPurpose=spdx3.software_SoftwarePurpose.source,
                )
            )
            for file_idx, source in enumerate(sorted(pkg.sources))
        ]
        relationships = [
            spdx3.LifecycleScopedRelationship(
                _id=f"{base}/relationship/{idx}-{pkg.name}/{rel_type.rsplit('/', 1)[-1]}",
                creationInfo=creation_info,
                from_=build,
                relationshipType=rel_type,
                to=to,
                scope=spdx3.LifecycleScopeType.build,
            )
            for rel_type, to in (
                (spdx3.RelationshipType.hasInput, source_files),
                (spdx3.RelationshipType.hasOutput, [element]),
            )
        ]
        pkg_elements += [build, *source_files, *(objset.add(r) for r in relationships)]

    sbom = objset.add(
        spdx3.software_Sbom(
            _id=f"{base}/sbom",
            creationInfo=creation_info,
            name=name,
            software_sbomType=[spdx3.software_SbomType.build],
            rootElement=pkg_elements,
            element=pkg_elements,
        )
    )
    objset.add(
        spdx3.SpdxDocument(
            _id=f"{base}/document",
            creationInfo=creation_info,
            name=name,
            profileConformance=[spdx3.ProfileIdentifierType.core, spdx3.ProfileIdentifierType.software],
            rootElement=[sbom],
            element=[sbom, *pkg_elements],
        )
    )

    with output.open("wb") as f:
        spdx3.JSONLDSerializer().write(objset, f, indent=2)


def gather_vex(
    log: Log,
    projects: Iterable[tuple[str, pathlib.Path]],
    commits: Mapping[str, str] | Iterable[str],
    output: pathlib.Path,
    topdir: pathlib.Path,
    author: str,
) -> pathlib.Path | None:
    """Combine the OpenVEX statements of every project that apply to the checked code

    Statements whose status_notes end with "Fix commits: <sha> ..." only apply when all of
    the commits are ancestors of the checked commit of that project. `commits` maps each
    project name to its checked commit. When the mapping is not known (release SBOMs only
    record commits), it is a collection of candidate commits, and the commit of each project
    is the candidate that exists in its repository.
    """
    candidates = None if isinstance(commits, Mapping) else sorted(set(commits))
    statements = []
    for project, repo in projects:
        vex_files = sorted(repo.glob(OPENVEX_GLOB))
        if not vex_files:
            continue

        if candidates is None:
            commit = commits.get(project)
        else:
            commit = next(
                (c for c in candidates if git(repo, "cat-file", "-e", f"{c}^{{commit}}").returncode == 0), None
            )
        for vex_file in vex_files:
            with vex_file.open("r", encoding="utf-8") as f:
                doc = json.load(f)
            applied = skipped = 0
            for statement in doc.get("statements", []):
                match = FIX_COMMITS_RE.search(statement.get("status_notes", ""))
                # Fixes only apply if the fix commits are in the checked code
                if match is not None and (
                    commit is None
                    or not all(
                        git(repo, "merge-base", "--is-ancestor", c, commit).returncode == 0
                        for c in match.group(1).split()
                    )
                ):
                    log.dbg(f"{statement['vulnerability']['name']}: fix commits not in checked code")
                    skipped += 1
                    continue
                statements.append(statement)
                applied += 1

            name = vex_file.relative_to(topdir)
            if skipped and commit is None:
                log.wrn(f"{name}: {project} commit not known, {skipped} fixes not applied")
            not_applied = f" ({skipped} with fix commits not in the checked commit)" if skipped else ""
            log.inf(f"Loaded {applied} CVE assessments from {name}{not_applied}")

    if not statements:
        return None

    combined = output / "assessments.openvex.json"
    doc = {
        "@context": "https://openvex.dev/ns/v0.2.0",
        "@id": f"https://github.com/Embeint/infuse-sdk/{author.replace(' ', '-')}/{output.name}",
        "author": author,
        "timestamp": datetime.datetime.now(datetime.timezone.utc).replace(microsecond=0).isoformat(),
        "version": 1,
        "statements": statements,
    }
    with combined.open("w", encoding="utf-8") as f:
        json.dump(doc, f, indent=2)
    return combined


def sbom_cve_check_command(
    sbom: pathlib.Path,
    exports: Iterable[tuple[str, pathlib.Path]],
    *,
    vex: pathlib.Path | None = None,
    filter_vulnerable: bool = True,
    databases_dir: pathlib.Path | None = None,
    offline: bool = False,
) -> list[str]:
    """sbom-cve-check command line, raises FileNotFoundError if it is not installed"""
    sbom_cve_check = shutil.which("sbom-cve-check")
    if sbom_cve_check is None:
        raise FileNotFoundError("sbom-cve-check is not installed, run 'pip install sbom-cve-check'")

    cmd = [sbom_cve_check, "--sbom-type", "spdx3", "--sbom-path", str(sbom)]
    for export_type, export_path in exports:
        cmd += ["--export-type", export_type, "--export-path", str(export_path)]
    if filter_vulnerable:
        cmd.append("--export-filter-vulnerable")
    if databases_dir:
        cmd += ["--databases-dir", str(databases_dir)]
    if offline:
        cmd += ["--disable-auto-updates", "--offline-mode"]
    cmd += ["--set-db-cfg", "cvelist", f"priority={CVELIST_PRIORITY}"]
    if vex:
        cmd += ["--add-db", "openvex-file", str(vex), "name=Infuse-IoT assessments"]
    return cmd


def vulnerable_cves(csv_file: pathlib.Path) -> list[dict]:
    """CSV export rows for vulnerable CVEs"""
    with csv_file.open("r", encoding="utf-8", newline="") as f:
        return [row for row in csv.DictReader(f) if row["status"] in VULNERABLE_STATUSES]


def untriaged_cves(vulnerable: list[dict], vex: pathlib.Path | None) -> list[dict]:
    """Vulnerable CVEs (CSV export rows) not covered by an applied VEX statement

    A statement covers a CVE when its CVE, CPE vendor:product and CPE version all match.
    """
    triaged = set()
    if vex is not None:
        with vex.open("r", encoding="utf-8") as f:
            statements = json.load(f)["statements"]
        triaged = {
            (s["vulnerability"]["name"], cpe_product(cpe), cpe_version(cpe))
            for s in statements
            for p in s.get("products", [])
            if (cpe := p.get("identifiers", {}).get("cpe23"))
        }
    return [
        row
        for row in vulnerable
        if not any((row["vuln-id"], vp, row["version"]) in triaged for vp in row["vendor-product"].split(";"))
    ]


def cve_score(row: dict) -> str | None:
    return row["scorev3"] or row["scorev4"] or row["scorev2"] or None


def cve_record(databases: pathlib.Path, cve: str) -> dict:
    """CNA container of a CVE List record"""
    year, number = cve.split("-")[1:]
    paths = glob.glob(str(databases / "cvelist" / "cves" / year / f"{number[:-3]}xxx" / f"{cve}.json"))
    if not paths:
        return {}
    with open(paths[0], encoding="utf-8") as f:
        return json.load(f).get("containers", {}).get("cna", {})


# Dotted numeric versions ("3.7.0", "v4.1"), optionally with a suffix ("4.2.0-rc1")
_NUMERIC_VERSION_RE = re.compile(r"v?(\d+(?:\.\d+)*)(?:[-+~].*)?")


def _version_key(version: str) -> tuple | None:
    """Comparable key for a numeric version, None for other versions (git commits, etc)"""
    match = _NUMERIC_VERSION_RE.fullmatch(version)
    if match is None:
        return None
    # Pad so that "3.7" == "3.7.0"
    return tuple(([int(x) for x in match.group(1).split(".")] + [0, 0])[:3])


def first_fixed_version(record: dict, version: str) -> str | None:
    """First version that fixes the CVE, for the CVE record version range containing `version`"""
    current = _version_key(version)
    if current is None:
        return None
    fixed = []
    for affected in record.get("affected", []):
        for r in affected.get("versions", []):
            # Ranges of other version types ("git" commits, etc) cannot be compared
            if r.get("status") != "affected" or "lessThan" not in r or r.get("versionType") == "git":
                continue
            start = r.get("version", "0")
            start = (0,) if start in ("0", "*") else _version_key(start)
            end = _version_key(r["lessThan"])
            if start is not None and end is not None and start <= current < end:
                fixed.append(r["lessThan"])
    return min(fixed, key=_version_key) if fixed else None
