/*
 * Implementation for our server class.
 */

#include "enclave_node.h"
#include "socket_send.h"
#include <iostream>
#include <fstream>
#include <assert.h>
#include <stdexcept>
#include <chrono>
#include <stdint.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <future>
#include <netdb.h>
#include "enclave.h"
#include "hashing.h"
#include "errno.h"

#ifdef NON_OE
#define AVERAGE_OCALL_OVERHEAD_MICROSECONDS 0.00003534999965
#else
#define AVERAGE_OCALL_OVERHEAD_MICROSECONDS 5.00908909
#endif

std::mutex cout_lock;

bool terminating = false;

const int MIN_BLOCK_COUNT = 50;

EnclaveNode::EnclaveNode(const std::string& config_file) {
    init(config_file);
}

EnclaveNode::~EnclaveNode() {

}

void EnclaveNode::init(const std::string& config_file) {
    num_threads = boost::thread::hardware_concurrency();

    std::ifstream enclave_config_file(config_file);
    enclave_config_file >> enclave_config;

    // Read in information we need to register enclave node
    port = enclave_config["enclave_node_bind_port"];

    // read in list institutions/dpis involved in GWAS
    for (int i = 0; i < enclave_config["institutions"].size(); ++i) {
        std::string institution = enclave_config["institutions"][i];
        institution_list.push_back(institution);
        expected_institutions.insert(institution);
    }

    for (int i = 0; i < enclave_config["covariants"].size(); ++i) {
        std::string covariant = enclave_config["covariants"][i];
        if (covariant != "1") {
            expected_covariants.insert(covariant);
        }
        covariant_list.append(covariant + " ");
    }

    y_val_name = enclave_config["y_val_name"];

    enc_mode = EncMode::sgx;
    if (enclave_config.count("flag")) {
        if (enclave_config["flag"] == "simulate") {
            enc_mode = EncMode::simulate;
        } else if (enclave_config["flag"] == "debug") {
            enc_mode = EncMode::debug;
        } else {
            throw std::runtime_error("Config \"flag\" type is unknown.");
        }
    }

    if (!enclave_config.count("analysis_type")) {
        throw std::runtime_error("No analysis type specified, use flag: \"analysis_type\".");
    }
    if (enclave_config["analysis_type"] == "linear_dummy") {
        enc_analysis = EncAnalysis::linear_dummy;
    } else if (enclave_config["analysis_type"] == "linear") {
        enc_analysis = EncAnalysis::linear;
    } else if (enclave_config["analysis_type"] == "logistic") {
        enc_analysis = EncAnalysis::logistic;
    } else if (enclave_config["analysis_type"] == "linear-oblivious") {
        enc_analysis = EncAnalysis::linear_oblivious;
    } else if (enclave_config["analysis_type"] == "logistic-oblivious") {
        enc_analysis = EncAnalysis::logistic_oblivious;
    } else {
        throw std::runtime_error("Invalid enclave analysis selected.");
    }

    impute_policy = ImputePolicy::EPACTS;
    if (enclave_config.count("impute_policy")) {
        if (enclave_config["impute_policy"] == "EPACTS") {
            // already default
        } else if (enclave_config["impute_policy"] == "Hail") {
            impute_policy = ImputePolicy::Hail;
        } else {
            throw std::runtime_error("Config \"impute_policy\" type is unknown.");
        }
    }

    server_eof = false;
    max_batch_lines = 0;
    global_id = -1;

    // Make a bucket for each possible port number. This is not very space
    // efficient but I couldn't find a good lock free unordered_set
    // so this will do just fine. All race conditions are basically benign.
    seen_fds.resize(65354);
    std::fill(seen_fds.begin(), seen_fds.end(), false);

    allele_queue_list.resize(num_threads);
    eof_read_list.resize(num_threads);

    for (int id = 0; id < num_threads; ++id) {
        eof_read_list[id] = false;
    }

    // Also start the enclave thread.
    boost::thread enclave_thread(start_enclave);
    enclave_thread.detach();
}

