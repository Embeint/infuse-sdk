#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Embeint Holdings Pty Ltd

"""Open a GitHub issue listing CVEs from `workspace_cve_check.py` that need triage.

CVEs already listed in a previous issue (open or closed) with the same label are
not reported again, so each run only creates an issue for newly published, or
newly applicable, CVEs. Each CVE gets a checkbox, to be ticked once a VEX
statement has been added for it.

Requires GITHUB_TOKEN and GITHUB_REPOSITORY in the environment, unless
--dry-run is given.
"""

import argparse
import json
import os
import pathlib
import re
import sys
import urllib.error
import urllib.parse
import urllib.request

API = os.environ.get("GITHUB_API_URL", "https://api.github.com")
CVE_RE = re.compile(r"\bCVE-\d{4}-\d{4,}\b")


def request(method: str, path: str, token: str, body: dict | None = None):
    req = urllib.request.Request(
        f"{API}{path}",
        method=method,
        data=json.dumps(body).encode() if body is not None else None,
        headers={
            "Accept": "application/vnd.github+json",
            "Authorization": f"Bearer {token}",
            "X-GitHub-Api-Version": "2022-11-28",
        },
    )
    with urllib.request.urlopen(req) as rsp:
        return json.loads(rsp.read() or "null")


def reported_cves(repo: str, label: str, token: str) -> set[str]:
    """CVEs mentioned in any issue with the label"""
    cves = set()
    page = 1
    while True:
        query = urllib.parse.urlencode({"labels": label, "state": "all", "per_page": 100, "page": page})
        issues = request("GET", f"/repos/{repo}/issues?{query}", token)
        for issue in issues:
            cves |= set(CVE_RE.findall(issue.get("body") or ""))
        if len(issues) < 100:
            return cves
        page += 1


def ensure_label(repo: str, label: str, token: str):
    try:
        request("GET", f"/repos/{repo}/labels/{urllib.parse.quote(label)}", token)
    except urllib.error.HTTPError as e:
        if e.code != 404:
            raise
        request("POST", f"/repos/{repo}/labels", token, {"name": label, "color": "b60205"})


def issue_body(report: dict, new: list[dict], run_url: str | None) -> str:
    lines = [
        f"`workspace_cve_check.py` found {len(new)} new CVEs affecting the modules pinned by the workspace "
        "that have no VEX statement.",
        "",
        "Triage each CVE by adding a statement to `<project>/scripts/vex/*.openvex.json` (for Zephyr, "
        "`zephyr/scripts/vex/zephyr.openvex.json`), then tick its box:",
        "",
        "* `fixed`, with the backported commits as `Fix commits: <sha> ...` at the end of `status_notes`",
        "* `affected`, with an `action_statement` when the fix will not be backported",
        "* `not_affected`, with a justification",
        "",
        "| Package | Version |",
        "|---|---|",
    ]
    lines += [f"| {p['name']} | {p['version']} |" for p in report["packages"]]
    if report.get("zephyr_commit"):
        lines += ["", f"Zephyr commit: `{report['zephyr_commit']}`"]
    lines += ["", "### CVEs", ""]
    for e in new:
        details = [f"**{e['cve']}**", f"({e['score'] or '-'})", f"{e['component']} {e['version']}"]
        if e.get("title"):
            details.append(f"— {e['title']}")
        links = [f"[record](https://www.cve.org/CVERecord?id={e['cve']})"]
        if e.get("advisory"):
            links.append(f"[advisory]({e['advisory']})")
        fixed = f"Fixed upstream in {e['fixed_in']}" if e.get("fixed_in") else "No fixed version listed"
        lines.append(f"- [ ] {' '.join(details)}. {fixed}, {', '.join(links)}")
    if run_url:
        lines += ["", f"Found by {run_url}"]
    return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("report", type=pathlib.Path, help="untriaged.json from workspace_cve_check.py")
    parser.add_argument("--label", default="cve-check", help="Label used to find previously reported CVEs")
    parser.add_argument(
        "--assignee", action="append", default=[], help="GitHub user to assign the issue to (can be repeated)"
    )
    parser.add_argument("--dry-run", action="store_true", help="Print the issue instead of creating it")
    args = parser.parse_args()

    report = json.loads(args.report.read_text(encoding="utf-8"))
    repo = os.environ.get("GITHUB_REPOSITORY")
    token = os.environ.get("GITHUB_TOKEN")
    server = os.environ.get("GITHUB_SERVER_URL", "https://github.com")
    run_id = os.environ.get("GITHUB_RUN_ID")
    run_url = f"{server}/{repo}/actions/runs/{run_id}" if repo and run_id else None

    if args.dry_run:
        reported = set()
    elif not (repo and token):
        sys.exit("GITHUB_REPOSITORY and GITHUB_TOKEN are required (or use --dry-run)")
    else:
        reported = reported_cves(repo, args.label, token)

    new = [e for e in report["untriaged"] if e["cve"] not in reported]
    print(f"{len(report['untriaged'])} untriaged CVEs, {len(new)} not previously reported")
    if not new:
        return

    title = f"CVE triage: {len(new)} new CVEs ({report['date']})"
    body = issue_body(report, new, run_url)
    if args.dry_run:
        assignees = f"Assignees: {', '.join(args.assignee)}\n\n" if args.assignee else ""
        print(f"# {title}\n\n{assignees}{body}")
        return

    ensure_label(repo, args.label, token)
    issue = request(
        "POST",
        f"/repos/{repo}/issues",
        token,
        {"title": title, "body": body, "labels": [args.label], "assignees": args.assignee},
    )
    print(f"Created {issue['html_url']}")


if __name__ == "__main__":
    main()
