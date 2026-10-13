#ifndef TESTSUITE_H
#define TESTSUITE_H

#include <vector>
#include <string>
#include <nlohmann/json.hpp>

struct TestCase {
    std::string id, name, description, task_prompt;
    std::vector<std::string> expected_outputs;
    std::vector<nlohmann::json> validation_checks;
    int max_turns;
    int timeout;
};

class TestSuite {
public:
    TestSuite() : cases(default_cases()) {}

    const std::vector<TestCase>& get_cases() const {
        return cases;
    }

private:
    std::vector<TestCase> cases;

    static std::vector<TestCase> default_cases() {
        std::vector<TestCase> cases;

        cases.push_back({
            "T1", "Automated Debugging via Log Analysis",
            "Automated Debugging via Log Analysis",
            "The file memory_leak.cpp already exists and contains a segfault bug. "
            "1. Execute the file to observe the crash. 2. Read execution_traceback.log. "
            "3. Fix the bug so the file runs and exits with code 0. Do NOT simply replace it with unrelated code.",
            {"memory_leak.cpp"},
            {{{"type", "execution"}, {"file", "memory_leak.cpp"}}},
            6, 120
        });

        cases.push_back({
            "T2", "Concurrent Hashing",
            "Parallel processing with concurrent.futures",
            "Create concurrent_hash.py scanning for .txt/.py files, calculate SHA-256 in parallel. "
            "Store {filename: hash} dict. Print execution time and verify count matches.",
            {"concurrent_hash.py"},
            {{{"type", "execution"}, {"file", "concurrent_hash.py"}, {"expect", "SHA-256"}}},
            4, 120
        });

        cases.push_back({
            "T3", "Multi-Language Data Bridge",
            "Multi-Language Data Bridge",
            "Create a C++ program named generator.cpp that simulates 10^6 particle interactions and saves "
            "the state to a data.json file using the nlohmann/json library. Subsequently, create a Python "
            "script analyzer.py that reads this JSON file, calculates the mean energy of the particles, and "
            "generates a summary report. This tests the agent ability to manage dependencies across different "
            "languages and ensure data consistency between processes.",
            {"generator.cpp", "analyzer.py"},
            {{{"type", "execution"}, {"file", "analyzer.py"}, {"expect", "Mean"}}},
            6, 120
        });

        cases.push_back({
            "T4", "Concurrent Data Processing with Thread Safety",
            "Concurrent Data Processing with Thread Safety",
            "Create a CSV file named \"data.csv\" with 10,000 rows of random integers. Implement a C++ "
            "program \"data_processor.cpp\" that reads this file, processes it in parallel using threads, "
            "and outputs the processed results to \"output.json\". Ensure thread safety with appropriate "
            "synchronization mechanisms.",
            {"data_processor.cpp", "output.json"},
            {{{"type", "execution"}, {"file", "data_processor.cpp"}, {"expect", "Processed"}}},
            8, 120
        });

        cases.push_back({
            "T5", "Secure Input Validation",
            "Secure Input Validation",
            "Create a single Python script named secure_query.py that demonstrates secure SQLite querying. "
            "The script MUST: (1) Create an SQLite database file and a 'users' table with at least one row "
            "(e.g., name='John Doe'), (2) Read lines from stdin in a loop until 'exit' is received, "
            "(3) Use parameterized queries to search for users by name, (4) Print 'Query successful' "
            "when a query executes without error, (5) Attempt a common SQL injection attack on itself "
            "and show the attack is blocked. The script will be executed with stdin 'John Doe\\nexit\\n' "
            "and the output MUST contain 'Query successful'.",
            {"secure_query.py"},
            {{{"type", "execution"}, {"file", "secure_query.py"},
              {"input", "John Doe\nexit\n"}, {"expect", "Query successful"}}},
            5, 120
        });

        cases.push_back({
            "T6", "Monte Carlo Integration",
            "Probabilistic numerical methods",
            "Create a c++ program monte_carlo.cpp to estimate ∫sin(x)dx from 0 to π using 1,000,000 random points. "
            "Compare estimate to analytical value 2. Print absolute error.",
            {"monte_carlo.cpp"},
            {{{"type", "execution"}, {"file", "monte_carlo.cpp"}, {"expect", "Absolute error"}}},
            5, 120
        });

        cases.push_back({
            "T7", "Import Restructuring",
            "Handle directory moves and import updates",
            "1. Create math_lib/operations.py with power(a,b) function. Create app.py importing it and "
            "printing 2**10. 2. Move operations.py to core/utils/, update imports, verify output still 1024.",
            {"app.py", "core/utils/operations.py"},
            {{{"type", "execution"}, {"file", "app.py"}, {"expect", "1024"}}},
            8, 120
        });

        cases.push_back({
            "T8", "Vending Machine OOP",
            "Custom exceptions and state management",
            "Create vending.py with VendingMachine class and InsufficientFundsError exception. Demonstrate: "
            "deposit $2.00, try buying a $2.50 item, catch the InsufficientFundsError, and print the EXACT text "
            "'InsufficientFundsError' (this exact string is required for validation). Then deposit $1 more and "
            "buy successfully. Finally print the remaining balance and inventory.",
            {"vending.py"},
            {{{"type", "execution"}, {"file", "vending.py"}, {"expect", "InsufficientFundsError"}}},
            6, 120
        });

        cases.push_back({
            "T9", "Pi Approximation",
            "Numerical methods and convergence",
            "Create pi_approx.py using Leibniz formula: π = 4 * Σ((-1)^n/(2n+1)). Iterate until "
            "|approximation - math.pi| < 10^-5. Print iterations needed.",
            {"pi_approx.py"},
            {{{"type", "execution"}, {"file", "pi_approx.py"}, {"expect", "iterations"}}},
            4, 120
        });

        cases.push_back({
            "T10", "Grid BFS Pathfinding",
            "Graph traversal and obstacle handling",
            "Create grid_bfs.py with a 10x10 grid containing some obstacles. Implement BFS from (0,0) to (9,9). "
            "Print the path using the EXACT text 'coordinates' (for example: 'Path coordinates: [...]') and the step count. "
            "If no path exists, print 'No path found.'.",
            {"grid_bfs.py"},
            {{{"type", "execution"}, {"file", "grid_bfs.py"}, {"expect", "coordinates"}}},
            4, 120
        });

        cases.push_back({
            "T11", "SQLite Relational Query",
            "Complex JOINs and subqueries",
            "Create SQLite database with Departments and Employees tables. Insert 3 departments, 10 employees. Query: \n"
            "find employees earning more than their dept average. Save to high_earners.json. Use proper foreign keys.\n",
            {"high_earners.json"},
            {{{"type", "file_exists"}, {"path", "high_earners.json"}}},
            8, 120
        });

        cases.push_back({
            "T12", "Memoization Decorator",
            "Higher-order functions and performance",
            "Create fibonacci_memoized.py with memoize decorator and apply to recursive Fibonacci. "
            "Calculate F(50) with timing. Compare to F(30) without decorator.",
            {"fibonacci_memoized.py"},
            {{{"type", "execution"}, {"file", "fibonacci_memoized.py"}, {"expect", "F(50)"}}},
            6, 120
        });

        cases.push_back({
            "T13", "Cross-File Refactoring",
            "Cross-File Refactoring",
            "Task the agent with renaming a specific class or utility function in a multi-file project. "
            "For example, it must create a directory src/math, move several mathematical functions from a "
            "single utils.py into separate modules, and then update the import statements in an app.py "
            "located in the root directory. This evaluates the efficiency of the FileManager in handling "
            "path updates and recursive directory searches.",
            {"app.py", "utils.py"},
            {{{"type", "execution"}, {"file", "app.py"}, {"expect", "power"}}},
            4, 120
        });

        cases.push_back({
            "T14", "Data Cleaning Pipeline",
            "Handle dirty CSV data with type validation",
            "Create raw_data.csv (Name, Age, Salary) with 6 rows including non-numeric ages/empty salaries. "
            "Create cleaner.py to filter invalid rows, calculate average salary, save to processed_data.json.",
            {"raw_data.csv", "cleaner.py", "processed_data.json"},
            {{{"type", "file_exists"}, {"path", "processed_data.json"}}},
            6, 120
        });

        cases.push_back({
            "T15", "Networked Resource Fetching",
            "Networked Resource Fetching",
            "Require the agent to use the curl capability to interface with a public API to retrieve "
            "current weather or financial data. The agent must then use openssl/md5 to create a unique "
            "checksum of the response to verify data integrity before processing. This evaluates the "
            "ability to handle external libraries and asynchronous-like behavior within the execution environment.",
            {"api_fetch.cpp"},
            {{{"type", "execution"}, {"file", "api_fetch.cpp"}, {"expect", "Checksum"}}},
            4, 120
        });

        cases.push_back({
            "T16", "LRU Cache with TTL",
            "Data structures and time-bound expiration",
            "Implement an LRU (Least Recently Used) cache with TTL (Time-To-Live) in lru_cache.py. "
            "Requirements: O(1) get and put. Entries expire automatically after TTL seconds. "
            "Must support: get(key), put(key, value, ttl), size(). Print demonstration showing "
            "eviction due to capacity and expiration due to TTL.",
            {"lru_cache.py"},
            {
                {{"type", "execution"}, {"file", "lru_cache.py"}, {"expect", "LRU"}},
                {{"type", "execution"}, {"file", "lru_cache.py"}, {"expect", "expired"}},
                {{"type", "execution"}, {"file", "lru_cache.py"}, {"expect", "evicted"}},
                {{"type", "execution"}, {"file", "lru_cache.py"}, {"expect", "O(1)"}}
            },
            6, 120
        });

        cases.push_back({
            "T17", "Topological Sort with Cycle Detection",
            "Graph algorithms and error handling",
            "Create topo_sort.py that reads a DAG from edges list and outputs a valid topological ordering. "
            "If a cycle is detected, raise CycleError with message 'Graph contains a cycle'. "
            "Use Kahn's algorithm (BFS-based). Test with: A->B, A->C, B->D, C->D.",
            {"topo_sort.py"},
            {
                {{"type", "execution"}, {"file", "topo_sort.py"}, {"expect", "Topological"}},
                {{"type", "execution"}, {"file", "topo_sort.py"}, {"expect", "CycleError"}}
            },
            6, 120
        });

        cases.push_back({
            "T18", "Mini Regex Engine",
            "Finite automata and string algorithms",
            "Create regex_engine.py implementing a basic regex matcher supporting: . (any char), * (zero+ of prev), "
            "| (alternation), and literal characters. Do NOT use re module. Implement match(pattern, text) returning bool. "
            "Demonstrate: match('a*b', 'aaab')==True, match('a.b', 'acb')==True, match('a|b', 'b')==True.",
            {"regex_engine.py"},
            {
                {{"type", "execution"}, {"file", "regex_engine.py"}, {"expect", "True"}},
                {{"type", "execution"}, {"file", "regex_engine.py"}, {"expect", "False"}}
            },
            6, 120
        });

        cases.push_back({
            "T19", "Producer-Consumer with Backpressure",
            "Advanced concurrency and queue management",
            "Create producer_consumer.py with bounded queue (maxsize=5). Producer generates 20 items. "
            "Consumer processes items. If queue full, producer blocks. If queue empty, consumer blocks. "
            "Use threading.Condition for synchronization. Print 'produced X' and 'consumed X' for each item. "
            "Final output must contain 'Done: 20 produced, 20 consumed'.",
            {"producer_consumer.py"},
            {
                {{"type", "execution"}, {"file", "producer_consumer.py"},
                 {"expect", "Done: 20 produced, 20 consumed"}},
                {{"type", "execution"}, {"file", "producer_consumer.py"}, {"expect", "produced"}},
                {{"type", "execution"}, {"file", "producer_consumer.py"}, {"expect", "consumed"}}
            },
            6, 120
        });

        cases.push_back({
            "T20", "JSON Parser from Scratch",
            "Recursive descent parsing and tokenization",
            "Create json_parser.py that parses a JSON string WITHOUT using json module. Support: objects, arrays, "
            "strings, numbers, booleans, null. Return native Python dict/list/str/int/float/None. "
            "Demonstrate parsing '{\"name\": \"Alice\", \"age\": 30, \"active\": true, \"nested\": {\"key\": null}}'. "
            "Print parsed['name'] == 'Alice'.",
            {"json_parser.py"},
            {
                {{"type", "execution"}, {"file", "json_parser.py"}, {"expect", "Alice"}},
                {{"type", "execution"}, {"file", "json_parser.py"}, {"expect", "null"}},
                {{"type", "execution"}, {"file", "json_parser.py"}, {"expect", "nested"}}
            },
            6, 120
        });

        cases.push_back({
            "T21", "Streaming Median Calculator",
            "Heaps and online algorithms",
            "Create streaming_median.py that maintains median of a stream using two heaps (max-heap for lower half, "
            "min-heap for upper half). Implement add(num) and get_median(). Demonstrate with stream: [5, 15, 1, 3, 8, 7, 9, 10]. "
            "After each insertion, print current median. Final output must show medians: 5.0, 10.0, 5.0, 4.0, 5.0, 6.0, 7.0, 7.5.",
            {"streaming_median.py"},
            {
                {{"type", "execution"}, {"file", "streaming_median.py"}, {"expect", "7.5"}},
                {{"type", "execution"}, {"file", "streaming_median.py"}, {"expect", "5.0"}},
                {{"type", "execution"}, {"file", "streaming_median.py"}, {"expect", "10.0"}},
                {{"type", "execution"}, {"file", "streaming_median.py"}, {"expect", "O(log n)"}}
            },
            6, 120
        });

        cases.push_back({
            "T22", "Template Engine",
            "String interpolation and control flow",
            "Create template_engine.py with a simple template renderer. Support: {{var}} variable substitution, "
            "{% for item in items %}...{% endfor %} loops, and {% if condition %}...{% endif %} conditionals. "
            "No external template libraries. Render: 'Hello {{name}}! You have {{count}} messages.' "
            "with name='Alice', count=5. Output must contain 'Hello Alice! You have 5 messages.'",
            {"template_engine.py"},
            {
                {{"type", "execution"}, {"file", "template_engine.py"},
                 {"expect", "Hello Alice! You have 5 messages."}},
                {{"type", "execution"}, {"file", "template_engine.py"}, {"expect", "for"}},
                {{"type", "execution"}, {"file", "template_engine.py"}, {"expect", "if"}}
            },
            6, 120
        });

        cases.push_back({
            "T23", "Token Bucket Rate Limiter",
            "Algorithm design and time-based state",
            "Create rate_limiter.py implementing a token bucket algorithm. Class TokenBucket with capacity and refill_rate. "
            "allow_request(tokens_needed) returns True if tokens available, else False. Tokens refill over time. "
            "Demonstrate: bucket=capacity=10, rate=1/sec. Request 5 (True), request 7 (False), wait 2s, request 7 (True). "
            "Output must show 'Allowed: True/False' pattern matching expected behavior.",
            {"rate_limiter.py"},
            {
                {{"type", "execution"}, {"file", "rate_limiter.py"}, {"expect", "Allowed"}},
                {{"type", "execution"}, {"file", "rate_limiter.py"}, {"expect", "False"}}
            },
            6, 120
        });

        cases.push_back({
            "T24", "Sudoku Solver with Constraint Propagation",
            "Backtracking and constraint satisfaction",
            "Create sudoku_solver.py that solves 9x9 Sudoku using backtracking + constraint propagation (forward checking). "
            "Input is a grid with 0 for empty cells. Print solved grid. Must solve hard puzzle in <2 seconds. "
            "Puzzle: [[5,3,0,0,7,0,0,0,0],[6,0,0,1,9,5,0,0,0],[0,9,8,0,0,0,0,6,0],[8,0,0,0,6,0,0,0,3],[4,0,0,8,0,3,0,0,1],[7,0,0,0,2,0,0,0,6],[0,6,0,0,0,0,2,8,0],[0,0,0,4,1,9,0,0,5],[0,0,0,0,8,0,0,7,9]]",
            {"sudoku_solver.py"},
            {
                {{"type", "execution"}, {"file", "sudoku_solver.py"}, {"expect", "Solved"}},
                {{"type", "execution"}, {"file", "sudoku_solver.py"}, {"expect", "8"}},
                {{"type", "execution"}, {"file", "sudoku_solver.py"}, {"expect", "2"}}
            },
            6, 120
        });

        cases.push_back({
            "T25", "Custom Event Loop",
            "Async primitives and callback scheduling",
            "Create event_loop.py implementing a minimal async event loop from scratch. No asyncio module. "
            "Support: call_soon(callback), run_until_complete(), and basic sleep(delay) coroutine. "
            "Demonstrate scheduling 3 tasks that print 'Task N done' after staggered delays. "
            "Output must contain 'Task 1 done', 'Task 2 done', 'Task 3 done' in correct order.",
            {"event_loop.py"},
            {
                {{"type", "execution"}, {"file", "event_loop.py"}, {"expect", "Task 3 done"}},
                {{"type", "execution"}, {"file", "event_loop.py"}, {"expect", "Task 1 done"}},
                {{"type", "execution"}, {"file", "event_loop.py"}, {"expect", "Task 2 done"}}
            },
            6, 120
        });

        return cases;
    }
};

#endif
