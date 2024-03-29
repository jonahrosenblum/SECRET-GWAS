#include <fstream>
#include <iostream>
#include <map>
#define BUFFER_LINES 1

#include <gwas.h>

#include <chrono>
#include <cstring>
#include <string>
#include <thread>
#include <vector>
#include <stdio.h>
#include <boost/thread.hpp>
#include "enclave_node.h"

#define MAX_ATTEMPT_TIMES 10
#define ATTEMPT_TIMEOUT 500  // in milliseconds

#ifdef NON_OE
#include "host_glue.h"
#include "../../enclave/include/enclave_glue.h"
#else
#include "gwas_u.h"
#endif


void setrsapubkey(uint8_t enc_rsa_pub_key[RSA_PUB_KEY_SIZE]) {
    std::memcpy(EnclaveNode::get_rsa_pub_key(), enc_rsa_pub_key, RSA_PUB_KEY_SIZE);
    
    // Once we have generated an RSA key pair we can start communication!
    EnclaveNode::finish_setup();
}

void setevidence(uint8_t evidence[MAX_EVIDENCE_SIZE], const int size) {
    if (size > MAX_EVIDENCE_SIZE) {
        throw std::runtime_error("Evidence is larger than the maximum evidence size!");
    }
    EnclaveNode::set_evidence_and_size(evidence, size);
}


void setmaxbatchlines(int lines) {
    EnclaveNode::set_max_batch_lines(lines);
}

void start_timer(const char func_name[MAX_DPINAME_LENGTH]) {
    EnclaveNode::start_timer(func_name);
}

void stop_timer(const char func_name[MAX_DPINAME_LENGTH]) {
    EnclaveNode::stop_timer(func_name);
}

int getdpinum() {
    return EnclaveNode::get_num_institutions();
}

void getcovlist(char covlist[ENCLAVE_SMALL_BUFFER_SIZE]) {
    std::memset(covlist, 0, ENCLAVE_SMALL_BUFFER_SIZE);
    strcpy(covlist, EnclaveNode::get_covariants().c_str());
}

bool getaes(const int dpi_num,
            const int thread_id,
            unsigned char key[256],
            unsigned char iv[256]) {
    std::string encrypted_aes_key = EnclaveNode::get_aes_key(dpi_num, thread_id);
    std::string encrypted_aes_iv = EnclaveNode::get_aes_iv(dpi_num, thread_id);
    if (!encrypted_aes_key.length() || !encrypted_aes_iv.length()) {
        return false;
    }
    std::memcpy(key, &encrypted_aes_key[0], 256);
    std::memcpy(iv, &encrypted_aes_iv[0], 256);
    return true;
}

int get_num_patients(const int dpi_num, char num_patients_buffer[ENCLAVE_SMALL_BUFFER_SIZE]) {
    std::string num_patients_encrypted = EnclaveNode::get_num_patients(dpi_num);
    if (!num_patients_encrypted.length()) {
        return 0;
    }
    std::memset(num_patients_buffer, 0, ENCLAVE_SMALL_BUFFER_SIZE);
    std::memcpy(num_patients_buffer, &num_patients_encrypted[0], num_patients_encrypted.length());
    return num_patients_encrypted.length();
}

int gety(const int dpi_num, char y[ENCLAVE_READ_BUFFER_SIZE]) {
    std::string y_data = EnclaveNode::get_y_data(dpi_num);
    if (!y_data.length()) {
        return 0;
    }
    std::memset(y, 0, ENCLAVE_READ_BUFFER_SIZE);
    std::memcpy(y, &y_data[0], y_data.length());
    return y_data.length();
}

int getcov(const int dpi_num,
           const char cov_name[MAX_DPINAME_LENGTH],
           char cov[ENCLAVE_READ_BUFFER_SIZE]) {
    if (strcmp(cov_name, "1") == 0) {
        std::memset(cov, 0, ENCLAVE_READ_BUFFER_SIZE);
        strcpy(cov, "1");
        return 1;
    }
    std::string cov_data = EnclaveNode::get_covariant_data(dpi_num, cov_name);
    if (!cov_data.length()) {
        return 0;
    }
    std::memset(cov, 0, ENCLAVE_READ_BUFFER_SIZE);
    if (cov_data.length() > ENCLAVE_READ_BUFFER_SIZE) {
        std::cout << "cov data length " << ENCLAVE_READ_BUFFER_SIZE << std::endl;
    }
    std::memcpy(cov, &cov_data[0], cov_data.length());

    return cov_data.length();
}

int getbatch(char batch[ENCLAVE_READ_BUFFER_SIZE], const int thread_id) {
    int res = EnclaveNode::get_allele_data(batch, thread_id);
    return res;
}

void writebatch(char buffer[ENCLAVE_READ_BUFFER_SIZE], const int buffer_size, const int thread_id) {
    EnclaveNode::write_allele_data(buffer, buffer_size, thread_id);
}