void EnclaveNode::run() {
    // set up the server to do nothing when it receives a broken pipe error
    //signal(SIGPIPE, signal_handler);
    // create a master socket to listen for requests on
    // (1) Create socket
	int sockfd = socket(AF_INET, SOCK_STREAM, 0);

    // (2) Set the "reuse port" socket option
	int yesval = 1;
	setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yesval, sizeof(yesval));

    // (3) Create a sockaddr_in struct for the proper port and bind() to it.
	struct sockaddr_in addr;
    socklen_t addrSize = sizeof(addr);
    memset(&addr, 0, addrSize);

    // specify socket family (Internet)
	addr.sin_family = AF_INET;

	// specify socket hostname
	// The socket will be a server, so it will only be listening.
	// Let the OS map it to the correct address.
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    // bind to our given port, or randomly get one if port = 0
	if (bind(sockfd, (struct sockaddr*) &addr, addrSize) < 0) {
        guarded_cout("bind failure: " + std::to_string(errno), cout_lock);
    }

    // update our member variable to the port we just assigned
    if (getsockname(sockfd, (struct sockaddr*) &addr, &addrSize) < 0) {
        guarded_cout("getsockname failure: " + std::to_string(errno), cout_lock);
    }

    // (4) Begin listening for incoming connections.
	if (listen(sockfd, 4096) < 0) {
        guarded_cout("listen: " + std::to_string(errno), cout_lock);
    }

    port = ntohs(addr.sin_port);
    guarded_cout("\n Running on port " + std::to_string(port), cout_lock);


    // (5) Serve incoming connections one by one forever (a lonely fate).
	while (true) {
        // accept connection from dpi
        int connFD = accept(sockfd, (struct sockaddr*) &addr, &addrSize);

        // spin up a new thread to handle this message
        boost::thread msg_thread(&EnclaveNode::start_thread, this, connFD, nullptr);
        msg_thread.detach();
    }
}

bool EnclaveNode::start_thread(int connFD, char* body_buffer) {
    // if we catch any errors we will throw an error to catch and close the connection
    bool buffer_given = body_buffer != nullptr;
    if (!buffer_given) {
        body_buffer = new char[MAX_MESSAGE_SIZE]();
    }
    try {
        char header_buffer[128];
        // receive header, byte by byte until we hit deliminating char
        memset(header_buffer, 0, sizeof(header_buffer));

        int header_size = 0;
        bool found_delim = false;
        while (header_size < 128) {
            // Receive exactly one byte
            int rval = recv(connFD, header_buffer + header_size, 1, MSG_WAITALL);
            if (rval == -1) {
                char buffer[ 256 ];
                char * errorMsg = strerror_r( errno, buffer, 256 ); // GNU-specific version, Linux default
                printf("Error %s\n", errorMsg); //return value has to be used since buffer might not be modified
                throw std::runtime_error("Socket recv failed\n");
            } else if (rval == 0) {
                std::cout << "rval was 0" << std::endl;
                return false;
            }
            // Stop if we received a deliminating character
            if (header_buffer[header_size] == '\n') {
                found_delim = true;
                break;
            }
            header_size++;
        }
        if (!found_delim) {
            std::cout << "Recieved message without null terminating char" << std::endl;
            std::cout << header_buffer << std::endl;
            return true;
            //throw std::runtime_error("Didn't read in a null terminating char");
        }
        std::string header(header_buffer, header_size);
        if (header.find("GET / HTTP/1.1") != std::string::npos) {
            std::cout << "Strange get request? Ignoring for now." << std::endl;
            delete[] body_buffer;
            return true;
        }

        unsigned int body_size;
        try {
            body_size = std::stoi(header);
        } catch(const std::invalid_argument& e) {
            std::cout << "Failed to read in body size" << std::endl;
            std::cout << header << std::endl;
            return true;
        }
        if (body_size != 0) {
            // read in encrypted body
            int rval = recv(connFD, body_buffer, body_size, MSG_WAITALL);
            if (rval == -1) {
                throw std::runtime_error("Error reading request body");
            }
        }
        std::string body(body_buffer, body_size);

        std::string msg;
        std::string dpi_name;
        EnclaveNodeMessageType mtype = DATA;
        if (!body.length()) {
            std::cout << "No body? " << header << " ! " << body << std::endl;
            return true;
        }
        parse_header_enclave_node_header(body, msg, dpi_name, mtype, connFD);

        // if (mtype != EnclaveNodeMessageType::DATA) {
        //     guarded_cout("Msg type: " + std::to_string(mtype) + " dpi: " + dpi_name, cout_lock);
        // }

        handle_message(connFD, dpi_name, mtype, msg);
    }
    catch (const std::runtime_error e)  {
        guarded_cout("Exception " + std::string(e.what()) + "\n", cout_lock);
        close(connFD);
        return false;
    }
    if (!buffer_given) {
        delete[] body_buffer;
    }
    return true;
}

