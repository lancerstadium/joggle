#!/usr/bin/env python3
"""Run one agent session against one compiler arm and record every step.

The agent is a local model behind ollama with a fixed prompt template, a fixed
tool set, and a step cap. Each run works in its own scratch copy, so a failed
attempt cannot damage the tree it is derived from, and every model call, tool
call, and tool result is appended to a JSONL transcript that is kept.

Usage:
    agent_loop.py --arm A --task C1-control-tile-size --run 1 \
                  --workdir <scratch> --out runs/
"""

import argparse
import json
import shutil
import subprocess
import time
import urllib.request
from pathlib import Path

HERE = Path(__file__).resolve().parent
OLLAMA = "http://localhost:11434/api/chat"
MAX_OUTPUT = 4000

SYSTEM = (
    "You are editing a compiler to implement one required change. Work only "
    "inside the given directory. Use the tools to inspect the sources, make the "
    "edit, and check your work. When the change is implemented and verified, "
    "call finish with a one-line summary. Do not ask questions; act."
)

TOOLS = [
    {"type": "function", "function": {
        "name": "read_file", "description": "Read a text file",
        "parameters": {"type": "object", "properties": {"path": {"type": "string"}},
                       "required": ["path"]}}},
    {"type": "function", "function": {
        "name": "write_file", "description": "Write a text file, replacing it",
        "parameters": {"type": "object", "properties": {
            "path": {"type": "string"}, "content": {"type": "string"}},
            "required": ["path", "content"]}}},
    {"type": "function", "function": {
        "name": "list_dir", "description": "List a directory",
        "parameters": {"type": "object", "properties": {"path": {"type": "string"}},
                       "required": ["path"]}}},
    {"type": "function", "function": {
        "name": "run_shell", "description": "Run a shell command in the work directory",
        "parameters": {"type": "object", "properties": {"command": {"type": "string"}},
                       "required": ["command"]}}},
    {"type": "function", "function": {
        "name": "finish", "description": "Declare the change complete",
        "parameters": {"type": "object", "properties": {"summary": {"type": "string"}},
                       "required": ["summary"]}}},
]


def post(payload, timeout=300):
    request = urllib.request.Request(
        OLLAMA, data=json.dumps(payload).encode(),
        headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(request, timeout=timeout) as response:
        return json.loads(response.read())


def inside(work, path):
    candidate = (work / path).resolve()
    if work.resolve() not in candidate.parents and candidate != work.resolve():
        raise ValueError(f"path escapes the work directory: {path}")
    return candidate


def run_tool(work, name, args):
    if name == "read_file":
        target = inside(work, args["path"])
        if not target.is_file():
            return f"error: no such file: {args['path']}"
        return target.read_text(errors="replace")[:MAX_OUTPUT]
    if name == "write_file":
        target = inside(work, args["path"])
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(args["content"])
        return f"wrote {len(args['content'])} bytes to {args['path']}"
    if name == "list_dir":
        target = inside(work, args.get("path", "."))
        if not target.is_dir():
            return f"error: no such directory: {args.get('path', '.')}"
        return "\n".join(sorted(p.name + ("/" if p.is_dir() else "")
                                for p in target.iterdir()))[:MAX_OUTPUT] or "(empty)"
    if name == "run_shell":
        result = subprocess.run(args["command"], shell=True, cwd=work,
                                capture_output=True, text=True, timeout=1800)
        out = (result.stdout + result.stderr)[-MAX_OUTPUT:]
        return f"exit={result.returncode}\n{out}"
    return f"error: unknown tool {name}"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--arm", required=True)
    ap.add_argument("--task", required=True)
    ap.add_argument("--run", type=int, required=True)
    ap.add_argument("--workdir", type=Path, required=True)
    ap.add_argument("--out", type=Path, default=HERE / "runs")
    a = ap.parse_args()

    spec = json.loads((HERE / "tasks.json").read_text())
    agent = spec["agent"]
    task = next(t for t in spec["tasks"] if t["id"] == a.task)
    a.out.mkdir(parents=True, exist_ok=True)

    prompt = (f"Required change:\n\n{task['statement']}\n\n"
              f"The tree you may edit is {a.workdir}.\n"
              f"Success means: {task['success_' + a.arm]}")
    messages = [{"role": "system", "content": SYSTEM},
                {"role": "user", "content": prompt}]
    transcript = a.out / f"{a.task}-{a.arm}-run{a.run}.jsonl"
    started = time.time()
    calls = tokens = rebuilds = 0
    finished = None

    with transcript.open("w") as log:
        log.write(json.dumps({"event": "start", "task": a.task, "arm": a.arm,
                              "run": a.run, "workdir": str(a.workdir),
                              "prompt": prompt}) + "\n")
        for step in range(agent["step_cap"]):
            reply = post({"model": agent["model"], "stream": False,
                          "think": agent.get("think", False),
                          "options": agent["options"], "tools": TOOLS,
                          "messages": messages})
            calls += 1
            tokens += int(reply.get("eval_count") or 0)
            message = reply.get("message", {})
            messages.append(message)
            log.write(json.dumps({"event": "model", "step": step,
                                  "content": message.get("content", ""),
                                  "tool_calls": message.get("tool_calls")}) + "\n")
            calls_now = message.get("tool_calls") or []
            if not calls_now:
                break
            for call in calls_now:
                name = call["function"]["name"]
                args = call["function"].get("arguments") or {}
                if isinstance(args, str):
                    args = json.loads(args)
                if name == "finish":
                    finished = args.get("summary", "")
                    log.write(json.dumps({"event": "finish", "summary": finished}) + "\n")
                    break
                result = run_tool(a.workdir, name, args)
                if name == "run_shell" and any(
                        k in args.get("command", "") for k in ("make", "ninja", "cmake --build")):
                    rebuilds += 1
                log.write(json.dumps({"event": "tool", "step": step, "name": name,
                                      "args": args, "result": result[-MAX_OUTPUT:]}) + "\n")
                messages.append({"role": "tool", "content": str(result)})
            if finished is not None:
                break

    record = {"task": a.task, "arm": a.arm, "run": a.run,
              "finished": finished is not None, "summary": finished,
              "steps": step + 1, "model_calls": calls, "eval_tokens": tokens,
              "rebuild_commands": rebuilds,
              "seconds": round(time.time() - started, 2),
              "transcript": transcript.name}
    (a.out / f"{a.task}-{a.arm}-run{a.run}.json").write_text(
        json.dumps(record, indent=2) + "\n")
    print(json.dumps(record))


if __name__ == "__main__":
    main()
