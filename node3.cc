#include <thread>
#include <ctime>
#include <fstream>
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <chrono>
#include <sstream>
#include <grpcpp/grpcpp.h>

#ifdef BAZEL_BUILD
#include "examples/protos/raft.grpc.pb.h"
#else
#include "raft.grpc.pb.h"
#endif

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerReaderWriter;
using grpc::Status;
using raft::RaftService;
using raft::Request;
using raft::Response;

// hardcoding all values for the time being
struct Node{
  std::string address = "localhost:50053";
  int id = 3;
  int status;  // 0 = follower, 1 = candidate , 2 = leader
  int term_num;
  bool hasVoted = false;
  std::string logfile = "log3.txt";
  std::vector<int> nodesVotedYes;
};

Node curr_node;


// Logic and data behind the server's behavior.
class RaftServiceImpl final : public RaftService::Service {

  Status StatusCheck(ServerContext* context, const Request* request, Response* response) override {
    return Status::OK;
  }

  Status GetData(ServerContext* context, const Request* request, Response* response) override {
    std::string msg = "Hello to Node ";
    
    int id = request->id();
    int status = request->status();
    int term_num = request->term_num();
    term_num++;

    msg += std::to_string(id);

    if(status == 0){
      msg += " (Follower) ";
    } else if (status == 1){
      msg += " (Candidate) ";
    } else {
      msg += " (Leader) ";
    }

    msg += " from Node 3";

    response->set_message(msg);
    response->set_term_num(term_num);

    return Status::OK;
  }

  Status Voting(ServerContext* context, const Request* request, Response* response) override {
    int term_num = request->term_num();
    std::string logfile_name = request->logfile();

    if(curr_node.hasVoted){
      response->set_message("no");
    } else {
      if(curr_node.term_num > term_num) {
        response->set_message("no");
      } else {
        response->set_message("yes");
      }
    }

    response->set_id(curr_node.id);
    response->set_term_num(curr_node.term_num);

    return Status::OK;
  }

};


class RaftClient {
 public:
  RaftClient(std::shared_ptr<Channel> channel)
      : stub_(RaftService::NewStub(channel)) {}

  std::string GetData(Node& node) {
    // Data we are sending to the server.
    Request request;
    Response response;
    ClientContext context;

    request.set_id(node.id);
    request.set_status(node.status);
    request.set_term_num(node.term_num);

    // The actual RPC.
    Status status = stub_->GetData(&context, request, &response);
    std:: string message = "";
    
    // Act upon its status.
    if (status.ok()) {
      message = response.message();
      node.term_num = response.term_num();
      return message;
    } else {      
      message = "RPC Failed";
      return message;
    }
  }

  void GetVotes(std::vector<std::pair<int, std::string>>& Votes){
    Request request;
    Response response;
    ClientContext context;

    request.set_term_num(curr_node.term_num);
    request.set_logfile(curr_node.logfile);

    Status status = stub_->Voting(&context, request, &response);
    std::string message = "";
    int id;
    int term_num;

    if(status.ok()){
      message = response.message();
      id = response.id();
      term_num = response.term_num();

      if(message == "no"){
        if(curr_node.term_num < term_num){
          curr_node.status = 0; // becomes a follower
        } else if(curr_node.term_num >= term_num) {
          int i;
          for(i = 0; i < Votes.size(); i++){
            if(Votes[i].first == id){
              Votes[i].second  = message;
              break;
            }
          }
        }
        std::cout << "Node " << id << " voted NO | Total Votes (YES): " << curr_node.nodesVotedYes.size() << std::endl;
      } else {
        curr_node.nodesVotedYes.push_back(id);
        std::cout << "Node " << id << " voted YES | Total Votes (YES): " << curr_node.nodesVotedYes.size() << std::endl;
      }
    }
  }

  bool StatusCheck() {

    Request request;
    Response response;
    ClientContext context;
    Status status = stub_->StatusCheck(&context, request, &response);

    if(status.ok()){
      return true;
    } else {
      return false;
    }
  }

