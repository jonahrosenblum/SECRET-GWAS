#include <iostream>
#include <thread>
#include <map>
#include <atomic>
#include <condition_variable>
#include <string.h>

#include "buffer.h"
#include "crypto.h"
#include "mxcsr.h"

#ifdef NON_OE
#include "enclave_glue.h"
#else
#include "gwas_t.h"
#endif

std::vector<Buffer*> buffer_list;
std::vector<DPIInfo> dpi_info_list;
std::vector<int> dpi_y_size;
int num_dpis;
GWAS *gwas;

// Both kernels
double *beta_g;

// Log reg
double *beta_delta_g;
double *Grad_g;

// Lin reg
double *XTY_g;
double *XTY_og_g;
double ***XTX_og_list;

int total_row_size;

std::condition_variable start_thread_cv;
volatile bool start_thread = false;


void mark_eof(const int thread_id) {
    buffer_list[thread_id]->mark_eof();
}

void setup_enclave_encryption(const int num_threads) {
    RSACrypto rsa = RSACrypto();
    if (!rsa.m_initialized) {
        std::cerr << "ERROR: failed to initialized RSA key" << std::endl;
        exit(1); 
    }
    buffer_t evidence = {0};
    rsa.get_evidence(&evidence);

    setevidence(evidence.buffer, evidence.size);
    setrsapubkey(rsa.get_pub_key());

    std::cout << "RSA pubkey/evidence generated and set" << std::endl;

    getdpinum(&num_dpis);

    std::cout << "enclave running on " << num_dpis << " dpis" << std::endl;

    dpi_info_list.resize(num_dpis);
    dpi_y_size.resize(num_dpis);
    buffer_list.resize(num_threads);

    // We should store num_dpi number of aes keys/iv/contexts.
    for (DPIInfo& dpi: dpi_info_list) {
        dpi.aes_list.resize(num_threads);
        for (int thread_id = 0; thread_id < num_threads; ++thread_id) {
            AESData aes;
            aes.aes_context = new mbedtls_aes_context();
            mbedtls_aes_init(aes.aes_context);
            dpi.aes_list[thread_id] = aes;
        }
    }

    try {
        unsigned char enc_aes_key[256];
        unsigned char enc_aes_iv[256];
        size_t* aes_length = new size_t(AES_KEY_LENGTH);
        for (int dpi = 0; dpi < num_dpis; ++dpi) {
            for (int thread_id = 0; thread_id < num_threads; ++thread_id) {
                bool rt = false;
                while (!rt) {
                    getaes(&rt, dpi, thread_id, enc_aes_key, enc_aes_iv);
                }
                AESData& thread_aes_data = dpi_info_list[dpi].aes_list[thread_id];
                rsa.decrypt(enc_aes_key, 256,
                            (uint8_t*)&thread_aes_data.aes_key, aes_length);
                rsa.decrypt(enc_aes_iv, 256, 
                            (uint8_t*)&thread_aes_data.aes_iv, aes_length);
                // Initialize AES context so that we can decrypt data coming into
                // the enclave.
                int ret = mbedtls_aes_setkey_dec(thread_aes_data.aes_context,
                                                 thread_aes_data.aes_key,
                                                 AES_KEY_LENGTH * 8);
                if (ret != 0) {
                    std::cout << "Set key failed." << std::endl;
                    exit(0);
                }
            }
        }
        delete aes_length;
        std::cout << "AES KEY and IV loaded" << std::endl;
    } catch (ERROR_t& err) {
        std::cerr << "ERROR: fail to get AES KEY " << err.msg << std::endl;
    }
}

