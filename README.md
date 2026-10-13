Advanced Coding Agent is a sophisticated, self-contained C++ AI coding agent that solves complex programming tasks by interleaving LLM reasoning with real tool execution (file I/O, compilation, shell commands, validation).

It features multi-provider LLM routing, robust tool-call parsing, semantic memory, planning, recovery mechanisms, and a 25-case automated test suite.

Features

- Multi-Backend LLM Orchestration  
  Supports Groq, OpenRouter, and Google Gemini with intelligent backend selection (UCB-style scoring), per-model health tracking, cooldowns, rate limiting, and automatic fallbacks.

- Structured Tool Calling  
  Communicates exclusively via JSON tool calls. Available tools:
  - `write_file`
  - `read_file`
  - `execute_file`
  - `compile_cpp`
  - `run_shell_command`
  - `make_directory`
  - `finish`

- Robust Parsing & Repair  
  Handles malformed JSON, truncated outputs, `<think>` tags, unescaped newlines, and common LLM formatting quirks through multi-stage extraction and sanitization.

- Semantic Memory  
  BM25-style retrieval over past successful solutions and failure patterns. Exact-match caching + dynamic thresholds. Persists to disk.

- Planning & Decomposition  
  Task decomposer and dependency planner that produce executable action sequences. Plan scoring against expected outputs + validation checks.

- Validation Engine  
  Supports file-existence checks and execution checks (with expected stdout substrings and stdin). Aggressive caching of compile/execute results keyed by workspace state.

- Anti-Loop & Recovery  
  Duplicate-action detection, semantic fingerprinting, ping-pong prevention, no-progress recovery actions, and deterministic recovery when the LLM fails to produce a valid tool call.

- Comprehensive Test Suite  
  25 carefully designed test cases covering debugging, concurrency, multi-language pipelines, algorithms, security, data cleaning, graph algorithms, caching, parsing, and more.

Architecture Overview

```
CodingAgent
├── LLMBackendManager          (routing, health, cooldowns)
├── SemanticMemoryManager      (BM25 retrieval + persistence)
├── TaskDecomposer
├── DependencyPlanner
├── ToolExecutor
│   ├── CodeExecutor           (compile + run with caching)
│   └── FileManager
└── Validation & Recovery logic
```

Core source files:

| File                        | Responsibility                              |
|----------------------------|---------------------------------------------|
| `Advanced_Coding_Agent_*.cpp` | Main agent loop, `CodingAgent` class, CLI  |
| `agent_backend.hpp`         | LLM clients + intelligent router            |
| `agent_tools.hpp`           | Tools, memory, planner, executor            |
| `agent_utils.hpp`           | JSON repair, sanitization, system prompt    |
| `TestSuite.h`               | 25 evaluation tasks                         |

Requirements

- C++20 compiler (`g++`)
- `libcurl`
- `nlohmann/json` (header-only)
- Optional: `yaml-cpp`, OpenSSL (for some test cases)

On Ubuntu/Debian:

```bash
sudo apt update
sudo apt install g++ libcurl4-openssl-dev nlohmann-json3-dev libssl-dev
```

---

Building

```bash
# Adjust the source name in the Makefile if needed
# (currently points at Advanced_Coding_Agent_J.cpp)
make
```

Or manually:

```bash
g++ -std=c++20 -O2 -Wall \
    Advanced_Coding_Agent_00.cpp \
    -o advanced_coding_agent \
    -lcurl -lcrypto
```

Configuration

Set at least one API key:

```bash
export GROQ_API_KEY="gsk_..."
export OPENROUTER_API_KEY="sk-or-..."
export GOOGLE_API_KEY="AIza..."
```

The agent prefers the backends in the order configured in the `CodingAgent` constructor (OpenRouter → Groq → Google by default in the provided source).

Usage

```bash
./advanced_coding_agent
```

Interactive menu:

1. Single task – enter a natural-language task, optional expected output files, and validation checks.
2. Run tests – execute any subset (or all) of the 25 built-in evaluation cases.
3. Stats – show semantic memory statistics.
4. Quit

Example single-task flow:

```
Enter task description: Create a Python script that computes the 50th Fibonacci number with memoization and prints the result and timing.
Expected output files (comma-separated, optional): fibonacci_memoized.py
Add validation checks? (y/n): y
Expected output substring for fibonacci_memoized.py: F(50)
Max turns (default 6): 6
```

Test Suite Highlights

The agent is evaluated on tasks such as:

- Automated debugging from crash logs
- Concurrent hashing / producer-consumer
- Multi-language (C++ → JSON → Python) pipelines
- Secure SQLite querying (parameterized + injection resistance)
- Monte-Carlo integration, streaming median, LRU+TTL cache
- Topological sort with cycle detection
- Mini regex engine, JSON parser from scratch
- Sudoku solver with constraint propagation
- Custom event loop, token-bucket rate limiter
- Template engine, vending machine OOP, etc.

Each test specifies expected files and runtime validation substrings that must appear in stdout.

Design Philosophy

- Correctness first – never call `finish` until validations pass.
- Resilience – extensive recovery paths when the LLM produces invalid or incomplete actions.
- Efficiency – aggressive caching of compilation, execution, and validation results; semantic memory to avoid re-solving similar tasks.
- Observability – clear per-turn logging of chosen actions, observations, and recovery decisions.

Limitations & Future Work

- Currently single-process / local filesystem only.
- Tool set is deliberately minimal (no network tools beyond what the agent itself can invoke via shell).
- Memory is local and not shared across machines.
- Potential improvements: vector embeddings, hierarchical planning, multi-agent collaboration, richer sandboxing.

License

This project is provided for research and educational purposes.

Built as a frontier-style autonomous coding agent with strong emphasis on reliability, recovery, and measurable progress.