bool EnclaveNode::handle_message(int connFD, const std::string& name, EnclaveNodeMessageType mtype, std::string& msg) {
    std::string response;
    DPIMessageType response_mtype;
    switch (mtype) {
        case GLOBAL_ID:
        {   
            global_id = std::stoi(msg);
            break;
        }
        case REGISTER:
        {
            // Wait until we have a global id!
            while (get_instance()->global_id < 0) { std::this_thread::yield(); }
            std::lock_guard<std::mutex> raii(institutions_lock);

            if (institutions.count(name)) {
                throw std::runtime_error("User already registered");
            }
            std::vector<std::string> hostname_and_port;
            Parser::split(hostname_and_port, msg, '\t');
            if (hostname_and_port.size() != 2) {
                throw std::runtime_error("Invalid register message: " + msg);
            }
            bool found = false;
            for (int id = 0; id < institution_list.size(); ++id) {
                // Look for dpi id!
                if (institution_list[id] == name) {
                    found = true;
                    
                    institutions[name] = new Institution(hostname_and_port[0], 
                                                         std::stoi(hostname_and_port[1]),
                                                         id,
                                                         num_threads);
                }
            }
            if (!found) {
                throw std::runtime_error("No institution with that name was found");
            }

            // Send the evidence and the RSA key
            response_mtype = EVIDENCE;
            response = std::string(reinterpret_cast<char *>(evidence), evidence_size);
            send_msg(name, response_mtype, response);

            response_mtype = RSA_PUB_KEY;
            response = std::string(reinterpret_cast<char *>(get_rsa_pub_key()), RSA_PUB_KEY_SIZE);
            break;
        }
        case AES_KEY:
        {
            std::vector<std::string> aes_info;
            Parser::split(aes_info, msg, '\t');

            int thread_id = std::stoi(aes_info[2]);
            // We only want 1 "check in"
            if (thread_id == 0) {
                check_in(name);
            }
            institutions_lock.lock();
            institutions[name]->set_key_and_iv(
                                    aes_info[0], // encrypted key
                                    aes_info[1], // encrypted iv
                                    thread_id); // thread id    
            institutions_lock.unlock();
            break;
        }
        case PATIENT_COUNT:
        {
            institutions_lock.lock();
            institutions[name]->set_num_patients(msg);
            institutions_lock.unlock();
            break;
        }
        case Y_VAL:
        {
            institutions[name]->set_y_data(msg);
            break;
        }
        case COVARIANT:
        {
            std::vector<std::string> name_data_split;
            Parser::split(name_data_split, msg, ' ', 1);
            std::string covariant_name = name_data_split.front();

            if (!expected_covariants.count(covariant_name)) {
                throw std::runtime_error("Unexpected covariant received.");
            }
            institutions[name]->set_covariant_data(covariant_name, name_data_split.back());
            break;
        }
        case EOF_DATA:
        {
            DataBlockBatch* batch = new DataBlockBatch;
            batch->pos = std::stoi(msg);
            DataBlock* block = new DataBlock;

            block->locus = EOFSeperator;
            block->data = EOFSeperator;

            batch->blocks_batch.push_back(block);
            institutions[name]->add_block_batch(batch);
            //institutions[name]->transfer_eligible_blocks();
            break;
        }
        case DATA:
        {
            // Added optimization to handle this within in the parser... might undo in the future.
            break;
        }
        case END_ENCLAVE:
        {
            guarded_cout("Exiting enclave", cout_lock);
            exit(0);
        }
        default:
            throw std::runtime_error("Not a valid response type");
    }
    // now let's send that response
    if (response.length()) {
        send_msg(name, response_mtype, response);
    }
    if (mtype != DATA && mtype != EOF_DATA) {
        // cool, well handled!
        //guarded_cout("\nClosing connection", cout_lock);
        close(connFD);
        //guarded_cout("\n--------------", cout_lock);
        return false;
    } 
    return true;  
}