 private:
  std::shared_ptr<RaftService::Stub> stub_;
};

void RunServer() {
  std::string server_address("0.0.0.0:50053");
  RaftServiceImpl service;

  ServerBuilder builder;
  // Listen on the given address without any authentication mechanism.
  builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
  // Register "service" as the instance through which we'll communicate with
  // clients. In this case, it corresponds to an *synchronous* service.
  builder.RegisterService(&service);
  // Finally assemble the server.
  std::unique_ptr<Server> server(builder.BuildAndStart());
  std::cout << "Node 3 listening on " << server_address << std::endl;

  // Wait for the server to shutdown. Note that some other thread must be
  // responsible for shutting down the server for this call to ever return.
  server->Wait();
}

std::string currentDateTime() {
  // get current time
  auto now = std::chrono::system_clock::now();
  std::time_t time = std::chrono::system_clock::to_time_t(now);
  
  // format time as string
  std::stringstream ss;
  ss << std::ctime(&time);
  std::string dateTimeStr = ss.str();
  
  // remove newline character from string
  dateTimeStr.erase(dateTimeStr.find_last_not_of("\n") + 1);
  
  return dateTimeStr;
}

int main(int argc, char** argv) {

  // creating connection with all other

  std::thread server_thread([] {RunServer();});

    // config file
  std::vector<std::pair<int, std::string>> Votes = {{1, "no"}, {2, "no"}, {4, "no"},{5, "no"}}; // { "id", "vote" }
  std::vector<std::string> addresses = {"localhost:50051", "localhost:50052", "localhost:50054", "localhost:50055"};
  std::vector<RaftClient> nodes;
  
  std::stringstream(argv[1]) >> curr_node.status;
  std::stringstream(argv[2]) >> curr_node.term_num;

  // making connections with other nodes
  for(int i = 0; i < addresses.size(); i++){
    nodes.push_back(grpc::CreateChannel(addresses[i], grpc::InsecureChannelCredentials()));
  }

  // making sure all nodes are alive
  int i = 0;
  bool response = false;
  while(i < 4) {
    RaftClient rc(grpc::CreateChannel(addresses[i], grpc::InsecureChannelCredentials()));
    response = rc.StatusCheck();
    if(response)
      i++;
  }

  std::ofstream logfile;
  logfile.open(curr_node.logfile, std::ios_base::app);

  // calling other nodes
  for(int i = 0; i < nodes.size(); i++){
    RaftClient rc = nodes[i];
    std::string reply = rc.GetData(curr_node);
    std::cout << reply << std::endl;
    std::string dateTime = currentDateTime();
    logfile << "[" << dateTime << "] " << reply << " | Term Number: " << curr_node.term_num << std::endl;
  }

  std::cout << "\nTerm Number of Node " << curr_node.id << ": " << curr_node.term_num << std::endl << std::endl;

  // wait for a random interval
  if(curr_node.status == 1){
    int max = 10, min = 4;
    srand(time(NULL));
    int interval = rand()%(max - min + 1) + min;
    for (auto start = std::chrono::steady_clock::now(), now = start; now < start + std::chrono::seconds{interval}; now = std::chrono::steady_clock::now());

    std::cout << "Getting Votes..." << std::endl;
    for(int i = 0; i < nodes.size(); i++){
      RaftClient rc = nodes[i];
      rc.GetVotes(Votes);
      std::string dateTime = currentDateTime();
    }

    if(curr_node.nodesVotedYes.size() >= (5/2 + 1)){
      curr_node.status = 2; // it has now become a leader
    }

    if(curr_node.status == 2){
      std::cout << "Voted Leader" << std::endl;
    }

    // calling other nodes
    for(int i = 0; i < nodes.size(); i++){
      RaftClient rc = nodes[i];
      std::string reply = rc.GetData(curr_node);
      std::cout << reply << std::endl;
      std::string dateTime = currentDateTime();
      logfile << "[" << dateTime << "] " << reply << " | Term Number: " << curr_node.term_num << std::endl;
    }
  }

  server_thread.join();
  
  return 0;
}