void setup_num_patients() {
    // Read in number of patients at each institution
    char num_patients_buffer[ENCLAVE_SMALL_BUFFER_SIZE];
    char buffer_decrypt[ENCLAVE_SMALL_BUFFER_SIZE];
    total_row_size = 0;
    for (int dpi = 0; dpi < num_dpis; ++dpi) {
        int num_patients_buffer_size = 0;
        while (!num_patients_buffer_size){
            get_num_patients(&num_patients_buffer_size, dpi, num_patients_buffer);
        }
        DPIInfo& info = dpi_info_list[dpi];
        memset(buffer_decrypt, 0, ENCLAVE_SMALL_BUFFER_SIZE);
        aes_decrypt_data(info.aes_list.front().aes_context,
                         info.aes_list.front().aes_iv,
                         (const unsigned char*) num_patients_buffer,
                         num_patients_buffer_size, 
                         (unsigned char*) buffer_decrypt);
        int dpi_num_patients;
        try {
            dpi_num_patients = std::stoi(buffer_decrypt);
        } catch(const std::invalid_argument& e) {
            std::cout << "Failed setup num patients" << std::endl;
            throw e;
        }
        dpi_y_size[dpi] = dpi_num_patients;
        dpi_info_list[dpi].size = (dpi_num_patients / 4) + (dpi_num_patients % 4 == 0 ? 0 : 1);
        total_row_size += dpi_num_patients;
    }
}