int EnclaveNode::send_msg(const std::string& hostname, const int port, const int mtype, const std::string& msg, int connFD) {
    std::string message = std::to_string(global_id) + " " + std::to_string(mtype) + " ";
    message = std::to_string(message.length() + msg.length()) + "\n" + message + msg;
    return send_message(hostname.c_str(), 
                        port, 
                        message.c_str(), 
                        message.length(), 
                        connFD);
}

int EnclaveNode::send_msg(const std::string& name, const int mtype, const std::string& msg, int connFD) {
    return send_msg(institutions[name]->hostname, 
                    institutions[name]->port, 
                    mtype, 
                    msg, 
                    connFD);
}

int EnclaveNode::send_msg_output(const std::string& msg, CoordinationServerMessageType msg_type, int connFD) {
    return send_msg(enclave_config["coordination_server_info"]["hostname"], 
                    enclave_config["coordination_server_info"]["port"],
                    msg_type,
                    msg,
                    connFD);
}

void EnclaveNode::check_in(const std::string& name) {
    std::lock_guard<std::mutex> raii(expected_lock);
    // for (std::string inst : expected_institutions)
    if (!expected_institutions.count(name)) {
        throw std::runtime_error("Unexpected institution!\n");
    }
    expected_institutions.erase(name);
    // All dpis checked in!
    if (!expected_institutions.size()) {
        // request y, cov, and data
        for (const auto& it : institutions) {
            send_msg(it.first, Y_AND_COV, covariant_list + y_val_name);

            institutions[it.first]->request_conn = send_msg(it.first, DATA_REQUEST, std::to_string(MIN_BLOCK_COUNT), institutions[it.first]->request_conn);
        }


        // Now that all institutions are registered, start up the allele matching thread.
        boost::thread msg_thread(&EnclaveNode::allele_matcher, this);
        msg_thread.detach();

        // And now start up the thread to send out our results to the register server.
        boost::thread output_thread(&EnclaveNode::output_sender, this);
        output_thread.detach();
    }
}

void EnclaveNode::data_listener(int connFD) {
    // We need a serial listener for this agreed upon connection!
    char* body_buffer = new char[MAX_MESSAGE_SIZE]();
    while(start_thread(connFD, body_buffer)) {}
    delete[] body_buffer;
}

void EnclaveNode::allele_matcher() {
    //auto start = std::chrono::high_resolution_clock::now();

    bool first = true;
    while(true) {
    loop_start:
        // transfer all blocks to eligible queue, making sure they are in order
        for (const auto& it : institutions) {
            Institution* inst = it.second;
            inst->transfer_eligible_blocks();
        }
        // match as many alleles together as possible
        while(true) {
            // arbitary "high" string, all real loci should be smaller than this string.
            std::string min_locus = "~";
            for (const auto& it : institutions) {
                Institution* inst = it.second;
                DataBlock* block = inst->get_top_block();
                // check if the institution has data
                if (!block) {
                    // if we've already received an <EOF> notice and emptied the blocks pq, skip this block
                    if (inst->all_data_received && !inst->get_blocks_size()) {
                        continue;
                    }
                    // otherwise, we need to wait for all institutions to send their data!
                    else {
                        std::this_thread::yield();
                        goto loop_start;
                    }
                }
                // test if block->locus < min_locus
                if (min_locus.compare(block->locus) > 0) {
                    min_locus = block->locus;
                }
            }
            // if we did not find a min locus, all data has been received and we have processed all of it.
            // enqueue EOF for all enclave threads then shut down the matcher, its work is done.
            if (min_locus == "~") {
                std::cout << "received last message: "  << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count() << std::endl;
                for (int thread_id = 0; thread_id < num_threads; ++thread_id) {
                    allele_queue_list[thread_id].enqueue(EOFSeperator);
                }
                // auto stop = std::chrono::high_resolution_clock::now();
                // auto duration = std::chrono::duration_cast<std::chrono::microseconds>(stop - start);
                // guarded_cout("Matcher time total: " + std::to_string(duration.count()), cout_lock);
                return;
            }

            std::string allele_line = min_locus + "\t";
            std::string data;

            int locus_hash_thread = hash_string(allele_line, num_threads, true); 

            for (int institutions_idx = 0; institutions_idx < institution_list.size(); ++institutions_idx) {
                Institution* inst = institutions[institution_list[institutions_idx]];
                DataBlock* block = inst->get_top_block();
                // if this institution is <EOF>, skip it.
                if (!block) continue;

                if (block->locus == min_locus) {
                    block = inst->pop_top_block();
                    allele_line.append(std::to_string(inst->get_id()) + "\t");
                    data.append(block->data);
                    delete block;
                }
            }
            // replace last tab with a space so that we know where the data starts
            allele_line.pop_back();
            allele_line.push_back(' ');

            allele_line.append(data);

            // For the first line we want to calculate the max number of lines per batch
            if (first) {
                std::cout << "received first message: "  << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count() << "\n";
                // start = std::chrono::high_resolution_clock::now();
                first = false;
            }
            
            // thread-safe enqueue
            allele_queue_list[locus_hash_thread].enqueue(allele_line);
        }
    }
}

