#include <iostream>
#include <thread> //std::thread for multithreading
#include <vector> //stores threads and latency data
#include <map>
#include <chrono>
#include <atomic>
#include <random>
#include <numeric>
#include <algorithm>
#include <iomanip>
#include "concurrentqueue.h" //header file for lock-free queue fetched with CMake

using Price = int;
using Quantity = int;
using OrderID = uint64_t;
using Timestamp = std::chrono::time_point<std::chrono::high_resolution_clock>;

//Defining an order below
enum class Side { BUY, SELL };

struct Order {
    OrderID id;
    Side side;
    Price price;
    Quantity quantity;
    //key times for latency time tracking below
    Timestamp timestamp_produce;   //order created by producer
    Timestamp timestamp_consume;   //time when matching engine dequeued the order
    Timestamp timestamp_processed; //time when matching engine finished processing
};


//this queue is the central lock-free structure (no blocks will occur)
moodycamel::ConcurrentQueue<Order> order_queue;

std::atomic<bool> running{true};
std::atomic<uint64_t> global_order_id{0};
//stores data of latency times
std::vector<long long> end_to_end_latencies_ns;

//Order Book Class below
//accessed by one thread only, the matching engine
class OrderBook {
public:
    void process_order(Order& order) {
        if (order.side == Side::BUY) {
            match_buy(order);
        } else {
            match_sell(order);
        }
    }
    //function to output the current top-of-book
    void print_top_of_book() const {
        std::cout << "--- Top of Book ---\n";
        if (bids.empty()) {
            std::cout << "BIDS: [EMPTY]\n";
        } else {
            std::cout << "BIDS: " << bids.rbegin()->second << " @ " << bids.rbegin()->first << "\n";
        }
        if (asks.empty()) {
            std::cout << "ASKS: [EMPTY]\n";
        } else {
            std::cout << "ASKS: " << asks.begin()->second << " @ " << asks.begin()->first << "\n";
        }
        std::cout << "-------------------\n";
    }
private:
    //sort bids from highest to lowest price
    std::map<Price, Quantity> bids;
    //gets sorted from lowest to highest
    std::map<Price, Quantity> asks;

    void add_to_book(Price price, Quantity quantity, Side side) {
        if (side == Side::BUY) {
            bids[price] += quantity;
        } else {
            asks[price] += quantity;
        }
    }

    void match_buy(Order& buy_order) {
        for (auto ask_iter = asks.begin(); ask_iter != asks.end() && buy_order.quantity > 0; ) {
            Price ask_price = ask_iter->first; //finds best ask by iterating from lowest
            Quantity& ask_quantity = ask_iter->second;
            //if buy price is not high enough then stop matching
            if (buy_order.price < ask_price) {
                break;
            }

            Quantity matched_quantity = std::min(buy_order.quantity, ask_quantity);
            buy_order.quantity -= matched_quantity;
            ask_quantity -= matched_quantity;
            //if the ask level is filled then remove it
            if (ask_quantity == 0) {
                ask_iter = asks.erase(ask_iter);
            } else {
                ++ask_iter;
            }
        }
        //if quantity remains, add it to the bid book
        if (buy_order.quantity > 0) {
            add_to_book(buy_order.price, buy_order.quantity, Side::BUY);
        }
    }

