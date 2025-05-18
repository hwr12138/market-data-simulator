#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <future>
#include <map>
#include <iomanip>

#include <grpcpp/grpcpp.h>

// Include the generated files
#include "market_data.grpc.pb.h"
#include "market_data.pb.h"

using grpc::Channel;
using grpc::ClientContext;
using grpc::ClientReaderWriter;
using grpc::Status;

using marketdata::MarketDataService;
using marketdata::SubscriptionRequest;
using marketdata::MarketDataUpdate;
using marketdata::OrderBookSnapshot;
using marketdata::OrderBookIncrementalUpdate;
using marketdata::PriceLevel;

// Helper function to print an order book
void PrintOrderBook(const std::string& instrument_id,
                    const std::map<double, double>& bids,
                    const std::map<double, double>& asks) {
    std::cout << "--- Order Book for " << instrument_id << " ---" << std::endl;
    std::cout << std::fixed << std::setprecision(2); // For consistent price formatting

    std::cout << "  ASKS:" << std::endl;
    // Iterate asks in descending price order
    for (auto it = asks.rbegin(); it != asks.rend(); ++it) {
        std::cout << "    Price: " << it->first << ", Quantity: " << it->second << std::endl;
    }

    std::cout << "  BIDS:" << std::endl;
    // Iterate bids in descending price order
    for (auto it = bids.rbegin(); it != bids.rend(); ++it) {
        std::cout << "    Price: " << it->first << ", Quantity: " << it->second << std::endl;
    }
    std::cout << "-----------------------------" << std::endl;
}

class MarketDataClient {
public:
    MarketDataClient(std::shared_ptr<Channel> channel)
        : stub_(MarketDataService::NewStub(channel)) {}

    void SubscribeToMarketData(const std::vector<std::string>& instrument_ids) {
        ClientContext context;
        stream_ = stub_->Subscribe(&context);

        auto writer_future = std::async(std::launch::async, [&]() {
            for (const auto& id : instrument_ids) {
                SubscriptionRequest request;
                request.set_action(SubscriptionRequest::SUBSCRIBE);
                request.set_instrument_id(id);
                std::cout << "Client sending SUBSCRIBE request for: " << id << std::endl;
                if (!stream_->Write(request)) {
                    std::cerr << "Client failed to write SUBSCRIBE request for " << id << ". Stream likely broken." << std::endl;
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            std::cout << "Writer thread finished." << std::endl;
        });


        MarketDataUpdate update;
        while (stream_->Read(&update)) {
            // Process the received update
            if (update.has_snapshot()) {
                const OrderBookSnapshot& snapshot = update.snapshot();
                std::string instrument_id = snapshot.instrument_id();
                std::cout << "Client received SNAPSHOT for instrument: " << instrument_id << std::endl;

                // Clear existing data for this instrument and populate from snapshot
                order_books_bids_[instrument_id].clear();
                order_books_asks_[instrument_id].clear();

                for (const auto& bid : snapshot.bids()) {
                    order_books_bids_[instrument_id][bid.price()] = bid.quantity();
                }
                for (const auto& ask : snapshot.asks()) {
                    order_books_asks_[instrument_id][ask.price()] = ask.quantity();
                }

                PrintOrderBook(instrument_id, order_books_bids_[instrument_id], order_books_asks_[instrument_id]);

            } else if (update.has_incremental_update()) {
                const OrderBookIncrementalUpdate& incremental_update = update.incremental_update();
                std::string instrument_id = incremental_update.instrument_id();
                std::cout << "Client received INCREMENTAL UPDATE for instrument: " << instrument_id << std::endl;

                // Apply incremental updates to the existing order book
                // Here, we'll assume quantity > 0 is an add/modify, and quantity == 0 is a deletion.
                for (const auto& bid_update : incremental_update.bid_updates()) {
                    if (bid_update.quantity() > 0) {
                        order_books_bids_[instrument_id][bid_update.price()] = bid_update.quantity();
                    } else {
                        // Assume quantity 0 means delete the price level
                        order_books_bids_[instrument_id].erase(bid_update.price());
                    }
                }

                for (const auto& ask_update : incremental_update.ask_updates()) {
                     if (ask_update.quantity() > 0) {
                        order_books_asks_[instrument_id][ask_update.price()] = ask_update.quantity();
                    } else {
                         // Assume quantity 0 means delete the price level
                        order_books_asks_[instrument_id].erase(ask_update.price());
                    }
                }

                PrintOrderBook(instrument_id, order_books_bids_[instrument_id], order_books_asks_[instrument_id]);
            }
        }

        std::cout << "Client read stream finished." << std::endl;
        context.TryCancel();
        writer_future.get();

        Status status = stream_->Finish();
        if (status.ok()) {
            std::cout << "Subscribe RPC completed successfully." << std::endl;
        } else {
            std::cerr << "Subscribe RPC failed: " << status.error_message() << std::endl;
        }
    }

    void UnsubscribeFromMarketData(const std::string& instrument_id) {
        if (!stream_) {
            std::cerr << "Cannot unsubscribe: stream is not active." << std::endl;
            return;
        }

        SubscriptionRequest request;
        request.set_action(SubscriptionRequest::UNSUBSCRIBE);
        request.set_instrument_id(instrument_id);

        std::cout << "Client sending UNSUBSCRIBE request for: " << instrument_id << std::endl;

        if (!stream_->Write(request)) {
            std::cerr << "Client failed to write UNSUBSCRIBE request for " << instrument_id << ". Stream likely broken." << std::endl;
        }
    }

private:
    std::unique_ptr<MarketDataService::Stub> stub_;
    std::shared_ptr<ClientReaderWriter<SubscriptionRequest, MarketDataUpdate>> stream_;

    // Data structures to hold the order book for each instrument
    // Using std::map<double, double> to store price-quantity levels, ordered by price.
    // Bids are typically sorted descending by price, Asks ascending.
    std::map<std::string, std::map<double, double>> order_books_bids_;
    std::map<std::string, std::map<double, double>> order_books_asks_;
};

int main(int argc, char** argv) {
    std::string server_address("localhost:50051");

    std::shared_ptr<Channel> channel = grpc::CreateChannel(
        server_address, grpc::InsecureChannelCredentials());

    MarketDataClient client(channel);

    std::vector<std::string> instruments_to_subscribe = {"AAPL", "MSFT"};

    std::cout << "Client connecting to server at " << server_address << std::endl;

    auto subscribe_future = std::async(std::launch::async, &MarketDataClient::SubscribeToMarketData, &client, instruments_to_subscribe);

    std::this_thread::sleep_for(std::chrono::seconds(10));

    client.UnsubscribeFromMarketData("AAPL");

    std::this_thread::sleep_for(std::chrono::seconds(10));

    subscribe_future.get();

    std::cout << "Client finished." << std::endl;

    return 0;
}