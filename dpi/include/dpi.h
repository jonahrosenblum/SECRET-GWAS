/*
 * Header file for our dpi class.
 */

#include <string>
#include <iostream>
#include <fstream>
#include <assert.h>
#include <stdexcept>
#include <stdint.h>
#include <string>
#include <thread>
#include <atomic>
#include <chrono>
#include <queue>
#include <condition_variable>
#include <boost/thread.hpp>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <pthread.h>

#include "buffer_size.h"
#include "parser.h"
#include "socket_send.h"
#include "output.h"
#include "aes-crypto.h"
#include "json.hpp"
#include "concurrentqueue.h"

#include "attestation.h"

#ifndef _DPI_H_
#define _DPI_H_

struct Phenotype {
    std::string message;
    EnclaveNodeMessageType mtype;
};

struct EncryptionBlock {
  unsigned int line_num;
  std::string line;
};

// did i ever verify this?
struct EncryptionBlockGT {
  inline bool operator()(const EncryptionBlock* a, const EncryptionBlock* b) const {
    return a->line_num > b->line_num;
  }
};

class DPI {
  private:
    nlohmann::json dpi_config;
    
    std::string dpi_name;
    std::string dpi_hostname;
    unsigned int listen_port;

    int num_patients;
    int num_lines_per_block;

    std::string allele_file_name;
    bool cov_work_start;

    std::chrono::time_point<std::chrono::high_resolution_clock> start;
    std::vector<buffer_t> evidence_list;
    std::atomic<int> verified_count;
    std::vector<std::vector<AESCrypto> > aes_encryptor_list;
    std::vector<std::vector<Phenotype> > phenotypes_list;
    std::vector<ConnectionInfo> enclave_node_info;
    std::vector<ConnectionInfo> dpi_info;
    std::vector<std::queue<std::string> *> allele_queue_list;
    std::vector<std::priority_queue<EncryptionBlock*, std::vector<EncryptionBlock* >, EncryptionBlockGT > > encryption_queue_list;
    std::vector<std::mutex> encryption_queue_lock_list;
    std::atomic<int> y_and_cov_count;
    std::atomic<int> filled_count;
    std::atomic<int> sync_count;
    std::atomic<int> work_distributed_count;
    std::mutex xval_file_lock;
    std::condition_variable start_sender_cv;
    std::condition_variable sync_cv;
    std::condition_variable queue_cv;

  public:
    DPI(const std::string& config_file);
    ~DPI();

    // Set up DPI data structures, establish connection with server
    void init(const std::string& config_file);
    // Create listening socket to handle requests on indefinitely
    void run();

    // parses and calls the appropriate handler for an incoming dpi request
    void handle_message(int connFD, const unsigned int global_id, const DPIMessageType mtype, std::string& msg);

    // construct response header, encrypt response body, and send
    void send_msg(const unsigned int global_id, const unsigned int mtype, const std::string& msg, int connFD=-1);
    int send_msg(const std::string& hostname, unsigned int port, unsigned int mtype, const std::string& msg, int connFD=-1);

    void start_thread_wrapper();

    // start a thread that will handle a message and exit properly if it finds an error
    bool start_thread(int connFD);

    void queue_helper(const int global_id, const int num_helpers);

    void fill_queue();

    void prepare_tsv_file(unsigned int global_id, const std::string& filename, EnclaveNodeMessageType mtype);

};

#endif /* _DPI_H_ */
