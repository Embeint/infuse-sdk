#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Embeint Holdings Pty Ltd

"""Check the modules of a west workspace for untriaged CVEs.

Unlike `west release-cve-check`, which checks the SBOM of a product release,
this checks the Zephyr version and modules that the workspace pins, without
building anything. There is no compiled file filtering, so every CVE affecting
a module version is reported unless a VEX statement in
`<project>/scripts/vex/*.openvex.json` covers it (for example `fixed` with the
backported fix commits, or `affected` with an action statement).

The untriaged CVEs are written to a JSON report for `cve_issue.py`.
"""

import argparse
import datetime
import json
import os
import pathlib
import subprocess
import sys

import yaml
from west.manifest import Manifest
from west.util import west_topdir

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "west_commands"))
import cve_check  # noqa: E402
from cve_check import SpdxPackage  # noqa: E402

TOOL = "workspace_cve_check.py"


class Log:
    def __init__(self, verbose: bool):
        self.verbose = verbose

    def dbg(self, msg: str):
        if self.verbose:
            print(msg)

    def inf(self, msg: str):
        print(msg)

    def wrn(self, msg: str):
        print(f"WARNING: {msg}", file=sys.stderr)


def zephyr_version(zephyr: pathlib.Path) -> str:
    fields = {}
    for line in (zephyr / "VERSION").read_text(encoding="utf-8").splitlines():
        if "=" in line:
            key, value = line.split("=", 1)
            fields[key.strip()] = value.strip()
    return ".".join(fields[k] for k in ("VERSION_MAJOR", "VERSION_MINOR", "PATCHLEVEL"))


def module_security(path: pathlib.Path) -> tuple[str, list[str], list[str]] | None:
    """Module name, CPEs and PURLs from the module.yml `security` section"""
    for module_yml in (path / "zephyr" / "module.yml", path / "module.yml"):
        if module_yml.exists():
            meta = yaml.safe_load(module_yml.read_text(encoding="utf-8")) or {}
            refs = (meta.get("security") or {}).get("external-references") or []
            cpes = [r for r in refs if r.startswith("cpe:2.3:")]
            purls = [r for r in refs if r.startswith("pkg:")]
            return meta.get("name", path.name), cpes, purls
    return None


def gather_workspace(manifest: Manifest) -> tuple[list[SpdxPackage], list[tuple[str, pathlib.Path]], dict[str, str]]:
    """Packages to check, the cloned projects and the checked out commit of each project"""
    zephyr_path = pathlib.Path(manifest.topdir) / "zephyr"
    packages = []
    projects = []
    commits = {}

    for project in manifest.projects:
        if project.abspath is None or not project.is_cloned():
            continue
        path = pathlib.Path(project.abspath)
        projects.append((project.name, path))
        proc = cve_check.git(path, "rev-parse", "HEAD")
        if proc.returncode == 0:
            commits[project.name] = proc.stdout.strip()

        if path == zephyr_path:
            version = zephyr_version(path)
            packages.append(
                SpdxPackage(name="zephyr", version=version, cpes=[cve_check.zephyr_cpe(version)], source=project.name)
            )
        elif (security := module_security(path)) is not None:
            name, cpes, purls = security
            for cpe in cpes:
                version = cve_check.cpe_version(cpe)
                packages.append(SpdxPackage(name=name, version=version, cpes=[cpe], purls=purls, source=project.name))

    return packages, projects, commits


def main():
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter, allow_abbrev=False
    )
    parser.add_argument("-o", "--output", type=pathlib.Path, required=True, help="Output directory")
    parser.add_argument("--databases-dir", type=pathlib.Path, help="CVE databases directory")
    parser.add_argument("--offline", action="store_true", help="Do not update the CVE databases")
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()

    log = Log(args.verbose)
    manifest = Manifest.from_topdir(west_topdir())
    args.output.mkdir(parents=True, exist_ok=True)

    packages, projects, commits = gather_workspace(manifest)
    log.inf("Packages checked:")
    for pkg in packages:
        log.inf(f"  {pkg.name:<24} {pkg.version:<12} ({cve_check.cpe_product(pkg.cpes[0])}, {pkg.source})")

    sbom_file = args.output / "workspace.spdx.json"
    cve_check.write_spdx3(sbom_file, "workspace", packages, TOOL)
    vex = cve_check.gather_vex(log, projects, commits, args.output, pathlib.Path(manifest.topdir), TOOL)

    csv_file = args.output / "cves.csv"
    try:
        cmd = cve_check.sbom_cve_check_command(
            sbom_file,
            [("csv", csv_file)],
            vex=vex,
            databases_dir=args.databases_dir,
            offline=args.offline,
        )
    except FileNotFoundError as e:
        sys.exit(str(e))
    if subprocess.run(cmd).returncode != 0:
        sys.exit("sbom-cve-check failed")

    vulnerable = cve_check.vulnerable_cves(csv_file)
    databases = args.databases_dir or pathlib.Path(
        os.environ.get("SBOM_CVE_CHECK_DATABASES_DIR", cve_check.DEFAULT_DATABASES_DIR)
    )
    untriaged = []
    for row in cve_check.untriaged_cves(vulnerable, vex):
        record = cve_check.cve_record(databases, row["vuln-id"])
        references = [r["url"] for r in record.get("references", [])]
        untriaged.append(
            {
                "cve": row["vuln-id"],
                "score": cve_check.cve_score(row),
                "component": row["build"],
                "version": row["version"],
                "title": record.get("title"),
                "fixed_in": cve_check.first_fixed_version(record, row["version"]),
                "advisory": next((url for url in references if "/security/advisories/GHSA-" in url), None),
            }
        )
    untriaged.sort(key=lambda e: (-float(e["score"] or 0), e["cve"]))

    report = {
        "date": datetime.date.today().isoformat(),
        "zephyr_commit": commits.get("zephyr"),
        "packages": [{"name": p.name, "version": p.version, "cpe": p.cpes[0]} for p in packages],
        "vulnerable": len(vulnerable),
        "untriaged": untriaged,
    }
    report_file = args.output / "untriaged.json"
    report_file.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    log.inf(f"{len(vulnerable)} vulnerable CVEs, {len(untriaged)} without a VEX statement ({report_file})")


if __name__ == "__main__":
    main()
