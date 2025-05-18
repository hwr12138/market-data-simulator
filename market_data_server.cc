#include <iostream>
#include <string>
#include <vector>
#include <map> // Include for using std::map
#include <set>  // Include for using std::set
#include <thread> // Include for using std::thread
#include <chrono> // Include for using std::chrono
#include <atomic> // Include for using std::atomic

#include <grpcpp/grpcpp.h>

// Include the generated files
#include "market_data.grpc.pb.h"
#include "market_data.pb.h"

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;

using marketdata::MarketDataService;
using marketdata::SubscriptionRequest;
using marketdata::MarketDataUpdate;
using marketdata::OrderBookSnapshot;
using marketdata::OrderBookIncrementalUpdate;
using marketdata::PriceLevel;

// A helper function to simulate streaming incremental updates
// In a real application, this would read from a market data source
void StreamIncrementalUpdates(
    const std::string& instrument_id, ServerContext* context,
    grpc::ServerReaderWriter<MarketDataUpdate, SubscriptionRequest>* stream,
    std::atomic<bool>& stop_streaming) {

    std::cout << "Starting incremental update stream for instrument: " << instrument_id << std::endl;

    // Simulate sending updates every second
    int update_count = 0;
    while (!stop_streaming.load() && !context->IsCancelled()) {
        MarketDataUpdate update;
        OrderBookIncrementalUpdate* incremental_update = update.mutable_incremental_update();
        incremental_update->set_instrument_id(instrument_id);

        // Simulate a small price change
        double price_change = (update_count % 2 == 0) ? 0.1 : -0.1;

        PriceLevel* bid_update = incremental_update->add_bid_updates();
        bid_update->set_price(99.0 + price_change);
        bid_update->set_quantity(200 + update_count * 10);

        PriceLevel* ask_update = incremental_update->add_ask_updates();
        ask_update->set_price(100.0 - price_change);
        ask_update->set_quantity(150 + update_count * 5);

        // Attempt to write the update to the stream
        if (!stream->Write(update)) {
            std::cerr << "Failed to write incremental update for " << instrument_id << ". Client likely disconnected." << std::endl;
            break; // Exit the loop if writing fails
        }

        update_count++;
        std::this_thread::sleep_for(std::chrono::seconds(1)); // Simulate update frequency
    }

    std::cout << "Stopped incremental update stream for instrument: " << instrument_id << std::endl;
}


// Class that implements the MarketDataService
class MarketDataServiceImpl final : public MarketDataService::Service {
public:
    // Using a map to keep track of active streams and their associated data
    // Key: A unique identifier for the stream (e.g., pointer address)
    // Value: A struct/class holding the stream pointer and subscribed instruments
    // For simplicity in this example, we'll manage threads directly in the Subscribe method scope,
    // but in a real app, you'd manage them centrally.

