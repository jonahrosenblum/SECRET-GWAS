/*
 * Implementation of our instituion class.
 */


#include "institution.h"

Institution::Institution(std::string hostname, int port, int id, const int num_threads) 
        : hostname(hostname), port(port), requested_for_data(false), listener_running(false), 
          request_conn(-1), current_pos(0), all_data_received(false), id(id) {
    aes_encrypted_key_list.resize(num_threads);
    aes_encrypted_iv_list.resize(num_threads);
    for (int i = 0; i < num_threads; ++i) {
        aes_encrypted_key_list[i] = "";
        aes_encrypted_iv_list[i] = "";
    }
}

Institution::~Institution() {

}

void Institution::add_block_batch(DataBlockBatch* block_batch) {
    // std::lock_guard<std::mutex> raii(blocks_lock);
    // blocks.push(block_batch);
    unsorted_blocks.enqueue(block_batch);
}

int Institution::get_blocks_size() {
    return blocks.size();
}

int Institution::get_covariant_size() {
    std::lock_guard<std::mutex> raii(covariant_data_lock);
    return covariant_data.size();
}

int Institution::get_id() {
    return id;
}

void Institution::set_key_and_iv(std::string aes_key, std::string aes_iv, const int thread_id) {
    std::lock_guard<std::mutex> raii(aes_key_iv_lock);
    aes_encrypted_key_list[thread_id] = decoder.decode(aes_key);
    aes_encrypted_iv_list[thread_id] = decoder.decode(aes_iv);
}

void Institution::set_num_patients(const std::string& num_patients) {
    std::lock_guard<std::mutex> raii(num_patients_lock);
    num_patients_encrypted = num_patients;
}

void Institution::set_y_data(std::string& y_data) {
    std::lock_guard<std::mutex> raii(y_val_data_lock);
    y_val_data = y_data;
}

void Institution::set_covariant_data(const std::string& covariant_name, const std::string& data) {
    std::lock_guard<std::mutex> raii(covariant_data_lock);
    if (covariant_data.count(covariant_name)) {
        throw std::runtime_error("Duplicate covariant received.");
    }
    if (data.length() >= ENCLAVE_READ_BUFFER_SIZE) {
        throw std::runtime_error("Covariant too large for enclave: " + std::to_string(data.length()));
    }
    covariant_data[covariant_name] = data;
}

std::string Institution::get_aes_key(const int thread_id) {
    std::lock_guard<std::mutex> raii(aes_key_iv_lock);
    return aes_encrypted_key_list[thread_id];
}

std::string Institution::get_aes_iv(const int thread_id) {
    std::lock_guard<std::mutex> raii(aes_key_iv_lock);
    return aes_encrypted_iv_list[thread_id];
}

std::string Institution::get_num_patients() {
    std::lock_guard<std::mutex> raii(num_patients_lock);
    return num_patients_encrypted;
}

std::string Institution::get_y_data() {
    std::lock_guard<std::mutex> raii(y_val_data_lock);
    return y_val_data;
}

std::string Institution::get_covariant_data(const std::string& covariant_name) {
    std::lock_guard<std::mutex> raii(covariant_data_lock);
    if (!covariant_data.count(covariant_name)) {
        return "";
    }
    std::string data = covariant_data[covariant_name];
    return data;
}

void Institution::transfer_eligible_blocks() {
    // std::lock_guard<std::mutex> raii(blocks_lock);
    DataBlockBatch *unsorted_batch;
    while (unsorted_blocks.try_dequeue(unsorted_batch)) {
        blocks.push(unsorted_batch);
    }
    while (!blocks.empty()) {
        DataBlockBatch* batch = blocks.top();
        if (batch->pos != current_pos) {
            return;
        }
        blocks.pop();

        // std::lock_guard<std::mutex> raii2(eligible_blocks_lock);
        for (DataBlock* parsed_block : batch->blocks_batch) {
            eligible_blocks.enqueue(parsed_block);
        }
        // Clean up block batch!
        delete batch;
        current_pos++;
    }
}

DataBlock* Institution::get_top_block() {
    //std::lock_guard<std::mutex> raii(eligible_blocks_lock);
    // DataBlock *ret;
    // bool success = eligible_blocks.try_dequeue(ret);
    // if (!success) {
    //     return nullptr;
    // }
    // return ret;
    // if (eligible_blocks.empty()) return nullptr;
    // return eligible_blocks.front();
    DataBlock **ret = eligible_blocks.peek();
    if (!ret) return nullptr;
    return *ret;
}

DataBlock* Institution::pop_top_block() {
    //std::lock_guard<std::mutex> raii(eligible_blocks_lock);
    // eligible_blocks.pop();
    DataBlock *ret;
    eligible_blocks.try_dequeue(ret);
    return ret;
}