bool check_simulate_opt(int* argc, const char* argv[]) {
    for (int i = 0; i < *argc; i++) {
        if (strcmp(argv[i], "--simulate") == 0) {
            fprintf(stdout, "Running in simulation mode\n");
            memmove(&argv[i], &argv[i + 1], (*argc - i) * sizeof(char*));
            (*argc)--;
            return true;
        }
    }
    return false;
}

bool check_debug_opt(int* argc, const char* argv[]) {
    for (int i = 0; i < *argc; i++) {
        if (strcmp(argv[i], "--debug") == 0) {
            fprintf(stdout, "Running in debug mode\n");
            memmove(&argv[i], &argv[i + 1], (*argc - i) * sizeof(char*));
            (*argc)--;
            return true;
        }
    }
    return false;
}

#ifndef NON_OE

oe_enclave_t* enclave;

void mark_eof_wrapper(const int thread_id) {
    mark_eof(enclave, thread_id);
}

int start_enclave() {
    oe_result_t result;
    int ret = 1;
    enclave = NULL;

    uint32_t flags = 0;

    if (EnclaveNode::get_mode() == simulate) flags |= OE_ENCLAVE_FLAG_SIMULATE;
    if (EnclaveNode::get_mode() == debug) flags |= OE_ENCLAVE_FLAG_DEBUG;

    EncAnalysis enc_analysis_type = EnclaveNode::get_analysis();

    // Create the enclave
    result = oe_create_gwas_enclave("../enclave/gwasenc.signed", OE_ENCLAVE_TYPE_AUTO, flags, NULL,
                                    0, &enclave);
    if (result != OE_OK) {
        fprintf(stderr, "oe_create_gwas_enclave(): result=%u (%s)\n", result,
                oe_result_str(result));
        goto exit;
    }

    try {
        std::cout << "\n\n**RUNNING REGRESSION**\n" << std::endl;

        int num_threads = EnclaveNode::get_num_threads();
        boost::thread_group thread_group;
        for (int thread_id = 0; thread_id < num_threads; ++thread_id) {
            boost::thread* enclave_thread = new boost::thread(regression, enclave, thread_id, enc_analysis_type);
            thread_group.add_thread(enclave_thread);
        }

        result = setup_enclave_encryption(enclave, num_threads);
        if (result != OE_OK) {
            fprintf(stderr,
                    "calling into enclave_gwas failed: result=%u (%s)\n",
                    result, oe_result_str(result));
            goto exit;
        }
        
        result = setup_num_patients(enclave);
        if (result != OE_OK) {
            fprintf(stderr,
                    "calling into enclave_gwas failed: result=%u (%s)\n",
                    result, oe_result_str(result));
            goto exit;
        }

        result = setup_enclave_phenotypes(enclave, num_threads, enc_analysis_type, EnclaveNode::get_impute_policy());
        if (result != OE_OK) {
            fprintf(stderr,
                    "calling into enclave_gwas failed: result=%u (%s)\n",
                    result, oe_result_str(result));
            goto exit;
        }
        auto start = std::chrono::high_resolution_clock::now();
        thread_group.join_all();
        auto stop = std::chrono::high_resolution_clock::now();


        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(stop - start);
        std::cout << "Enclave time total: " << duration.count() << std::endl;

        EnclaveNode::print_timings();
        EnclaveNode::cleanup_output();
    } catch (ERROR_t& err) {
        std::cerr << "ERROR: " << err.msg << std::endl << std::flush;
    }

    ret = 0;

exit:
    // Clean up the enclave if we created one
    if (enclave) oe_terminate_enclave(enclave);

    return ret;
}

#else


int start_enclave() {
    int ret = 1;

    try {
        std::cout << "\n\n**RUNNING REGRESSION**\n" << std::endl;

        EncAnalysis enc_analysis_type = EnclaveNode::get_analysis();
        int num_threads = EnclaveNode::get_num_threads();
        boost::thread_group thread_group;

        for (int thread_id = 0; thread_id < num_threads; ++thread_id) {
            boost::thread* enclave_thread = new boost::thread(regression, thread_id, enc_analysis_type);
            thread_group.add_thread(enclave_thread);
        }

        setup_enclave_encryption(num_threads);
        setup_num_patients();
        setup_enclave_phenotypes(num_threads, enc_analysis_type, EnclaveNode::get_impute_policy());
        auto start = std::chrono::high_resolution_clock::now();
        thread_group.join_all();
        auto stop = std::chrono::high_resolution_clock::now();

        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(stop - start);
        std::cout << "Enclave time total: " << duration.count() << std::endl;
        EnclaveNode::print_timings();
        EnclaveNode::cleanup_output();
    } catch (ERROR_t& err) {
        std::cerr << "ERROR: " << err.msg << std::endl << std::flush;
    }

    ret = 0;

exit:
    return ret;
}


#endif