void setup_enclave_phenotypes(const int num_threads, EncAnalysis analysis_type, ImputePolicy impute_policy) {
    char* buffer_decrypt = new char[ENCLAVE_READ_BUFFER_SIZE];
    char* phenotype_buffer = new char[ENCLAVE_READ_BUFFER_SIZE];

    // Read in covariants from each institution
    char covl[ENCLAVE_SMALL_BUFFER_SIZE];
    getcovlist(covl);
    std::string covlist(covl);
    std::vector<std::string> covariant_names;
    split_delim(covlist.c_str(), covariant_names);

    gwas = new GWAS(analysis_type, total_row_size, covariant_names.size() + 1);

    try {
        for (int thread_id = 0; thread_id < num_threads; ++thread_id) {
            // TODO: set buffer type accordingly
            buffer_list[thread_id] = new Buffer(total_row_size, analysis_type, num_dpis, thread_id);
        }
    } catch (const std::exception &e) { 
        std::cout << "Crash in buffer malloc with " << e.what() << std::endl;
    }

    /* set up encrypted size and max batch line */
    int total_crypto_size = 0;
    for (int dpi = 0; dpi < num_dpis; dpi++) {
        // Calculate compaction factor, ceil(plaintext size / 4) -> rounded up to nearest multiple of 16
        //int compacted_size = dpi_y_size[dpi];
        int compacted_size = (dpi_y_size[dpi] + 3) / 4;
        int compacted_remainder = compacted_size % 16;
        
        // I should test this with more sizes - I assumed that if the compacted remainder was divisble by 16 we wouldn't need to add any padding... I guess not?
        compacted_size += 16 - compacted_remainder;

        dpi_info_list[dpi].crypto_size = compacted_size + 1;

        // Add 2 for the tab delimiter and null terminating char
        total_crypto_size += compacted_size + 2;
    }
    // Add padding for Loci + Allele and list of dpis + 1 for new line at very end of sequence
    total_crypto_size += MAX_LOCI_ALLELE_STR_SIZE + (num_dpis * 2) + 1;

    int max_batch_lines = ENCLAVE_READ_BUFFER_SIZE / total_crypto_size;
    if (!max_batch_lines) {
        std::cerr << "Data is too long to fit into enclave read buffer" << std::endl;
        exit(1);
    }
    setmaxbatchlines(max_batch_lines);

    std::cout << "Init finished" << std::endl;

    // Read in y-vals from each institution
    try {
        for (int dpi = 0; dpi < num_dpis; ++dpi) {
            int y_buffer_size = 0;
            while (!y_buffer_size){
                gety(&y_buffer_size, dpi, phenotype_buffer);
            }
            DPIInfo& info = dpi_info_list[dpi];
            memset(buffer_decrypt, 0, ENCLAVE_READ_BUFFER_SIZE);
            aes_decrypt_data(info.aes_list.front().aes_context,
                             info.aes_list.front().aes_iv,
                             (const unsigned char*) phenotype_buffer,
                             y_buffer_size, 
                             (unsigned char*) buffer_decrypt);
            int read_size = gwas->phenotype_and_covars.read(buffer_decrypt, dpi_y_size[dpi]);
            if (read_size != dpi_y_size[dpi]) {
                throw ReadtsvERROR("y val size mismatch from dpi: " +
                                    std::to_string(dpi) + " size expected: " + 
                                    std::to_string(dpi_y_size[dpi]) + " got: " + 
                                    std::to_string(read_size));
            }
        }
    }
    catch (ERROR_t& err) {
        std::cerr << "ERROR: fail to get correct y values " << err.msg << std::endl;
    }
    gwas->phenotype_and_covars.after_covar();

    std::cout << "Y value loaded" << std::endl;
    std::cout << "Starting Enclave: "  << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count() << "\n";
    try{
        for (int i = 0; i < covariant_names.size(); ++i) {
            const std::string& cov_name = covariant_names[i];
            if (cov_name == "1") {
                gwas->phenotype_and_covars.init_1_covar(total_row_size);
                gwas->phenotype_and_covars.after_covar();
                continue;
            }
            for (int dpi = 0; dpi < num_dpis; ++dpi) {
                int cov_buffer_size = 0;
                while (!cov_buffer_size) {
                    getcov(&cov_buffer_size, dpi, cov_name.c_str(), phenotype_buffer);
                }
                DPIInfo& info = dpi_info_list[dpi];
                memset(buffer_decrypt, 0, ENCLAVE_READ_BUFFER_SIZE);
                aes_decrypt_data(info.aes_list.front().aes_context,
                                info.aes_list.front().aes_iv,
                                (const unsigned char*) phenotype_buffer,
                                cov_buffer_size, 
                                (unsigned char*) buffer_decrypt);
                int read_size = gwas->phenotype_and_covars.read(buffer_decrypt, dpi_y_size[dpi]);
                if (read_size != dpi_y_size[dpi]) {
                    throw ReadtsvERROR("covariant size mismatch from dpi: " +
                                       std::to_string(dpi) + 
                                       " size expected: " + std::to_string(dpi_y_size[dpi]) + 
                                       " got: " + std::to_string(read_size));
                }
            }
            gwas->phenotype_and_covars.after_covar();
        }
    } catch (ERROR_t& err) {
        std::cerr << "ERROR: fail to get correct covariant values: " << err.msg << std::endl;
    } catch (const std::exception &e) { 
        std::cout << "Crash in cov setup with " << e.what() << std::endl;
    }
    std::cout << "Cov loaded" << std::endl;

    delete[] phenotype_buffer;
    delete[] buffer_decrypt;

    // Padding to avoid false sharing - for some reason false sharing can still happen unless we make
    // this larger than a single cache block. Maybe prefetching/compiler optimizations cause invalidations?
    int size_of_thread_buffer = get_padded_buffer_len(gwas->dim());
    beta_delta_g = new double[num_threads * size_of_thread_buffer];
    Grad_g = new double[num_threads * size_of_thread_buffer];
    beta_g = new double[num_threads * size_of_thread_buffer];
    XTY_g = new double[num_threads * size_of_thread_buffer];
    XTY_og_g = new double[num_threads * size_of_thread_buffer];
    XTX_og_list = new double**[num_threads * size_of_thread_buffer];

    try {
        for (int thread_id = 0; thread_id < num_threads; ++thread_id) {
            buffer_list[thread_id]->add_gwas(gwas, impute_policy, dpi_y_size);
        }
    } catch (const std::exception &e) { 
        std::cout << "Crash in add gwas with " << e.what() << std::endl;
    }

    start_thread = true;
    start_thread_cv.notify_all();
    std::cout << "Setup finished" << std::endl;
}