    void match_sell(Order& sell_order) {
        //iterate through bids from highest price (best bid)
        for (auto bid_iter = bids.rbegin(); bid_iter != bids.rend() && sell_order.quantity > 0; ) {
            Price bid_price = bid_iter->first;
            Quantity& bid_quantity = bid_iter->second;
            //if sell price is not low enough then stop matching
            if (sell_order.price > bid_price) {
                break;
            }
            Quantity matched_quantity = std::min(sell_order.quantity, bid_quantity);
            sell_order.quantity -= matched_quantity;
            bid_quantity -= matched_quantity;
            //if bid level is fully filled then remove it
            if (bid_quantity == 0) {
                bid_iter = std::map<Price, Quantity>::reverse_iterator(bids.erase(std::next(bid_iter).base()));
            } else {
                ++bid_iter;
            }
        }
        //if quantity remains then add it to the ask book
        if (sell_order.quantity > 0) {
            add_to_book(sell_order.price, sell_order.quantity, Side::SELL);
        }
    }
};
//Producer Thread Function to simulate client sending orders
void producer_thread(int thread_id) {
    std::cout << "Producer thread " << thread_id << " started.\n";
    //each thread gets its own random number generator
    std::mt19937 gen(std::random_device{}() + thread_id);
    std::uniform_int_distribution<> price_dist(95, 105);
    std::uniform_int_distribution<> qty_dist(1, 10); //the quantity
    std::uniform_int_distribution<> side_dist(0, 1); // 0 = BUY 1 = SELL
    while (running) {
        //creates new order
        Order order{};
        order.id = global_order_id.fetch_add(1, std::memory_order_relaxed);
        order.side = (side_dist(gen) == 0) ? Side::BUY : Side::SELL;
        order.price = price_dist(gen);
        order.quantity = qty_dist(gen);
        //Latency Point 1
        order.timestamp_produce = std::chrono::high_resolution_clock::now();
        //enqueues the order into the lock-free queue
        order_queue.enqueue(order);
        //to avoid overwhelming the system there is a 10ms wait added below
        std::this_thread::sleep_for(std::chrono::microseconds(10));
    }
}
//Consumer Thread Function for singe matching engine thread
void consumer_thread() {
    std::cout << "Consumer (Matching Engine) thread started.\n";
    OrderBook book;
    Order order;
    while (running || order_queue.size_approx() > 0) {
        //non-blocking call to try and dequeue an order
        if (order_queue.try_dequeue(order)) {
            //Latency Point 2
            order.timestamp_consume = std::chrono::high_resolution_clock::now();
            book.process_order(order);
            //Latency Point 3
            order.timestamp_processed = std::chrono::high_resolution_clock::now();
            //calculates and stores the total end-to-end latency
            auto latency = std::chrono::duration_cast<std::chrono::nanoseconds>(
                order.timestamp_processed - order.timestamp_produce
            ).count();
            end_to_end_latencies_ns.push_back(latency);
        } else if (running) {
            std::this_thread::yield();
        }
    }
    //outputs final book state
    std::cout << "\n--- FINAL ---" << std::endl;
    book.print_top_of_book();
}
//Latency Statistics Function
void print_latency_stats() {
    if (end_to_end_latencies_ns.empty()) {
        std::cout << "No latencies recorded.\n";
        return;
    }
    std::sort(end_to_end_latencies_ns.begin(), end_to_end_latencies_ns.end());
    long long total_count = end_to_end_latencies_ns.size();
    long long sum = std::accumulate(end_to_end_latencies_ns.begin(), end_to_end_latencies_ns.end(), 0LL);
    double mean_ns = static_cast<double>(sum) / total_count;
    long long min_ns = end_to_end_latencies_ns.front();
    long long max_ns = end_to_end_latencies_ns.back();
    long long p50_ns = end_to_end_latencies_ns[total_count * 0.50];
    long long p90_ns = end_to_end_latencies_ns[total_count * 0.90];
    long long p99_ns = end_to_end_latencies_ns[total_count * 0.99];
    std::cout << "\n--- Latency Statistics (End-to-End) ---\n";
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "Total Orders: " << total_count << "\n";
    std::cout << "Mean:         " << mean_ns / 1000.0 << " us\n";
    std::cout << "Min:          " << min_ns / 1000.0 << " us\n";
    std::cout << "Median (p50): " << p50_ns / 1000.0 << " us\n";
    std::cout << "p90:          " << p90_ns / 1000.0 << " us\n";
    std::cout << "p99:          " << p99_ns / 1000.0 << " us\n";
    std::cout << "Max:          " << max_ns / 1000.0 << " us\n";
}

//Main Function
int main() {
    const int NUM_PRODUCER_THREADS = 4;
    const int SIMULATION_DURATION_SECONDS = 10;
    std::cout << "Starting " << NUM_PRODUCER_THREADS << " producer threads.\n";
    std::cout << "Starting 1 consumer (matching engine) thread.\n";
    std::cout << "Simulation will run for " << SIMULATION_DURATION_SECONDS << " seconds.\n\n";
    std::vector<std::thread> producers;
    std::thread consumer = std::thread(consumer_thread); //Starts the single consumer thread
    for (int i = 0; i < NUM_PRODUCER_THREADS; ++i) {
        producers.emplace_back(producer_thread, i); //starts all producer threads
    }
    //simulation runs
    std::this_thread::sleep_for(std::chrono::seconds(SIMULATION_DURATION_SECONDS));
    running = false; //signals threads to stop
    std::cout << "\nStopping simulation, waiting for threads to finish...\n";
    for (auto& t : producers) {
        t.join();
    }
    std::cout << "Producer threads joined.\n";
    //wait for consumer thread to join
    consumer.join();
    std::cout << "Consumer thread joined.\n";
    print_latency_stats();
    return 0;
}