void EnclaveNode::output_sender() {
    std::string output_str;
    std::unique_lock<std::mutex> lk(get_instance()->output_queue_lock);
    bool eof_sent = false;
    while(!terminating || output_queue.size()) {
        // This wait -> signal system seriously improves performance as it reduces busy waiting
        if (!terminating && !output_queue.size())
            output_queue_cv.wait(lk);

        while (output_queue.size()) {
            output_str = output_queue.front();
            output_queue.pop();
            if (terminating && !output_queue.size()) {
                get_instance()->send_msg_output(output_str, CoordinationServerMessageType::EOF_OUTPUT);
                eof_sent = true;
            } else {
                get_instance()->send_msg_output(output_str, CoordinationServerMessageType::OUTPUT);
            }
            
        }
    }
    lk.unlock();
    if (!eof_sent) {
        get_instance()->send_msg_output(EOFSeperator, EOF_OUTPUT);
    }
} 

void EnclaveNode::parse_header_enclave_node_header(const std::string& header, std::string& msg, 
                                                       std::string& dpi_name, EnclaveNodeMessageType& mtype,
                                                       int connFD) {
    int header_idx = 0;
    // Parse dpi name
    while(header[header_idx] != ' ') {
        dpi_name.push_back(header[header_idx++]);
        if (header_idx >= header.length()) {
            std::cout << "1 Invalid header? " << header << std::endl;
        }
    }
    header_idx++;
    // Parse mtype
    std::string mtype_str;

    while(header[header_idx] != ' ') {
        mtype_str.push_back(header[header_idx++]);
        if (header_idx >= header.length()) {
            std::cout << "2 Invalid header? " << header << std::endl;
        }
    }
    header_idx++;
    try {
        mtype = static_cast<EnclaveNodeMessageType>(std::stoi(mtype_str));
    } catch(const std::invalid_argument& e) {
        std::cout << "Failed to read in mtype" << std::endl;
        std::cout << "header " << header << "\n msg " << msg << std::endl;
        return;
    }

    if (mtype != EnclaveNodeMessageType::DATA) {
        for (; header_idx < header.length(); ++header_idx) {
            msg.push_back(header[header_idx]);
        }
        return;
    }

    if (connFD > 65353 || connFD < 0) {
        throw std::runtime_error("ConnFD out of range " + std::to_string(connFD));
    }
    if (!seen_fds[connFD]) {
        seen_fds[connFD] = true;
        boost::thread data_listener_thread(&EnclaveNode::data_listener, this, connFD);
        data_listener_thread.detach();
    }

    DataBlockBatch* batch = new DataBlockBatch;
    std::string pos_str;
    // Parse batch position
    while(header[header_idx++] != '\t') {
        pos_str.push_back(header[header_idx - 1]);
        if (header_idx > header.length()) {
            std::cout << "3 Invalid header? " << header << std::endl;
        }
    }

    try {
        batch->pos = std::stoi(pos_str);
    } catch(const std::invalid_argument& e) {
        std::cout << "Failed to read in pos" << std::endl;
        std::cout << "header " << header << "\n msg " << msg << std::endl;
        return;
    }

    

    // msg format: blocks sent \t lengths (tab delimited) \n (terminating char) blocks of data w no delimiters
    std::vector<int> lengths;
    std::string length_str;
    try {
        while(header[header_idx++] != '\n') {
            if (header[header_idx - 1] == '\t') {
                lengths.push_back(std::stoi(length_str));
                length_str.clear();
                continue;
            }
            length_str.push_back(header[header_idx - 1]);
            if (header_idx > header.length()) {
                std::cout << "4 Invalid header? " << header << std::endl;
            }
        }
        lengths.push_back(std::stoi(length_str));
    } catch (const std::exception& e) {
        std::cout << "Failed header parse with error " << e.what() << std::endl;
    }

    for (int length : lengths) {
        const std::string substr = header.substr(header_idx, length);
        header_idx += length;
        int num_delims = 0;
        DataBlock* block = new DataBlock;

        for (char c : substr) {
            if (c == '\t') {
                // Throw out this tab char!
                if (++num_delims == 2) {
                    continue;
                }
            }
            if (num_delims < 2) {
                    block->locus.push_back(c);
            } else {
                block->data.push_back(c);
            }
        }
        batch->blocks_batch.push_back(block);
    }

    institutions[dpi_name]->add_block_batch(batch);
}

