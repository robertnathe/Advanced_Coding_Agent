<div align="center">

# 🧠 Frontier Coding Agent

An autonomous C++ coding agent that plans, writes, executes, validates, and repairs code using multiple LLM backends.

[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.cppreference.com/w/cpp/20)
[![Platform](https://img.shields.io/badge/platform-Linux-lightgrey.svg)](#)
[![Docker](https://img.shields.io/badge/sandbox-Docker-2496ED?logo=docker&logoColor=white)](#docker-sandbox-recommended)
[![License](https://img.shields.io/badge/license-Research%20%2F%20Educational-orange.svg)](#license--usage-terms)

[Quick Start](#-quick-start) · [How It Works](#-how-it-works) · [Architecture](#-architecture) · [Configuration](#%EF%B8%8F-configuration) · [Safety](#%EF%B8%8F-safety--operational-considerations)

</div>

---

`FrontierCodingAgent.cpp` combines validation-driven planning, reusable solution memory, and a closed-loop repair process. It is a research-oriented agent designed around reliable tool use, deterministic recovery, and validation of generated artifacts.

> [!WARNING]
> Security: The agent can modify files, run shell commands, install packages, and execute generated code. Use the provided **Docker sandbox** for untrusted tasks. The sandbox mounts the current directory, so use a dedicated working directory without sensitive files.

---

## 📑 Table of Contents

- [Quick Start](#-quick-start)
  - [1. Configure an LLM Provider](#1-configure-an-llm-provider)
  - [2. Choose How to Run](#2-choose-how-to-run)
- [Running Tasks](#-running-tasks)
- [Configuration](#%EF%B8%8F-configuration)
- [Capabilities](#-capabilities)
- [How It Works](#-how-it-works)
- [Architecture](#-architecture)
- [Testing](#-testing)
- [Safety and Operational Considerations](#%EF%B8%8F-safety--operational-considerations)
- [Design Principles](#-design-principles)
- [License & Usage Terms](#license--usage-terms)

---

## 🚀 Quick Start

Run the following commands from the project directory.

### 1. Configure an LLM Provider

Set at least one provider API key in your environment:

```bash
# Set one or more of these variables with your own credentials.
export GROQ_API_KEY="your-groq-api-key"
export OPENROUTER_API_KEY="your-openrouter-api-key"
export GOOGLE_API_KEY="your-google-api-key"
```

> [!TIP]
> Never commit API keys to source control. Prefer a `.env` loader or your shell's secret manager.

### 2. Choose How to Run

#### 🐳 Docker Sandbox (recommended)

With Docker installed:

```bash
chmod +x run_cpp_safely_agent.sh
./run_cpp_safely_agent.sh
```

The runner builds the sandbox image when needed and reuses it when available. To force a rebuild:

```bash
./run_cpp_safely_agent.sh --rebuild
```

<details>
<summary><b>What the sandbox gives you</b></summary>

- Ubuntu 24.04 base with required libraries
- Runs as UID 1000
- Drops all Linux capabilities
- Enables `no-new-privileges`
- Applies memory, CPU, and process limits
- Provides an executable `/tmp` tmpfs (needed by the `compile_cpp` tool)

</details>

#### 💻 Local Build

> [!CAUTION]
> Local execution runs outside the provided container protections.

Requirements

- A C++20 compiler (`g++` 10 or newer recommended)
- `libcurl`
- `nlohmann-json`
- `yaml-cpp`
- OpenSSL / crypto libraries

Option A — Makefile (recommended):

```bash
make
```

Option B — Direct compilation:

```bash
g++ -std=c++20 -O2 -Wall FrontierCodingAgent.cpp -o output \
    -lcurl -lyaml-cpp -lcrypto -pthread
```

Both build paths produce the `output` executable:

```bash
./output
```

---

## 🎯 Running Tasks

The agent provides an interactive menu:

| Option | Action |
| :---: | --- |
| `1` | Run a single task |
| `2` | Run the built-in test suite |
| `3` | View semantic-memory statistics |
| `4` | Propose self-improvements |
| `5` | Quit |

### Submit a Task

Choose option `1` and provide:

1. A task description
2. Expected output files, separated by commas
3. Optional runtime validation checks, including execution expectations and standard input
4. The maximum number of turns

> [!IMPORTANT]
> Use validation checks to define observable success. File creation alone does not establish that generated code behaves correctly.

---

## ⚙️ Configuration

### Provider Credentials

At least one provider API key is required.

| Variable | Provider |
| --- | --- |
| `GROQ_API_KEY` | Groq |
| `OPENROUTER_API_KEY` | OpenRouter |
| `GOOGLE_API_KEY` | Google Generative Language API |

### Docker Resource Limits

| Variable | Purpose | Default |
| --- | --- | --- |
| `MEMORY_LIMIT` | Container memory limit | `4g` |
| `CPU_LIMIT` | Container CPU limit | `2` |
| `PIDS_LIMIT` | Maximum number of processes | `256` |
| `TMP_SIZE` | Size of the executable `/tmp` tmpfs | _not set_ |

Example:

```bash
MEMORY_LIMIT=4g CPU_LIMIT=2 PIDS_LIMIT=256 ./run_cpp_safely_agent.sh
```

---

## ✨ Capabilities

<table>
<tr>
<td valign="top" width="50%">

**🔌 Multi-Provider LLM Support**

Native clients for Google, OpenRouter, and Groq with:

- Automatic fallback
- Health tracking
- Rate limiting
- Quota classification
- Structured JSON mode when available

</td>
<td valign="top" width="50%">

**🧭 Validation-Driven Planning**

Plans account for expected files and runtime checks. Failed checks become counterexamples for targeted repairs.

</td>
</tr>
<tr>
<td valign="top">

🔁 Closed-Loop Repair

- Revalidates after plan execution or individual actions
- Restores backups when failure counts increase
- Generates recovery actions

</td>
<td valign="top">

🧠 Semantic Memory & Replay

Successful plans and artifacts can be retrieved for similar tasks. High-reward cached solutions can bypass the LLM loop.

</td>
</tr>
<tr>
<td valign="top">

🛠️ File & Execution Tools

`write_file`, `run_shell_command`, `compile_cpp`, `execute_file`, `install_package`, plus backup/restore. Actions are normalized and **deduplicated** to reduce redundant calls.

</td>
<td valign="top">

📈 Progress Monitoring

The critic tracks written files, validation state, no-progress turns, action penalties, and the minimum observed failure count — triggering recovery when progress stalls.

</td>
</tr>
<tr>
<td valign="top" colspan="2">

🧬 Self-Improvement Proposals — the self-modification engine analyzes the agent's source and proposes architectural improvements.

</td>
</tr>
</table>

---

## 🔄 How It Works

The main loop combines memory retrieval, planning, execution, and validation:

```text
   ┌──────────────┐
   │ 1. Retrieve  │  reusable plans & artifacts from semantic memory
   └──────┬───────┘
          ▼
   ┌──────────────┐
   │ 2. Plan      │  generate & execute plan conditioned on validation
   └──────┬───────┘
          ▼
   ┌──────────────┐
   │ 3. Evaluate  │  run generated artifacts & runtime checks
   └──────┬───────┘
          ▼
   ┌──────────────┐
   │ 4. Repair    │  failures → structured tool calls & targeted fixes
   └──────┬───────┘
          ▼
   ┌──────────────┐
   │ 5. Recover   │  restore backups on regression; deterministic recovery
   └──────┬───────┘
          ▼
   ┌──────────────┐
   │ 6. Exit      │  stop early when all validation checks pass
   └──────────────┘
```

This repair process follows a Counterexample-Guided Inductive Synthesis (CEGIS) style: failed validations inform the next repair rather than merely ending the attempt.

---

## 🏗️ Architecture

```text
CodingAgent
│
├── LLM Layer
│   └── LLMBackendManager
│       ├── Groq / OpenRouter / Google
│       └── Health tracking, fallback, and rate limiting
│
├── Memory & Learning
│   └── SemanticMemoryManager
│       ├── Plan and artifact storage
│       └── Similarity retrieval and replay
│
├── Planning
│   ├── AgentPlanner
│   │   └── Validation-conditioned planning, scoring, and repair
│   ├── DependencyPlanner
│   │   └── Dependency-aware plan generation
│   └── TaskDecomposer
│       └── Plan decomposition into subgoals
│
├── Execution
│   └── ToolExecutor
│       └── FileManager
│           ├── Write, backup, and restore
│           └── Shell, compile, execute, and install
│
├── Critique & Control
│   └── AgentCritic
│       ├── Validation cache and progress tracking
│       ├── Recovery action synthesis
│       └── Duplicate-action guarding
│
└── Self-Improvement
    └── SelfModificationEngine
        └── Source analysis and improvement proposals
```

---

## 🧪 Testing

Launch the agent, choose menu option `2`, and enter specific test IDs or `all` to run the built-in `TestSuite`.

Each test case defines:

- 🎯 A task prompt
- 📁 Expected output files
- ✅ Execution checks and expected output substrings
- ⏱️ A maximum turn count and timeout

---

## ⚠️ Safety & Operational Considerations

> [!WARNING]
> Treat everything the agent produces as untrusted until reviewed.

- Sandbox first. Use the Docker runner for untrusted tasks rather than executing the agent directly on your host.
- Mount awareness. The mounted working directory is **writable by the agent**. Container isolation does not protect files intentionally exposed through that mount.
- Secrets hygiene. Keep API keys in environment variables; never hard-code or commit them.
- Backups aren't VCS. The agent takes file backups before overwriting, but do not treat them as a substitute for version control or independent backups.
- Review outputs. Review generated code, shell commands, and self-improvement proposals before trusting their effects.
- Provider ToS. Follow the terms of service of every LLM provider you configure.

---

## 📐 Design Principles

| Principle | Rationale |
| --- | --- |
| One primary action per turn | Expressed as a structured tool call — keeps trajectories auditable |
| Preserve correctness on solved cases | Prevents regressions during repair |
| Structural > micro-optimizations | Self-modification favors asymptotic improvements |
| Deterministic recovery | Invoked when LLM output is empty or progress stalls |
| Validate before declaring success | Expected files must exist and runtime checks must pass |
| Account for quotas | Provider rate limits are modeled explicitly |

---

## 📜 License & Usage Terms

This project is provided as research / educational software.

> Use at your own risk. Ensure you comply with the terms of service of any LLM provider you connect.

<div align="center">
<sub>Built for research on autonomous, validation-driven code synthesis.</sub>
</div>
