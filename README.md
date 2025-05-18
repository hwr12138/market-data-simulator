# gRPC Market Data Simulator

This project demonstrates a basic market data dissemination system using gRPC and Protocol Buffers in C++. It consists of a server that simulates streaming market data (snapshots and incremental updates) and a client that subscribes to this data and maintains a local order book view.

## Features

* **Client-Server Architecture:** A clear separation between the market data server and multiple potential clients.
* **gRPC Communication:** Utilizes gRPC for efficient and structured inter-process communication.
* **Protocol Buffers:** Defines the service and message formats using `.proto` files for language-agnostic data serialization.
* **Bidirectional Streaming:** Employs gRPC's bidirectional streaming to allow clients to send subscription requests and the server to stream data back on the same connection.
* **Market Data Simulation:** The server simulates generating and disseminating order book snapshots and incremental updates.
* **Client-side Processing:** The client receives and processes market data updates to maintain a local order book representation.

## Prerequisites

To build and run this project, you need the following installed on your system:

* **C++ Compiler**
* **gRPC and Protocol Buffers**

Ensure that the `protoc` executable and the `grpc_cpp_plugin` are in your system's PATH environment variable after installation.

## How to Build

1.  **Compile:** Compile all the `.cc` files. The exact command depends on your system and gRPC installation. Using `pkg-config` is often helpful:

    ```bash
    g++ -std=c++11 market_data_server.cc market_data.pb.cc market_data.grpc.pb.cc `pkg-config --cflags --libs protobuf grpc++` -pthread -Wl,--no-as-needed -lgrpc++_reflection -Wl,--as-needed -ldl -Wl,--no-as-needed -lgrpc++ -Wl,--as-needed -o market_data_server
    ```

    ```bash
    g++ -std=c++11 market_data_client.cc market_data.pb.cc market_data.grpc.pb.cc `pkg-config --cflags --libs protobuf grpc++` -pthread -Wl,--no-as-needed -lgrpc++_reflection -Wl,--as-needed -ldl -Wl,--no-as-needed -lgrpc++ -Wl,--as-needed -o market_data_client
    ```
    * *Adjust compiler flags and libraries as needed based on your environment.*

## How to Run

1.  **Start the Server:** Open a terminal, navigate to the project directory, and run the server executable:

    ```bash
    ./market_data_server
    ```
    You should see output indicating the server is listening on port 50051.

2.  **Start the Client:** Open a *new* terminal (keep the server running), navigate to the project directory, and run the client executable:

    ```bash
    ./market_data_client
    ```
    You should see output from the client indicating it's connecting, subscribing, receiving snapshots and updates, and processing the order book data. The server terminal will show when it receives subscription requests.