EnclaveNode* EnclaveNode::get_instance(const std::string& config_file) {
    static EnclaveNode instance(config_file);
    return &instance;
}

EncMode EnclaveNode::get_mode() {
    return get_instance()->enc_mode;
}

EncAnalysis EnclaveNode::get_analysis() {
    return get_instance()->enc_analysis;
}

ImputePolicy EnclaveNode::get_impute_policy() {
    return get_instance()->impute_policy;
}

void EnclaveNode::finish_setup() {
    // Register with the register server!
    const nlohmann::json config = get_instance()->enclave_config;
    std::string enclave_node_hostname;
    std::ifstream ipfile("ip.txt");
    std::getline(ipfile, enclave_node_hostname);
    std::string msg = enclave_node_hostname + "\t" + std::to_string(get_instance()->port) + "\t" + std::to_string(get_instance()->num_threads);
    get_instance()->send_msg(config["coordination_server_info"]["hostname"], 
                            config["coordination_server_info"]["port"],
                            CoordinationServerMessageType::ENCLAVE_REGISTER,
                            msg);
}

void EnclaveNode::start_timer(const std::string& func_name) {
    get_instance()->enclave_clocks[func_name] = std::chrono::high_resolution_clock::now();
}

void EnclaveNode::stop_timer(const std::string& func_name) {
    EnclaveNode* inst = get_instance();
    if (!inst->enclave_total_times.count(func_name)) {
        inst->enclave_total_times[func_name] = 0;
        inst->enclave_total_calls[func_name] = 0;
    }
    inst->enclave_total_times[func_name] += std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - inst->enclave_clocks[func_name]).count();
    inst->enclave_total_calls[func_name] += 1;
}

void EnclaveNode::print_timings() {
    EnclaveNode* inst = get_instance();
    for (auto it : inst->enclave_total_times) {
        guarded_cout(it.first + "\t" + std::to_string(it.second - (inst->enclave_total_calls[it.first] * AVERAGE_OCALL_OVERHEAD_MICROSECONDS)), cout_lock);
    }
}

void EnclaveNode::set_max_batch_lines(unsigned int lines) {
    get_instance()->max_batch_lines = lines;
}

uint8_t* EnclaveNode::get_rsa_pub_key() {
    return get_instance()->rsa_public_key;
}

void EnclaveNode::set_evidence_and_size(uint8_t* evidence, unsigned int size) {
    std::memcpy(get_instance()->evidence, evidence, size);
    get_instance()->evidence_size = size;
}

int EnclaveNode::get_num_threads() {
    return get_instance()->num_threads;
}

int EnclaveNode::get_num_institutions() {
    return get_instance()->institution_list.size();
}

