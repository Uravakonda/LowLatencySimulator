## LOW-LATENCY C++ ORDER BOOK SIMULATOR/TRADING SIMULATOR

### REQUIREMENTS:
- C++17 Compiler
- CMake (v3.15+)
- IDE (Recommended): CLion
- Manual Dependency: requires concurrentqueue.h header file and it must be placed in the 'include' folder

### HOW TO BUILD AND RUN ON CLION:
1. Open Project - Open LowLatencySim folder in CLion
2. Find the build configuration switcher, and switch Debug to Release (enables -03 compiler optimisations for low-latency performance. Debug mode is 100-1000x slower)
3. Click the Build button
4. Click the run button to start the simulation

### CORE DESIGN
- Use of Multithreading:
  - Architecture is Multi-Producer Single-Consumer (MPSC)
  - Multiple producer_thread instances are launched, and act like independent clients
  - One consumer_thread is launched, which is the matching engine. It pulls orders one-by-one and matches against OrderBook
- Use of Lock-Free Structures:
  - Connects producers and consumer using lock-free queue
  - moodycamel::ConcurrentQueue is used, it is a header-only lock-free queue
  - Uses low-level atomic CPU instructions to manage internal state, so all producer threads can concurrently add items to the queue without blocking each other

### LATENCY STATISTICS
- Total Orders: The total number of orders processed
- Mean: The average latency
- Min: The latency of single fastest order
- Median: The 50th percentile
- p90/p99: The 90th and 99th percentiles
- Max: The latency of single slowest order

### CHANGING PARAMETERS
- Parameters are located at the top of the main() function in main.cpp
- NUM_PRODUCER_THREADS: higher value = more clients and more load on the system
- SIMULATION_DURATION_SECONDS: Higher value = longer simulation and more stable average and processes more orders