    Status Subscribe(ServerContext* context,
                     grpc::ServerReaderWriter<MarketDataUpdate, SubscriptionRequest>* stream) override {

        std::cout << "Client connected." << std::endl;

        // Use a map within the scope of this stream to manage update threads for instruments on THIS stream
        std::map<std::string, std::thread> update_threads;
        std::map<std::string, std::atomic<bool>> stop_signals;


        SubscriptionRequest request;
        // This loop will receive messages from the client
        while (stream->Read(&request)) {
            std::string instrument_id = request.instrument_id();
            std::cout << "Received subscription request: Action="
                      << (request.action() == SubscriptionRequest::SUBSCRIBE ? "SUBSCRIBE" : "UNSUBSCRIBE")
                      << ", Instrument=" << instrument_id << std::endl;

            if (request.action() == SubscriptionRequest::SUBSCRIBE) {
                // Check if we are already streaming for this instrument on this stream
                if (update_threads.find(instrument_id) == update_threads.end() || !update_threads.at(instrument_id).joinable()) {
                     // Send initial snapshot (as implemented before)
                    MarketDataUpdate snapshot_update;
                    OrderBookSnapshot* snapshot = snapshot_update.mutable_snapshot();
                    snapshot->set_instrument_id(instrument_id);

                    // Add some dummy bid and ask levels for the snapshot
                    PriceLevel* bid1 = snapshot->add_bids();
                    bid1->set_price(99.5);
                    bid1->set_quantity(100);

                    PriceLevel* bid2 = snapshot->add_bids();
                    bid2->set_price(99.0);
                    bid2->set_quantity(200);

                    PriceLevel* ask1 = snapshot->add_asks();
                    ask1->set_price(100.0);
                    ask1->set_quantity(150);

                    PriceLevel* ask2 = snapshot->add_asks();
                    ask2->set_price(100.5);
                    ask2->set_quantity(250);

                    if (stream->Write(snapshot_update)) {
                      std::cout << "Sent snapshot for instrument: " << instrument_id << std::endl;
                    } else {
                      std::cerr << "Failed to send snapshot for instrument: " << instrument_id << ". Client likely disconnected." << std::endl;
                      // If sending snapshot fails, the client might be gone, break the read loop
                      break;
                    }

                    // Start a new thread to stream incremental updates for this instrument
                    stop_signals[instrument_id].store(false); // Initialize stop signal
                    update_threads[instrument_id] = std::thread(
                        StreamIncrementalUpdates, instrument_id, context, stream, std::ref(stop_signals[instrument_id]));

                } else {
                    std::cout << "Already streaming updates for " << instrument_id << " on this stream." << std::endl;
                }

            } else if (request.action() == SubscriptionRequest::UNSUBSCRIBE) {
                // Find the thread for this instrument and signal it to stop
                if (update_threads.find(instrument_id) != update_threads.end() && update_threads.at(instrument_id).joinable()) {
                     std::cout << "Signaling stop for update stream for instrument: " << instrument_id << std::endl;
                     stop_signals[instrument_id].store(true); // Signal the thread to stop
                     // We don't join here to avoid blocking the read loop.
                     // The thread will be joined when this Subscribe RPC finishes.
                }

                // Send an empty snapshot upon unsubscription
                MarketDataUpdate unsubscribe_update;
                OrderBookSnapshot* snapshot = unsubscribe_update.mutable_snapshot();
                snapshot->set_instrument_id(instrument_id); // Send for the specific instrument

                 if (stream->Write(unsubscribe_update)) {
                     std::cout << "Sent empty snapshot for unsubscription: " << instrument_id << std::endl;
                 } else {
                     std::cerr << "Failed to send empty snapshot for unsubscription: " << instrument_id << std::endl;
                     // If sending fails, client might be gone, break the read loop
                     break;
                 }
            }
        }

        // This point is reached when the client stream is closed (stream->Read returns false)
        std::cout << "Client stream closed. Stopping all update threads for this stream." << std::endl;

        // Signal all active update threads for this stream to stop
        for(auto const& [id, stop_signal] : stop_signals) {
            stop_signals[id].store(true);
        }

        // Join all update threads associated with this stream to ensure they finish cleanly
        for(auto& pair : update_threads) {
            if (pair.second.joinable()) {
                pair.second.join();
            }
        }

        std::cout << "All update threads joined for this stream." << std::endl;

        return Status::OK;
    }
};

void RunServer() {
    std::string server_address("0.0.0.0:50051"); // Listen on all interfaces, port 50051
    MarketDataServiceImpl service;

    ServerBuilder builder;
    // Listen on the given address without any authentication mechanism.
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    // Register "service" as the instance awaiting incoming RPCs.
    builder.RegisterService(&service);

    // Assemble the server
    std::unique_ptr<Server> server(builder.BuildAndStart());
    std::cout << "Server listening on " << server_address << std::endl;

    // Wait for the server to shut down. Note that some systems don't
    // allow interrupts to quit the server.
    server->Wait();
}

int main(int argc, char** argv) {
    RunServer();
    return 0;
}