std::string EnclaveNode::get_covariants() {
    std::string cov_list;
    std::vector<std::string> covariants;
    Parser::split(covariants, get_instance()->covariant_list);
    for (std::string covariant : covariants) {
        cov_list.append(covariant + "\t");
    }
    return cov_list;
}

std::string EnclaveNode::get_aes_key(const int institution_num, const int thread_id) {
    const std::string institution_name = get_instance()->institution_list[institution_num];
    std::lock_guard<std::mutex> raii(get_instance()->institutions_lock);
    if (!get_instance()->institutions.count(institution_name)) {
        return "";
    }

    return get_instance()->institutions[institution_name]->get_aes_key(thread_id);
}

std::string EnclaveNode::get_aes_iv(const int institution_num, const int thread_id) {
    const std::string institution_name = get_instance()->institution_list[institution_num];
    std::lock_guard<std::mutex> raii(get_instance()->institutions_lock);
    if (!get_instance()->institutions.count(institution_name)) {
        return "";
    }

    return get_instance()->institutions[institution_name]->get_aes_iv(thread_id);
}

std::string EnclaveNode::get_num_patients(const int institution_num) {
    const std::string institution_name = get_instance()->institution_list[institution_num];
    std::lock_guard<std::mutex> raii(get_instance()->institutions_lock);
    if (!get_instance()->institutions.count(institution_name)) {
        return "";
    }

    return get_instance()->institutions[institution_name]->get_num_patients();;
}

std::string EnclaveNode::get_y_data(const int institution_num) {
    const std::string institution_name = get_instance()->institution_list[institution_num];
    if (!get_instance()->institutions.count(institution_name)) {
        return "";
    }

    std::string y_vals = get_instance()->institutions[institution_name]->get_y_data();
    return y_vals;
}

std::string EnclaveNode::get_covariant_data(const int institution_num, const std::string& covariant_name) {
    const std::string institution_name = get_instance()->institution_list[institution_num];
    if (!get_instance()->institutions.count(institution_name)) {
        return "";
    }

    std::string cov_vals = get_instance()->institutions[institution_name]->get_covariant_data(covariant_name);
    return cov_vals;
}

int EnclaveNode::get_allele_data(char* batch_data, const int thread_id) {
    std::string batch_data_str;
    std::string tmp;
    // Currently the enclave expects the ~EOF~ to be by itself (not in a batch).
    // This is not very clean code, but searching for ~EOF~ in the enclave is slow!
    if (get_instance()->eof_read_list[thread_id]) {
        // Why do I use async here? Good question! The mark_eof ECall can't be triggered by the same context of the OCall or else it gives me a "re-entrant" error
        // so here's a workaround that calls mark_eof not in this function and without the need for threads
        std::async(mark_eof_wrapper, thread_id);

        return -1;
    }

    int num_lines = 0;
    moodycamel::ReaderWriterQueue<std::string>& allele_queue = get_instance()->allele_queue_list[thread_id];
    while (num_lines < get_instance()->max_batch_lines && allele_queue.try_dequeue(tmp)) {
        if (strcmp(EOFSeperator, tmp.c_str()) == 0) {
            get_instance()->eof_read_list[thread_id] = true;
            break;
        }
        num_lines++;
        batch_data_str += tmp;
    }
    if (num_lines) {
        if (batch_data_str.length() > ENCLAVE_READ_BUFFER_SIZE) {
            throw std::runtime_error("Batch larger than buffer");
        }
        memset(batch_data, 0, ENCLAVE_READ_BUFFER_SIZE);
        memcpy(batch_data, &batch_data_str[0], batch_data_str.length());
        batch_data[batch_data_str.length()] = '\0';
    }
    return num_lines;
}

void EnclaveNode::write_allele_data(char* output_data, const int buffer_size, const int thread_id) {
    std::unique_lock<std::mutex> lk(get_instance()->output_queue_lock);
    get_instance()->output_queue.push(std::string(output_data));
    lk.unlock();
    get_instance()->output_queue_cv.notify_all();
}

void EnclaveNode::cleanup_output() {
    std::unique_lock<std::mutex> lk(get_instance()->output_queue_lock);
    terminating = true;
    lk.unlock();
    get_instance()->output_queue_cv.notify_all();
    std::cout << "Sending EOF message: "  << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count() << std::endl;
}