void regression(const int thread_id, EncAnalysis analysis_type) {
    MXCSR mxcsr;
    mxcsr.set_mxcsr_flags();
    
    if (!mxcsr.FTZ_and_DTZ_flags_set()) {
        std::cout << "FTZ or DTZ flag was not correctly set - machine at risk for subnormal sidechannel attack." << std::endl;
        return;
    }

    std::string output_string;
    std::string loci_string;
    std::string alleles_string;
    output_string.reserve(50);
    loci_string.reserve(50);
    alleles_string.reserve(20);

    std::mutex useless_lock;
    std::unique_lock<std::mutex> useless_lock_wrapper(useless_lock);
    // experimental - checking to see if spinning up threads adds a noticable
    // amount of overhead... need +1 TCS in config
    while (!start_thread) {
        // std::this_thread::yield();
        start_thread_cv.wait(useless_lock_wrapper);
    }

    Buffer* buffer = buffer_list[thread_id];
    Batch* batch = nullptr;
    Row* row;
    /* process rows */
    while (true) {
        //start_timer("input()");
        if (!batch || batch->st != Batch::Working) {
            batch = buffer->launch(dpi_info_list, thread_id);
        }
        if (!batch) {
            // std::cout << "id " << thread_id << std::endl;
            buffer->clean_up();
            break;
        }
        //stop_timer("input()");
        // starting the get_row timer happens within the function because our code is written weirdly and we do our output
        try {
            switch(analysis_type) {
                case EncAnalysis::linear_dummy:
                    if (!(row = static_cast<Lin_row_dummy*>(batch->get_row(buffer)))) continue;
                    break;
                case EncAnalysis::linear:
                    if (!(row = static_cast<Lin_row*>(batch->get_row(buffer)))) continue;
                    break;
                case EncAnalysis::logistic:
                    if (!(row = static_cast<Log_row*>(batch->get_row(buffer)))) continue;
                    break;
                case EncAnalysis::linear_oblivious:
                    if (!(row = static_cast<Oblivious_lin_row*>(batch->get_row(buffer)))) continue;
                    break;
                case EncAnalysis::logistic_oblivious:
                    if (!(row = static_cast<Oblivious_log_row*>(batch->get_row(buffer)))) continue;
                    break;
                default:
                    throw std::runtime_error("Invalid analysis type");
            }
            
        } catch (ERROR_t& err) {
            std::cerr << "ERROR: " << err.msg << std::endl << std::flush;
            exit(0);
        } catch (const std::exception &e) { 
            std::cout << "Crash in get_row with " << e.what() << std::endl;
            exit(0);
        }
        //stop_timer("parse_and_decrypt()");
        //  compute results
        loci_to_str(row->getloci(), loci_string);
        alleles_to_str(row->getalleles(), alleles_string);
        output_string += loci_string + "\t" + alleles_string;
        //start_timer("kernel()");
        bool converge;
        //std::cout << i++ << std::endl;
        try {
            converge = row->fit(thread_id);
            row->get_outputs(thread_id, output_string);

            if (analysis_type == EncAnalysis::logistic || analysis_type == EncAnalysis::logistic_oblivious) {
                output_string += + "\t" + std::to_string(row->get_iterations()) + "\t";
                // wanted to use a ternary, but the compiler doesn't like it?
                if (converge) {
                    output_string += "true";
                } else {
                    output_string += "false";
                }
            }
            output_string += "\n";
        } catch (MathError& err) {
            output_string += "\tNA\tNA\tNA\t1\tfalse\n";
            // cerr << "MathError while fiting " << ss.str() << ": " << err.msg
            //      << std::endl;
            // ss << "\tNA\tNA\tNA" << std::endl;
        } catch (ERROR_t& err) {
            std::cerr << "ERROR " << err.msg << std::endl << std::flush;
            // ss << "\tNA\tNA\tNA" << std::endl;
            exit(1);
        }
        //stop_timer("kernel()");
        batch->write(output_string);
        output_string.clear();
    }
}